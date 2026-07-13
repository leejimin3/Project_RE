// Copyright Epic Games, Inc. All Rights Reserved.

#include "REBossCharacter.h"
#include "REBulletSpawnSubsystem.h"
#include "REBulletPatternGenerator.h"
#include "Net/UnrealNetwork.h"

AREBossCharacter::AREBossCharacter()
{
	PrimaryActorTick.bCanEverTick = false;

	// 체력 초기화 — MaxHealth 조정 시 정합 유지 (RECharacterBase 동일 패턴)
	Health = MaxHealth;
}

void AREBossCharacter::TriggerBulletPattern(EBulletPattern Pattern, int32 Seed, float StartTime)
{
	if (bIsDead)
	{
		return;
	}

	// TODO M5: Multicast_TriggerPattern RPC로 교체 (서버→클라 시드 브로드캐스트, 총알 자체는 미전송).
	//          현재는 싱글 로컬 직접 스폰 경로.

	UREBulletSpawnSubsystem* Spawner = GetWorld() ? GetWorld()->GetSubsystem<UREBulletSpawnSubsystem>() : nullptr;
	if (!Spawner)
	{
		UE_LOG(LogTemp, Warning, TEXT("[RE] Boss::TriggerBulletPattern: UREBulletSpawnSubsystem NULL"));
		return;
	}

	TArray<FBulletSpawnParams> Params;
	switch (Pattern)
	{
	case EBulletPattern::Spiral:
	{
		REBulletPattern::FSpiralParams SP;
		SP.BaseAngleDeg = SpiralBaseAngleDeg;
		Params = REBulletPattern::GenerateSpiral(GetActorLocation(), SP);
		UE_LOG(LogTemp, Log, TEXT("[RE] Boss Spiral: BaseAngle=%.1f -> N=%d"), SpiralBaseAngleDeg, Params.Num());
		SpiralBaseAngleDeg += SpiralRotationStepDeg;  // 다음 호출 시 회전
		break;
	}
	case EBulletPattern::Fan:
	{
		REBulletPattern::FFanParams FP;
		Params = REBulletPattern::GenerateFan(GetActorLocation(), FP);
		UE_LOG(LogTemp, Log, TEXT("[RE] Boss Fan: Spread=%.1f -> N=%d"), FP.SpreadDeg, Params.Num());
		break;
	}
	case EBulletPattern::Homing:
		// M1 범위 밖 — 슬롯만 유지, 미구현. 스폰 없이 종료.
		UE_LOG(LogTemp, Warning, TEXT("[RE] Boss: Homing 미구현 (M1 범위 밖)"));
		return;
	}
	Spawner->SpawnBulletBatch(Params);

	UE_LOG(LogTemp, Log, TEXT("[RE] Boss::TriggerBulletPattern: Pattern=%d Seed=%d Start=%.2f -> spawned %d entities at %s"),
		(int32)Pattern, Seed, StartTime, Params.Num(), *GetActorLocation().ToString());
}

float AREBossCharacter::TakeDamage(float DamageAmount, const FDamageEvent& DamageEvent,
                                    AController* EventInstigator, AActor* DamageCauser)
{
	// 서버 권위 가드 — 게임상태(Health) 변경은 서버에서만
	if (!HasAuthority())
	{
		return 0.f;
	}

	const float Applied = Super::TakeDamage(DamageAmount, DamageEvent, EventInstigator, DamageCauser);
	Health = FMath::Clamp(Health - Applied, 0.f, MaxHealth);

	if (Health <= 0.f && !bIsDead)
	{
		bIsDead = true;
		UE_LOG(LogTemp, Log, TEXT("[RE] Boss died (Health<=0)"));
	}

	return Applied;
}

void AREBossCharacter::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AREBossCharacter, Health);
}
