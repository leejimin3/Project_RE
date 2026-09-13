// Copyright Epic Games, Inc. All Rights Reserved.

#include "REExplosionFx.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "Project_RE.h"                              // LogRE / LogREBullet / LogRENet

namespace
{
	/**
	 *  폭발 on/off (1=켬). 프로파일 하네스가 0으로 고정한다 (#98).
	 *
	 *  하네스는 re.Cheat.PlayerInvincible 로 플레이어를 살려두는데(#46, 안 그러면 Mass 탄이
	 *  0발로 측정된다), 그러면 무적 플레이어가 탄막 한가운데 서서 **초당 109회** 맞는다.
	 *  실제 게임플레이는 10발이면 사망하므로 결코 나오지 않는 비율이다.
	 */
	static TAutoConsoleVariable<int32> CVarExplosions(
		TEXT("re.Fx.Explosions"),
		1,
		TEXT("탄환 소멸 폭발 표시 (0=끔). 프로파일 측정 시 0으로 고정한다."),
		ECVF_Cheat);

	/**
	 *  동시 폭발 상한 (0 이하 = 무제한). 기본값이 #149 에서 12 → 256 으로 올라갔다.
	 *
	 *  **역할이 바뀌었다.** #147 때는 이 값이 드로우콜 상한이었다 — 폭발 1개가
	 *  컴포넌트 1개였고 드로우콜이 개수에 선형이라, 12 를 넘기면 드로우콜이 튀었다.
	 *  지금은 폭발이 ISM 인스턴스라 드로우콜이 개수와 무관하므로 그 근거가 사라졌다.
	 *
	 *  남겨두는 이유는 **오버드로우**다. 반투명 구체·링이 화면에서 겹치면 같은 픽셀을
	 *  여러 번 칠하고, 그 비용은 여전히 개수에 선형이다. 256 은 그 축의 안전장치다.
	 *  0 으로 두면 상한이 사라지므로 재빌드 없이 A/B 가 된다.
	 */
	static TAutoConsoleVariable<int32> CVarExplosionBudget(
		TEXT("re.Fx.ExplosionBudget"),
		256,
		TEXT("동시 폭발 상한 (0 이하=무제한). 드로우콜이 아니라 오버드로우 안전망이다 (#149)."),
		ECVF_Cheat);

	/**
	 *  폭발 수명(초). 진행도(0→1)의 분모이고 목록에서 언제 빠지는지가 이 값이다.
	 *  이전에는 Niagara 파티클 수명(convert_explosion_fx.py --maxlife)과 맞춰야 했지만,
	 *  이제 폭발 전체가 이 값으로만 그려지므로 외부와 맞출 대상이 없다 — 순수 룩 값이다.
	 */
	constexpr float ExplosionLifeSec = 0.8f;

	/** 살아있는 폭발. 최대 길이가 예산이라 선형 순회로 충분하다. */
	TArray<REExplosionFx::FLiveExplosion> GLiveExplosions;

	/**
	 *  만료분 제거 + 남은 것의 Progress 갱신.
	 *
	 *  스폰 경로와 렌더 경로가 같이 쓴다. 렌더 프로세서만 프루닝하면, 그 프로세서가
	 *  돌지 않는 넷모드에서 목록이 영원히 안 비고 예산이 가득 찬 채 잠겨 폭발이 조용히
	 *  전량 버려진다 — 스폰은 REBulletHitProcessor(AllNetModes)가 부르는데 렌더
	 *  프로세서는 Standalone|Client 라 리슨 서버에서 갈린다. 구 구조는 예산 확인이
	 *  곧 프루닝이라 이 틈이 없었다.
	 */
	void PruneExpiredExplosions(float NowSeconds)
	{
		// 뒤에서부터 돌며 RemoveAtSwap 한다. 마지막 원소가 i 로 들어오는데 그 원소의 원래
		// 인덱스는 항상 i 보다 크므로 이미 검사된 것이다 — 건너뛰는 항목이 없다.
		for (int32 i = GLiveExplosions.Num() - 1; i >= 0; --i)
		{
			const float Age = NowSeconds - GLiveExplosions[i].SpawnTime;

			// Age < 0 은 월드가 바뀌어 게임 시간이 되감긴 경우다. 그대로 두면 새 월드에서
			// 0.8초가 지날 때까지 유령 폭발이 남고 예산도 그만큼 먹는다.
			if (Age >= ExplosionLifeSec || Age < 0.f)
			{
				GLiveExplosions.RemoveAtSwap(i);
				continue;
			}
			GLiveExplosions[i].Progress = Age / ExplosionLifeSec;
		}
	}

	/** ExplosionProbe 누적 — 이름을 다른 파일과 겹치지 않게 둔다(유니티 빌드 C4459). */
	int32 GExplosionSpawnedSinceLog = 0;
	int32 GExplosionDroppedSinceLog = 0;
	float GExplosionLastProbeLog = 0.f;
}

void REExplosionFx::SpawnBulletExplosion(const UWorld* World, const FVector& Location)
{
	if (!World || World->GetNetMode() == NM_DedicatedServer)
	{
		return;   // 데디서버는 렌더가 없다
	}
	if (CVarExplosions.GetValueOnGameThread() == 0)
	{
		return;
	}

	// 예산을 재기 전에 만료분을 턴다. 렌더 프로세서가 돌지 않는 넷모드(리슨 서버)에서
	// 목록이 잠기는 것을 막는다 — 자세한 이유는 PruneExpiredExplosions 주석.
	PruneExpiredExplosions(World->GetTimeSeconds());

	// 오버드로우 안전망 (#149). 드로우콜은 이 값과 무관하다.
	const int32 Budget = CVarExplosionBudget.GetValueOnGameThread();
	if (Budget > 0 && GLiveExplosions.Num() >= Budget)
	{
		++GExplosionDroppedSinceLog;
		return;
	}

	FLiveExplosion& E = GLiveExplosions.AddDefaulted_GetRef();
	E.Loc = Location;
	E.SpawnTime = World->GetTimeSeconds();
	E.Progress = 0.f;
	++GExplosionSpawnedSinceLog;
}

const TArray<REExplosionFx::FLiveExplosion>& REExplosionFx::PruneAndGetLive(float NowSeconds)
{
	PruneExpiredExplosions(NowSeconds);

	// 프로브 — 1초에 1줄. 창 길이(Window)를 같이 찍는다: 프레임 간격이 일정하지 않으면
	// 실제 창이 1초보다 길다. 버린 수를 안 찍으면 예산이 걸린 것과 애초에 요청이 적은
	// 것을 구별할 수 없어 "폭발이 왜 안 보이나"를 되짚을 근거가 사라진다.
	if (GExplosionLastProbeLog == 0.f)
	{
		GExplosionLastProbeLog = NowSeconds;
	}
	const float Window = NowSeconds - GExplosionLastProbeLog;
	if (Window >= 1.f)
	{
		UE_LOG(LogREBullet, Log, TEXT("[RE] ExplosionProbe: 스폰=%d 버림=%d / %.2fs (%.1f/s, 동시=%d)"),
			GExplosionSpawnedSinceLog, GExplosionDroppedSinceLog, Window,
			GExplosionSpawnedSinceLog / Window, GLiveExplosions.Num());
		GExplosionSpawnedSinceLog = 0;
		GExplosionDroppedSinceLog = 0;
		GExplosionLastProbeLog = NowSeconds;
	}
	else if (Window < 0.f)
	{
		GExplosionLastProbeLog = NowSeconds;   // 월드 전환 — 창을 다시 잡는다
	}

	return GLiveExplosions;
}
