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

	//~ 곡선 블룸 — 탄을 곡선 위에 스폰하고 속도를 위치 벡터에 비례시킨다.
	//
	//  속력 변조(Rose/Cardioid)는 파면이 r(θ) 인 극좌표 곡선이라 **한 각도에 한 반경**인
	//  모양(별모양 영역)만 만든다 — ∞ 처럼 자기교차하는 곡선은 원리상 못 만든다.
	//  블룸은 그 제약이 없다: 모양을 스폰 위치로 직접 그리고 속도를 위치에 비례시키면
	//
	//      V = P·k   →   위치(T) = P + P·k·T = P·(1 + kT)
	//
	//  가 되어 도형이 **완전한 자기닮음으로** 부푼다. 어떤 닫힌 곡선이든 된다.
	//  FBulletSpawnParams::Location 이 원래 탄별 필드라 스폰 인프라는 그대로다.
	//  지연 보정(Location += Velocity·Elapsed)도 등속 직선이라 정확히 맞는다.

	struct FCurveBloomParams
	{
		FCurveBloomParams();         // Lifetime 을 Settings에서 초기화
		/**
		 *  초당 배율 증가량(1/s). T초 뒤 도형 크기가 (1 + ScaleRate·T) 배가 된다.
		 *  곡선 반경 R 인 점의 속력이 R·ScaleRate 이므로 큰 도형일수록 빨라진다.
		 */
		float ScaleRate = 1.75f;
		float Lifetime  = 3.f;
	};

	/**
	 *  곡선(보스 기준 오프셋 배열) 위에 탄을 놓고 바깥으로 자기닮음 확대시킨다.
	 *  ColorSel 은 표본 인덱스 홀짝 — 곡선을 따라 색이 번갈아 곡선의 진행이 읽힌다.
	 */
	TArray<FBulletSpawnParams> GenerateCurveBloom(const FVector& Origin, TConstArrayView<FVector2D> Curve,
	                                              const FCurveBloomParams& P);

	//~ 곡선 생성기 — 전부 원점 기준 2D 오프셋. 순수 함수.

	/**
	 *  별 다각형 {N/Skip}: 꼭짓점 N개를 Skip 칸씩 건너뛰며 이은 성형별({7/3} 등).
	 *  변마다 SegPerEdge 개로 표본을 잘라 **직선 변**을 낸다 — 각진 모서리가 이 도형의 정체성이라
	 *  꼭짓점만 찍으면 안 된다. gcd(N,Skip)=1 이라야 한붓그리기로 닫힌다.
	 */
	TArray<FVector2D> GenStarPolygon(int32 N, int32 Skip, float Radius, float RotDeg, int32 SegPerEdge);

	/**
	 *  베르누이 렘니스케이트(∞). x = A·cos u/(1+sin²u), y = A·sin u·cos u/(1+sin²u).
	 *  원점에서 자기교차하므로 극좌표 r(θ) 로는 표현되지 않는다 — 블룸이라야 나오는 모양이다.
	 */
	TArray<FVector2D> GenLemniscate(int32 N, float A, float RotDeg);

	/**
	 *  Gielis 초공식:  r(φ) = [ |cos(m·φ/4)/a|^n2 + |sin(m·φ/4)/b|^n3 ]^(-1/n1)
	 *  m 이 대칭 가지 수, n1 이 뾰족함이다. m 을 연속으로 움직이면 꽃↔별↔다각형으로 변태한다
	 *  (정수가 아니어도 정의된다 — 비대칭 중간 모양이 나온다).
	 *  a=b=1 고정. Radius 는 최대 반경으로 정규화한 뒤 곱한다 — m 이 변해도 크기가 안 튄다.
	 */
	TArray<FVector2D> GenSuperformula(int32 N, float M, float N1, float N2, float N3,
	                                  float Radius, float RotDeg);

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
		 *  **2차** 제어점의 추가 오프셋. 궤적의 뼈대는 C = 중점 + (0,0,2·MaxHeight) + 이 값이고,
		 *  스포너가 이걸 3차로 차수 상승시킨다. XY 성분을 주면 탄이 직선을 벗어나 한쪽으로
		 *  휘감아 들어간다 — 대칭적인 한 번 휨이다.
		 */
		FVector CtrlOffset = FVector::ZeroVector;
		/**
		 *  차수 상승 뒤 **3차 제어점 각각**에 더하는 오프셋. 두 값을 다르게 주면 2차로는
		 *  못 만드는 궤적이 나온다 — 서로 반대로 밀면 S자, 같은 방향으로 밀면 감김이 깊어진다.
		 *  Ctrl1 은 출발 쪽, Ctrl2 는 착지 쪽 굽힘을 쥔다. 둘 다 0 이면 순수 2차 궤적이다.
		 */
		FVector Ctrl1Offset = FVector::ZeroVector;
		FVector Ctrl2Offset = FVector::ZeroVector;
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
	/**
	 *  장미 착지: r = Radius·cos(Petals·θ). r 이 음수인 구간은 반대쪽 꽃잎을 그리므로
	 *  **부호를 살려야** 꽃이 완성된다(|r| 로 접으면 꽃잎이 절반만 나온다).
	 *  Petals 가 홀수면 꽃잎 Petals 장, 짝수면 2×Petals 장이다.
	 */
	TArray<FVector> GenRoseCurve(const FVector& Center, float Radius, int32 Petals,
	                             float RotDeg, int32 N, float GroundZ);

	/**
	 *  하이포트로코이드(스피로그래프) 착지:
	 *      x = (R−r)·cos t + D·cos((R−r)/r · t)
	 *      y = (R−r)·sin t − D·sin((R−r)/r · t)
	 *  R,r 이 서로소면 t 를 2π·r 까지 돌려야 도형이 닫힌다 — 그 구간을 N등분한다.
	 *  최대 반경이 (R−r)+D 이므로 그 값으로 정규화한 뒤 Radius 를 곱해 아레나에 맞춘다.
	 */
	TArray<FVector> GenHypotrochoid(const FVector& Center, float Radius, int32 BigR, int32 SmallR,
	                                float D, float RotDeg, int32 N, float GroundZ);

	FVector ArcSwirlOffset(const FVector& Start, const FVector& Target, float Swirl);

	//~ 3차 궤적 성형 프리셋 — 제어점 **쌍**을 낸다. 2차(제어점 하나)로는 한 번 휘는 것밖에
	//  못 하므로 아래 셋은 전부 3차라야 나오는 모양이다.
	struct FArcShapeOffsets
	{
		FVector Ctrl1 = FVector::ZeroVector;
		FVector Ctrl2 = FVector::ZeroVector;
	};

	/**
	 *  나선 기둥: 두 제어점을 **같은** 접선 방향으로 밀되 뒤쪽을 더 멀리 민다.
	 *  탄이 솟았다가 축을 크게 감아 돌아 착지점으로 떨어진다 — 볼리가 쌓이면 회전하는 기둥이다.
	 */
	FArcShapeOffsets ArcSpiralColumn(const FVector& Start, const FVector& Target, float Swirl, float Rise);

	/**
	 *  돔 껍질: 제어점을 각자 자기 끝점 쪽으로 당기고 위로 올린다 →
	 *  급상승 · 고공 수평 이동 · 급강하. 착지점을 링으로 두면 궤적들이 반구 껍질의 자오선이 된다.
	 */
	FArcShapeOffsets ArcDomeShell(const FVector& Start, const FVector& Target, float Rise);

	/**
	 *  S자: 두 제어점을 **반대** 접선 방향으로 민다. 탄이 한 번 왼쪽, 한 번 오른쪽으로 휜다.
	 *  Swing 부호를 탄마다 뒤집으면 이웃 궤적이 서로 엇갈려 공중에 리본이 짜인다.
	 */
	FArcShapeOffsets ArcSCurve(const FVector& Start, const FVector& Target, float Swing, float Rise);

	/**
	 *  나침반 로브: 출발 제어점을 **Start→Target 과 무관한 고정 방향** Dir 로 밀어낸다.
	 *  3차 베지어의 초기 접선이 (Ctrl1 − Start) 라, 이렇게 하면 탄이 목표와 상관없이 먼저
	 *  그 방향(동/서/남/북)으로 비스듬히 솟았다가 꺾여 들어간다 — 유도가 아니라 **고정된
	 *  발사 방향 + 고정된 착지점**이므로 궤적이 발사 순간에 완전히 결정된다.
	 *  착지 제어점은 착지점 바로 위로 당겨 급강하시킨다.
	 *
	 *  Dir 은 수평 단위벡터를 기대한다(정규화하지 않는다 — 호출자가 축 벡터를 그대로 준다).
	 */
	FArcShapeOffsets ArcCompassLob(const FVector& Start, const FVector& Target, const FVector& Dir,
	                               float OutDist, float Rise1, float Rise2);
}
