// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MassProcessor.h"
// FMassEntityQuery 를 값 멤버로 들고 있으므로 완전한 타입이 필요하다. MassProcessor.h 는
// 전방 선언만 한다 — 지금까지는 다른 경로로 전이 포함돼 우연히 됐고, Shipping 구성의
// PCH 에서 그게 끊겨 C2079 로 드러났다 (#141 게이트 3).
#include "MassEntityQuery.h"
#include "REBulletRenderProcessor.generated.h"

/**
 *  탄막 렌더(ISM 트랜스폼) Processor.
 *  ExecutionFlags = Standalone|Client(5): 표시할 화면이 있는 곳만 실행.
 *  데디서버(Server 넷모드)는 자동 skip — 서버권위 분리의 핵심.
 *  M0은 구조만 — Execute는 스텁. 실제 ISM 갱신은 M1.
 */
UCLASS()
class UREBulletRenderProcessor : public UMassProcessor
{
	GENERATED_BODY()

public:
	UREBulletRenderProcessor();

protected:
	virtual void ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager) override;
	virtual void Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context) override;

private:
	FMassEntityQuery EntityQuery;
};
