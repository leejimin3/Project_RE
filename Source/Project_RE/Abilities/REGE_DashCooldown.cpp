// Copyright Epic Games, Inc. All Rights Reserved.

#include "REGE_DashCooldown.h"
#include "REGameplayTags.h"
#include "GameplayEffectComponents/TargetTagsGameplayEffectComponent.h"

UREGE_DashCooldown::UREGE_DashCooldown(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// 지속형(HasDuration) — 2.0초 후 자동 소멸.
	DurationPolicy = EGameplayEffectDurationType::HasDuration;
	DurationMagnitude = FGameplayEffectModifierMagnitude(FScalableFloat(2.0f));

	// 이 GE가 대상에 부여하는 태그 = Cooldown.Dash. GAS가 GetCooldownTags로 읽어 재활성 차단.
	//
	// InheritableOwnedTagsContainer 는 5.8 에서 폐기됐다. 그대로 두면 GAS 가
	// "CooldownGameplayEffectClass 'REGE_DashCooldown' grants no tags" 경고를 내고
	// **쿨다운 태그를 빈 것으로 취급한다** — GetCooldownTimeRemaining() 이 항상 0 을 돌려줘
	// HUD 의 쿨다운 표시(#100)가 죽어 있었다. 컴파일 경고도 "다음 릴리즈에서 컴파일 불가"다.
	//
	// CDO 생성 시점엔 CombinedTags 가 자동 재계산되지 않으므로 Added/CombinedTags 둘 다 세팅한다.
	UTargetTagsGameplayEffectComponent* TagsComponent =
		ObjectInitializer.CreateDefaultSubobject<UTargetTagsGameplayEffectComponent>(this, TEXT("TargetTagsComponent"));
	FInheritedTagContainer TagChanges;
	TagChanges.Added.AddTag(RETag_Cooldown_Dash);
	TagChanges.CombinedTags.AddTag(RETag_Cooldown_Dash);
	TagsComponent->SetAndApplyTargetTagChanges(TagChanges);
	GEComponents.Add(TagsComponent);
}
