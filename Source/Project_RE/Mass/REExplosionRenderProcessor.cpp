// Copyright Epic Games, Inc. All Rights Reserved.

#include "REExplosionRenderProcessor.h"
#include "REExplosionFx.h"
#include "REBulletGeometry.h"                        // EngineSphereRadius — 메시 기본 반경 단일 출처
#include "REBulletRenderSubsystem.h"
#include "MassExecutionContext.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Engine/World.h"
#include "ProfilingDebugging/CsvProfiler.h"
#include "Project_RE.h"                              // LogRE / LogREBullet / LogRENet

CSV_DECLARE_CATEGORY_EXTERN(REBullet);  // 정의는 REBulletSimProcessor.cpp

namespace
{
	/** 코어(구체) 반경(cm). 탄 지름 50, HitRadius 60 보다 커야 폭발로 읽힌다. */
	constexpr float ExplosionCoreRadiusStart = 20.f;
	constexpr float ExplosionCoreRadiusEnd   = 120.f;

	/** 링(수평 원환) 반경(cm). 코어보다 빠르고 넓게 퍼진다. */
	constexpr float ExplosionRingRadiusStart = 40.f;
	constexpr float ExplosionRingRadiusEnd   = 220.f;

	/** /Engine/BasicShapes/Plane 은 100x100 이라 반경 50. 스케일 = 반경/50.
	 *  (마커 쪽 CylinderBaseRadius 와 같은 값이지만 이름을 다르게 둔다 — 익명
	 *  네임스페이스 동명 상수가 유니티 빌드에서 충돌한 전례가 있다.) */
	constexpr float ExplosionPlaneBaseRadius = 50.f;

	/** 링 Z 스케일. **1.0 미만으로 두지 말 것** — XY 를 키운 상태에서 Z 를 낮추면
	 *  인스턴스가 화면에서 통째로 사라진다(마커에서 실RHI 스크린샷 이진탐색으로 확인:
	 *  0.02/0.15/0.4 전부 무렌더, 등방 2.0 은 정상. ISM 극단 비등방 스케일의 컬링/
	 *  바운즈 이슈로 추정, 엔진 레벨·원인 미상 — REArcRenderProcessor.cpp 참조). */
	constexpr float ExplosionRingZScale = 1.0f;

	/** 인스턴스 수를 목표에 맞춘다. 꼬리에서 add/remove 하므로 다른 인덱스가 안 밀린다. */
	void SyncExplosionISM(UInstancedStaticMeshComponent* ISM, const TArray<FTransform>& Xf)
	{
		int32 Count = ISM->GetInstanceCount();
		const int32 N = Xf.Num();
		while (Count < N) { ISM->AddInstance(FTransform::Identity, /*bWorldSpace=*/true); ++Count; }
		while (Count > N) { ISM->RemoveInstance(Count - 1);                               --Count; }
		if (N > 0)
		{
			// 인스턴스당 개별 UpdateInstanceTransform 은 개수에 비례해 GT 를 먹는다 (#95).
			ISM->BatchUpdateInstancesTransforms(0, Xf, /*bWorldSpace=*/true,
				/*bMarkRenderStateDirty=*/true, /*bTeleport=*/true);
		}
	}
}

UREExplosionRenderProcessor::UREExplosionRenderProcessor()
{
	// 데디서버 skip — 렌더가 없다.
	ExecutionFlags = (int32)(EProcessorExecutionFlags::Standalone | EProcessorExecutionFlags::Client);

	// ISM(씬 컴포넌트) 변형은 게임 스레드 전용 — AddInstance 가 물리 바디를 만들어
	// 워커 스레드에서 어서션 크래시한다.
	bRequiresGameThreadExecution = true;

	// 쿼리가 없는 프로세서는 기본값(Prune)에서 런타임에 통째로 프루닝돼 Execute 가
	// 한 번도 안 돈다. 끄지 않으면 이 파일 전체가 조용한 dead code 가 된다.
	QueryBasedPruning = EMassQueryBasedPruning::Never;
}

void UREExplosionRenderProcessor::Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(RE_ExplosionRender);
	CSV_SCOPED_TIMING_STAT(REBullet, ExplosionRender);

	UWorld* World = EntityManager.GetWorld();
	if (!World)
	{
		return;
	}

	// 만료 제거를 ISM 확인보다 **먼저** 한다. 뒤에 두면 ISM 이 없는 월드에서 목록이
	// 영원히 안 비고, 예산(re.Fx.ExplosionBudget)이 가득 찬 채 잠겨 폭발이 조용히
	// 전량 버려진다.
	const TArray<REExplosionFx::FLiveExplosion>& Live =
		REExplosionFx::PruneAndGetLive(World->GetTimeSeconds());

	UREBulletRenderSubsystem* RS = World->GetSubsystem<UREBulletRenderSubsystem>();
	UInstancedStaticMeshComponent* CoreISM = RS ? RS->GetExplosionCoreISM() : nullptr;
	UInstancedStaticMeshComponent* RingISM = RS ? RS->GetExplosionRingISM() : nullptr;
	if (!CoreISM || !RingISM)
	{
		return;
	}

	const int32 M = Live.Num();
	TArray<FTransform> CoreXf;
	TArray<FTransform> RingXf;
	TArray<float> Cd;
	CoreXf.Reserve(M);
	RingXf.Reserve(M);
	Cd.Reserve(M);

	for (const REExplosionFx::FLiveExplosion& E : Live)
	{
		const float CoreR = FMath::Lerp(ExplosionCoreRadiusStart, ExplosionCoreRadiusEnd, E.Progress);
		const float RingR = FMath::Lerp(ExplosionRingRadiusStart, ExplosionRingRadiusEnd, E.Progress);

		// 구체는 등방 스케일이고 회전이 의미 없다. 카메라가 월드 고정(pitch -50 절대)이라
		// 빌보드 갱신도 필요 없다.
		const float CoreS = CoreR / REBulletGeometry::EngineSphereRadius;
		CoreXf.Add(FTransform(FRotator::ZeroRotator, E.Loc, FVector(CoreS)));

		// Plane 은 XY 평면(법선 +Z)이라 회전 없이 그대로 수평 원판이다.
		// Z 는 1.0 고정 — 위 ExplosionRingZScale 주석의 함정.
		const float RingS = RingR / ExplosionPlaneBaseRadius;
		RingXf.Add(FTransform(FRotator::ZeroRotator, E.Loc,
			FVector(RingS, RingS, ExplosionRingZScale)));

		Cd.Add(E.Progress);
	}

	SyncExplosionISM(CoreISM, CoreXf);
	SyncExplosionISM(RingISM, RingXf);

	// 인스턴스 수를 맞춘 뒤라야 SetCustomData 의 인덱스 범위가 유효하다.
	if (M > 0)
	{
		CoreISM->SetCustomData(0, M - 1, Cd, /*bMarkRenderStateDirty=*/true);
		RingISM->SetCustomData(0, M - 1, Cd, /*bMarkRenderStateDirty=*/true);
	}
}
