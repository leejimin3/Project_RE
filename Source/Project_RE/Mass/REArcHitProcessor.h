// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MassProcessor.h"
// FMassEntityQuery 를 값 멤버로 들고 있으므로 완전한 타입이 필요하다. MassProcessor.h 는
// 전방 선언만 한다 — 지금까지는 다른 경로로 전이 포함돼 우연히 됐고, Shipping 구성의
// PCH 에서 그게 끊겨 C2079 로 드러났다 (#141 게이트 3).
#include "MassEntityQuery.h"
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
