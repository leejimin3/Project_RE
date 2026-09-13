// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MassProcessor.h"
#include "REExplosionRenderProcessor.generated.h"

/**
 *  폭발 ISM 동기 (#149, #151).
 *
 *  REExplosionFx 의 살아있는 목록을 세 ISM(코어 구체 / 수평 링 / 연기 구체)에
 *  프레임당 한 번 배치 반영한다. 폭발 1개 = 인스턴스 3개이므로 드로우콜이 동시 폭발
 *  개수와 무관하다.
 *
 *  **엔티티를 읽지 않는다 — 쿼리가 없다.** UMassProcessor 를 쓰는 이유는 두 가지다:
 *  (1) ExecutionFlags 로 데디서버 제외가 선언적으로 되고, (2) bRequiresGameThreadExecution
 *  으로 GT 고정이 보장된다(ISM 변형은 GT 전용). 기존 ISM 동기가 전부 프로세서에
 *  있는 배치와도 맞는다(REBulletRenderProcessor / REArcRenderProcessor).
 *
 *  쿼리가 없으므로 생성자에서 QueryBasedPruning 을 Never 로 꺼야 한다 — 기본값
 *  Prune 이면 쿼리 0개인 프로세서가 런타임에 통째로 프루닝돼 Execute 가 한 번도
 *  안 돈다(조용한 dead code).
 *
 *  탄환 렌더 프로세서에 얹지 않는 이유: 그 함수는 CSV_SCOPED_TIMING_STAT(REBullet,
 *  BulletRender) 안쪽이라 폭발 비용이 BulletRender 로 집계된다. 프로파일 문서와
 *  포트폴리오가 그 숫자를 쓴다.
 */
UCLASS()
class UREExplosionRenderProcessor : public UMassProcessor
{
	GENERATED_BODY()

public:
	UREExplosionRenderProcessor();

protected:
	// ConfigureQueries 는 오버라이드하지 않는다 — 베이스가 순수 가상이 아니고, 읽을
	// 엔티티가 없다.
	virtual void Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context) override;
};
