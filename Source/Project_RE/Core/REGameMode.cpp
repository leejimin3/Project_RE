// Copyright Epic Games, Inc. All Rights Reserved.

#include "REGameMode.h"
#include "RECharacterBase.h"
#include "REPlayerController.h"
#include "MassEntitySubsystem.h"
#include "MassEntityManager.h"
#include "REBulletSimProcessor.h"
#include "REBulletRenderProcessor.h"
#include "REBossCharacter.h"

AREGameMode::AREGameMode()
{
	DefaultPawnClass = ARECharacterBase::StaticClass();
	PlayerControllerClass = AREPlayerController::StaticClass();
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
}
