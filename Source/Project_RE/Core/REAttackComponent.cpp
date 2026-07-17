// Copyright Epic Games, Inc. All Rights Reserved.

#include "REAttackComponent.h"
#include "REBossCharacter.h"
#include "Engine/DamageEvents.h"
#include "DrawDebugHelpers.h"
#include "GameFramework/Pawn.h"

UREAttackComponent::UREAttackComponent()
{
	// 호출 구동(Server RPC 경유) — 틱/타이머 불필요.
	PrimaryComponentTick.bCanEverTick = false;
}

bool UREAttackComponent::FireInDirection(const FVector& Dir)
{
	// 서버 전용 — 판정·데미지는 서버 권위 (호출자가 Server RPC지만 방어적 재가드).
	if (!GetOwner()->HasAuthority())
	{
		return false;
	}

	// rate limit — 클라 페이싱과 별개로 서버가 재검증. 0.9배: 프레임/네트워크 지터 허용 오차.
	const double Now = GetWorld()->GetTimeSeconds();
	if (LastFireTime >= 0.0 && Now - LastFireTime < AttackInterval * 0.9)
	{
		UE_LOG(LogTemp, Log, TEXT("[Attack] rate-limited (dt=%.2f)"), Now - LastFireTime);
		return false;
	}
	LastFireTime = Now;

	// 총구 높이(Z+50)에서 Dir 방향으로 사거리만큼 수평 트레이스. 자기 자신 무시.
	const FVector Start = GetOwner()->GetActorLocation() + FVector(0.f, 0.f, 50.f);
	const FVector End = Start + Dir * AttackRange;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(REAttack), /*bTraceComplex=*/false, GetOwner());
	FHitResult Hit;
	const bool bBlockingHit = GetWorld()->LineTraceSingleByChannel(Hit, Start, End, ECC_Pawn, Params);

	AREBossCharacter* Boss = bBlockingHit ? Cast<AREBossCharacter>(Hit.GetActor()) : nullptr;
	if (Boss)
	{
		// 서버 권위 데미지 — 보스 HP 차감은 AREBossCharacter::TakeDamage override가 수신.
		APawn* OwnerPawn = Cast<APawn>(GetOwner());
		AController* InstigatorController = OwnerPawn ? OwnerPawn->GetController() : nullptr;
		const float Applied = Boss->TakeDamage(Damage, FDamageEvent(), InstigatorController, GetOwner());
		UE_LOG(LogTemp, Log, TEXT("[Attack] hit boss, applied=%.1f"), Applied);
	}
	else
	{
		// 방향이 빗나감(0) 또는 다른 것에 막힘(1) — 유저 조준 실패는 정상 케이스.
		UE_LOG(LogTemp, Log, TEXT("[Attack] miss (blocked=%d)"), bBlockingHit ? 1 : 0);
	}

#if ENABLE_DRAW_DEBUG
	// 개발 확인용 트레이스 라인 — 히트=빨강, 미스=초록. Shipping 자동 제외.
	DrawDebugLine(GetWorld(), Start, bBlockingHit ? Hit.ImpactPoint : End,
		Boss ? FColor::Red : FColor::Green, false, 0.2f, 0, 1.f);
#endif

	return true;
}
