// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MassProcessor.h"
// FMassEntityQuery 를 값 멤버로 들고 있으므로 완전한 타입이 필요하다. MassProcessor.h 는
// 전방 선언만 한다 — 지금까지는 다른 경로로 전이 포함돼 우연히 됐고, Shipping 구성의
// PCH 에서 그게 끊겨 C2079 로 드러났다 (#141 게이트 3).
#include "MassEntityQuery.h"
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
