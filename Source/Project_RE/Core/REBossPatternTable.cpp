// Copyright Epic Games, Inc. All Rights Reserved.

#include "REBossPatternTable.h"
#include "Project_RE.h"   // LogRE

namespace REBoss
{
	const FPatternDef& GetPatternDef(EBulletPattern P)
	{
		const int32 Idx = (int32)P;
		if (Idx >= 0 && Idx < PatternTableNum)
		{
			return PatternTable[Idx];
		}
		// 네트워크 페이로드(uint8 enum)로 들어온 값이라 범위 밖이 이론상 가능하다 — 구버전 클라,
		// 손상된 패킷. 크래시 대신 Spiral 로 폴백하되 **조용히 넘기지 않는다.**
		// 로그 없는 폴백이 이 프로젝트의 측정을 두 번 망쳤다 (#50, #88).
		UE_LOG(LogRE, Error, TEXT("[RE] GetPatternDef: 범위 밖 패턴 %d — Spiral 폴백"), Idx);
		return PatternTable[0];
	}
}
