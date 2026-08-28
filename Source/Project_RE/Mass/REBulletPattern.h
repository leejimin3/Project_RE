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
	RoseEnvelope,    // 장미 포락선: 균등 링에 각도별 속력 변조 — 직선탄만으로 파면이 곡선을 그린다.
	Cardioid,        // 심장형 조준: 로브 하나짜리 리마송이 플레이어를 향해 볼록해진다.
	LissajousStorm,  // 곡사 리사주: 착지점이 매듭 곡선. δ 가 돌아 무늬가 통째로 꿈틀거린다.
	BezierVortex,    // 곡사 소용돌이: 베지어 제어점을 접선으로 밀어 공중에서 휘감아 들어간다.
	//~ 곡선 블룸 3종(직선탄). 탄을 곡선 위에 스폰하고 속도를 위치에 비례시켜 자기닮음으로 부푼다.
	StarBloom,       // 별 다각형 {7/3} 이 각을 유지한 채 부푼다.
	LemniscateBloom, // ∞(렘니스케이트). 자기교차라 속력 변조로는 못 만드는 모양이다.
	SuperformulaBloom, // Gielis 초공식. m 이 시간에 따라 움직여 꽃↔별↔다각형으로 변태한다.
	//~ 곡사 3종. 바닥에 **그래프를 그리고** 3D 비행으로 그 위에 내려앉는다.
	//  판정은 착지 XY 하나뿐이라 비행 경로는 전부 3D로 자유롭다.
	RoseField,       // 바닥에 장미 r=A·cos(kθ). 탄은 감아 돌며 꽃잎 위로 내려온다.
	AerialDome,      // 바닥에 팽창하는 링. 급상승·고공 수평·급강하 — 궤적이 반구 껍질의 자오선.
	Spirograph,      // 바닥에 하이포트로코이드 로제트. 탄은 S자로 엇갈리며 내려온다.
	MicroMissile     // 동/서/남/북 윗대각선으로 뻗었다가 꺾여 플레이어 위치로 내리꽂는 4발.
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
