// Copyright Epic Games, Inc. All Rights Reserved.

#include "REGameMode.h"
#include "RECharacterBase.h"
#include "REPlayerController.h"
#include "MassEntitySubsystem.h"
#include "MassEntityManager.h"

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
}
