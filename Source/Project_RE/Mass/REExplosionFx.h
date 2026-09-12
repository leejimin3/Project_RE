// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

/**
 *  탄환 소멸 폭발 스폰 — 단일 진입점 (#98).
 *
 *  스폰 방식을 나중에 바꿀 때(개별 → NDC 배칭) 이 함수 하나만 고치면 되도록
 *  호출부를 여기로 모은다. 현재는 엔진 컴포넌트 풀(AutoRelease)을 쓴 개별 스폰 +
 *  동시 개수 예산(#147)이다. 폭발 1개 = 컴포넌트 1개 = 프리미티브 1개고 컴포넌트끼리는
 *  배칭되지 않으므로, 드로우콜을 쥐는 것은 풀링이 아니라 그 예산이다.
 *
 *  데디서버에서는 아무것도 하지 않는다 — 렌더가 없다.
 *  게임 스레드에서만 부를 것. Niagara 스폰은 GT 전용이다.
 */
namespace REExplosionFx
{
	/**
	 *  폭발 에셋을 미리 로드한다 (#107). 월드 시작 시 1회 호출할 것.
	 *
	 *  안 부르면 첫 폭발에서 동기 로드가 걸린다 — 실측 ~290ms 블로킹(FlushAsyncLoading).
	 *  Niagara 시스템 하나가 의존 패키지 수십 개를 끌어온다.
	 *  데디서버에서는 아무것도 하지 않는다.
	 */
	void Preload(const UWorld* World);

	void SpawnBulletExplosion(const UWorld* World, const FVector& Location);
}
