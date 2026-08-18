// Copyright Epic Games, Inc. All Rights Reserved.

#include "REArcFxProcessor.h"
#include "REArcSimProcessor.h"
#include "REBulletFragments.h"
#include "REExplosionFx.h"
#include "MassExecutionContext.h"
#include "Mass/EntityFragments.h"   // FTransformFragment
#include "Engine/World.h"

namespace
{
	/** 곡사탄 폭발 Z 오프셋(cm) — 착지점(Z 2)이 바닥 윗면(Z 40)보다 아래라 그대로 터뜨리면
	 *  폭발 중심이 바닥에 묻힌다. 마커의 MarkerZOffset(55) 과 같은 근거이므로 같은 값을 쓴다.
	 *  이름을 마커 쪽과 다르게 둔다: 익명 네임스페이스 동명 상수가 유니티 빌드에서 충돌한
	 *  전례가 있다(REArcRenderProcessor.cpp 의 ArcPopDuration 주석 참조). */
	constexpr float ExplosionZOffset = 55.f;
}

UREArcFxProcessor::UREArcFxProcessor()
	: EntityQuery(*this)
{
	// 클라·싱글 전용 — 데디서버는 렌더가 없다.
	ExecutionFlags = (int32)(EProcessorExecutionFlags::Standalone | EProcessorExecutionFlags::Client);

	// Niagara 스폰은 게임 스레드 전용. 워커에서 부르면 크래시한다.
	bRequiresGameThreadExecution = true;

	// 시뮬 "뒤"에 돈다. 먼저 돌면 폭발이 한 번도 안 뜬다 — Elapsed 를 증가시키는 것이 시뮬이라,
	// 착지 프레임에 먼저 돌면 아직 갱신 전 Elapsed(< FlightTime)를 보고, 다음 프레임에는
	// 엔티티가 이미 없다. 뒤에 돌아도 안전하다: 시뮬의 파괴는 Defer() 라 이 페이즈 끝에야
	// 반영되므로 트랜스폼을 그대로 읽을 수 있다(REArcHitProcessor 가 같은 방식으로 동작한다).
	// 클래스에서 이름을 얻는다: 문자열 리터럴은 오타가 나도 조용히 무시된다.
	ExecutionOrder.ExecuteAfter.Add(UREArcSimProcessor::StaticClass()->GetFName());
}

void UREArcFxProcessor::ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager)
{
	EntityQuery.AddRequirement<FTransformFragment>(EMassFragmentAccess::ReadOnly);
	EntityQuery.AddRequirement<FArcBulletFragment>(EMassFragmentAccess::ReadOnly);
	EntityQuery.AddTagRequirement<FArcBulletTag>(EMassFragmentPresence::All);
}

void UREArcFxProcessor::Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(RE_ArcFx);

	UWorld* World = EntityManager.GetWorld();
	if (!World)
	{
		return;
	}

	EntityQuery.ForEachEntityChunk(Context, [World](FMassExecutionContext& Ctx)
	{
		const int32 Num = Ctx.GetNumEntities();
		const TConstArrayView<FTransformFragment> T = Ctx.GetFragmentView<FTransformFragment>();
		const TConstArrayView<FArcBulletFragment> A = Ctx.GetFragmentView<FArcBulletFragment>();
		for (int32 i = 0; i < Num; ++i)
		{
			// REArcSimProcessor 와 같은 착지 조건. 시뮬이 이미 Elapsed 를 갱신했고, 파괴는
			// 지연되므로 여기서 착지한 탄이 정확히 한 번 관측된다.
			// 중복 스폰 가드(bFxSpawned)는 두지 않는다. 착지한 탄을 두 번 볼 경로가 없기 때문이다:
			// 같은 페이즈에서 ArcSim 이 곧바로 Defer().DestroyEntity 를 걸고, Mass 는 지연 명령을
			// 처리 페이즈 끝에 플러시한다 → 다음 프레임에는 엔티티가 이미 없다.
			if (A[i].Elapsed >= A[i].FlightTime)
			{
				// 착지 평면은 보스 캡슐 바닥 근사(BossLoc.Z - 88 = Z 2)라 Main 레벨 바닥
				// 윗면(Z 40)보다 아래다. 그대로 터뜨리면 폭발 중심이 바닥 속에 묻혀
				// 위로 삐져나온 부분만 보인다 - "중앙이 비었다"는 화면이 이것이다 (#119).
				// 마커가 MarkerZOffset 55 로 올라간 것과 같은 이유다 (#122).
				FVector Loc = T[i].GetTransform().GetLocation();
				Loc.Z += ExplosionZOffset;
				REExplosionFx::SpawnBulletExplosion(World, Loc);
			}
		}
	});
}
