// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MassProcessor.h"
// FMassEntityQuery 를 값 멤버로 들고 있으므로 완전한 타입이 필요하다. MassProcessor.h 는
// 전방 선언만 한다 — 지금까지는 다른 경로로 전이 포함돼 우연히 됐고, Shipping 구성의
// PCH 에서 그게 끊겨 C2079 로 드러났다 (#141 게이트 3).
#include "MassEntityQuery.h"
#include "REArcFxProcessor.generated.h"

/**
 *  곡사탄 착지 폭발 (#98).
 *
 *  REArcSimProcessor 는 워커 스레드에서 도는데 Niagara 스폰은 게임 스레드 전용이다.
 *  시뮬을 GT 로 옮기면 곡사탄이 수천 개로 늘었을 때 확장 여지를 잃으므로, 폭발만
 *  떼어 이 프로세서가 GT 에서 처리한다. 시뮬 앞에 배치해 파괴 전에 위치를 읽는다.
 */
UCLASS()
class UREArcFxProcessor : public UMassProcessor
{
	GENERATED_BODY()

public:
	UREArcFxProcessor();

protected:
	virtual void ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager) override;
	virtual void Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context) override;

private:
	FMassEntityQuery EntityQuery;
};
