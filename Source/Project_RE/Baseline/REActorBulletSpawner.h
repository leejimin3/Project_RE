// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Subsystems/WorldSubsystem.h"
#include "REActorBulletSpawner.generated.h"

class UMaterialInstanceDynamic;

/**
 *  액터 탄환 스포너 (#45 비교군). CVar re.ActorBullets.Count 로만 켜진다 — 기본 0 = 평상시 영향 0.
 *
 *  자립 구동: OnWorldBeginPlay에서 자기 타이머(0.1s)를 건다. GameMode에 의존하지 않는다
 *  (REBulletRenderSubsystem과 같은 패턴). Mass 데모도 0.1s 타이머라 발사 메커니즘까지 대칭이다.
 *
 *  Count는 "동시 유지 목표 탄환 수"다. 발사당 탄 수는 Count / (Lifetime/Interval) 로 역산한다.
 */
UCLASS()
class UREActorBulletSpawner : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;

private:
	/** 0.1초마다 호출. CVar가 0이면 즉시 return (no-op). */
	void Fire();

	/** 전 탄환이 공유하는 빨강 MID. 액터마다 MID를 만들면 액터 경로에 불공정한 추가 비용이 붙는다. */
	UPROPERTY()
	TObjectPtr<UMaterialInstanceDynamic> SharedMID = nullptr;

	FTimerHandle FireTimer;

	/** Spiral 시작각 누적 — Boss의 SpiralRotationStepDeg(15°)와 동일하게 링을 회전시킨다. */
	float BaseAngleDeg = 0.f;

	/** 발사당 탄 수 소수부 누산 (Count/30 이 정수가 아닐 때 반올림 오차 누적 방지). */
	float PerShotAccum = 0.f;

	/** 프로브 로그 주기 카운터 (10회 = 1초마다 1줄). */
	int32 FireCount = 0;
};
