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

			const float X = FMath::Lerp(A.Start.X, A.Target.X, t);
			const float Y = FMath::Lerp(A.Start.Y, A.Target.Y, t);
			const float BaseZ = FMath::Lerp(A.Start.Z, A.Target.Z, t);
			const float Z = BaseZ + 4.f * A.MaxHeight * t * (1.f - t);   // 포물선 높이

			Transforms[i].GetMutableTransform().SetLocation(FVector(X, Y, Z));

			if (A.Elapsed >= A.FlightTime)
			{
				// 착지. Defer 소멸(커맨드버퍼 — 이 프레임 Hit이 아직 관측 가능).
				Ctx.Defer().DestroyEntity(Ctx.GetEntity(i));
			}
		}
	});
}
