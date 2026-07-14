// Copyright Epic Games, Inc. All Rights Reserved.

#include "REBulletHitProcessor.h"
#include "REBulletFragments.h"
#include "REBulletSimProcessor.h"
#include "MassExecutionContext.h"
#include "Mass/EntityFragments.h"  // FTransformFragment
#include "Core/RECharacterBase.h"
#include "Abilities/REGameplayTags.h"
#include "AbilitySystemComponent.h"
#include "Engine/DamageEvents.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"

namespace
{
	/** 히트 반경(cm) — 탄환 시각 반경 25(BulletScale 0.5 × Sphere 50) + 플레이어 캡슐 반경 ~35. */
	constexpr float HitRadius = 60.f;
	/** 탄환 1발 데미지 — 100 HP 기준 10발 사망. */
	constexpr float BulletDamage = 10.f;
}

UREBulletHitProcessor::UREBulletHitProcessor()
	: EntityQuery(*this)
{
	// 서버 권위 판정만 — 싱글(Standalone) + 데디서버(Server). 클라 실행 없음.
	ExecutionFlags = (int32)(EProcessorExecutionFlags::Standalone | EProcessorExecutionFlags::Server);

	// TakeDamage는 액터 호출 — 게임 스레드 전용.
	bRequiresGameThreadExecution = true;

	// 이동 후 판정 — Sim이 위치를 전진시킨 뒤 같은 프레임에 히트 체크.
	ExecutionOrder.ExecuteAfter.Add(UREBulletSimProcessor::StaticClass()->GetFName());
}

void UREBulletHitProcessor::ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager)
{
	EntityQuery.AddRequirement<FTransformFragment>(EMassFragmentAccess::ReadOnly);
	EntityQuery.AddTagRequirement<FBulletTag>(EMassFragmentPresence::All);
}

void UREBulletHitProcessor::Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context)
{
	UWorld* World = EntityManager.GetWorld();
	APawn* Pawn = World ? UGameplayStatics::GetPlayerPawn(World, 0) : nullptr;
	ARECharacterBase* Player = Cast<ARECharacterBase>(Pawn);
	if (!Player)
	{
		return;  // 플레이어 없으면 no-op (레벨 전환 등)
	}

	// 대쉬 중 무적 — #25가 부여하는 State.Dashing 태그를 여기서 소비.
	// 판정 자체를 스킵(탄환 미파괴) — 대쉬는 탄막을 "통과"하지 "지우지" 않는다.
	const UAbilitySystemComponent* ASC = Player->GetAbilitySystemComponent();
	if (ASC && ASC->HasMatchingGameplayTag(RETag_State_Dashing))
	{
		// 대쉬 중엔 매 프레임 찍히므로 Verbose — 검증 시 -LogCmds="LogTemp Verbose"로 관측.
		UE_LOG(LogTemp, Verbose, TEXT("[RE] BulletHit: skipped (State.Dashing)"));
		return;
	}

	const FVector PlayerLoc = Player->GetActorLocation();

	EntityQuery.ForEachEntityChunk(Context, [&](FMassExecutionContext& Ctx)
	{
		const int32 Num = Ctx.GetNumEntities();
		const TConstArrayView<FTransformFragment> Transforms = Ctx.GetFragmentView<FTransformFragment>();

		for (int32 i = 0; i < Num; ++i)
		{
			// 탑다운 — XY 평면 거리만 비교 (탄환 Z와 캡슐 중심 Z 불일치 함정 회피)
			if (FVector::DistSquaredXY(Transforms[i].GetTransform().GetLocation(), PlayerLoc)
				<= HitRadius * HitRadius)
			{
				const float Applied = Player->TakeDamage(BulletDamage, FDamageEvent(), nullptr, nullptr);
				Ctx.Defer().DestroyEntity(Ctx.GetEntity(i));
				UE_LOG(LogTemp, Log, TEXT("[RE] BulletHit: Applied=%.0f"), Applied);
			}
		}
	});
}
