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

			// 3차 베지어. 스포너가 오프셋 0 일 때 2차(= 기존 포물선)와 같은 곡선이 되도록
			// 제어점을 차수 상승시켜 넣는다 — 오프셋을 주면 2차로는 못 만드는 S자·깊은 감김이 된다.
			// 끝점은 t=0/1 에서 Start/Target 그대로라 착지 시각·착지점은 제어점과 무관하다.
			const float u = 1.f - t;
			const FVector Pos = u * u * u * A.Start
			                  + 3.f * u * u * t * A.Ctrl1
			                  + 3.f * u * t * t * A.Ctrl2
			                  + t * t * t * A.Target;

			Transforms[i].GetMutableTransform().SetLocation(Pos);

			if (A.Elapsed >= A.FlightTime)
			{
				// 착지. Defer 소멸(커맨드버퍼 — 이 프레임 Hit이 아직 관측 가능).
				Ctx.Defer().DestroyEntity(Ctx.GetEntity(i));
			}
		}
	});
}
