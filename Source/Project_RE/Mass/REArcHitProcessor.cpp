// Copyright Epic Games, Inc. All Rights Reserved.

#include "REArcHitProcessor.h"
#include "REArcSimProcessor.h"
#include "REBulletFragments.h"
#include "MassExecutionContext.h"
#include "Core/RECharacterBase.h"
#include "Abilities/REGameplayTags.h"
#include "AbilitySystemComponent.h"
#include "Engine/DamageEvents.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"

UREArcHitProcessor::UREArcHitProcessor()
	: EntityQuery(*this)
{
	ExecutionFlags = (int32)(EProcessorExecutionFlags::Standalone | EProcessorExecutionFlags::Server);
	bRequiresGameThreadExecution = true;   // TakeDamage 액터 호출 → GT 전용

	// Sim이 위치·착지 상태를 갱신한 뒤 같은 프레임에 판정.
	ExecutionOrder.ExecuteAfter.Add(UREArcSimProcessor::StaticClass()->GetFName());
}

void UREArcHitProcessor::ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager)
{
	EntityQuery.AddRequirement<FArcBulletFragment>(EMassFragmentAccess::ReadOnly);
	EntityQuery.AddTagRequirement<FArcBulletTag>(EMassFragmentPresence::All);
}

void UREArcHitProcessor::Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(RE_ArcHit);

	UWorld* World = EntityManager.GetWorld();
	APawn* Pawn = World ? UGameplayStatics::GetPlayerPawn(World, 0) : nullptr;
	ARECharacterBase* Player = Cast<ARECharacterBase>(Pawn);
	if (!Player)
	{
		return;  // 플레이어 없으면 no-op
	}

	// 대쉬 무적 — State.Dashing이면 이번 프레임 착지 판정 전체 스킵.
	const UAbilitySystemComponent* ASC = Player->GetAbilitySystemComponent();
	if (ASC && ASC->HasMatchingGameplayTag(RETag_State_Dashing))
	{
		return;
	}

	const FVector PlayerLoc = Player->GetActorLocation();

	EntityQuery.ForEachEntityChunk(Context, [&](FMassExecutionContext& Ctx)
	{
		const int32 Num = Ctx.GetNumEntities();
		const TConstArrayView<FArcBulletFragment> Arcs = Ctx.GetFragmentView<FArcBulletFragment>();

		for (int32 i = 0; i < Num; ++i)
		{
			const FArcBulletFragment& A = Arcs[i];
			if (A.Elapsed < A.FlightTime)
			{
				continue;   // 아직 비행 중 — 착지 프레임만 판정
			}
			// 착지: 마커 반경 안 플레이어면 범위 데미지. XY 평면 거리(탑다운).
			if (FVector::DistSquaredXY(A.Target, PlayerLoc) <= A.Radius * A.Radius)
			{
				const float Applied = Player->TakeDamage(A.Damage, FDamageEvent(), nullptr, nullptr);
				UE_LOG(LogTemp, Log, TEXT("[RE] ArcHit: Applied=%.0f R=%.0f"), Applied, A.Radius);
			}
		}
	});
}
