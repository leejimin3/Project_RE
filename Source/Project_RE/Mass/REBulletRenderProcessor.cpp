// Copyright Epic Games, Inc. All Rights Reserved.

#include "REBulletRenderProcessor.h"
#include "REBulletFragments.h"
#include "REBulletGeometry.h"                              // 탄환 스케일 단일 출처 (#141)
#include "REBulletRenderSubsystem.h"
#include "REBulletPatternGenerator.h"   // BulletLifetimeSec() — 스폰 팝 나이 계산 (#97)
#include "MassExecutionContext.h"
#include "Mass/EntityFragments.h"  // FTransformFragment
#include "Components/InstancedStaticMeshComponent.h"
#include "Engine/World.h"
#include "ProfilingDebugging/CsvProfiler.h"
#include "Project_RE.h"                              // LogRE / LogREBullet / LogRENet

CSV_DECLARE_CATEGORY_EXTERN(REBullet);  // 정의는 REBulletSimProcessor.cpp

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
			B.SetScale3D(FVector(REBulletGeometry::BulletScale));  // 탄환 크기 통일
			Xf.Add(B);

			// Lifetime 은 잔여시간(REBulletSimProcessor 가 Dt 만큼 감소) → 나이 = 총수명 - 잔여
			const float Age = TotalLife - S[i].Lifetime;
			Cd.Add(FMath::Clamp(Age / UREBulletRenderSubsystem::SpawnPopSec, 0.f, 1.f));   // [0] 스폰 팝
			Cd.Add(S[i].ColorSel);                                // [1] 색 선택(스폰 시 고정)
		}
	});

	// 2) i번째 인스턴스 = i번째 live 탄환. 배치 API 로 한 번에 넘긴다 —
	// 인스턴스당 개별 호출은 실측(40,000발) BulletRender 7.22 ms 로 GT의 52%였다 (#95).
	const int32 M = Xf.Num();
	UREBulletRenderSubsystem::SyncInstances(ISM, Xf);
	if (M > 0)
	{
		ISM->SetCustomData(0, M - 1, Cd, /*bMarkRenderStateDirty=*/true);
	}

	// 프로브: 인스턴스 수 == live 탄환 수 추종 확인 (매 30틱 1회, 로그 과다 방지).
	static int32 ProbeTick = 0;
	if (((ProbeTick++) % 30) == 0)
	{
		UE_LOG(LogREBullet, Log, TEXT("[RE] RenderProbe: live=%d ISM.Count=%d"), M, ISM->GetInstanceCount());
	}
}
