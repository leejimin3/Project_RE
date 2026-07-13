// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayEffect.h"
#include "REGE_DashCooldown.generated.h"

/**
 *  대쉬 쿨다운 GameplayEffect. 2.0초간 Cooldown.Dash 태그를 부여한다.
 *  REGA_Dash가 CooldownGameplayEffectClass로 참조 → CommitAbility 시 적용.
 *  코드로 구성(uasset 없음).
 */
UCLASS()
class UREGE_DashCooldown : public UGameplayEffect
{
	GENERATED_BODY()

public:
	UREGE_DashCooldown();
};
