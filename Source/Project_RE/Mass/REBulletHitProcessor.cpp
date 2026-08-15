// Copyright Epic Games, Inc. All Rights Reserved.

#include "REBulletHitProcessor.h"
#include "REBulletFragments.h"
#include "REBulletSimProcessor.h"
#include "MassExecutionContext.h"
#include "Mass/EntityFragments.h"  // FTransformFragment
#include "Core/RECharacterBase.h"
#include "Engine/DamageEvents.h"
#include "Engine/World.h"
#include "ProfilingDebugging/CsvProfiler.h"
#include "REHitTargets.h"
#include "REExplosionFx.h"   // 피격 폭발 (#98)

CSV_DECLARE_CATEGORY_EXTERN(REBullet);  // 정의는 REBulletSimProcessor.cpp

namespace
{
	/**
	 *  히트 반경(cm) — 탄환 시각 반경 25(BulletScale 0.5 × Sphere 반경 50) + 플레이어 캡슐 반경 ~35.
	 *  BulletScale 을 바꾸면 여기도 같이 바꿔야 한다. 안 그러면 눈에 안 닿았는데 맞거나
	 *  닿았는데 안 맞아 공정성이 깨진다 (#97).
	 */
	constexpr float HitRadius = 60.f;
	/** 탄환 1발 데미지 — 100 HP 기준 10발 사망. */
	constexpr float BulletDamage = 10.f;
}

UREBulletHitProcessor::UREBulletHitProcessor()
	: EntityQuery(*this)
{
	// 판정을 클라에도 연다 — 클라가 자기 시뮬로 탄을 지우고 폭발을 띄운다 (#98).
	// 데미지는 아래 HasAuthority 가드로 서버에만 남는다. 복제 RPC 는 쓰지 않는다:
	// 클라는 이미 ServerTime 으로 탄 위치를 자체 계산하므로(#84) 판정 근거가 있다.
	ExecutionFlags = (int32)EProcessorExecutionFlags::AllNetModes;

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
	TRACE_CPUPROFILER_EVENT_SCOPE(RE_BulletHit);
	CSV_SCOPED_TIMING_STAT(REBullet, BulletHit);

	// 살아있고 대쉬 중이 아닌 플레이어 전원 (#86). 대상이 없으면 탄을 순회할 이유가 없다.
	UWorld* World = EntityManager.GetWorld();
	TArray<FREHitTarget> Targets;
	GatherHitTargets(World, Targets);
	if (Targets.IsEmpty())
	{
		return;
	}

	EntityQuery.ForEachEntityChunk(Context, [&](FMassExecutionContext& Ctx)
	{
		const int32 Num = Ctx.GetNumEntities();
		const TConstArrayView<FTransformFragment> Transforms = Ctx.GetFragmentView<FTransformFragment>();

		for (int32 i = 0; i < Num; ++i)
		{
			// 탑다운 — XY 평면 거리만 비교 (탄환 Z와 캡슐 중심 Z 불일치 함정 회피)
			const FVector BulletLoc = Transforms[i].GetTransform().GetLocation();
			for (const FREHitTarget& T : Targets)
			{
				if (FVector::DistSquaredXY(BulletLoc, T.Location) <= HitRadius * HitRadius)
				{
					// 데미지는 서버 권위. 클라는 파괴와 폭발만 한다 (#98).
					if (T.Player->HasAuthority())
					{
						const float Applied = T.Player->TakeDamage(BulletDamage, FDamageEvent(), nullptr, nullptr);
						// netmode 를 함께 찍는다 — 클라 프로세스는 접속 전 로컬 월드를 잠깐 돌리므로
						// 로그에 데미지가 보인다고 곧 "클라가 데미지를 줬다"가 아니다. 판정에 필요하다 (#98).
						UE_LOG(LogTemp, Log, TEXT("[RE] BulletHit: Applied=%.0f netmode=%d"),
							Applied, (int32)World->GetNetMode());
					}
					REExplosionFx::SpawnBulletExplosion(World, BulletLoc);
					Ctx.Defer().DestroyEntity(Ctx.GetEntity(i));
					break;   // 투사체 하나는 한 명만 — 몸으로 막는 탱 플레이가 성립한다
				}
			}
		}
	});
}
