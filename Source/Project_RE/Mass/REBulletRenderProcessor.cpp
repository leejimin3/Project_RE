// Copyright Epic Games, Inc. All Rights Reserved.

#include "REBulletRenderProcessor.h"
#include "REBulletFragments.h"
#include "MassExecutionContext.h"

UREBulletRenderProcessor::UREBulletRenderProcessor()
	: EntityQuery(*this)
{
	// 5: 데디서버(Server) skip, 싱글/클라만 렌더
	ExecutionFlags = (int32)(EProcessorExecutionFlags::Standalone | EProcessorExecutionFlags::Client);
}

void UREBulletRenderProcessor::ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager)
{
	EntityQuery.AddRequirement<FBulletSimFragment>(EMassFragmentAccess::ReadOnly);       // sim 위치 읽기
	EntityQuery.AddRequirement<FBulletRenderFragment>(EMassFragmentAccess::ReadWrite);   // 인스턴스 쓰기
}

void UREBulletRenderProcessor::Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context)
{
	// M0: 구조만. 실제 ISM 트랜스폼 갱신은 M1.
	UE_LOG(LogTemp, Verbose, TEXT("[RE] RenderProcessor::Execute"));
}
