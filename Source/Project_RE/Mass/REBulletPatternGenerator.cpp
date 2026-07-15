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

	FSpiralParams MakeSpiralRing(int32 Count, float BaseAngleDeg)
	{
		FSpiralParams P;
		// Max(1,...): Count<=0 시 360/0 나눗셈 방지.
		P.Count        = FMath::Max(1, Count);
		P.AngleStepDeg = 360.f / P.Count;   // Count 무관 균등 링
		P.BaseAngleDeg = BaseAngleDeg;
		return P;
	}

	FSpiralParams MakeSpiralForLiveCount(int32 TargetLive, float BaseAngleDeg)
	{
		// 오픈루프 피드포워드: steady-state 동시 탄환 = 발사당_탄수 / 발사주기 × 수명.
		// 탄이 소멸 없이 수명까지 사는 경로(Actor 베이스라인)에서만 목표를 정확히 맞춘다.
		// Mass는 히트 프로세서 소멸분 때문에 미달 → Boss가 클로즈드루프로 보정(#51).
		return MakeSpiralRing(FMath::RoundToInt(TargetLive * FireIntervalSec / BulletLifetimeSec), BaseAngleDeg);
	}
}
