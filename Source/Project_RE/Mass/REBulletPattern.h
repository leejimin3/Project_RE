// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "REBulletPattern.generated.h"

/**
 *  보스 탄막 패턴 종류. M0은 슬롯만 — 각 패턴의 발사 수학은 M1.
 */
UENUM(BlueprintType)
enum class EBulletPattern : uint8
{
	Spiral,
	Fan,
	Homing,
	Artillery,       // 곡사: 포물선 착지 + 예고 마커 + 범위 데미지
	ArtilleryStorm,  // 곡사 폭풍: 짧은 간격 + 긴 체공으로 동시 체공 탄을 쌓는다. 착지 모양은 Spiral 고정.
	RoseEnvelope     // 장미 포락선: 균등 링에 각도별 속력 변조 — 직선탄만으로 파면이 곡선을 그린다.
};

/** 곡사 착지점 모양. Artillery 페이즈에서 랜덤 선택. */
UENUM(BlueprintType)
enum class EArtilleryShape : uint8
{
	Ring,          // 원형 링
	Line,          // 보스→플레이어 수직 벽
	Grid,          // 아레나 균등 격자
	Spiral,        // 아르키메데스 나선(황금각)
	PlayerAimed,   // 플레이어 위치 + 주변 클러스터
	Random         // 아레나 반경 내 균등 랜덤
};
