// Copyright Epic Games, Inc. All Rights Reserved.

#include "REBulletSimProcessor.h"
#include "REBulletFragments.h"
#include "MassExecutionContext.h"
#include "Mass/EntityFragments.h"  // FTransformFragment

UREBulletSimProcessor::UREBulletSimProcessor()
	: EntityQuery(*this)
{
	ExecutionFlags = (int32)EProcessorExecutionFlags::AllNetModes;  // 7: 싱글/서버/클라 모두 시뮬
}

void UREBulletSimProcessor::ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager)
{
	EntityQuery.AddRequirement<FTransformFragment>(EMassFragmentAccess::ReadWrite);
	EntityQuery.AddRequirement<FBulletSimFragment>(EMassFragmentAccess::ReadWrite);
	EntityQuery.AddTagRequirement<FBulletTag>(EMassFragmentPresence::All);
}

void UREBulletSimProcessor::Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(RE_BulletSim);

	EntityQuery.ForEachEntityChunk(Context, [](FMassExecutionContext& Context)
	{
		const float Dt = Context.GetDeltaTimeSeconds();
		const int32 Num = Context.GetNumEntities();
		const TArrayView<FTransformFragment> Transforms = Context.GetMutableFragmentView<FTransformFragment>();
		const TArrayView<FBulletSimFragment> Sims       = Context.GetMutableFragmentView<FBulletSimFragment>();

		for (int32 i = 0; i < Num; ++i)
		{
			FBulletSimFragment& Sim = Sims[i];
			Transforms[i].GetMutableTransform().AddToTranslation(Sim.Velocity * Dt);
			Sim.Lifetime -= Dt;
			if (Sim.Lifetime <= 0.f)
			{
				Context.Defer().DestroyEntity(Context.GetEntity(i));
			}
		}
	});
}
