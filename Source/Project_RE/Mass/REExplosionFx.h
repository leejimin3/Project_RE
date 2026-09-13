// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

/**
 *  탄환 소멸 폭발 — 단일 진입점 (#98) + 살아있는 폭발 목록 (#149).
 *
 *  폭발 1개 = ISM 인스턴스 2개(구체 코어 + 수평 링)다. 컴포넌트를 만들지 않으므로
 *  드로우콜이 동시 폭발 개수와 무관하다 — 층 수가 상한이다. 이전 구조(개별
 *  UNiagaraComponent 스폰)는 컴포넌트당 4.9 드로우콜로 개수에 선형이었다(#147).
 *
 *  여기는 상태만 들고 있고 그리지 않는다. 그리는 것은 REExplosionRenderProcessor 다.
 *
 *  데디서버에서는 아무것도 하지 않는다 — 렌더가 없다.
 *  게임 스레드에서만 부를 것. 락이 없다(호출부가 전부 GT 고정이다).
 */
namespace REExplosionFx
{
	/** 살아있는 폭발 하나. Progress 는 PruneAndGetLive 가 갱신한다. */
	struct FLiveExplosion
	{
		FVector Loc = FVector::ZeroVector;
		float   SpawnTime = 0.f;   // World->GetTimeSeconds() — 게임 시간이다
		float   Progress = 0.f;    // 0 → 1 (나이 / 수명)
	};

	/**
	 *  폭발 요청. 예산(re.Fx.ExplosionBudget)이 차 있으면 조용히 버린다 —
	 *  버린 수는 ExplosionProbe 로그가 센다.
	 *
	 *  예산을 재기 전에 만료분을 턴다. 렌더 프로세서가 돌지 않는 넷모드에서도
	 *  목록이 잠기지 않는다.
	 */
	void SpawnBulletExplosion(const UWorld* World, const FVector& Location);

	/**
	 *  만료분을 제거하고 남은 것들의 Progress 를 갱신해 돌려준다.
	 *
	 *  렌더 프로세서가 프레임당 **한 번만** 부른다. 두 번 부르면 ExplosionProbe 의
	 *  창 길이가 왜곡돼 비율이 거짓으로 읽힌다.
	 */
	const TArray<FLiveExplosion>& PruneAndGetLive(float NowSeconds);
}
