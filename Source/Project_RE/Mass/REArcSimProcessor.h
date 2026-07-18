// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MassProcessor.h"
#include "REArcSimProcessor.generated.h"

/**
 *  곡사탄 궤적 시뮬. 목표주도 파라메트릭: XY=lerp(Start,Target,t), Z=baseZ+4H·t(1-t).
 *  t=Elapsed/FlightTime. Elapsed>=FlightTime이면 착지 → Defer 소멸.
 *  AllNetModes: 시뮬은 서버/클라 동일. 소멸도 여기서 담당(Hit은 판정만).
 */
UCLASS()
class UREArcSimProcessor : public UMassProcessor
{
	GENERATED_BODY()

public:
	UREArcSimProcessor();

protected:
	virtual void ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager) override;
	virtual void Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context) override;

private:
	FMassEntityQuery EntityQuery;
};
