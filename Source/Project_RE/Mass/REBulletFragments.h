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
	/** 색 선택 0/1 — 스폰 시 고정. 매 프레임 파생하면 탄이 깜빡인다(나이 기반은 주기적으로 뒤집힌다) (#97). */
	float   ColorSel = 0.f;
};

/** 탄막 렌더 상태. M1에서 ISM 인스턴스 인덱스로 사용한다. */
USTRUCT()
struct FBulletRenderFragment : public FMassFragment
{
	GENERATED_BODY()

	int32 InstanceIndex = INDEX_NONE;
};

/** 탄환 식별 태그. Query 필터 전용(데이터 없음). 후속 Processor가 이 태그로 탄환만 선별. */
USTRUCT()
struct FBulletTag : public FMassTag
{
	GENERATED_BODY()
};

/** 곡사탄 시뮬 상태. 위치는 FTransformFragment, arc 파라미터는 여기. */
USTRUCT()
struct FArcBulletFragment : public FMassFragment
{
	GENERATED_BODY()

	FVector Start      = FVector::ZeroVector;
	FVector Target     = FVector::ZeroVector;
	float   FlightTime = 1.5f;
	float   Elapsed    = 0.f;
	float   MaxHeight  = 400.f;
	float   Damage     = 15.f;
	float   Radius     = 120.f;
};

/** 곡사탄 식별 태그. 기존 FBulletTag(직선탄)와 분리 — arc 프로세서만 선별. */
USTRUCT()
struct FArcBulletTag : public FMassTag
{
	GENERATED_BODY()
};
