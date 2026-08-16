// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

/**
 *  탄환 소멸 폭발 스폰 — 단일 진입점 (#98).
 *
 *  스폰 방식을 나중에 바꿀 때(개별 → NDC 배칭) 이 함수 하나만 고치면 되도록
 *  호출부를 여기로 모은다. 현재는 엔진 컴포넌트 풀(AutoRelease)을 쓴 개별 스폰이다.
 *
 *  데디서버에서는 아무것도 하지 않는다 — 렌더가 없다.
 *  게임 스레드에서만 부를 것. Niagara 스폰은 GT 전용이다.
 */
namespace REExplosionFx
{
	void SpawnBulletExplosion(const UWorld* World, const FVector& Location);
}
