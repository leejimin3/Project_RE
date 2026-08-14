// Copyright Epic Games, Inc. All Rights Reserved.

#include "REBulletRenderProcessor.h"
#include "REBulletFragments.h"
#include "REBulletRenderSubsystem.h"
#include "REBulletPatternGenerator.h"   // BulletLifetimeSec() — 스폰 팝 나이 계산 (#97)
#include "MassExecutionContext.h"
#include "Mass/EntityFragments.h"  // FTransformFragment
#include "Components/InstancedStaticMeshComponent.h"
#include "Engine/World.h"
#include "ProfilingDebugging/CsvProfiler.h"

CSV_DECLARE_CATEGORY_EXTERN(REBullet);  // 정의는 REBulletSimProcessor.cpp

namespace
{
	/**
	 *  탄환 인스턴스 스케일 — 엔진 Sphere(지름 100cm)를 지름 50cm로. #17: 0.2는 카메라 거리서 sub-pixel이라 0.5로 상향.
	 *
	 *  탄이 서로 겹친다: 간격 = BulletSpeed(200) × BossFireInterval(0.15) = 30uu < 지름 50uu.
	 *  겹침 자체는 의도적으로 허용한다 — 축소해서 틈을 만들면(0.2 시도) 탄이 너무 작아
	 *  탄막의 압도적인 인상이 사라진다. 대신 인접 탄을 **다른 색으로 교차**시켜 가른다(#97).
	 *  바꾸면 REBulletHitProcessor 의 HitRadius 와 Baseline/REBulletActor 의
	 *  ActorBulletScale 도 같이 맞춰야 한다.
	 */
	constexpr float BulletScale = 0.5f;
}

UREBulletRenderProcessor::UREBulletRenderProcessor()
	: EntityQuery(*this)
{
	// 5: 데디서버(Server) skip, 싱글/클라만 렌더
	ExecutionFlags = (int32)(EProcessorExecutionFlags::Standalone | EProcessorExecutionFlags::Client);

	// ISM(씬 컴포넌트) 변형은 게임 스레드 전용 — AddInstance가 물리 바디를 만들어
	// 워커 스레드에서 실행 시 BodyInstance 어서션 크래시. Execute를 GT에 고정.
	bRequiresGameThreadExecution = true;
}

void UREBulletRenderProcessor::ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager)
{
	EntityQuery.AddRequirement<FTransformFragment>(EMassFragmentAccess::ReadOnly);  // 위치 읽기
	EntityQuery.AddRequirement<FBulletSimFragment>(EMassFragmentAccess::ReadOnly);  // Lifetime — 스폰 팝 나이 (#97)
	EntityQuery.AddTagRequirement<FBulletTag>(EMassFragmentPresence::All);          // 탄환만 선별
}

void UREBulletRenderProcessor::Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(RE_BulletRender);
	CSV_SCOPED_TIMING_STAT(REBullet, BulletRender);

	UWorld* World = EntityManager.GetWorld();
	UREBulletRenderSubsystem* RS = World ? World->GetSubsystem<UREBulletRenderSubsystem>() : nullptr;
	UInstancedStaticMeshComponent* ISM = RS ? RS->GetISM() : nullptr;
	if (!ISM)
	{
		return;  // 데디서버 등 ISM 없으면 no-op
	}

	// 스폰 팝 지속시간(s). 태어난 직후만 밝기가 솟았다가 정상으로 붙는다.
	// 수명 페이드는 넣지 않는다 — 죽기 직전 탄이 흐려지면 여전히 치명적인데 사라지는 중으로
	// 오독되고, 탄막에서 히트박스 가독성은 공정성 문제다 (#97).
	constexpr float PopDuration = 0.1f;
	const float TotalLife = REBulletPattern::BulletLifetimeSec();

	// 1) live 탄환 트랜스폼 + 커스텀데이터 수집 (청크를 가로질러 누적 → 전역 인스턴스 인덱스 연속).
	// 커스텀데이터는 인스턴스당 연속으로 인터리브된다: [0]=스폰팝, [1]=색선택.
	TArray<FTransform> Xf;
	TArray<float> Cd;
	EntityQuery.ForEachEntityChunk(Context, [&Xf, &Cd, TotalLife](FMassExecutionContext& Ctx)
	{
		const int32 Num = Ctx.GetNumEntities();
		const TConstArrayView<FTransformFragment> T = Ctx.GetFragmentView<FTransformFragment>();
		const TConstArrayView<FBulletSimFragment> S = Ctx.GetFragmentView<FBulletSimFragment>();
		for (int32 i = 0; i < Num; ++i)
		{
			FTransform B = T[i].GetTransform();
			B.SetScale3D(FVector(BulletScale));  // 탄환 크기 통일
			Xf.Add(B);

			// Lifetime 은 잔여시간(REBulletSimProcessor 가 Dt 만큼 감소) → 나이 = 총수명 - 잔여
			const float Age = TotalLife - S[i].Lifetime;
			Cd.Add(FMath::Clamp(Age / PopDuration, 0.f, 1.f));   // [0] 스폰 팝
			Cd.Add(S[i].ColorSel);                                // [1] 색 선택(스폰 시 고정)
		}
	});

	// 2) 인스턴스 수를 M에 맞춤 (꼬리에서 add/remove → 타 인덱스 불변, swap 없음).
	const int32 M = Xf.Num();
	int32 Count = ISM->GetInstanceCount();
	while (Count < M) { ISM->AddInstance(FTransform::Identity, /*bWorldSpace=*/true); ++Count; }
	while (Count > M) { ISM->RemoveInstance(Count - 1);                               --Count; }

	// 3) i번째 인스턴스 = i번째 live 탄환. 배열째 한 번에 넘긴다.
	// 인스턴스당 개별 UpdateInstanceTransform 호출은 탄환 수에 비례해 게임 스레드를 먹었다 —
	// 실측(40,000발) BulletRender 7.22 ms 로 GT의 52%. Xf 는 이미 만들어져 있으므로 배치 API가 그대로 받는다 (#95).
	if (M > 0)
	{
		// 커스텀데이터를 먼저 쓰고 트랜스폼을 나중에 쓴다 — dirty 마크는 트랜스폼 호출이 담당한다.
		// 위 while 루프가 인스턴스 수를 이미 M 으로 맞췄으므로 인덱스 범위가 유효하다.
		ISM->SetCustomData(0, M - 1, Cd, /*bMarkRenderStateDirty=*/false);
		ISM->BatchUpdateInstancesTransforms(0, Xf, /*bWorldSpace=*/true,
			/*bMarkRenderStateDirty=*/true, /*bTeleport=*/true);
	}

	// 프로브: 인스턴스 수 == live 탄환 수 추종 확인 (매 30틱 1회, 로그 과다 방지).
	static int32 ProbeTick = 0;
	if (((ProbeTick++) % 30) == 0)
	{
		UE_LOG(LogTemp, Log, TEXT("[RE] RenderProbe: live=%d ISM.Count=%d"), M, ISM->GetInstanceCount());
	}
}
