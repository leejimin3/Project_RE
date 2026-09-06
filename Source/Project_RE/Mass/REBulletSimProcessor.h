// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MassProcessor.h"
// FMassEntityQuery 를 값 멤버로 들고 있으므로 완전한 타입이 필요하다. MassProcessor.h 는
// 전방 선언만 한다 — 지금까지는 다른 경로로 전이 포함돼 우연히 됐고, Shipping 구성의
// PCH 에서 그게 끊겨 C2079 로 드러났다 (#141 게이트 3).
#include "MassEntityQuery.h"
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
