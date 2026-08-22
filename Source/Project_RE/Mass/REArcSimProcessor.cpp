// Copyright Epic Games, Inc. All Rights Reserved.

#include "REArcSimProcessor.h"
#include "REBulletFragments.h"
#include "MassExecutionContext.h"
#include "Mass/EntityFragments.h"  // FTransformFragment

UREArcSimProcessor::UREArcSimProcessor()
	: EntityQuery(*this)
{
	ExecutionFlags = (int32)EProcessorExecutionFlags::AllNetModes;
}

void UREArcSimProcessor::ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager)
{
	EntityQuery.AddRequirement<FTransformFragment>(EMassFragmentAccess::ReadWrite);
	EntityQuery.AddRequirement<FArcBulletFragment>(EMassFragmentAccess::ReadWrite);
	EntityQuery.AddTagRequirement<FArcBulletTag>(EMassFragmentPresence::All);
}

void UREArcSimProcessor::Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(RE_ArcSim);

	EntityQuery.ForEachEntityChunk(Context, [](FMassExecutionContext& Ctx)
	{
		const float Dt = Ctx.GetDeltaTimeSeconds();
		const int32 Num = Ctx.GetNumEntities();
		const TArrayView<FTransformFragment> Transforms = Ctx.GetMutableFragmentView<FTransformFragment>();
		const TArrayView<FArcBulletFragment> Arcs       = Ctx.GetMutableFragmentView<FArcBulletFragment>();

		for (int32 i = 0; i < Num; ++i)
		{
			FArcBulletFragment& A = Arcs[i];
			A.Elapsed += Dt;
			const float t = (A.FlightTime > 0.f) ? FMath::Min(A.Elapsed / A.FlightTime, 1.f) : 1.f;

			// 2차 베지어. 제어점이 중점 + (0,0,2·MaxHeight) 면 XY 는 정확히 선형보간으로,
			// Z 는 정확히 4·MaxHeight·t(1-t) 로 환원된다 — 기존 포물선의 일반화다.
			// 제어점에 XY 성분이 실리면 탄이 직선을 벗어나 휘감아 들어간다.
			// 끝점은 t=0/1 에서 Start/Target 그대로라 착지 시각·착지점은 제어점과 무관하다.
			const float u = 1.f - t;
			const FVector Pos = u * u * A.Start + 2.f * u * t * A.Ctrl + t * t * A.Target;

			Transforms[i].GetMutableTransform().SetLocation(Pos);

			if (A.Elapsed >= A.FlightTime)
			{
				// 착지. Defer 소멸(커맨드버퍼 — 이 프레임 Hit이 아직 관측 가능).
				Ctx.Defer().DestroyEntity(Ctx.GetEntity(i));
			}
		}
	});
}
