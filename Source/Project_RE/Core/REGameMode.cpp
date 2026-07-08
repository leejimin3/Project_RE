// Copyright Epic Games, Inc. All Rights Reserved.

#include "REGameMode.h"
#include "RECharacterBase.h"
#include "REPlayerController.h"
#include "MassEntitySubsystem.h"
#include "MassEntityManager.h"
#include "REBulletSimProcessor.h"
#include "REBulletRenderProcessor.h"
#include "REBossCharacter.h"
#include "REBulletSpawnSubsystem.h"
#include "Mass/EntityFragments.h"  // FTransformFragment

AREGameMode::AREGameMode()
{
	DefaultPawnClass = ARECharacterBase::StaticClass();
	PlayerControllerClass = AREPlayerController::StaticClass();
	PrimaryActorTick.bCanEverTick = true;
}

void AREGameMode::BeginPlay()
{
	Super::BeginPlay();

	// Mass 스모크 테스트: 서브시스템 얻고 엔티티 1개 생성 → 로그.
	// GameMode는 서버 권위라 HasAuthority 가드 불필요.
	if (UMassEntitySubsystem* Mass = GetWorld()->GetSubsystem<UMassEntitySubsystem>())
	{
		FMassEntityManager& EM = Mass->GetMutableEntityManager();
		FMassArchetypeHandle Arch = EM.CreateArchetype({ FRETestFragment::StaticStruct() });
		FMassEntityHandle E = EM.CreateEntity(Arch);
		UE_LOG(LogTemp, Log, TEXT("[RE] Mass entity created: Index=%d Serial=%d"), E.Index, E.SerialNumber);
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("[RE] UMassEntitySubsystem NULL"));
	}

	// #4 검증: 두 Processor CDO의 ExecutionFlags 확인. Sim=7(AllNetModes), Render=5(Standalone|Client).
	const uint8 SimFlags    = (uint8)GetDefault<UREBulletSimProcessor>()->GetExecutionFlags();
	const uint8 RenderFlags = (uint8)GetDefault<UREBulletRenderProcessor>()->GetExecutionFlags();
	UE_LOG(LogTemp, Log, TEXT("[RE] SimProcessor flags=%d  RenderProcessor flags=%d"), SimFlags, RenderFlags);

	// #5 검증: 보스 스폰 후 탄막 트리거 → 싱글 경로 스폰 카운트 실증.
	// AlwaysSpawn: 원점 캡슐 충돌로 스폰 실패하는 것 방지 (검증용 보스라 위치 무관).
	FActorSpawnParameters BossSpawnParams;
	BossSpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	if (AREBossCharacter* Boss = GetWorld()->SpawnActor<AREBossCharacter>(
			AREBossCharacter::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator, BossSpawnParams))
	{
		Boss->TriggerBulletPattern(EBulletPattern::Spiral, 12345, 0.f);
	}

	// #15 프로브: nonzero velocity/lifetime 탄환 1발 → SimProcessor 이동/파괴 관측용.
	// MassGameplay 플러그인의 페이즈 매니저가 SimProcessor를 매 프레임 자동 구동(PrePhysics).
	if (UREBulletSpawnSubsystem* Spawner = GetWorld()->GetSubsystem<UREBulletSpawnSubsystem>())
	{
		ProbeBullet = Spawner->SpawnBullet(FVector::ZeroVector, FVector(100.f, 0.f, 0.f), 0.5f);
		UE_LOG(LogTemp, Log, TEXT("[RE] SimProbe spawn: Vel=(100,0,0) Life=0.50"));
	}
}

void AREGameMode::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (!ProbeBullet.IsSet())
	{
		return;
	}

	ProbeElapsed += DeltaSeconds;

	UMassEntitySubsystem* Mass = GetWorld()->GetSubsystem<UMassEntitySubsystem>();
	if (!Mass)
	{
		return;
	}
	FMassEntityManager& EM = Mass->GetMutableEntityManager();

	if (EM.IsEntityValid(ProbeBullet))
	{
		const FVector Loc = EM.GetFragmentDataChecked<FTransformFragment>(ProbeBullet).GetTransform().GetLocation();
		UE_LOG(LogTemp, Log, TEXT("[RE] SimProbe: t=%.2f Loc=%s Alive=1"), ProbeElapsed, *Loc.ToString());
	}
	else
	{
		UE_LOG(LogTemp, Log, TEXT("[RE] SimProbe: t=%.2f Alive=0 (destroyed)"), ProbeElapsed);
		ProbeBullet.Reset();  // 파괴 확인 후 로그 종료.
	}
}
