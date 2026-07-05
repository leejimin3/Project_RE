// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MassEntityTypes.h"
#include "REBulletFragments.generated.h"

/** 탄막 시뮬 상태. 위치는 M1에서 FTransformFragment로 다룬다. */
USTRUCT()
struct FBulletSimFragment : public FMassFragment
{
	GENERATED_BODY()

	FVector Velocity = FVector::ZeroVector;
	float   Lifetime = 0.f;
};

/** 탄막 렌더 상태. M1에서 ISM 인스턴스 인덱스로 사용한다. */
USTRUCT()
struct FBulletRenderFragment : public FMassFragment
{
	GENERATED_BODY()

	int32 InstanceIndex = INDEX_NONE;
};
