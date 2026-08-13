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
};

/**
 *  피격 판정 대상 수집 (#86). 살아있고 대쉬 중이 아닌 플레이어만 담는다.
 *  서버 판정 프로세서 2종 공용 — 한쪽만 고쳐지는 사고를 막는다(이 이슈의 버그가 그 중복 탓이었다).
 *  Out은 Reset 후 채운다. World가 null이면 빈 배열을 남기고 반환한다.
 *
 *  한 프레임 지연 있음: Execute 진입 시 1회만 스냅샷하므로, 같은 틱 안에서 앞선 탄에 죽은
 *  플레이어는 뒤따르는 탄에는 여전히 죽기 전 위치의 대상으로 보인다(시체가 그 틱 동안 계속
 *  탄을 흡수). 의도적으로 남긴 것 — 설계 근거는 2026-08-13-coop-hit-detection-design.md 참조.
 */
void GatherHitTargets(const UWorld* World, TArray<FREHitTarget>& Out);
