// Copyright Epic Games, Inc. All Rights Reserved.

#include "REHitTargets.h"
#include "Core/RECharacterBase.h"
#include "Abilities/REGameplayTags.h"
#include "AbilitySystemComponent.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "EngineUtils.h"   // TActorIterator — 클라에서도 전원 탐지 (#98)

void GatherHitTargets(const UWorld* World, TArray<FREHitTarget>& Out)
{
	Out.Reset();
	if (!World)
	{
		return;
	}

	// 컨트롤러가 아니라 캐릭터를 순회한다 — 클라에서 GetPlayerControllerIterator 는
	// 로컬 컨트롤러 하나만 돌려주므로, 그대로 두면 클라 판정(#98)이 동료 피격을 놓친다.
	// 서버에서는 결과가 같다(플레이어 폰은 전부 ARECharacterBase).
	for (TActorIterator<ARECharacterBase> It(World); It; ++It)
	{
		ARECharacterBase* Player = *It;
		if (!Player || !Player->IsAlive())
		{
			// 사망자 제외 — 시체가 탄을 흡수해 뒤에 선 생존자를 가리지 않게 한다.
			continue;
		}
		// 대쉬 무적은 본인만 (#25/#39). 목록에서 빠지면 거리 비교 자체가 없으므로
		// "판정만 스킵하고 탄환은 파괴하지 않는다"는 기존 의미가 그대로 유지된다.
		const UAbilitySystemComponent* ASC = Player->GetAbilitySystemComponent();
		if (ASC && ASC->HasMatchingGameplayTag(RETag_State_Dashing))
		{
			// 대쉬 중엔 매 프레임 찍히므로 Verbose — 검증 시 -LogCmds="LogTemp Verbose"로 관측.
			// 프로세서에 있던 같은 진단을 여기로 옮긴 것이다(이제 스킵 판단이 여기서 난다).
			UE_LOG(LogTemp, Verbose, TEXT("[RE] HitTargets: skipped (State.Dashing)"));
			continue;
		}
		Out.Add(FREHitTarget{ Player, Player->GetActorLocation() });
	}
}
