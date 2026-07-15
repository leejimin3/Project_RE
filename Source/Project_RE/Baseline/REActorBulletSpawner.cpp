// Copyright Epic Games, Inc. All Rights Reserved.

#include "REActorBulletSpawner.h"
#include "REBulletActor.h"
#include "REBulletPatternGenerator.h"   // Mass와 공유하는 순수 함수 — 읽기 전용 참조 (복붙 금지)
#include "Materials/MaterialInstanceDynamic.h"
#include "Engine/World.h"
#include "EngineUtils.h"                // TActorIterator
#include "TimerManager.h"

namespace
{
	/** 동시 유지 목표 탄환 수. 0 = 액터 경로 비활성 (기본). Mass 기본 부하와 맞추려면 480. */
	static TAutoConsoleVariable<int32> CVarActorBulletCount(
		TEXT("re.ActorBullets.Count"),
		0,
		TEXT("Actor 탄환 동시 유지 목표 수 (0=비활성). Mass 비교군 — 측정 전용."),
		ECVF_Cheat);

	/** Mass 데모 발사 주기 (REGameMode.cpp:67 DemoFireTimer). */
	constexpr float FireInterval = 0.1f;

	/** Mass 스폰 원점 = Boss 스폰 위치 (REGameMode.cpp:53). Boss는 움직이지 않는다. */
	const FVector SpawnOrigin(0.f, 0.f, 90.f);

	/** Boss의 SpiralRotationStepDeg (REBossCharacter.h:58). */
	constexpr float RotationStepDeg = 15.f;
}

void UREActorBulletSpawner::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);

	// 데디서버/에디터 프리뷰 월드는 제외 — 측정은 싱글(게임 월드)에서만.
	if (InWorld.GetNetMode() == NM_DedicatedServer || !InWorld.IsGameWorld())
	{
		return;
	}

	// 공유 MID 1개 — 색은 Mass ISM과 동일 (REBulletRenderSubsystem.cpp:37-43).
	if (UMaterialInterface* Base = LoadObject<UMaterialInterface>(
			nullptr, TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial")))
	{
		SharedMID = UMaterialInstanceDynamic::Create(Base, this);
		if (SharedMID)
		{
			SharedMID->SetVectorParameterValue(TEXT("Color"), FLinearColor::Red);
		}
	}
	if (!SharedMID)
	{
		UE_LOG(LogTemp, Warning, TEXT("[RE] ActorBulletSpawner: SharedMID 생성 실패 (탄환 기본색으로 진행)"));
	}

	// 자립 구동 — GameMode 무관. CVar가 0이면 Fire()가 즉시 return이라 비용 무시 가능.
	InWorld.GetTimerManager().SetTimer(FireTimer, this, &UREActorBulletSpawner::Fire,
		FireInterval, /*bLoop=*/true);
}

void UREActorBulletSpawner::Fire()
{
	const int32 Target = CVarActorBulletCount.GetValueOnGameThread();
	if (Target <= 0)
	{
		return;  // 0 = 비활성. 평상시 게임 영향 0.
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	REBulletPattern::FSpiralParams SP;  // Speed 300 / Lifetime 3 = 기본값 = Mass와 동일

	// 정상상태 탄 수 = PerShot * (Lifetime / Interval). 목표 Target을 만족하는 PerShot 역산.
	// 소수부는 누산해 다음 발사로 넘긴다 (매번 올림하면 목표를 최대 +30% 초과한다).
	PerShotAccum += Target / (SP.Lifetime / FireInterval);
	const int32 N = FMath::FloorToInt(PerShotAccum);
	PerShotAccum -= N;
	if (N <= 0)
	{
		return;
	}

	SP.Count        = N;
	SP.AngleStepDeg = 360.f / N;   // 균등 링
	SP.BaseAngleDeg = BaseAngleDeg;

	FActorSpawnParameters SpawnParams;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	for (const FBulletSpawnParams& P : REBulletPattern::GenerateSpiral(SpawnOrigin, SP))
	{
		if (AREBulletActor* Bullet = World->SpawnActor<AREBulletActor>(
				AREBulletActor::StaticClass(), P.Location, FRotator::ZeroRotator, SpawnParams))
		{
			Bullet->Init(P.Velocity, P.Lifetime, SharedMID);
		}
	}

	BaseAngleDeg += RotationStepDeg;

	// 프로브: 1초에 1줄 (로그가 측정을 오염시키지 않게).
	if ((FireCount++ % 10) == 0)
	{
		int32 Live = 0;
		for (TActorIterator<AREBulletActor> It(World); It; ++It)
		{
			++Live;
		}
		UE_LOG(LogTemp, Log, TEXT("[RE] ActorBulletProbe: live=%d target=%d perShot=%d"), Live, Target, N);
	}
}
