// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "REBulletPattern.generated.h"

/**
 *  보스 탄막 패턴 종류. M0은 슬롯만 — 각 패턴의 발사 수학은 M1.
 */
UENUM(BlueprintType)
enum class EBulletPattern : uint8
{
	Spiral,
	Fan,
	Homing
};
