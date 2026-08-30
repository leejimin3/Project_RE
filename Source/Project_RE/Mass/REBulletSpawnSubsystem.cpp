// Copyright Epic Games, Inc. All Rights Reserved.

#include "REBulletSpawnSubsystem.h"
#include "REBulletFragments.h"
#include "MassEntitySubsystem.h"
#include "MassEntityManager.h"
#include "Mass/EntityFragments.h"  // FTransformFragment
#include "REBulletPatternGenerator.h"
#include "Project_RE.h"                              // LogRE / LogREBullet / LogRENet

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

FMassEntityHandle UREBulletSpawnSubsystem::SpawnBullet(FVector Location, FVector Velocity, float Lifetime, float ColorSel)
{
	FMassEntityManager* EM = GetEntityManager();
	if (!EM)
	{
		UE_LOG(LogREBullet, Warning, TEXT("[RE] SpawnBullet: EntityManager NULL"));
		return FMassEntityHandle();
	}

	EnsureArchetype(*EM);
	FMassEntityHandle Entity = EM->CreateEntity(BulletArchetype);

	EM->GetFragmentDataChecked<FTransformFragment>(Entity).GetMutableTransform().SetLocation(Location);
	FBulletSimFragment& Sim = EM->GetFragmentDataChecked<FBulletSimFragment>(Entity);
	Sim.Velocity = Velocity;
	Sim.Lifetime = Lifetime;
	Sim.ColorSel = ColorSel;
	// FBulletRenderFragment.InstanceIndex는 기본값 INDEX_NONE 유지 (#17에서 할당).

	return Entity;
}

void UREBulletSpawnSubsystem::SpawnBulletBatch(TConstArrayView<FBulletSpawnParams> Params)
{
	for (const FBulletSpawnParams& P : Params)
	{
		SpawnBullet(P.Location, P.Velocity, P.Lifetime, P.ColorSel);
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
                                                          float MaxHeight, float Damage, float Radius, float InElapsed,
                                                          FVector CtrlOffset, FVector Ctrl1Offset, FVector Ctrl2Offset)
{
	FMassEntityManager* EM = GetEntityManager();
	if (!EM)
	{
		UE_LOG(LogREBullet, Warning, TEXT("[RE] SpawnArcBullet: EntityManager NULL"));
		return FMassEntityHandle();
	}

	EnsureArcArchetype(*EM);
	FMassEntityHandle Entity = EM->CreateEntity(ArcArchetype);

	// 발사 순간 위치 = Start (t=0에서 Sim이 곧바로 궤적으로 덮어씀).
	EM->GetFragmentDataChecked<FTransformFragment>(Entity).GetMutableTransform().SetLocation(Start);
	FArcBulletFragment& Arc = EM->GetFragmentDataChecked<FArcBulletFragment>(Entity);
	Arc.Start      = Start;
	Arc.Target     = Target;
	// 제어점을 여기서 한 번만 굳힌다 — 매 프레임 다시 만들면 Sim 이 MaxHeight 와 오프셋을
	// 전부 들고 있어야 한다.
	// C 는 2차 제어점이고, 아래 ⅔ 식이 그 2차 곡선을 **같은 곡선인 채로** 3차로 차수 상승시킨다.
	// 세 오프셋이 모두 0 이면 결과가 기존 포물선과 대수적으로 같다.
	const FVector C = (Start + Target) * 0.5f + FVector(0.f, 0.f, 2.f * MaxHeight) + CtrlOffset;
	Arc.Ctrl1      = Start  + (C - Start)  * (2.f / 3.f) + Ctrl1Offset;
	Arc.Ctrl2      = Target + (C - Target) * (2.f / 3.f) + Ctrl2Offset;
	Arc.FlightTime = FlightTime;
	Arc.Elapsed    = InElapsed;
	Arc.Damage     = Damage;
	Arc.Radius     = Radius;

	return Entity;
}

void UREBulletSpawnSubsystem::SpawnArcBulletBatch(TConstArrayView<REBulletPattern::FArcBulletSpawnParams> Params)
{
	for (const REBulletPattern::FArcBulletSpawnParams& P : Params)
	{
		SpawnArcBullet(P.Start, P.Target, P.FlightTime, P.MaxHeight, P.Damage, P.Radius, P.Elapsed,
		               P.CtrlOffset, P.Ctrl1Offset, P.Ctrl2Offset);
	}
}
