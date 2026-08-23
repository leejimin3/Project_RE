// Copyright Epic Games, Inc. All Rights Reserved.

#include "REHitTargets.h"
#include "Core/RECharacterBase.h"
#include "Abilities/REGameplayTags.h"
#include "AbilitySystemComponent.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "EngineUtils.h"   // TActorIterator — 클라에서도 전원 탐지 (#98)
#include "HAL/IConsoleManager.h"

namespace
{
	/**
	 *  측정 전용: 판정 대상을 N명으로 부풀린다.
	 *
	 *  인원에 비례하는 유일한 항이 히트 판정의 내부 루프(탄 × 인원)인데, 그걸 실제로 재려면
	 *  플레이어를 여럿 붙여야 하고 그러면 한 머신에서 UE 프로세스가 여러 개 돌아 **CPU 경합이
	 *  측정을 덮는다**(실측: 같은 탄막을 그리는 두 클라가 12ms vs 25ms 로 갈렸다).
	 *  프로세스 하나로 그 항만 격리해 재려고 둔다.
	 *
	 *  1(기본)이면 아무것도 하지 않는 죽은 경로다.
	 */
	TAutoConsoleVariable<int32> CVarFakeHitTargets(
		TEXT("re.Debug.FakeHitTargets"),
		1,
		TEXT("측정용: 판정 대상을 N명으로 부풀린다(1=기본, 무동작). 인원 비례 비용 격리용."),
		ECVF_Cheat);

	/**
	 *  복제본을 원본에서 벌려 놓는 반경(uu).
	 *  같은 자리에 겹치면 한 탄이 N번 맞아 폭발이 N배로 튄다 — 실제 2인은 서로 떨어져 있어
	 *  한 탄이 한 명에게만 맞으므로, 겹쳐 두면 있지도 않은 비용을 재게 된다.
	 */
	constexpr float FakeTargetSpreadRadius = 400.f;
}

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
		// 대쉬 무적은 본인만 (#25/#39). 목록에서 빼지 않고 표시만 한다 (#102) —
		// 빼면 거리 비교 자체가 없어 탄환도 소멸하지 않는다. 데미지 면제는 소비자가 한다.
		const UAbilitySystemComponent* ASC = Player->GetAbilitySystemComponent();
		const bool bDashing = ASC && ASC->HasMatchingGameplayTag(RETag_State_Dashing);
		if (bDashing)
		{
			// 대쉬 중엔 매 프레임 찍히므로 Verbose — 검증 시 -LogCmds="LogTemp Verbose"로 관측.
			UE_LOG(LogTemp, Verbose, TEXT("[RE] HitTargets: invulnerable (State.Dashing)"));
		}
		Out.Add(FREHitTarget{ Player, Player->GetActorLocation(), bDashing });
	}

	// 측정용 부풀리기. 복제본은 bInvulnerable 로 둔다 — 탄 소멸과 폭발(= 재려는 비용)은
	// 그대로 발생시키되 데미지는 원본 한 명분만 남긴다. 같은 Player 포인터라 안 그러면
	// 한 번 맞을 때 N배로 깎인다.
	const int32 Fake = CVarFakeHitTargets.GetValueOnGameThread();
	if (Fake > 1 && Out.Num() > 0)
	{
		const int32 Base = Out.Num();
		Out.Reserve(Base * Fake);
		for (int32 c = 1; c < Fake; ++c)
		{
			const float Ang = 2.f * PI * c / Fake;
			const FVector Off(FakeTargetSpreadRadius * FMath::Cos(Ang),
			                  FakeTargetSpreadRadius * FMath::Sin(Ang), 0.f);
			for (int32 i = 0; i < Base; ++i)
			{
				FREHitTarget T = Out[i];   // 값 복사 — 아래 Add 가 배열을 재할당할 수 있다
				T.Location += Off;
				T.bInvulnerable = true;
				Out.Add(T);
			}
		}
	}
}
