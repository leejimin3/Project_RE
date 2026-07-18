// Copyright Epic Games, Inc. All Rights Reserved.

#include "REBulletSpawnSubsystem.h"
#include "REBulletFragments.h"
#include "MassEntitySubsystem.h"
#include "MassEntityManager.h"
#include "Mass/EntityFragments.h"  // FTransformFragment
#include "REBulletPatternGenerator.h"

FMassEntityManager* UREBulletSpawnSubsystem::GetEntityManager() const
{
	UMassEntitySubsystem* Mass = GetWorld() ? GetWorld()->GetSubsystem<UMassEntitySubsystem>() : nullptr;
	return Mass ? &Mass->GetMutableEntityManager() : nullptr;
}

void UREBulletSpawnSubsystem::EnsureArchetype(FMassEntityManager& EntityManager)
{
	if (BulletArchetype.IsValid())
	{
		return;
	}
	BulletArchetype = EntityManager.CreateArchetype({
		FTransformFragment::StaticStruct(),
		FBulletSimFragment::StaticStruct(),
		FBulletRenderFragment::StaticStruct(),
		FBulletTag::StaticStruct() });
}

FMassEntityHandle UREBulletSpawnSubsystem::SpawnBullet(FVector Location, FVector Velocity, float Lifetime)
{
	FMassEntityManager* EM = GetEntityManager();
	if (!EM)
	{
		UE_LOG(LogTemp, Warning, TEXT("[RE] SpawnBullet: EntityManager NULL"));
		return FMassEntityHandle();
	}

	EnsureArchetype(*EM);
	FMassEntityHandle Entity = EM->CreateEntity(BulletArchetype);

	EM->GetFragmentDataChecked<FTransformFragment>(Entity).GetMutableTransform().SetLocation(Location);
	FBulletSimFragment& Sim = EM->GetFragmentDataChecked<FBulletSimFragment>(Entity);
	Sim.Velocity = Velocity;
	Sim.Lifetime = Lifetime;
	// FBulletRenderFragment.InstanceIndex는 기본값 INDEX_NONE 유지 (#17에서 할당).

	return Entity;
}

void UREBulletSpawnSubsystem::SpawnBulletBatch(TConstArrayView<FBulletSpawnParams> Params)
{
	for (const FBulletSpawnParams& P : Params)
	{
		SpawnBullet(P.Location, P.Velocity, P.Lifetime);
	}
}

void UREBulletSpawnSubsystem::EnsureArcArchetype(FMassEntityManager& EntityManager)
{
	if (ArcArchetype.IsValid())
	{
		return;
	}
	ArcArchetype = EntityManager.CreateArchetype({
		FTransformFragment::StaticStruct(),
		FArcBulletFragment::StaticStruct(),
		FBulletRenderFragment::StaticStruct(),
		FArcBulletTag::StaticStruct() });
}

FMassEntityHandle UREBulletSpawnSubsystem::SpawnArcBullet(FVector Start, FVector Target, float FlightTime,
                                                          float MaxHeight, float Damage, float Radius)
{
	FMassEntityManager* EM = GetEntityManager();
	if (!EM)
	{
		UE_LOG(LogTemp, Warning, TEXT("[RE] SpawnArcBullet: EntityManager NULL"));
		return FMassEntityHandle();
	}

	EnsureArcArchetype(*EM);
	FMassEntityHandle Entity = EM->CreateEntity(ArcArchetype);

	// 발사 순간 위치 = Start (t=0에서 Sim이 곧바로 궤적으로 덮어씀).
	EM->GetFragmentDataChecked<FTransformFragment>(Entity).GetMutableTransform().SetLocation(Start);
	FArcBulletFragment& Arc = EM->GetFragmentDataChecked<FArcBulletFragment>(Entity);
	Arc.Start      = Start;
	Arc.Target     = Target;
	Arc.FlightTime = FlightTime;
	Arc.Elapsed    = 0.f;
	Arc.MaxHeight  = MaxHeight;
	Arc.Damage     = Damage;
	Arc.Radius     = Radius;

	return Entity;
}

void UREBulletSpawnSubsystem::SpawnArcBulletBatch(TConstArrayView<REBulletPattern::FArcBulletSpawnParams> Params)
{
	for (const REBulletPattern::FArcBulletSpawnParams& P : Params)
	{
		SpawnArcBullet(P.Start, P.Target, P.FlightTime, P.MaxHeight, P.Damage, P.Radius);
	}
}
