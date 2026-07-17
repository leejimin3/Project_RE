// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "REBulletSpawnSubsystem.h"   // FBulletSpawnParams

/**
 *  보스 탄막 패턴 발사 수학. 엔진/액터 의존 없는 순수 함수 → headless 단위 검증 가능.
 *  Settings(CDO) 조회만 예외 — 월드 불필요, headless 검증 유지.
 *  Spiral BaseAngle 누적 등 회전 상태는 호출자(Boss)가 소유. 제너레이터는 무상태.
 *  Homing은 M1 범위 밖 — 슬롯만.
 */
namespace REBulletPattern
{
	/** 발사 주기(s). Settings(BossFireInterval) 단일 출처 — GameMode 타이머·역산 공식 공용. */
	float FireIntervalSec();
	/** 탄 수명(s). Settings(BulletLifetime) 단일 출처. */
	float BulletLifetimeSec();

	struct FSpiralParams
	{
		FSpiralParams();             // Speed/Lifetime을 Settings에서 초기화
		int32 Count        = 16;
		float BaseAngleDeg = 0.f;    // 이번 발사 시작각 (Boss가 누적해 전달)
		float AngleStepDeg = 22.5f;  // 탄 간 각 간격 (기본 360/16 = 균등 링)
		float Speed        = 300.f;  // uu/s — 생성자가 Settings로 덮어씀
		float Lifetime     = 3.f;    // s — 생성자가 Settings로 덮어씀
	};

	struct FFanParams
	{
		FFanParams();                // Speed/Lifetime을 Settings에서 초기화
		int32 Count          = 16;
		float CenterAngleDeg = 0.f;  // 부채꼴 중심 방향
		float SpreadDeg      = 90.f; // 전체 벌어짐 각
		float Speed          = 300.f;
		float Lifetime       = 3.f;
	};

	/** 나선 팔 1개: 각도 = BaseAngle + i*AngleStep, i=0..Count-1. */
	TArray<FBulletSpawnParams> GenerateSpiral(const FVector& Origin, const FSpiralParams& P);

	/** 부채꼴: CenterAngle 기준 -Spread/2 .. +Spread/2 를 Count 등분 동시 발사. */
	TArray<FBulletSpawnParams> GenerateFan(const FVector& Origin, const FFanParams& P);

	/**
	 *  정확한 Count로 균등 링 Spiral 파라미터. Count는 호출자가 결정한다
	 *  (Boss의 클로즈드루프 스폰 컨트롤러가 라이브 카운트 피드백으로 산출한 값 — REBossCharacter.cpp).
	 *  steady-state 동시 탄환 ≈ 발사당_탄수 / 발사주기 × 수명 이 되도록 Count를 호출자가 조절한다.
	 */
	FSpiralParams MakeSpiralRing(int32 Count, float BaseAngleDeg);
}
