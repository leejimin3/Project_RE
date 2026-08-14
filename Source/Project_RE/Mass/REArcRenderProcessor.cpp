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
	/** 마커 원판 두께 스케일 — Cylinder(높이 100cm)를 낮은 원판(100cm 두께 그대로, XY만 확대)으로.
	 *  실측 확정(실RHI 스크린샷 이진탐색): XY 스케일(2.4) 대비 Z 스케일이 1.0 미만이면
	 *  인스턴스가 화면에서 완전히 사라짐(0.02/0.15/0.4 전부 무렌더 확인, 등방 2.0은 정상 렌더 확인) —
	 *  ISM 극단적 비등방 스케일에서의 컬링/바운즈 계산 이슈로 추정(엔진 레벨 이슈, 원인 미상).
	 *  Z=1.0이 확인된 안전 하한선 — 더 낮추지 말 것. */
	constexpr float MarkerThickness = 1.0f;
	/** Cylinder 기본 반경(cm) — /Engine/BasicShapes/Cylinder. 스케일 = Radius/50. */
	constexpr float CylinderBaseRadius = 50.f;
	/** 마커 바닥 오프셋(cm) — Target.Z에서 띄워 바닥 매몰/Z-fighting 방지. 실RHI 스크린샷으로 확인된 값. */
	constexpr float MarkerZOffset = 10.f;
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

	// 스폰 팝 지속시간(s). 직선탄과 같은 값 — 수명 페이드는 넣지 않는다 (#97).
	// 이름을 직선탄 쪽 PopDuration 과 다르게 둔다: 익명 네임스페이스 동명 상수가
	// 유니티 빌드에서 충돌한 전례가 있다(같은 파일 ActorBulletScale 주석 참조).
	constexpr float ArcPopDuration = 0.1f;

	// 1) live arc탄 → 탄 트랜스폼 + 마커 트랜스폼 + 스폰 팝 수집.
	TArray<FTransform> BulletXf;
	TArray<FTransform> MarkerXf;
	TArray<float>      BulletPop;
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

			// 곡사탄은 Elapsed 가 곧 나이다(0 에서 시작해 FlightTime 까지 증가).
			// 커스텀데이터는 인스턴스당 [0]=스폰팝, [1]=색선택 으로 인터리브된다.
			// 곡사탄은 색 교차를 쓰지 않으므로 항상 0 — 두 색을 같게 둬서 단색으로 보인다 (#97).
			BulletPop.Add(FMath::Clamp(A[i].Elapsed / ArcPopDuration, 0.f, 1.f));
			BulletPop.Add(0.f);

			// 마커: Target 바닥, 반경=Radius(Cylinder 스케일), 낮은 원판.
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
		// 배열째 한 번에 — 인스턴스당 개별 호출은 개수에 비례해 게임 스레드를 먹는다 (#95).
		if (M > 0)
		{
			ISM->BatchUpdateInstancesTransforms(0, Xf, /*bWorldSpace=*/true,
				/*bMarkRenderStateDirty=*/true, /*bTeleport=*/true);
		}
	};
	SyncISM(ArcISM, BulletXf);
	SyncISM(MarkerISM, MarkerXf);

	// 팝은 곡사탄에만. 마커는 바닥 디스크라 커스텀데이터를 쓰지 않는다 (#97).
	// SyncISM 이 인스턴스 수를 맞춘 뒤라야 SetCustomData 의 인덱스 범위가 유효하다.
	// 트랜스폼 배치가 이미 끝나 뒤따르는 플러시가 없으므로 dirty 를 여기서 true 로 준다.
	if (BulletXf.Num() > 0)
	{
		ArcISM->SetCustomData(0, BulletXf.Num() - 1, BulletPop, /*bMarkRenderStateDirty=*/true);
	}
}
