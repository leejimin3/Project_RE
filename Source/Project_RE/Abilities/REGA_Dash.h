// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Abilities/GameplayAbility.h"
#include "REGA_Dash.generated.h"

/**
 *  스페이스 대쉬 어빌리티 (GAS, 목표지점형 RootMotion, 서버권위).
 *  아바타(ARECharacterBase)의 PendingDashDir 방향으로 고정거리(678uu / 0.2s) 이동.
 *  MoveTo 를 쓰는 이유는 긴 프레임에서의 오버슈트 방지다 — REGA_Dash.cpp 참조.
 *  쿨다운은 UREGE_DashCooldown(2.0s) 커밋. 활성 동안 State.Dashing 태그(#27이 읽음).
 *
 *  NetExecutionPolicy = ServerOnly (클라 예측 미사용).
 *  TODO M4: 데디 원격클라 지연 측정 후 LocalPredicted 전환. 지금은 리슨서버라 지연 0 → 예측 이득 0.
 */
UCLASS()
class UREGA_Dash : public UGameplayAbility
{
	GENERATED_BODY()

public:
	UREGA_Dash();

	virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle,
	                             const FGameplayAbilityActorInfo* ActorInfo,
	                             const FGameplayAbilityActivationInfo ActivationInfo,
	                             const FGameplayEventData* TriggerEventData) override;

private:
	/** RootMotion 태스크 완료 콜백 → EndAbility. */
	UFUNCTION()
	void OnDashFinished();

	/** 대쉬 속도(uu/s). 거리≈Strength*Duration. 프로브 실측으로 600uu에 맞춰 조정. */
	// 대쉬 이동거리(uu). 기존 ConstantForce 의 3390uu/s x 0.2s = 678uu 를 그대로 유지한다.
	float DashDistance = 678.f;

	/** 대쉬 지속(s). */
	float DashDuration = 0.2f;
};
