// Copyright Epic Games, Inc. All Rights Reserved.

#include "REArcRenderProcessor.h"
#include "REBulletFragments.h"
#include "REBulletRenderSubsystem.h"
#include "MassExecutionContext.h"
#include "Mass/EntityFragments.h"  // FTransformFragment
#include "Components/InstancedStaticMeshComponent.h"
#include "Engine/World.h"

namespace
{
	/** 곡사탄 구체 스케일 — 직선탄(0.5)보다 약간 크게(0.7) 눈에 띄게. */
	constexpr float ArcBulletScale = 0.7f;
	/** 마커 원판 두께 스케일 — Cylinder(높이 100)를 거의 평면으로. */
	constexpr float MarkerThickness = 0.02f;
	/** Cylinder 기본 반경(cm) — /Engine/BasicShapes/Cylinder. 스케일 = Radius/50. */
	constexpr float CylinderBaseRadius = 50.f;
	/** 마커 바닥 오프셋(cm) — Target.Z에서 살짝 띄워 Z-fighting 방지. */
	constexpr float MarkerZOffset = 2.f;
}

UREArcRenderProcessor::UREArcRenderProcessor()
	: EntityQuery(*this)
{
	ExecutionFlags = (int32)(EProcessorExecutionFlags::Standalone | EProcessorExecutionFlags::Client);
	bRequiresGameThreadExecution = true;   // ISM 변형은 GT 전용
}

void UREArcRenderProcessor::ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager)
{
	EntityQuery.AddRequirement<FTransformFragment>(EMassFragmentAccess::ReadOnly);
	EntityQuery.AddRequirement<FArcBulletFragment>(EMassFragmentAccess::ReadOnly);
	EntityQuery.AddTagRequirement<FArcBulletTag>(EMassFragmentPresence::All);
}

void UREArcRenderProcessor::Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(RE_ArcRender);

	UWorld* World = EntityManager.GetWorld();
	UREBulletRenderSubsystem* RS = World ? World->GetSubsystem<UREBulletRenderSubsystem>() : nullptr;
	UInstancedStaticMeshComponent* ArcISM    = RS ? RS->GetArcISM() : nullptr;
	UInstancedStaticMeshComponent* MarkerISM = RS ? RS->GetMarkerISM() : nullptr;
	if (!ArcISM || !MarkerISM)
	{
		return;  // 데디서버 등 ISM 없으면 no-op
	}

	// 1) live arc탄 → 탄 트랜스폼 + 마커 트랜스폼 수집.
	TArray<FTransform> BulletXf;
	TArray<FTransform> MarkerXf;
	EntityQuery.ForEachEntityChunk(Context, [&](FMassExecutionContext& Ctx)
	{
		const int32 Num = Ctx.GetNumEntities();
		const TConstArrayView<FTransformFragment> T = Ctx.GetFragmentView<FTransformFragment>();
		const TConstArrayView<FArcBulletFragment> A = Ctx.GetFragmentView<FArcBulletFragment>();
		for (int32 i = 0; i < Num; ++i)
		{
			FTransform B = T[i].GetTransform();
			B.SetScale3D(FVector(ArcBulletScale));
			BulletXf.Add(B);

			// 마커: Target 바닥, 반경=Radius(Cylinder 스케일), 납작.
			const float RadScale = A[i].Radius / CylinderBaseRadius;
			FTransform M;
			M.SetLocation(FVector(A[i].Target.X, A[i].Target.Y, A[i].Target.Z + MarkerZOffset));
			M.SetScale3D(FVector(RadScale, RadScale, MarkerThickness));
			MarkerXf.Add(M);
		}
	});

	// 2) 두 ISM 인스턴스 수를 각각 맞춤(꼬리 add/remove → 타 인덱스 불변).
	auto SyncISM = [](UInstancedStaticMeshComponent* ISM, const TArray<FTransform>& Xf)
	{
		const int32 M = Xf.Num();
		int32 Count = ISM->GetInstanceCount();
		while (Count < M) { ISM->AddInstance(FTransform::Identity, /*bWorldSpace=*/true); ++Count; }
		while (Count > M) { ISM->RemoveInstance(Count - 1);                               --Count; }
		for (int32 i = 0; i < M; ++i)
		{
			ISM->UpdateInstanceTransform(i, Xf[i], /*bWorldSpace=*/true,
				/*bMarkRenderStateDirty=*/(i == M - 1), /*bTeleport=*/true);
		}
	};
	SyncISM(ArcISM, BulletXf);
	SyncISM(MarkerISM, MarkerXf);
}
