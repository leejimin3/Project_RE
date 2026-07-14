// Copyright Epic Games, Inc. All Rights Reserved.

#include "REBulletPatternGenerator.h"

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
	TArray<FBulletSpawnParams> GenerateSpiral(const FVector& Origin, const FSpiralParams& P)
	{
		TArray<FBulletSpawnParams> Out;
		Out.Reserve(P.Count);
		for (int32 i = 0; i < P.Count; ++i)
		{
			const float Angle = P.BaseAngleDeg + i * P.AngleStepDeg;
			Out.Add({ Origin, DirFromDeg(Angle) * P.Speed, P.Lifetime });
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

	FSpiralParams MakeSpiralForLiveCount(int32 TargetLive, float BaseAngleDeg)
	{
		FSpiralParams P;
		// Max(1,...): TargetLive<=0 (CVar 사용자 입력) 시 360/0 나눗셈 방지.
		P.Count        = FMath::Max(1, FMath::RoundToInt(TargetLive * FireIntervalSec / BulletLifetimeSec));
		P.AngleStepDeg = 360.f / P.Count;   // Count 무관 균등 링
		P.BaseAngleDeg = BaseAngleDeg;
		return P;
	}
}
