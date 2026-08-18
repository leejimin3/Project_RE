// Copyright Epic Games, Inc. All Rights Reserved.

#include "REExplosionFx.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraSystem.h"
#include "NiagaraComponentPoolMethodEnum.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"

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
	 *  폭발 스케일. 엔진 템플릿 SimpleExplosion 은 기본 크기가 이 게임 스케일(탄 지름 50uu)에
	 *  비해 과하다 — 실측 스크린샷에서 플레이어를 완전히 덮었다.
	 */
	constexpr float ExplosionScale = 0.06f;

	/** 폭발 에셋. scripts/make_explosion_fx.py 가 엔진 템플릿을 복제해 만든다. */
	const TCHAR* ExplosionAssetPath = TEXT("/Game/FX/NS_REBulletExplosion.NS_REBulletExplosion");

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
			UE_LOG(LogTemp, Error, TEXT("[RE] NS_REBulletExplosion 로드 실패 — 폭발이 표시되지 않는다 (#98)"));
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
	UE_LOG(LogTemp, Log, TEXT("[RE] ExplosionFx: 선로드 %s (#107)"),
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

	// AutoRelease: 엔진 컴포넌트 풀에서 꺼내 쓰고 자동 반납. one-shot fx 전용 경로다.
	// 개별 스폰의 실용 상한을 크게 올려준다 — 인자 하나이므로 안 쓸 이유가 없다.
	UNiagaraFunctionLibrary::SpawnSystemAtLocation(
		World, GCachedSystem, Location,
		FRotator::ZeroRotator, FVector(ExplosionScale),
		/*bAutoDestroy=*/true, /*bAutoActivate=*/true,
		ENCPoolMethod::AutoRelease);

	// 스폰 수 계측 — 폭발 비용이 "템플릿이 무거운가"인지 "스폰이 폭주하는가"인지 가른다.
	// 로그가 측정을 오염시키지 않게 1초에 1줄로 억제한다.
	// 창 길이(Window)를 같이 찍는다: 이 블록은 스폰이 일어난 순간에만 실행되므로 스폰이
	// 드문드문하면 실제 창이 1초보다 길다. 창을 안 찍으면 서로 다른 비율이 모두 같은 수로
	// 보여 오독한다 — 실제로 그렇게 오독했다.
	{
		static int32 Count = 0;
		static double LastLog = 0.0;
		++Count;
		const double Now = FPlatformTime::Seconds();
		if (LastLog == 0.0) { LastLog = Now; }
		const double Window = Now - LastLog;
		if (Window >= 1.0)
		{
			UE_LOG(LogTemp, Log, TEXT("[RE] ExplosionProbe: 스폰=%d / %.2fs (%.1f/s)"),
				Count, Window, Count / Window);
			Count = 0;
			LastLog = Now;
		}
	}
}
