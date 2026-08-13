// Copyright Epic Games, Inc. All Rights Reserved.

#include "REHitTargets.h"
#include "Core/RECharacterBase.h"
#include "Abilities/REGameplayTags.h"
#include "AbilitySystemComponent.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"

void GatherHitTargets(const UWorld* World, TArray<FREHitTarget>& Out)
{
	Out.Reset();
	if (!World)
	{
		return;
	}

	for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
	{
		if (!It->IsValid())
		{
			continue;   // 접속 종료 중인 PC가 섞일 수 있다
		}
		ARECharacterBase* Player = Cast<ARECharacterBase>(It->Get()->GetPawn());
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
