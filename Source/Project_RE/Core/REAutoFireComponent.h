// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Engine/TimerHandle.h"
#include "REAutoFireComponent.generated.h"

/**
 *  플레이어 자동사격 컴포넌트 (#26).
 *  서버 전용 타이머로 최근접 AREBossCharacter를 라인트레이스 조준·주기 발사.
 *  입력 없음 → RPC 없음: 조준·트레이스·데미지 전부 서버 계산 (치팅 표면 0).
 *  데미지는 AActor::TakeDamage 호출까지 — 보스 HP 차감은 #28이 override로 수신 (계약).
 */
UCLASS()
class UREAutoFireComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UREAutoFireComponent();

	/**
	 *  발사 중지 (#40 게임 종료). 자동사격은 입력이 아니라 서버 타이머 구동이라
	 *  PlayerController의 DisableInput으로는 안 멈춘다 — 명시적 정지가 필요하다.
	 */
	void StopFiring();

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	/** 발사 1회: 최근접 보스 탐색 → 라인트레이스 → 히트 시 TakeDamage. */
	void Fire();

	/** 발사 간격(s). */
	UPROPERTY(EditDefaultsOnly, Category = "AutoFire")
	float FireInterval = 0.25f;

	/** 발사당 데미지. */
	UPROPERTY(EditDefaultsOnly, Category = "AutoFire")
	float Damage = 10.f;

	FTimerHandle FireTimer;
};
