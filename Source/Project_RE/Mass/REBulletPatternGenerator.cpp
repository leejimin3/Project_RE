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
