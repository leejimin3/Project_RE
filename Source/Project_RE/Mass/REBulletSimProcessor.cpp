// Copyright Epic Games, Inc. All Rights Reserved.

#include "REBulletSimProcessor.h"
#include "REBulletFragments.h"
#include "MassExecutionContext.h"

UREBulletSimProcessor::UREBulletSimProcessor()
	: EntityQuery(*this)
{
	ExecutionFlags = (int32)EProcessorExecutionFlags::AllNetModes;  // 7: 싱글/서버/클라 모두 시뮬
}

void UREBulletSimProcessor::ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager)
{
	EntityQuery.AddRequirement<FBulletSimFragment>(EMassFragmentAccess::ReadWrite);
}

void UREBulletSimProcessor::Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context)
{
	// M0: 구조만. 실제 이동/수명 계산은 M1.
	UE_LOG(LogTemp, Verbose, TEXT("[RE] SimProcessor::Execute"));
}
