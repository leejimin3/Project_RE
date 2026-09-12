// Copyright Epic Games, Inc. All Rights Reserved.

#include "REExplosionFx.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraSystem.h"
#include "NiagaraComponentPoolMethodEnum.h"
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
	 *  실제 게임플레이는 10발이면 사망하므로 결코 나오지 않는 비율이다. 이 상태로 재면
	 *  탄환 상한 측정이 폭발 비용에 오염돼 변경 간 비교가 불가능해진다.
	 *  폭발 자체의 비용은 별도로, 현실적인 빈도에서 잰다.
	 */
	static TAutoConsoleVariable<int32> CVarExplosions(
		TEXT("re.Fx.Explosions"),
		1,
		TEXT("탄환 소멸 폭발 표시 (0=끔). 프로파일 측정 시 0으로 고정한다."),
		ECVF_Cheat);

	/**
	 *  폭발 스케일. 팩 이펙트는 "Big" 계열이라 이 게임 스케일(탄 지름 50uu)에 비해 과하다 —
	 *  실측 스크린샷 기준값이다. 엔진 템플릿(SimpleExplosion)을 쓰던 시절 값은 0.25 였다;
	 *  애셋을 갈아끼우면 이 값도 화면으로 다시 잡아야 한다 (#119).
	 */
	constexpr float ExplosionScale = 0.06f;

	/** 폭발 에셋. scripts/convert_explosion_fx.py 가 팩의 Cascade 시스템을 Niagara 로
	 *  변환해 만든다. 팩은 gitignore 대상이라 새 환경에서는 그 스크립트를 돌려야 한다 (#119). */
	const TCHAR* ExplosionAssetPath = TEXT("/Game/FX/NS_REBulletExplosion.NS_REBulletExplosion");

	/**
	 *  동시 폭발 컴포넌트 상한 (#147). 0 이하 = 무제한(수정 전 동작 — A/B 측정용).
	 *
	 *  폭발 1회 = `UNiagaraComponent` 1개 = 프리미티브 프록시 1개다. 컴포넌트 사이에는
	 *  배칭이 없으므로 **드로우콜이 동시 컴포넌트 수에 그대로 비례**한다 — 실측 컴포넌트당
	 *  4.9 드로우콜(5,000발 기준 스폰 115/s → 동시 ~92개 → Draws 228 → 677).
	 *  `AutoRelease` 풀링은 컴포넌트 *할당* 비용만 줄이고 살아있는 개수는 그대로라
	 *  이 축에 대해 아무것도 하지 않는다.
	 *
	 *  상한이 없으면 ArtilleryStorm(착지 StormCount×StormArms/FireInterval = 160/s,
	 *  수명 0.8s → 동시 ~128개)에서 드로우콜이 1,000+ 로 튀어 탄막 ISM 배칭 이점(#50)을
	 *  통째로 상쇄한다.
	 *
	 *  기본 12 는 **실플레이에서 실측한 최대 동시 개수**다 — 피격 ~10/s × 수명 0.8s 구간의
	 *  `ExplosionProbe` 가 동시 7~12개를 찍었다. 즉 상한이 걸리기 시작하는 지점이 평상시
	 *  최대치와 같아, 폭풍 페이즈가 아닌 구간에서는 화면이 수정 전과 같다.
	 *  16 도 재봤으나 드로우콜만 +22 늘고(297 → 319) 화면에서 얻는 것이 없었다.
	 *
	 *  **자리로 병합하는 방식은 쓰지 않는다.** 직선탄 피격은 전부 플레이어 반경
	 *  `REBulletGeometry::HitRadius`(60uu) 안에서 터지므로, 그보다 큰 병합 반경은 폭풍이
	 *  아니라 **평상시 피격 연출을 죽인다** — 실측으로 250uu 병합이 10/s 를 1/s(동시 1개)로
	 *  깎았다. 드로우콜 상한은 개수만으로 잡히므로 병합은 비용만 있고 얻는 것이 없다.
	 */
	static TAutoConsoleVariable<int32> CVarExplosionBudget(
		TEXT("re.Fx.ExplosionBudget"),
		12,
		TEXT("동시 폭발 컴포넌트 상한 (0 이하=무제한). 드로우콜 상한을 이 값이 쥔다."),
		ECVF_Cheat);

	/**
	 *  폭발 파티클 수명(초) — 예산을 언제 회수할지가 이 값이다.
	 *  `scripts/convert_explosion_fx.py` 의 `--maxlife` 기본값과 같아야 한다.
	 *  스크립트 쪽을 바꾸면 여기도 바꿔라 — 어긋나면 예산이 실제 컴포넌트 수와 따로 논다.
	 */
	constexpr double ExplosionLifeSec = 0.8;

	/** 살아있는 폭발의 만료 시각. 최대 길이가 예산이라 선형 순회로 충분하다. */
	TArray<double> GLiveExplosionExpiry;

	/**
	 *  예산 확보. 만료분을 걷어내고, 상한을 넘으면 버린다.
	 *
	 *  호출부가 전부 게임 스레드 고정이라(Niagara 스폰 자체가 GT 전용) 락이 없다.
	 *  월드가 바뀌어도 따로 비우지 않는다 — 항목이 시간으로 만료되므로 최대 0.8초 뒤
	 *  스스로 정리된다. 그 창에서 잃는 것은 폭발 몇 개뿐이다.
	 */
	bool TryClaimExplosionBudget(double Now)
	{
		for (int32 i = GLiveExplosionExpiry.Num() - 1; i >= 0; --i)
		{
			if (GLiveExplosionExpiry[i] <= Now)
			{
				GLiveExplosionExpiry.RemoveAtSwap(i);
			}
		}

		const int32 Budget = CVarExplosionBudget.GetValueOnGameThread();
		if (Budget > 0 && GLiveExplosionExpiry.Num() >= Budget)
		{
			return false;
		}

		GLiveExplosionExpiry.Add(Now + ExplosionLifeSec);
		return true;
	}

	/**
	 *  로드 캐시 — 매 폭발마다 LoadObject 하지 않는다.
	 *  raw 정적 포인터라 GC 가 추적하지 않는다. AddToRoot 로 하드 참조를 잡아두지 않으면
	 *  에셋이 수집된 뒤 댕글링 포인터로 크래시한다.
	 */
	UNiagaraSystem* GCachedSystem = nullptr;
	bool GLoadAttempted = false;

	/** 1회만 로드. 실패해도 재시도하지 않는다 — 매 폭발마다 실패 로드를 반복하지 않게. */
	void EnsureLoaded()
	{
		if (GLoadAttempted)
		{
			return;
		}
		GLoadAttempted = true;
		GCachedSystem = LoadObject<UNiagaraSystem>(nullptr, ExplosionAssetPath);
		if (GCachedSystem)
		{
			GCachedSystem->AddToRoot();   // GC 가 추적하지 않는 정적 포인터 — 하드 참조 필수
		}
		else
		{
			// 조용한 무동작 금지 — 폭발이 안 나오는 것을 눈치채기 어렵다.
			UE_LOG(LogREBullet, Error, TEXT("[RE] NS_REBulletExplosion 로드 실패 — 폭발이 표시되지 않는다 (#98)"));
		}
	}
}

void REExplosionFx::Preload(const UWorld* World)
{
	if (!World || World->GetNetMode() == NM_DedicatedServer)
	{
		return;
	}
	EnsureLoaded();
	UE_LOG(LogREBullet, Log, TEXT("[RE] ExplosionFx: 선로드 %s (#107)"),
		GCachedSystem ? TEXT("완료") : TEXT("실패"));
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

	// 정상 경로에서는 Preload 가 이미 끝냈다 (#107). 여기 남겨두는 건 안전망이다 —
	// 선로드를 안 탄 월드에서도 폭발은 나와야 한다. 그 경우 첫 스폰이 ~290ms 멈춘다.
	EnsureLoaded();
	if (!GCachedSystem)
	{
		return;
	}

	// 드로우콜 상한 (#147). 컴포넌트 사이에 배칭이 없어 드로우콜이 동시 개수에 비례하므로
	// 개수 자체를 쥐는 것 말고는 손댈 축이 없다.
	const double Now = FPlatformTime::Seconds();
	const bool bClaimed = TryClaimExplosionBudget(Now);

	if (bClaimed)
	{
		// AutoRelease: 엔진 컴포넌트 풀에서 꺼내 쓰고 자동 반납. one-shot fx 전용 경로다.
		// 개별 스폰의 실용 상한을 크게 올려준다 — 인자 하나이므로 안 쓸 이유가 없다.
		// (풀링은 *할당* 비용만 줄인다. 드로우콜은 위 예산이 쥔다.)
		UNiagaraFunctionLibrary::SpawnSystemAtLocation(
			World, GCachedSystem, Location,
			FRotator::ZeroRotator, FVector(ExplosionScale),
			/*bAutoDestroy=*/true, /*bAutoActivate=*/true,
			ENCPoolMethod::AutoRelease);
	}

	// 스폰 수 계측 — 폭발 비용이 "템플릿이 무거운가"인지 "스폰이 폭주하는가"인지 가른다.
	// 로그가 측정을 오염시키지 않게 1초에 1줄로 억제한다.
	// 창 길이(Window)를 같이 찍는다: 이 블록은 요청이 들어온 순간에만 실행되므로 요청이
	// 드문드문하면 실제 창이 1초보다 길다. 창을 안 찍으면 서로 다른 비율이 모두 같은 수로
	// 보여 오독한다 — 실제로 그렇게 오독했다.
	// 버린 수(#147)를 같이 찍는다: 안 찍으면 예산이 걸린 것과 애초에 요청이 적은 것을
	// 구별할 수 없어 "폭발이 왜 안 보이나"를 되짚을 근거가 사라진다. 요청 시점에 찍으므로
	// 전량이 버려지는 상황에서도 로그는 계속 나온다.
	{
		static int32 Count = 0;
		static int32 Dropped = 0;
		static double LastLog = 0.0;
		if (bClaimed) { ++Count; } else { ++Dropped; }
		if (LastLog == 0.0) { LastLog = Now; }
		const double Window = Now - LastLog;
		if (Window >= 1.0)
		{
			UE_LOG(LogREBullet, Log, TEXT("[RE] ExplosionProbe: 스폰=%d 버림=%d / %.2fs (%.1f/s, 동시=%d)"),
				Count, Dropped, Window, Count / Window, GLiveExplosionExpiry.Num());
			Count = 0;
			Dropped = 0;
			LastLog = Now;
		}
	}
}
