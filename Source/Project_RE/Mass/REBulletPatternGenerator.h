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

	struct FRoseParams
	{
		FRoseParams();               // Speed/Lifetime을 Settings에서 초기화
		int32 Count        = 16;
		float BaseAngleDeg = 0.f;    // 이번 발사 시작각 (Boss가 누적해 전달)
		float Speed        = 300.f;  // 기준 속력(변조 전) — 생성자가 Settings로 덮어씀
		float Lifetime     = 3.f;    // s — 생성자가 Settings로 덮어씀
		int32 Lobes        = 5;      // k — 속력 변조의 각주기. 로브 수와 같다
		float Amp          = 0.5f;   // 변조 깊이. 1.0이면 골에서 속력이 0이 된다
		float PhaseDeg     = 0.f;    // 로브 위상(deg). 호출자가 ω·ServerTime 으로 산출해 넣는다
	};

	/** 나선 팔 1개: 각도 = BaseAngle + i*AngleStep, i=0..Count-1. */
	TArray<FBulletSpawnParams> GenerateSpiral(const FVector& Origin, const FSpiralParams& P);

	/**
	 *  장미 포락선: 각도는 균등 링이고 **속력만** 각도의 함수다.
	 *      θᵢ = BaseAngle + i·(360/Count)
	 *      sᵢ = Speed · (1 + Amp·cos(Lobes·θᵢ + PhaseDeg))
	 *
	 *  발사 T초 뒤 이 볼리의 파면은 r(θ) = Speed·T·(1 + Amp·cos(Lobes·θ + PhaseDeg)) —
	 *  Lobes 장 로브의 극좌표 곡선이 자기닮음으로 확대된다. 탄 하나하나는 완전한 직선이고
	 *  곡선인 것은 집합의 파면뿐이다. PhaseDeg 가 볼리마다 달라 로브가 회전하므로 화면에는
	 *  크기와 위상이 다른 꽃이 여러 겹 겹쳐 보인다.
	 *
	 *  ColorSel 은 로브 부호다 — 빠른 로브와 느린 로브를 색으로 가른다(회피 가독성).
	 */
	TArray<FBulletSpawnParams> GenerateRose(const FVector& Origin, const FRoseParams& P);

	struct FPhyllotaxisParams
	{
		FPhyllotaxisParams();        // Speed/Lifetime을 Settings에서 초기화
		int32 Count         = 200;
		float BaseAngleDeg  = 0.f;   // 이번 발사 시작각 (Boss가 누적해 전달)
		float Speed         = 300.f; // 가장 바깥 탄의 속력 — 생성자가 Settings로 덮어씀
		float Lifetime      = 3.f;   // s — 생성자가 Settings로 덮어씀
		float DivergenceDeg = 137.507764f;   // 황금각. 이 값이라야 어느 방향으로도 줄이 안 선다
	};

	/**
	 *  해바라기(Vogel) 원반: 각 i·137.5°, 속력 Speed·√((i+1)/Count).
	 *  T초 뒤 탄 i 의 반경이 Speed·T·√((i+1)/N) 이 되어 파면이 Vogel 나선 — 즉 해바라기 씨앗
	 *  배열이 된다. √ 는 원판 균등 면적 보정이고 황금각은 어느 방향에서도 줄이 서지 않게 한다.
	 *  Rose 가 굵은 띠라면 이쪽은 균일한 점 격자다 — 같은 직선탄인데 화면이 전혀 다르다.
	 *  안쪽 탄일수록 느려 보스 발밑에 조밀한 핵이 생기고, 그게 그대로 근접 위험 구역이 된다.
	 */
	TArray<FBulletSpawnParams> GeneratePhyllotaxis(const FVector& Origin, const FPhyllotaxisParams& P);

	struct FCounterSpiralParams
	{
		FCounterSpiralParams();      // Speed/Lifetime을 Settings에서 초기화
		int32 Count        = 96;     // 두 팔 **합계**. 홀수면 팔 하나가 한 발 많다
		float BaseAngleDeg = 0.f;    // 팔 A 의 시작각. 팔 B 는 부호가 뒤집힌다
		float Speed        = 300.f;
		float Lifetime     = 3.f;
	};

	/**
	 *  역회전 이중 나선: 같은 링을 두 벌 쏘되 회전 방향이 반대다.
	 *  팔 A 는 각 +BaseAngle + i·Step, 팔 B 는 -BaseAngle + i·Step 에서 출발한다.
	 *  두 나선이 서로 반대로 감기며 교차해 마름모 격자(모아레)가 생긴다 — 격자 구멍이
	 *  곧 회피 통로이고, 통로가 두 방향으로 동시에 흘러 읽기가 까다롭다.
	 *  ColorSel 로 팔을 갈라 어느 격자에 속한 탄인지 눈으로 분리된다.
	 */
	TArray<FBulletSpawnParams> GenerateCounterSpiral(const FVector& Origin, const FCounterSpiralParams& P);

	struct FCardioidParams
	{
		FCardioidParams();           // Speed/Lifetime을 Settings에서 초기화
		int32 Count       = 64;
		float AimAngleDeg = 0.f;     // 볼록한 쪽이 향할 방향(플레이어). 서버가 페이로드로 준다
		float RingBaseDeg = 0.f;     // 링 자체 회전. ServerTime 에서 유도한다(페이로드 각은 조준에 쓴다)
		float Speed       = 300.f;
		float Lifetime    = 3.f;
		float Amp         = 0.6f;    // 1.0 이면 첨두에서 속력 0 인 진짜 심장형
	};

	/**
	 *  심장형(리마송) 조준: s(θ) = Speed·(1 + Amp·cos(θ − AimAngle)).
	 *  파면이 플레이어 쪽으로 볼록한 심장형이 되어 조준 방향의 탄이 가장 빠르고 반대편이
	 *  가장 느리다 — 안전지대가 **보스 뒤편 하나**로 고정되므로 플레이어는 보스를 끼고
	 *  돌아야 한다. Rose 와 식은 같은 꼴이지만 로브가 하나뿐이고 그 하나가 사람을 따라온다.
	 *
	 *  링 회전을 ServerTime 에서 유도하는 이유: FireDirect 페이로드의 각은 하나뿐이고
	 *  그 자리를 조준각이 쓴다. 링 회전은 상수·ServerTime 에서 순수 유도되므로 실을 필요가 없다.
	 */
	TArray<FBulletSpawnParams> GenerateCardioid(const FVector& Origin, const FCardioidParams& P);

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

		/**
		 *  베지어 제어점의 **추가** 오프셋. 0 이면 기존 포물선과 완전히 같은 궤적이다.
		 *  궤적은 Start·Ctrl·Target 의 2차 베지어이고 Ctrl = 중점 + (0,0,2·MaxHeight) + 이 값이다.
		 *  XY 성분을 주면 탄이 직선을 벗어나 옆으로 휘감아 들어간다(Z까지 같이 쓰는 3D 궤적).
		 */
		FVector CtrlOffset = FVector::ZeroVector;
	};

	//~ 착지점 생성기 — 전부 월드 착지점(Z=GroundZ) 배열 반환. 순수함수(FRandomStream 제외).
	/** 원형 링: 중심 C, 반경 R, N개 균등각. BaseAngleDeg 로 링을 통째로 돌린다(볼리마다 착지점 이동). */
	TArray<FVector> GenRing(const FVector& Center, float Radius, int32 N, float GroundZ,
	                        float BaseAngleDeg = 0.f);

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

	/**
	 *  리사주 착지: x = ExtentX·sin(FreqX·φ + δ), y = ExtentY·sin(FreqY·φ), φ 를 N등분.
	 *  FreqX·FreqY 가 서로소면 닫힌 매듭이 되고 δ 를 돌리면 매듭이 통째로 꿈틀거린다.
	 *  볼리 하나가 곡선 전체를 N점으로 그리므로 마커가 무늬를 통째로 예고한다 — 밀도가 아니라
	 *  **읽고 통과하는** 회피다. 아레나가 정사각(±2000)이라 Extent 를 그 안쪽에 두면 전부 바닥 위다.
	 */
	TArray<FVector> GenLissajous(const FVector& Center, float ExtentX, float ExtentY,
	                             int32 FreqX, int32 FreqY, float DeltaDeg, int32 N, float GroundZ);

	/**
	 *  소용돌이 제어점 오프셋 — Start→Target 에 수직인 수평 방향으로 Swirl 만큼 민다.
	 *  2차 베지어의 제어점이 직선 밖으로 나가므로 탄이 옆으로 크게 휘감아 들어간다.
	 *  착지점은 t=1 에서 정확히 Target 이라 마커·판정은 그대로다 — 변하는 건 가는 길뿐이다.
	 *  Start 와 Target 이 수평으로 겹치면 접선이 정의되지 않아 0을 돌려준다(직선 폴백).
	 */
	FVector ArcSwirlOffset(const FVector& Start, const FVector& Target, float Swirl);
}
