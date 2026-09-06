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
	/**
	 *  ObjectInitializer 를 받는다 — 태그 부여를 UTargetTagsGameplayEffectComponent 로 하는데,
	 *  GE 컴포넌트는 CDO 생성자에서 CreateDefaultSubobject 로 만들어야 한다.
	 *  FindOrAddComponent 는 내부에서 NewObject 를 불러 생성자에서 쓰면 죽는다
	 *  ("NewObject with empty name can't be used to create default subobjects" — 실측 크래시).
	 */
	UREGE_DashCooldown(const FObjectInitializer& ObjectInitializer);
};
