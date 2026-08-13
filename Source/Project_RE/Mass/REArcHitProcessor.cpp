// Copyright Epic Games, Inc. All Rights Reserved.

#include "REArcHitProcessor.h"
#include "REArcSimProcessor.h"
#include "REBulletFragments.h"
#include "MassExecutionContext.h"
#include "Core/RECharacterBase.h"
#include "Engine/DamageEvents.h"
#include "Engine/World.h"
#include "REHitTargets.h"

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

	// 살아있고 대쉬 중이 아닌 플레이어 전원 (#86).
	TArray<FREHitTarget> Targets;
	GatherHitTargets(EntityManager.GetWorld(), Targets);
	if (Targets.IsEmpty())
	{
		return;
	}

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
			// 착지: 마커 반경 안 플레이어 전원에게 범위 데미지. XY 평면 거리(탑다운).
			// break 없음 — 범위 폭발이라 겹친 인원이 모두 맞는다. 소멸은 Sim이 착지 시 처리한다.
			for (const FREHitTarget& T : Targets)
			{
				if (FVector::DistSquaredXY(A.Target, T.Location) <= A.Radius * A.Radius)
				{
					const float Applied = T.Player->TakeDamage(A.Damage, FDamageEvent(), nullptr, nullptr);
					UE_LOG(LogTemp, Log, TEXT("[RE] ArcHit: Applied=%.0f R=%.0f"), Applied, A.Radius);
				}
			}
		}
	});
}
