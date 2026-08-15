// Copyright Epic Games, Inc. All Rights Reserved.

#include "REArcFxProcessor.h"
#include "REArcSimProcessor.h"
#include "REBulletFragments.h"
#include "REExplosionFx.h"
#include "MassExecutionContext.h"
#include "Mass/EntityFragments.h"   // FTransformFragment
#include "Engine/World.h"

UREArcFxProcessor::UREArcFxProcessor()
	: EntityQuery(*this)
{
	// 클라·싱글 전용 — 데디서버는 렌더가 없다.
	ExecutionFlags = (int32)(EProcessorExecutionFlags::Standalone | EProcessorExecutionFlags::Client);

	// Niagara 스폰은 게임 스레드 전용. 워커에서 부르면 크래시한다.
	bRequiresGameThreadExecution = true;

	// 시뮬보다 먼저 돈다 — 시뮬이 착지한 탄을 파괴하기 전에 위치를 읽어야 한다.
	// 클래스에서 이름을 얻는다: 문자열 리터럴은 오타가 나도 조용히 무시돼 폭발이 한 번도 안 뜬다.
	ExecutionOrder.ExecuteBefore.Add(UREArcSimProcessor::StaticClass()->GetFName());
}

void UREArcFxProcessor::ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager)
{
	EntityQuery.AddRequirement<FTransformFragment>(EMassFragmentAccess::ReadOnly);
	EntityQuery.AddRequirement<FArcBulletFragment>(EMassFragmentAccess::ReadWrite);   // bFxSpawned 표시
	EntityQuery.AddTagRequirement<FArcBulletTag>(EMassFragmentPresence::All);
}

void UREArcFxProcessor::Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(RE_ArcFx);

	UWorld* World = EntityManager.GetWorld();
	if (!World)
	{
		return;
	}

	EntityQuery.ForEachEntityChunk(Context, [World](FMassExecutionContext& Ctx)
	{
		const int32 Num = Ctx.GetNumEntities();
		const TConstArrayView<FTransformFragment> T = Ctx.GetFragmentView<FTransformFragment>();
		const TArrayView<FArcBulletFragment> A = Ctx.GetMutableFragmentView<FArcBulletFragment>();
		for (int32 i = 0; i < Num; ++i)
		{
			// 착지했고 아직 폭발을 안 띄웠으면 한 번만.
			// bFxSpawned 없이 실행 순서에만 기대면 지연 파괴 때문에 같은 탄이 여러 프레임
			// 관측돼 폭주한다(실측 초당 110회 = 기대치의 16배).
			if (A[i].Elapsed >= A[i].FlightTime && !A[i].bFxSpawned)
			{
				A[i].bFxSpawned = true;
				REExplosionFx::SpawnBulletExplosion(World, T[i].GetTransform().GetLocation());
			}
		}
	});
}
