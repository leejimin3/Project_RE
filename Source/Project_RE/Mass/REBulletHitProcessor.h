// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MassProcessor.h"
#include "REBulletHitProcessor.generated.h"

/**
 *  탄막→플레이어 피격 판정 Processor.
 *  ExecutionFlags = Standalone|Server: 판정/데미지는 서버 권위. 클라는 복제 Health만 소비.
 *  탄환은 액터가 아님(ISM 인스턴스) → 물리 오버랩 불가, XY 거리 비교로 판정.
 */
UCLASS()
class UREBulletHitProcessor : public UMassProcessor
{
	GENERATED_BODY()

public:
	UREBulletHitProcessor();

protected:
	virtual void ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager) override;
	virtual void Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context) override;

private:
	FMassEntityQuery EntityQuery;
};
