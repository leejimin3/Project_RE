// Copyright Epic Games, Inc. All Rights Reserved.

#include "REAutoFireComponent.h"
#include "REBossCharacter.h"
#include "EngineUtils.h"
#include "TimerManager.h"
#include "Engine/DamageEvents.h"
#include "DrawDebugHelpers.h"
#include "GameFramework/Pawn.h"

UREAutoFireComponent::UREAutoFireComponent()
{
	// 타이머 구동 — 틱 불필요.
	PrimaryComponentTick.bCanEverTick = false;
}

void UREAutoFireComponent::BeginPlay()
{
	Super::BeginPlay();

	// 서버 전용 — 자동사격엔 클라 입력이 없어 RPC 불필요. 클라에선 타이머 자체를 안 돈다.
	if (!GetOwner()->HasAuthority())
	{
		return;
	}
	GetWorld()->GetTimerManager().SetTimer(FireTimer, this, &UREAutoFireComponent::Fire, FireInterval, /*bLoop=*/true);
}

void UREAutoFireComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	GetWorld()->GetTimerManager().ClearTimer(FireTimer);
	Super::EndPlay(EndPlayReason);
}

void UREAutoFireComponent::Fire()
{
	// 최근접 보스 탐색 — 보스 1~2마리 전제, 매 발사 전체 스캔(캐싱 불필요).
	AREBossCharacter* Nearest = nullptr;
	float BestDistSq = TNumericLimits<float>::Max();
	const FVector OwnerLoc = GetOwner()->GetActorLocation();
	for (TActorIterator<AREBossCharacter> It(GetWorld()); It; ++It)
	{
		const float DistSq = FVector::DistSquared(OwnerLoc, It->GetActorLocation());
		if (DistSq < BestDistSq)
		{
			BestDistSq = DistSq;
			Nearest = *It;
		}
	}
	if (!Nearest)
	{
		// 보스 없는 맵 — 발사 스킵 (스팸 방지 Verbose).
		UE_LOG(LogTemp, Verbose, TEXT("[AutoFire] no boss found"));
		return;
	}

	// 총구 높이(Z+50)에서 보스 캡슐 중심으로 트레이스. 자기 자신 무시.
	const FVector Start = OwnerLoc + FVector(0.f, 0.f, 50.f);
	const FVector End = Nearest->GetActorLocation();
	FCollisionQueryParams Params(SCENE_QUERY_STAT(REAutoFire), /*bTraceComplex=*/false, GetOwner());
	FHitResult Hit;
	const bool bBlockingHit = GetWorld()->LineTraceSingleByChannel(Hit, Start, End, ECC_Pawn, Params);

	const bool bHitBoss = bBlockingHit && Hit.GetActor() == Nearest;
	if (bHitBoss)
	{
		// 서버 권위 데미지 — AActor::TakeDamage 진입점 호출까지가 #26. HP 차감은 #28의 override.
		APawn* OwnerPawn = Cast<APawn>(GetOwner());
		AController* InstigatorController = OwnerPawn ? OwnerPawn->GetController() : nullptr;
		const float Applied = Nearest->TakeDamage(Damage, FDamageEvent(), InstigatorController, GetOwner());
		UE_LOG(LogTemp, Log, TEXT("[AutoFire] hit boss, applied=%.1f"), Applied);
	}
	else
	{
		// 장애물에 막힘(1) 또는 노히트(0) — 탑뷰 개활지에선 드묾.
		UE_LOG(LogTemp, Log, TEXT("[AutoFire] miss (blocked=%d)"), bBlockingHit ? 1 : 0);
	}

#if ENABLE_DRAW_DEBUG
	// 개발 확인용 트레이스 라인 — 히트=빨강, 미스=초록. Shipping 자동 제외.
	DrawDebugLine(GetWorld(), Start, bBlockingHit ? Hit.ImpactPoint : End,
		bHitBoss ? FColor::Red : FColor::Green, false, 0.2f, 0, 1.f);
#endif
}
