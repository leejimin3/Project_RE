// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MassProcessor.h"
// FMassEntityQuery 를 값 멤버로 들고 있으므로 완전한 타입이 필요하다. MassProcessor.h 는
// 전방 선언만 한다 — 지금까지는 다른 경로로 전이 포함돼 우연히 됐고, Shipping 구성의
// PCH 에서 그게 끊겨 C2079 로 드러났다 (#141 게이트 3).
#include "MassEntityQuery.h"
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
