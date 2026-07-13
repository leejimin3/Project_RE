// Copyright Epic Games, Inc. All Rights Reserved.

#include "REGE_DashCooldown.h"
#include "REGameplayTags.h"

UREGE_DashCooldown::UREGE_DashCooldown()
{
	// 지속형(HasDuration) — 2.0초 후 자동 소멸.
	DurationPolicy = EGameplayEffectDurationType::HasDuration;
	DurationMagnitude = FGameplayEffectModifierMagnitude(FScalableFloat(2.0f));

	// 이 GE가 소유하는 태그 = Cooldown.Dash. GAS가 GetCooldownTags로 읽어 재활성 차단.
	// CDO 생성 시점엔 CombinedTags가 자동 재계산되지 않으므로 Added/CombinedTags 둘 다 세팅.
	InheritableOwnedTagsContainer.Added.AddTag(RETag_Cooldown_Dash);
	InheritableOwnedTagsContainer.CombinedTags.AddTag(RETag_Cooldown_Dash);
}
