// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Abilities/GameplayAbility.h"
#include "REGA_Dash.generated.h"

class UAnimSequence;

/**
 *  스페이스 대쉬 어빌리티 (GAS, 시간형 RootMotion, 서버권위).
 *  아바타(ARECharacterBase)의 PendingDashDir 방향으로 고정거리(600uu / 0.2s) 이동.
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

	/** 대쉬 모션 (AnimSequence — ABP DefaultSlot에 다이나믹 몽타주로 재생). 코스메틱. */
	UPROPERTY()
	TObjectPtr<UAnimSequence> DashAnim;

	/** 대쉬 속도(uu/s). 거리≈Strength*Duration. 프로브 실측으로 600uu에 맞춰 조정. */
	float DashStrength = 3390.f;

	/** 대쉬 지속(s). */
	float DashDuration = 0.2f;
};
