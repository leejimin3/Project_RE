// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MassProcessor.h"
#include "REBulletRenderProcessor.generated.h"

/**
 *  탄막 렌더(ISM 트랜스폼) Processor.
 *  ExecutionFlags = Standalone|Client(5): 표시할 화면이 있는 곳만 실행.
 *  데디서버(Server 넷모드)는 자동 skip — 서버권위 분리의 핵심.
 *  M0은 구조만 — Execute는 스텁. 실제 ISM 갱신은 M1.
 */
UCLASS()
class UREBulletRenderProcessor : public UMassProcessor
{
	GENERATED_BODY()

public:
	UREBulletRenderProcessor();

protected:
	virtual void ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager) override;
	virtual void Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context) override;

private:
	FMassEntityQuery EntityQuery;
};
