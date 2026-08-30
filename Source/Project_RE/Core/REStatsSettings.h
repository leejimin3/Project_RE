// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "REStatsSettings.generated.h"

/**
 *  플레이어/보스 스탯 단일 출처 (M3.5 ③).
 *  에디터 Project Settings > Game > RE Stats 에서 편집 → DefaultGame.ini 저장. 리빌드 불필요.
 *  값 변경은 재시작 시 반영 (소비처가 생성자/스폰 시점에 조회).
 *  기본값 = 이관 전 하드코딩과 동일 (ini 미변경 시 회귀 없음).
 *  탄막은 BulletsPerShot 고정 발수(오픈루프 — 균일 패턴)가 기본. CVar re.Bullets.Count(≥0)는
 *  측정용 클로즈드루프(동시 탄수 유지) 오버라이드. 스폰 게인(re.Bullets.SpawnKi)도 측정 전용 CVar.
 */
UCLASS(Config = Game, defaultconfig, meta = (DisplayName = "RE Stats"))
class UREStatsSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	/** Project Settings 배치 카테고리 — Game 섹션. */
	virtual FName GetCategoryName() const override { return FName("Game"); }

	/** 플레이어 최대 체력. */
	UPROPERTY(EditAnywhere, Config, Category = "Player")
	float PlayerMaxHealth = 100.f;

	/** 좌클릭 공격 발사당 데미지. */
	UPROPERTY(EditAnywhere, Config, Category = "Player")
	float AttackDamage = 10.f;

	/** 좌클릭 공격 발사 간격(s). 서버 rate limit + 클라 페이싱 공용. */
	UPROPERTY(EditAnywhere, Config, Category = "Player")
	float AttackInterval = 0.25f;

	/** 좌클릭 공격 히트스캔 사거리(uu). */
	UPROPERTY(EditAnywhere, Config, Category = "Player")
	float AttackRange = 2000.f;

	/** 보스 최대 체력. */
	UPROPERTY(EditAnywhere, Config, Category = "Boss")
	float BossMaxHealth = 100.f;

	/** 보스 탄막 탄속(uu/s). */
	UPROPERTY(EditAnywhere, Config, Category = "Boss")
	float BulletSpeed = 300.f;

	/** 보스 탄막 탄 수명(s) = "탄막 지속시간". 클로즈드루프 역산 공식에도 사용. */
	UPROPERTY(EditAnywhere, Config, Category = "Boss")
	float BulletLifetime = 3.f;

	/** 보스 탄막 발사 주기(s). */
	UPROPERTY(EditAnywhere, Config, Category = "Boss")
	float BossFireInterval = 0.1f;

	/**
	 *  발사당 탄 수(균등 링) — 오픈루프 고정이라 패턴이 매 발사 균일하다.
	 *  동시 탄수는 발수 × 수명/주기로 자연 결정 (ini 기본 48 × 15/0.15 = 4,800).
	 *  CVar re.Bullets.Count가 0 이상이면 측정용 클로즈드루프가 대신 돈다.
	 */
	UPROPERTY(EditAnywhere, Config, Category = "Boss")
	int32 BulletsPerShot = 16;

	/**
	 *  보스 탄환 1발이 주는 데미지. PlayerMaxHealth 100 기준 10발 사망.
	 *
	 *  전에는 REBulletHitProcessor.cpp 의 익명 네임스페이스 상수였다 — 플레이어 HP,
	 *  공격 데미지, 보스 HP 가 전부 여기 있는데 **탄 데미지만 cpp 상수**라 밸런스 축
	 *  하나가 리빌드를 요구했다 (#141).
	 *  곡사탄은 별개다: 패턴마다 발수가 달라 발당 데미지를 같이 조절해야 하므로
	 *  REBossPatternTable 쪽 ArtilleryDamage / MicroDamage 가 쥔다.
	 */
	UPROPERTY(EditAnywhere, Config, Category = "Boss")
	float BulletDamage = 10.f;
};
