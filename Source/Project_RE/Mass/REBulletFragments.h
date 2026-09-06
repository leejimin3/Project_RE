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
	/**
	 *  3차 베지어 제어점(월드). 궤적은 Start·Ctrl1·Ctrl2·Target 이고 끝점은 항상
	 *  Start/Target 이라 착지 시각·착지점·마커는 제어점과 무관하다 — 바뀌는 건 가는 길뿐이다.
	 *
	 *  2차는 3차의 특수 케이스다(차수 상승): 2차 제어점 C 에 대해
	 *      Ctrl1 = Start + ⅔(C − Start),  Ctrl2 = Target + ⅔(C − Target)
	 *  이면 두 곡선이 완전히 같다. 그리고 C = 중점 + (0,0,2·MaxHeight) 면 그 2차 곡선이
	 *  다시 기존 포물선(XY 선형보간 + 4H·t(1-t))과 같다. 즉 오프셋이 전부 0이면 원래 궤적이다.
	 *  스포너가 MaxHeight 와 세 오프셋에서 계산해 채운다.
	 */
	FVector Ctrl1      = FVector::ZeroVector;
	FVector Ctrl2      = FVector::ZeroVector;
	float   FlightTime = 1.5f;
	float   Elapsed    = 0.f;
	float   Damage     = 15.f;
	float   Radius     = 120.f;
};

/** 곡사탄 식별 태그. 기존 FBulletTag(직선탄)와 분리 — arc 프로세서만 선별. */
USTRUCT()
struct FArcBulletTag : public FMassTag
{
	GENERATED_BODY()
};
