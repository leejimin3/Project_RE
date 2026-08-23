// Copyright Epic Games, Inc. All Rights Reserved.

#include "REBulletPatternGenerator.h"
#include "REStatsSettings.h"

namespace
{
	/** 각도(deg) → 수평면(XY, Z up) 단위벡터. */
	FVector DirFromDeg(float Deg)
	{
		const float R = FMath::DegreesToRadians(Deg);
		return FVector(FMath::Cos(R), FMath::Sin(R), 0.f);
	}
}

namespace REBulletPattern
{
	float FireIntervalSec()
	{
		return GetDefault<UREStatsSettings>()->BossFireInterval;
	}

	float BulletLifetimeSec()
	{
		return GetDefault<UREStatsSettings>()->BulletLifetime;
	}

	FSpiralParams::FSpiralParams()
	{
		const UREStatsSettings* S = GetDefault<UREStatsSettings>();
		Speed    = S->BulletSpeed;
		Lifetime = S->BulletLifetime;
	}

	FFanParams::FFanParams()
	{
		const UREStatsSettings* S = GetDefault<UREStatsSettings>();
		Speed    = S->BulletSpeed;
		Lifetime = S->BulletLifetime;
	}

	FRoseParams::FRoseParams()
	{
		const UREStatsSettings* S = GetDefault<UREStatsSettings>();
		Speed    = S->BulletSpeed;
		Lifetime = S->BulletLifetime;
	}

	TArray<FBulletSpawnParams> GenerateSpiral(const FVector& Origin, const FSpiralParams& P)
	{
		TArray<FBulletSpawnParams> Out;
		Out.Reserve(P.Count);
		for (int32 i = 0; i < P.Count; ++i)
		{
			const float Angle = P.BaseAngleDeg + i * P.AngleStepDeg;
			// 체커보드 — 방사(회차)와 원주(링 인덱스) 두 축 모두에서 인접 탄이 다른 색이 된다 (#97).
			const float ColorSel = float((P.ShotParity + i) & 1);
			Out.Add({ Origin, DirFromDeg(Angle) * P.Speed, P.Lifetime, ColorSel });
		}
		return Out;
	}

	TArray<FBulletSpawnParams> GenerateFan(const FVector& Origin, const FFanParams& P)
	{
		TArray<FBulletSpawnParams> Out;
		Out.Reserve(P.Count);
		const float Start = P.CenterAngleDeg - P.SpreadDeg * 0.5f;
		const float Step  = (P.Count > 1) ? P.SpreadDeg / (P.Count - 1) : 0.f;
		for (int32 i = 0; i < P.Count; ++i)
		{
			const float Angle = Start + i * Step;
			Out.Add({ Origin, DirFromDeg(Angle) * P.Speed, P.Lifetime });
		}
		return Out;
	}

	TArray<FBulletSpawnParams> GenerateRose(const FVector& Origin, const FRoseParams& P)
	{
		TArray<FBulletSpawnParams> Out;
		// Max(1,...): Count<=0 시 360/0 나눗셈 방지.
		const int32 Count = FMath::Max(P.Count, 1);
		Out.Reserve(Count);
		const float Step = 360.f / Count;   // Count 무관 균등 링
		for (int32 i = 0; i < Count; ++i)
		{
			const float Angle = P.BaseAngleDeg + i * Step;
			// 변조는 **절대 각**의 함수다 — 링이 회전해도 로브는 제자리다.
			// 로브를 돌리는 축은 PhaseDeg 하나뿐이라 두 회전이 서로 상쇄되지 않는다.
			const float Mod   = FMath::Cos(FMath::DegreesToRadians(P.Lobes * Angle + P.PhaseDeg));
			const float Speed = P.Speed * (1.f + P.Amp * Mod);
			Out.Add({ Origin, DirFromDeg(Angle) * Speed, P.Lifetime, (Mod > 0.f) ? 1.f : 0.f });
		}
		return Out;
	}

	FPhyllotaxisParams::FPhyllotaxisParams()
	{
		const UREStatsSettings* S = GetDefault<UREStatsSettings>();
		Speed    = S->BulletSpeed;
		Lifetime = S->BulletLifetime;
	}

	TArray<FBulletSpawnParams> GeneratePhyllotaxis(const FVector& Origin, const FPhyllotaxisParams& P)
	{
		TArray<FBulletSpawnParams> Out;
		const int32 Count = FMath::Max(P.Count, 1);
		Out.Reserve(Count);
		for (int32 i = 0; i < Count; ++i)
		{
			const float Angle = P.BaseAngleDeg + i * P.DivergenceDeg;
			// √ 는 원판 균등 면적 보정이다 — 선형으로 주면 안쪽이 성기고 바깥이 뭉친다.
			const float Speed = P.Speed * FMath::Sqrt((float)(i + 1) / Count);
			// 색은 씨앗 인덱스 홀짝 — 이웃한 나선 줄기끼리 갈려 원반의 나선 결이 드러난다.
			Out.Add({ Origin, DirFromDeg(Angle) * Speed, P.Lifetime, float(i & 1) });
		}
		return Out;
	}

	FCounterSpiralParams::FCounterSpiralParams()
	{
		const UREStatsSettings* S = GetDefault<UREStatsSettings>();
		Speed    = S->BulletSpeed;
		Lifetime = S->BulletLifetime;
	}

	TArray<FBulletSpawnParams> GenerateCounterSpiral(const FVector& Origin, const FCounterSpiralParams& P)
	{
		TArray<FBulletSpawnParams> Out;
		const int32 Total = FMath::Max(P.Count, 2);
		const int32 PerArm = Total / 2;
		Out.Reserve(PerArm * 2);
		const float Step = 360.f / PerArm;   // 팔 하나가 균등 링을 이룬다
		for (int32 Arm = 0; Arm < 2; ++Arm)
		{
			// 팔 B 는 시작각 부호만 뒤집는다 — 볼리가 쌓이면 두 나선이 반대로 감긴다.
			const float Base = (Arm == 0) ? P.BaseAngleDeg : -P.BaseAngleDeg;
			for (int32 i = 0; i < PerArm; ++i)
			{
				Out.Add({ Origin, DirFromDeg(Base + i * Step) * P.Speed, P.Lifetime, float(Arm) });
			}
		}
		return Out;
	}

	FCardioidParams::FCardioidParams()
	{
		const UREStatsSettings* S = GetDefault<UREStatsSettings>();
		Speed    = S->BulletSpeed;
		Lifetime = S->BulletLifetime;
	}

	TArray<FBulletSpawnParams> GenerateCardioid(const FVector& Origin, const FCardioidParams& P)
	{
		TArray<FBulletSpawnParams> Out;
		const int32 Count = FMath::Max(P.Count, 1);
		Out.Reserve(Count);
		const float Step = 360.f / Count;
		for (int32 i = 0; i < Count; ++i)
		{
			const float Angle = P.RingBaseDeg + i * Step;
			// 조준각과의 차가 0 인 방향이 가장 빠르다 → 파면이 그쪽으로 볼록해진다.
			const float Mod   = FMath::Cos(FMath::DegreesToRadians(Angle - P.AimAngleDeg));
			const float Speed = P.Speed * (1.f + P.Amp * Mod);
			Out.Add({ Origin, DirFromDeg(Angle) * Speed, P.Lifetime, (Mod > 0.f) ? 1.f : 0.f });
		}
		return Out;
	}

	FCurveBloomParams::FCurveBloomParams()
	{
		Lifetime = GetDefault<UREStatsSettings>()->BulletLifetime;
	}

	TArray<FBulletSpawnParams> GenerateCurveBloom(const FVector& Origin, TConstArrayView<FVector2D> Curve,
	                                              const FCurveBloomParams& P)
	{
		TArray<FBulletSpawnParams> Out;
		Out.Reserve(Curve.Num());
		for (int32 i = 0; i < Curve.Num(); ++i)
		{
			const FVector Off(Curve[i].X, Curve[i].Y, 0.f);
			// 속도가 위치에 비례해야 도형이 안 일그러진다 — 균일 속력을 주면 모든 점이
			// 같은 거리를 나아가 모양이 바깥으로 갈수록 둥글게 뭉개진다.
			Out.Add({ Origin + Off, Off * P.ScaleRate, P.Lifetime, float(i & 1) });
		}
		return Out;
	}

	TArray<FVector2D> GenStarPolygon(int32 N, int32 Skip, float Radius, float RotDeg, int32 SegPerEdge)
	{
		TArray<FVector2D> Out;
		const int32 Verts = FMath::Max(N, 3);
		const int32 Step  = FMath::Clamp(Skip, 1, Verts - 1);
		const int32 Seg   = FMath::Max(SegPerEdge, 1);
		Out.Reserve(Verts * Seg);
		const float Rot = FMath::DegreesToRadians(RotDeg);
		// 한붓그리기: v → v+Skip → v+2·Skip … gcd(N,Skip)=1 이면 Verts 걸음에 제자리로 온다.
		for (int32 e = 0; e < Verts; ++e)
		{
			const float A0 = Rot + 2.f * PI * ((e * Step)       % Verts) / Verts;
			const float A1 = Rot + 2.f * PI * (((e + 1) * Step) % Verts) / Verts;
			const FVector2D V0(Radius * FMath::Cos(A0), Radius * FMath::Sin(A0));
			const FVector2D V1(Radius * FMath::Cos(A1), Radius * FMath::Sin(A1));
			for (int32 s = 0; s < Seg; ++s)
			{
				Out.Add(FMath::Lerp(V0, V1, (float)s / Seg));   // 끝점은 다음 변이 찍는다
			}
		}
		return Out;
	}

	TArray<FVector2D> GenLemniscate(int32 N, float A, float RotDeg)
	{
		TArray<FVector2D> Out;
		const int32 Count = FMath::Max(N, 0);
		Out.Reserve(Count);
		const float Rot = FMath::DegreesToRadians(RotDeg);
		const float C = FMath::Cos(Rot), S = FMath::Sin(Rot);
		for (int32 i = 0; i < Count; ++i)
		{
			const float U = 2.f * PI * i / FMath::Max(Count, 1);
			const float Sn = FMath::Sin(U), Cs = FMath::Cos(U);
			const float D = 1.f + Sn * Sn;              // 분모는 항상 [1,2] — 0 나눗셈이 없다
			const float X = A * Cs / D;
			const float Y = A * Sn * Cs / D;
			Out.Add(FVector2D(X * C - Y * S, X * S + Y * C));   // 회전
		}
		return Out;
	}

	TArray<FVector2D> GenSuperformula(int32 N, float M, float N1, float N2, float N3,
	                                  float Radius, float RotDeg)
	{
		TArray<FVector2D> Out;
		const int32 Count = FMath::Max(N, 0);
		Out.Reserve(Count);
		const float Rot = FMath::DegreesToRadians(RotDeg);
		const float SafeN1 = (FMath::Abs(N1) > KINDA_SMALL_NUMBER) ? N1 : 1.f;

		// 반경을 그때그때 최대값으로 정규화한다 — m/n 이 변하면 raw 반경이 몇 배씩 뛰어서
		// 정규화 없이는 morph 중에 도형 크기가 요동친다.
		TArray<float> Raw;
		Raw.Reserve(Count);
		float MaxR = KINDA_SMALL_NUMBER;
		for (int32 i = 0; i < Count; ++i)
		{
			const float Phi = 2.f * PI * i / FMath::Max(Count, 1);
			const float T1 = FMath::Pow(FMath::Abs(FMath::Cos(M * Phi * 0.25f)), N2);
			const float T2 = FMath::Pow(FMath::Abs(FMath::Sin(M * Phi * 0.25f)), N3);
			// T1+T2 는 0 이 될 수 있다(두 항이 동시에 0인 각). 음의 지수라 0 나눗셈이 된다.
			const float Sum = FMath::Max(T1 + T2, KINDA_SMALL_NUMBER);
			const float R   = FMath::Pow(Sum, -1.f / SafeN1);
			Raw.Add(R);
			MaxR = FMath::Max(MaxR, R);
		}
		for (int32 i = 0; i < Count; ++i)
		{
			const float Phi = Rot + 2.f * PI * i / FMath::Max(Count, 1);
			const float R   = Radius * Raw[i] / MaxR;
			Out.Add(FVector2D(R * FMath::Cos(Phi), R * FMath::Sin(Phi)));
		}
		return Out;
	}

	FSpiralParams MakeSpiralRing(int32 Count, float BaseAngleDeg)
	{
		FSpiralParams P;
		// Max(1,...): Count<=0 시 360/0 나눗셈 방지.
		P.Count        = FMath::Max(1, Count);
		P.AngleStepDeg = 360.f / P.Count;   // Count 무관 균등 링
		P.BaseAngleDeg = BaseAngleDeg;
		return P;
	}

	TArray<FVector> GenRing(const FVector& Center, float Radius, int32 N, float GroundZ, float BaseAngleDeg)
	{
		TArray<FVector> Out;
		const int32 Count = FMath::Max(N, 0);
		Out.Reserve(Count);
		const float Base = FMath::DegreesToRadians(BaseAngleDeg);
		for (int32 i = 0; i < Count; ++i)
		{
			const float Ang = Base + 2.f * PI * i / FMath::Max(Count, 1);
			Out.Add(FVector(Center.X + Radius * FMath::Cos(Ang),
			                Center.Y + Radius * FMath::Sin(Ang), GroundZ));
		}
		return Out;
	}

	TArray<FVector> GenSweepSpiral(const FVector& Center, float MinRadius, float MaxRadius, float Turns,
	                               float T0, float T1, int32 N, int32 Arms, float GroundZ)
	{
		TArray<FVector> Out;
		const int32 Slots   = FMath::Max(N, 0);
		const int32 ArmNum  = FMath::Max(Arms, 1);
		const float ArmStep = 360.f / ArmNum;
		Out.Reserve(Slots * ArmNum);
		for (int32 s = 0; s < Slots; ++s)
		{
			// Slots==1 이면 T0 하나만 낸다(0 나눗셈 방지).
			const float f = (Slots > 1) ? ((float)s / (Slots - 1)) : 0.f;
			const float t = FMath::Lerp(T0, T1, f);
			const float Radius  = FMath::Lerp(MinRadius, MaxRadius, t);
			const float BaseDeg = t * 360.f * Turns;
			for (int32 a = 0; a < ArmNum; ++a)
			{
				const float Ang = FMath::DegreesToRadians(BaseDeg + a * ArmStep);
				Out.Add(FVector(Center.X + Radius * FMath::Cos(Ang),
				                Center.Y + Radius * FMath::Sin(Ang), GroundZ));
			}
		}
		return Out;
	}

	TArray<FVector> GenLine(const FVector& BossLoc, const FVector& PlayerLoc, float WallLen, int32 N, float GroundZ)
	{
		TArray<FVector> Out;
		const int32 Count = FMath::Max(N, 0);
		Out.Reserve(Count);

		FVector Dir = PlayerLoc - BossLoc;
		Dir.Z = 0.f;
		if (!Dir.Normalize())
		{
			Dir = FVector(1.f, 0.f, 0.f);   // 보스=플레이어 겹침 폴백
		}
		const FVector Normal(-Dir.Y, Dir.X, 0.f);          // 벽 방향(진행 수직)
		const FVector Mid(PlayerLoc.X, PlayerLoc.Y, GroundZ);
		for (int32 i = 0; i < Count; ++i)
		{
			const float f = (Count > 1) ? ((float)i / (Count - 1) - 0.5f) : 0.f;  // -0.5..0.5
			Out.Add(Mid + Normal * (f * WallLen));
		}
		return Out;
	}

	TArray<FVector> GenGrid(const FVector& Center, float ExtentX, float ExtentY, int32 Cols, int32 Rows, float GroundZ)
	{
		TArray<FVector> Out;
		const int32 C = FMath::Max(Cols, 1);
		const int32 R = FMath::Max(Rows, 1);
		Out.Reserve(C * R);
		for (int32 r = 0; r < R; ++r)
		{
			for (int32 c = 0; c < C; ++c)
			{
				const float fx = (C > 1) ? ((float)c / (C - 1) - 0.5f) : 0.f;
				const float fy = (R > 1) ? ((float)r / (R - 1) - 0.5f) : 0.f;
				Out.Add(FVector(Center.X + fx * 2.f * ExtentX,
				                Center.Y + fy * 2.f * ExtentY, GroundZ));
			}
		}
		return Out;
	}

	TArray<FVector> GenArcSpiral(const FVector& Center, float MaxRadius, int32 N, float GroundZ)
	{
		TArray<FVector> Out;
		const int32 Count = FMath::Max(N, 0);
		Out.Reserve(Count);
		for (int32 i = 0; i < Count; ++i)
		{
			const float Ang = FMath::DegreesToRadians(i * 137.5f);            // 황금각(기존 Spiral 재사용)
			const float Rad = MaxRadius * FMath::Sqrt((float)(i + 1) / FMath::Max(Count, 1));
			Out.Add(FVector(Center.X + Rad * FMath::Cos(Ang),
			                Center.Y + Rad * FMath::Sin(Ang), GroundZ));
		}
		return Out;
	}

	TArray<FVector> GenPlayerCluster(const FVector& PlayerLoc, float ClusterRadius, int32 RingN, float GroundZ)
	{
		TArray<FVector> Out;
		const int32 Ring = FMath::Max(RingN, 0);
		Out.Reserve(Ring + 1);
		Out.Add(FVector(PlayerLoc.X, PlayerLoc.Y, GroundZ));   // 중심(직격)
		for (int32 i = 0; i < Ring; ++i)
		{
			const float Ang = 2.f * PI * i / FMath::Max(Ring, 1);
			Out.Add(FVector(PlayerLoc.X + ClusterRadius * FMath::Cos(Ang),
			                PlayerLoc.Y + ClusterRadius * FMath::Sin(Ang), GroundZ));
		}
		return Out;
	}

	TArray<FVector> GenLissajous(const FVector& Center, float ExtentX, float ExtentY,
	                             int32 FreqX, int32 FreqY, float DeltaDeg, int32 N, float GroundZ)
	{
		TArray<FVector> Out;
		const int32 Count = FMath::Max(N, 0);
		Out.Reserve(Count);
		const float Delta = FMath::DegreesToRadians(DeltaDeg);
		for (int32 i = 0; i < Count; ++i)
		{
			// φ 는 [0,2π) 를 N등분한다 — 마지막 점이 첫 점과 겹치지 않도록 N 으로 나눈다.
			const float Phi = 2.f * PI * i / FMath::Max(Count, 1);
			Out.Add(FVector(Center.X + ExtentX * FMath::Sin(FreqX * Phi + Delta),
			                Center.Y + ExtentY * FMath::Sin(FreqY * Phi), GroundZ));
		}
		return Out;
	}

	TArray<FVector> GenRoseCurve(const FVector& Center, float Radius, int32 Petals,
	                             float RotDeg, int32 N, float GroundZ)
	{
		TArray<FVector> Out;
		const int32 Count = FMath::Max(N, 0);
		Out.Reserve(Count);
		const int32 K   = FMath::Max(Petals, 1);
		const float Rot = FMath::DegreesToRadians(RotDeg);
		// K 가 짝수면 꽃이 2π 에서 닫히고, 홀수면 π 에서 이미 전체를 그린다 —
		// 홀수에 2π 를 돌리면 같은 꽃잎을 두 번 그려 표본이 절반으로 낭비된다.
		const float Span = (K % 2 == 0) ? (2.f * PI) : PI;
		for (int32 i = 0; i < Count; ++i)
		{
			const float Th = Span * i / FMath::Max(Count, 1);
			const float R  = Radius * FMath::Cos(K * Th);   // 음수 허용 — 반대쪽 꽃잎이다
			const float A  = Th + Rot;
			Out.Add(FVector(Center.X + R * FMath::Cos(A), Center.Y + R * FMath::Sin(A), GroundZ));
		}
		return Out;
	}

	TArray<FVector> GenHypotrochoid(const FVector& Center, float Radius, int32 BigR, int32 SmallR,
	                                float D, float RotDeg, int32 N, float GroundZ)
	{
		TArray<FVector> Out;
		const int32 Count = FMath::Max(N, 0);
		Out.Reserve(Count);
		const float Rr = (float)FMath::Max(SmallR, 1);
		const float Diff = (float)BigR - Rr;
		// 최대 반경으로 정규화 — 파라미터를 바꿔도 아레나에 맞는 크기가 유지된다.
		const float Norm = Radius / FMath::Max(FMath::Abs(Diff) + FMath::Abs(D), KINDA_SMALL_NUMBER);
		const float Rot = FMath::DegreesToRadians(RotDeg);
		const float C = FMath::Cos(Rot), S = FMath::Sin(Rot);
		// 서로소면 t 가 2π·r 까지 돌아야 닫힌다. 아니어도 이 구간이면 최소 한 바퀴는 넘는다.
		const float Span = 2.f * PI * Rr;
		for (int32 i = 0; i < Count; ++i)
		{
			const float T = Span * i / FMath::Max(Count, 1);
			const float X = (Diff * FMath::Cos(T) + D * FMath::Cos(Diff / Rr * T)) * Norm;
			const float Y = (Diff * FMath::Sin(T) - D * FMath::Sin(Diff / Rr * T)) * Norm;
			Out.Add(FVector(Center.X + X * C - Y * S, Center.Y + X * S + Y * C, GroundZ));
		}
		return Out;
	}

	FVector ArcSwirlOffset(const FVector& Start, const FVector& Target, float Swirl)
	{
		FVector D = Target - Start;
		D.Z = 0.f;
		if (!D.Normalize())
		{
			return FVector::ZeroVector;   // 수평으로 겹침 — 접선이 없다. 직선 폴백.
		}
		return FVector(-D.Y, D.X, 0.f) * Swirl;   // 진행 방향 좌측 수직
	}

	FArcShapeOffsets ArcSpiralColumn(const FVector& Start, const FVector& Target, float Swirl, float Rise)
	{
		const FVector Tan = ArcSwirlOffset(Start, Target, 1.f);   // 단위 좌수직(겹치면 0)
		FArcShapeOffsets O;
		// 뒤쪽 제어점을 1.6배 더 멀리 민다 — 같은 거리면 대칭 C자라 감김이 얕다.
		// 비대칭이라야 '올라가서 돌아 내려온다'는 인상이 나온다.
		O.Ctrl1 = Tan * Swirl        + FVector(0.f, 0.f, Rise);
		O.Ctrl2 = Tan * Swirl * 1.6f + FVector(0.f, 0.f, Rise * 0.4f);
		return O;
	}

	FArcShapeOffsets ArcDomeShell(const FVector& Start, const FVector& Target, float Rise)
	{
		const FVector Mid = (Start + Target) * 0.5f;
		FArcShapeOffsets O;
		// 차수 상승 기본값은 두 제어점이 중점 쪽으로 ⅔ 당겨져 있어 완만한 포물선이 된다.
		// 각자 자기 끝점 쪽으로 되밀면 오르내림이 가팔라져 아치가 된다.
		O.Ctrl1 = (Start  - Mid) * 0.5f + FVector(0.f, 0.f, Rise);
		O.Ctrl2 = (Target - Mid) * 0.5f + FVector(0.f, 0.f, Rise);
		return O;
	}

	FArcShapeOffsets ArcSCurve(const FVector& Start, const FVector& Target, float Swing, float Rise)
	{
		const FVector Tan = ArcSwirlOffset(Start, Target, 1.f);
		FArcShapeOffsets O;
		// 반대 방향 — 2차 베지어는 제어점이 하나뿐이라 이 모양을 만들 수 없다.
		O.Ctrl1 =  Tan * Swing + FVector(0.f, 0.f, Rise);
		O.Ctrl2 = -Tan * Swing + FVector(0.f, 0.f, Rise);
		return O;
	}

	TArray<FVector> GenRandom(const FVector& Center, float ArenaRadius, int32 N, FRandomStream& Rng, float GroundZ)
	{
		TArray<FVector> Out;
		const int32 Count = FMath::Max(N, 0);
		Out.Reserve(Count);
		for (int32 i = 0; i < Count; ++i)
		{
			const float Ang = Rng.FRandRange(0.f, 2.f * PI);
			const float Rad = ArenaRadius * FMath::Sqrt(Rng.FRand());  // √ 보정 = 원판 균등 면적
			Out.Add(FVector(Center.X + Rad * FMath::Cos(Ang),
			                Center.Y + Rad * FMath::Sin(Ang), GroundZ));
		}
		return Out;
	}

}
