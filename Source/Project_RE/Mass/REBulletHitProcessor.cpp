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
#include "REBulletGeometry.h"                              // 히트 반경 단일 출처 (#141)
#include "Core/REStatsSettings.h"                          // 탄 데미지 ini 이관 (#141)
#include "REExplosionFx.h"   // 피격 폭발 (#98)
#include "Project_RE.h"                              // LogRE / LogREBullet / LogRENet

CSV_DECLARE_CATEGORY_EXTERN(REBullet);  // 정의는 REBulletSimProcessor.cpp

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

	// 살아있는 플레이어 전원 (#86). 대쉬 중이면 bInvulnerable 로 들어온다 (#102).
	// 대상이 없으면 탄을 순회할 이유가 없다.
	UWorld* World = EntityManager.GetWorld();
	TArray<FREHitTarget> Targets;
	GatherHitTargets(World, Targets);

	// ini 조회는 진입부에서 1회. 엔티티 루프 안에서 GetDefault 를 부르지 않는다 —
	// 볼리당 수천 발이라 CDO 조회가 그대로 프레임 비용이 된다.
	const float BulletDamage = GetDefault<UREStatsSettings>()->BulletDamage;
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
				if (FVector::DistSquaredXY(BulletLoc, T.Location)
					<= REBulletGeometry::HitRadius * REBulletGeometry::HitRadius)
				{
					// 데미지는 서버 권위. 클라는 파괴와 폭발만 한다 (#98).
					// 대쉬 무적이면 데미지만 건너뛴다 — 소멸과 폭발은 그대로 (#102).
					if (T.bInvulnerable)
					{
						// 이 경로는 로그가 없으면 관측 불가다 — 데미지를 안 주므로 아래 Applied 로그가
						// 안 찍히고, "탄이 그냥 사라진 것"과 구별되지 않는다. 대쉬 한 번에 다수라 Verbose
						// (검증 시 -LogCmds="LogTemp Verbose").
						UE_LOG(LogREBullet, Verbose, TEXT("[RE] BulletHit: dash-destroy (무적, 데미지 없음)"));
					}
					else if (T.Player->HasAuthority())
					{
						const float Applied = T.Player->TakeDamage(BulletDamage, FDamageEvent(), nullptr, nullptr);
						// netmode 를 함께 찍는다 — 클라 프로세스는 접속 전 로컬 월드를 잠깐 돌리므로
						// 로그에 데미지가 보인다고 곧 "클라가 데미지를 줬다"가 아니다. 판정에 필요하다 (#98).
						UE_LOG(LogREBullet, Log, TEXT("[RE] BulletHit: Applied=%.0f netmode=%d"),
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
