// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class ARECharacterBase;
class UWorld;

/** 피격 판정 대상 1명 — 액터와 수집 시점 위치. UStruct 아님(함수 인자 전용 경량 구조체). */
struct FREHitTarget
{
	ARECharacterBase* Player   = nullptr;
	FVector           Location = FVector::ZeroVector;

	/**
	 *  대쉬 무적 중 (#102). 판정 대상에는 들어가되 데미지를 받지 않는다.
	 *  목록에서 통째로 빼면 거리 비교 자체가 없어 탄환도 소멸하지 않는다 — 그게 #102 이전 동작이었다.
	 *  넣고 표시만 하면 "맞았지만 데미지 0, 탄은 소멸"을 표현할 수 있다.
	 */
	bool              bInvulnerable = false;
};

/**
 *  피격 판정 대상 수집 (#86). 살아있는 플레이어 전원을 담는다.
 *  대쉬 중이어도 담되 bInvulnerable 로 표시한다 (#102) — 소비자가 데미지만 건너뛰고
 *  탄환 소멸은 그대로 하도록. 데미지 면제는 소비자 쪽 책임이다.
 *  서버 판정 프로세서 2종 공용 — 한쪽만 고쳐지는 사고를 막는다(이 이슈의 버그가 그 중복 탓이었다).
 *  Out은 Reset 후 채운다. World가 null이면 빈 배열을 남기고 반환한다.
 *
 *  한 프레임 지연 있음: Execute 진입 시 1회만 스냅샷하므로, 같은 틱 안에서 앞선 탄에 죽은
 *  플레이어는 뒤따르는 탄에는 여전히 죽기 전 위치의 대상으로 보인다(시체가 그 틱 동안 계속
 *  탄을 흡수). 의도적으로 남긴 것 — 설계 근거는 2026-08-13-coop-hit-detection-design.md 참조.
 */
void GatherHitTargets(const UWorld* World, TArray<FREHitTarget>& Out);
