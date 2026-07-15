// Copyright Epic Games, Inc. All Rights Reserved.

#include "REBulletRenderProcessor.h"
#include "REBulletFragments.h"
#include "REBulletRenderSubsystem.h"
#include "MassExecutionContext.h"
#include "Mass/EntityFragments.h"  // FTransformFragment
#include "Components/InstancedStaticMeshComponent.h"
#include "Engine/World.h"

namespace
{
	/** 탄환 인스턴스 스케일 — 엔진 Sphere(반경 50cm)를 반경 ~25cm로 축소. #17: 0.2는 카메라 거리서 sub-pixel이라 0.5로 상향. */
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
	EntityQuery.AddTagRequirement<FBulletTag>(EMassFragmentPresence::All);          // 탄환만 선별
}

void UREBulletRenderProcessor::Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(RE_BulletRender);

	UWorld* World = EntityManager.GetWorld();
	UREBulletRenderSubsystem* RS = World ? World->GetSubsystem<UREBulletRenderSubsystem>() : nullptr;
	UInstancedStaticMeshComponent* ISM = RS ? RS->GetISM() : nullptr;
	if (!ISM)
	{
		return;  // 데디서버 등 ISM 없으면 no-op
	}

	// 1) live 탄환 트랜스폼 수집 (청크를 가로질러 누적 → 전역 인스턴스 인덱스 연속).
	TArray<FTransform> Xf;
	EntityQuery.ForEachEntityChunk(Context, [&Xf](FMassExecutionContext& Ctx)
	{
		const int32 Num = Ctx.GetNumEntities();
		const TConstArrayView<FTransformFragment> T = Ctx.GetFragmentView<FTransformFragment>();
		for (int32 i = 0; i < Num; ++i)
		{
			FTransform B = T[i].GetTransform();
			B.SetScale3D(FVector(BulletScale));  // 탄환 크기 통일
			Xf.Add(B);
		}
	});

	// 2) 인스턴스 수를 M에 맞춤 (꼬리에서 add/remove → 타 인덱스 불변, swap 없음).
	const int32 M = Xf.Num();
	int32 Count = ISM->GetInstanceCount();
	while (Count < M) { ISM->AddInstance(FTransform::Identity, /*bWorldSpace=*/true); ++Count; }
	while (Count > M) { ISM->RemoveInstance(Count - 1);                               --Count; }

	// 3) i번째 인스턴스 = i번째 live 탄환. dirty flush는 마지막 1회만.
	for (int32 i = 0; i < M; ++i)
	{
		ISM->UpdateInstanceTransform(i, Xf[i], /*bWorldSpace=*/true,
			/*bMarkRenderStateDirty=*/(i == M - 1), /*bTeleport=*/true);
	}

	// 프로브: 인스턴스 수 == live 탄환 수 추종 확인 (매 30틱 1회, 로그 과다 방지).
	static int32 ProbeTick = 0;
	if (((ProbeTick++) % 30) == 0)
	{
		UE_LOG(LogTemp, Log, TEXT("[RE] RenderProbe: live=%d ISM.Count=%d"), M, ISM->GetInstanceCount());
	}
}
