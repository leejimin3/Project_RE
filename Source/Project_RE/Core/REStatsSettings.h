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
 *  탄환 수(re.Bullets.Count)·스폰 게인은 프로파일링 즉석 노브라 CVar 유지 — 여기 없음.
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
};
