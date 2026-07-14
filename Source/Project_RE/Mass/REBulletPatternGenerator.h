// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "REBulletSpawnSubsystem.h"   // FBulletSpawnParams

/**
 *  보스 탄막 패턴 발사 수학. 엔진/액터 의존 없는 순수 함수 → headless 단위 검증 가능.
 *  Spiral BaseAngle 누적 등 회전 상태는 호출자(Boss)가 소유. 제너레이터는 무상태.
 *  Homing은 M1 범위 밖 — 슬롯만.
 */
namespace REBulletPattern
{
	/** 발사 주기(s). GameMode 발사 타이머와 역산 공식의 단일 출처. */
	constexpr float FireIntervalSec   = 0.1f;
	/** 탄 수명(s). FSpiralParams::Lifetime 기본값과 역산 공식의 단일 출처. */
	constexpr float BulletLifetimeSec = 3.f;

	struct FSpiralParams
	{
		int32 Count        = 16;
		float BaseAngleDeg = 0.f;    // 이번 발사 시작각 (Boss가 누적해 전달)
		float AngleStepDeg = 22.5f;  // 탄 간 각 간격 (기본 360/16 = 균등 링)
		float Speed        = 300.f;  // uu/s
		float Lifetime     = BulletLifetimeSec;  // s
	};

	struct FFanParams
	{
		int32 Count          = 16;
		float CenterAngleDeg = 0.f;   // 부채꼴 중심 방향
		float SpreadDeg      = 90.f;  // 전체 벌어짐 각
		float Speed          = 300.f;
		float Lifetime       = 3.f;
	};

	/** 나선 팔 1개: 각도 = BaseAngle + i*AngleStep, i=0..Count-1. */
	TArray<FBulletSpawnParams> GenerateSpiral(const FVector& Origin, const FSpiralParams& P);

	/** 부채꼴: CenterAngle 기준 -Spread/2 .. +Spread/2 를 Count 등분 동시 발사. */
	TArray<FBulletSpawnParams> GenerateFan(const FVector& Origin, const FFanParams& P);

	/**
	 *  목표 동시 탄환 수 N → 균등 링 Spiral 파라미터.
	 *  steady-state 동시 탄환 = 발사당_탄수 / 발사주기 × 수명 이므로
	 *  Count = round(N × FireIntervalSec / BulletLifetimeSec), AngleStep = 360/Count.
	 *  ceil이 아니라 round인 이유: ceil(100/30)=4 → live≈120 (+20%)으로 ±10% 허용치를 넘는다.
	 *  Speed/Lifetime은 FSpiralParams 기본값 유지.
	 */
	FSpiralParams MakeSpiralForLiveCount(int32 TargetLive, float BaseAngleDeg);
}
