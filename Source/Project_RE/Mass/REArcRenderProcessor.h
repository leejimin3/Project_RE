// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MassProcessor.h"
// FMassEntityQuery 를 값 멤버로 들고 있으므로 완전한 타입이 필요하다. MassProcessor.h 는
// 전방 선언만 한다 — 지금까지는 다른 경로로 전이 포함돼 우연히 됐고, Shipping 구성의
// PCH 에서 그게 끊겨 C2079 로 드러났다 (#141 게이트 3).
#include "MassEntityQuery.h"
#include "REArcRenderProcessor.generated.h"

/**
 *  곡사탄 렌더. arc탄 → ArcISM(주황 구체, Z 궤적). 착지 마커 → MarkerISM(빨강 평면 원, Target 바닥).
 *  arc탄 1개당 탄 인스턴스 1 + 마커 인스턴스 1. 매 프레임 리빌드(기존 RenderProcessor 패턴).
 *  Standalone|Client: 데디서버 skip. ISM 변형은 GT 전용.
 */
UCLASS()
class UREArcRenderProcessor : public UMassProcessor
{
	GENERATED_BODY()

public:
	UREArcRenderProcessor();

protected:
	virtual void ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager) override;
	virtual void Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context) override;

private:
	FMassEntityQuery EntityQuery;
};
