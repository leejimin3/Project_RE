// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MassProcessor.h"
#include "REBulletSimProcessor.generated.h"

/**
 *  탄막 이동/수명 시뮬 Processor.
 *  ExecutionFlags = AllNetModes(7): 싱글/서버/클라 모두 실행 (시뮬은 어디서나 동일).
 *  M0은 구조만 — Execute는 스텁. 실제 이동은 M1.
 */
UCLASS()
class UREBulletSimProcessor : public UMassProcessor
{
	GENERATED_BODY()

public:
	UREBulletSimProcessor();

protected:
	virtual void ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager) override;
	virtual void Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context) override;

private:
	FMassEntityQuery EntityQuery;
};
