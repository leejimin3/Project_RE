// Copyright Epic Games, Inc. All Rights Reserved.

#include "REBossCharacter.h"
#include "REBulletSpawnSubsystem.h"

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

	UREBulletSpawnSubsystem* Spawner = GetWorld() ? GetWorld()->GetSubsystem<UREBulletSpawnSubsystem>() : nullptr;
	if (!Spawner)
	{
		UE_LOG(LogTemp, Warning, TEXT("[RE] Boss::TriggerBulletPattern: UREBulletSpawnSubsystem NULL"));
		return;
	}

	switch (Pattern)
	{
	case EBulletPattern::Spiral:
		// TODO M1(#16): 나선 — 각도 증분으로 Velocity 세팅.
		break;
	case EBulletPattern::Fan:
		// TODO M1(#16): 부채꼴 — 중심각 기준 좌우 분산 Velocity.
		break;
	case EBulletPattern::Homing:
		// TODO M1(#16): 호밍 — 타깃 방향 Velocity + 추적 플래그.
		break;
	}

	// #14: 보스 위치에서 N발 스폰. Velocity=0/Lifetime=0 (패턴 수학은 #16, 이동은 #15).
	TArray<FBulletSpawnParams> Params;
	Params.Reserve(BulletsPerPattern);
	for (int32 i = 0; i < BulletsPerPattern; ++i)
	{
		Params.Add({ GetActorLocation(), FVector::ZeroVector, 0.f });
	}
	Spawner->SpawnBulletBatch(Params);

	UE_LOG(LogTemp, Log, TEXT("[RE] Boss::TriggerBulletPattern: Pattern=%d Seed=%d Start=%.2f -> spawned %d entities at %s"),
		(int32)Pattern, Seed, StartTime, BulletsPerPattern, *GetActorLocation().ToString());
}
