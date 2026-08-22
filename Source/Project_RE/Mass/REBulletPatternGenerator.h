// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "REBulletSpawnSubsystem.h"   // FBulletSpawnParams
#include "Math/RandomStream.h"

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
		/**
		 *  발사 회차 패리티(0/1). 색 체커보드의 한 축이다 (#97).
		 *  탄은 두 방향으로 겹친다 — 방사(연속 발사)와 원주(링 내부). 회차 패리티와
		 *  링 인덱스를 더해 홀짝을 내면 두 축 모두에서 인접 탄이 다른 색이 된다.
		 *  호출자(Boss)가 ServerTime 으로 산출해 넣는다 — 서버·클라가 같은 값을 얻는다.
		 */
		int32 ShotParity   = 0;
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

	/** 곡사탄 1발 스폰 파라미터(UStruct 아님 — 함수 인자 전용). */
	struct FArcBulletSpawnParams
	{
		FVector Start      = FVector::ZeroVector;
		FVector Target     = FVector::ZeroVector;
		float   FlightTime = 1.5f;
		float   MaxHeight  = 400.f;
		float   Damage     = 15.f;
		float   Radius     = 120.f;

		/** 스폰 시점의 비행 경과초 (#84 지연 보정). 0 = 갓 발사됨 — 기존 호출부는 무변경. */
		float   Elapsed    = 0.f;
	};

	//~ 착지점 생성기 — 전부 월드 착지점(Z=GroundZ) 배열 반환. 순수함수(FRandomStream 제외).
	/** 원형 링: 중심 C, 반경 R, N개 균등각. */
	TArray<FVector> GenRing(const FVector& Center, float Radius, int32 N, float GroundZ);

	/**
	 *  스윕 나선: 한 발씩 이어 그리는 **단일** 나선. 진행도 t 는 0=제일 안쪽, 1=제일 바깥이고
	 *  반경 = Lerp(MinRadius, MaxRadius, t), 각 = t·360·Turns 다.
	 *
	 *  T0..T1 을 N등분해 슬롯을 만든다. 이 구간이 곧 '한 볼리가 실어 나르는 서브샷'이다 —
	 *  보스는 발사 주기마다 RPC 를 한 번 보내되 그 안의 N슬롯은 지난 주기 동안 한 발씩 나간
	 *  것으로 취급한다(호출자가 비행 경과를 어긋나게 준다). 초당 N/주기 발을 단발로 쏘면서
	 *  RPC 는 주기당 1회로 묶는 것이 목적이다.
	 *
	 *  Arms 는 같은 슬롯에서 동시에 나가는 팔의 수다. 팔 a 는 360/Arms·a 만큼 각이 어긋나
	 *  같은 나선이 Arms 겹으로 겹쳐 돈다 — 체공 탄을 Arms 배로 늘리는 손잡이다.
	 *  반환은 **슬롯 우선** 순서다: [슬롯0팔0, 슬롯0팔1, …, 슬롯1팔0, …] — 총 N×Arms 개.
	 *  호출자는 i/Arms 로 슬롯을 얻어 비행 경과를 어긋낸다(같은 슬롯의 팔들은 동시 발사다).
	 */
	TArray<FVector> GenSweepSpiral(const FVector& Center, float MinRadius, float MaxRadius, float Turns,
	                               float T0, float T1, int32 N, int32 Arms, float GroundZ);
	/** 라인: 보스→플레이어 방향의 수직 벽. 중심=플레이어, 길이 WallLen, N등분. */
	TArray<FVector> GenLine(const FVector& BossLoc, const FVector& PlayerLoc, float WallLen, int32 N, float GroundZ);
	/** 격자: 중심 기준 ±Extent 범위 Cols×Rows 균등 그리드. */
	TArray<FVector> GenGrid(const FVector& Center, float ExtentX, float ExtentY, int32 Cols, int32 Rows, float GroundZ);
	/** 나선: 아르키메데스 — 각 i·137.5°, 반경 MaxRadius·√((i+1)/N). */
	TArray<FVector> GenArcSpiral(const FVector& Center, float MaxRadius, int32 N, float GroundZ);
	/** 플레이어 조준: 중심 1점 + 반경 ClusterRadius 링 RingN점. */
	TArray<FVector> GenPlayerCluster(const FVector& PlayerLoc, float ClusterRadius, int32 RingN, float GroundZ);
	/** 랜덤: 중심 기준 반경 ArenaRadius 내 균등 면적 분포 N점(√ 보정). */
	TArray<FVector> GenRandom(const FVector& Center, float ArenaRadius, int32 N, FRandomStream& Rng, float GroundZ);
}
