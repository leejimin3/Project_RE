// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MassProcessor.h"
#include "REArcHitProcessor.generated.h"

/**
 *  곡사탄 착지 판정. Elapsed>=FlightTime(착지 프레임)인 탄만, 플레이어 XY가 Target 반경 안이면
 *  TakeDamage(범위 데미지). 소멸은 Sim이 담당(여기선 판정만) — ExecuteAfter(ArcSim) + Defer 규약으로
 *  Sim이 소멸 예약한 같은 프레임에 엔티티가 아직 살아있어 관측 가능.
 *  Standalone|Server: 서버 권위 판정. GT 전용(TakeDamage 액터 호출).
 */
UCLASS()
class UREArcHitProcessor : public UMassProcessor
{
	GENERATED_BODY()

public:
	UREArcHitProcessor();

protected:
	virtual void ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager) override;
	virtual void Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context) override;

private:
	FMassEntityQuery EntityQuery;
};
