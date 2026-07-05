// Copyright Epic Games, Inc. All Rights Reserved.

#include "REBossCharacter.h"
#include "REBulletFragments.h"
#include "MassEntitySubsystem.h"
#include "MassEntityManager.h"

namespace
{
	/** M0 placeholder 스폰 개수. 실제 패턴별 탄 수는 M1. */
	constexpr int32 BulletsPerPattern = 16;
}

AREBossCharacter::AREBossCharacter()
{
	PrimaryActorTick.bCanEverTick = false;
}

void AREBossCharacter::TriggerBulletPattern(EBulletPattern Pattern, int32 Seed, float StartTime)
{
	// TODO M5: Multicast_TriggerPattern RPC로 교체 (서버→클라 시드 브로드캐스트, 총알 자체는 미전송).
	//          현재는 싱글 로컬 직접 스폰 경로.

	UMassEntitySubsystem* Mass = GetWorld() ? GetWorld()->GetSubsystem<UMassEntitySubsystem>() : nullptr;
	if (!Mass)
	{
		UE_LOG(LogTemp, Warning, TEXT("[RE] Boss::TriggerBulletPattern: UMassEntitySubsystem NULL"));
		return;
	}

	FMassEntityManager& EM = Mass->GetMutableEntityManager();
	FMassArchetypeHandle Arch = EM.CreateArchetype(
		{ FBulletSimFragment::StaticStruct(), FBulletRenderFragment::StaticStruct() });

	switch (Pattern)
	{
	case EBulletPattern::Spiral:
		// TODO M1: 나선 — 각도 증분으로 Velocity 세팅.
		break;
	case EBulletPattern::Fan:
		// TODO M1: 부채꼴 — 중심각 기준 좌우 분산 Velocity.
		break;
	case EBulletPattern::Homing:
		// TODO M1: 호밍 — 타깃 방향 Velocity + 추적 플래그.
		break;
	}

	// M0: 패턴 무관하게 placeholder 엔티티 N개 스폰 (Velocity=0, Lifetime=0). 실제 값은 M1.
	for (int32 i = 0; i < BulletsPerPattern; ++i)
	{
		EM.CreateEntity(Arch);
	}

	UE_LOG(LogTemp, Log, TEXT("[RE] Boss::TriggerBulletPattern: Pattern=%d Seed=%d Start=%.2f -> spawned %d entities"),
		(int32)Pattern, Seed, StartTime, BulletsPerPattern);
}
