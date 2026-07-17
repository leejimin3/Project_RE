// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MassProcessor.h"
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
