// Copyright Epic Games, Inc. All Rights Reserved.

#include "REExplosionRenderProcessor.h"
#include "REExplosionFx.h"
#include "REBulletGeometry.h"                        // EngineSphereRadius — 메시 기본 반경 단일 출처
#include "REBulletRenderSubsystem.h"
#include "MassExecutionContext.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Engine/World.h"
#include "UnrealClient.h"   // FScreenshotRequest — 시각 검증 (#97)
#include "ProfilingDebugging/CsvProfiler.h"
#include "Project_RE.h"                              // LogRE / LogREBullet / LogRENet

CSV_DECLARE_CATEGORY_EXTERN(REBullet);  // 정의는 REBulletSimProcessor.cpp

namespace
{
	/**
	 *  N>0 이면 이 프로세서의 N번째 실행에서 화면을 PNG로 저장한다 (0=끔).
	 *
	 *  연출의 시각 결과(색이 읽히는지, 밝기가 톤매퍼에서 씻기는지)는 수치 게이트로
	 *  판정할 수 없다. 이 CVar가 없으면 매번 사람이 눈으로 봐야 하고, 그 왕복이 렌더
	 *  작업의 실제 병목이었다. 산출물: Saved/Screenshots/ (#97)
	 *
	 *  **탄환 렌더 프로세서가 아니라 여기 있는 이유 (#151):** 저쪽은 FBulletTag 쿼리를
	 *  들고 있어서 Mass 탄환이 한 발도 없는 패턴에서는 통째로 프루닝돼 Execute 가 안 돈다
	 *  — Artillery(re.Debug.BossPattern 2)는 FireArtillery() 로 조기 리턴해 Mass 탄환을
	 *  만들지 않으므로 곡사 화면은 영원히 못 찍혔다. 이 프로세서는 쿼리가 없고
	 *  QueryBasedPruning=Never 라 패턴과 무관하게 매 프레임 돈다.
	 *
	 *  렌더 프로세서는 Standalone|Client 에서만 도므로 데디서버에는 영향이 없다.
	 */
	static TAutoConsoleVariable<int32> CVarDebugShotFrame(
		TEXT("re.Debug.ScreenshotFrame"),
		0,
		TEXT("N번째 렌더 프로세서 실행에서 스크린샷 저장 (0=끔). 시각 검증용."),
		ECVF_Cheat);

	/**
	 *  스크린샷에 화면공간 UI 를 포함할지 (#100).
	 *
	 *  기본 0 은 렌더 검증(#97)용이다 — HUD 가 화면을 가리면 색·밝기 판정을 방해한다.
	 *  1 로 켜면 HUD 를 포함해 찍는다. 화면공간 위젯은 이걸 안 켜면 PNG 에 아예 안 나온다
	 *  (월드스페이스 위젯인 보스 체력바는 0 에서도 찍히므로, 안 나오는 이유를 오해하기 쉽다).
	 */
	static TAutoConsoleVariable<int32> CVarDebugShotUI(
		TEXT("re.Debug.ScreenshotUI"),
		0,
		TEXT("스크린샷에 화면공간 UI 포함 (0=제외). UI 검증용."),
		ECVF_Cheat);

	/** 코어(구체) 반경(cm). 탄 지름 50, HitRadius 60 보다 커야 폭발로 읽힌다. */
	constexpr float ExplosionCoreRadiusStart = 20.f;
	constexpr float ExplosionCoreRadiusEnd   = 120.f;

	/** 링(수평 원환) 반경(cm). 코어보다 빠르고 넓게 퍼진다. */
	constexpr float ExplosionRingRadiusStart = 40.f;
	constexpr float ExplosionRingRadiusEnd   = 220.f;

	/** 연기 대역(구체) 반경(cm) (#151). 시작은 코어(20)보다 크되 링(40)보다 작게 —
	 *  스폰 순간 코어를 삼키지 않는다. 끝은 링(220)보다 크게 — 마지막까지 남는 층이다. */
	constexpr float ExplosionSmokeRadiusStart = 30.f;
	constexpr float ExplosionSmokeRadiusEnd   = 260.f;

	/** /Engine/BasicShapes/Plane 은 100x100 이라 반경 50. 스케일 = 반경/50.
	 *  (마커 쪽 CylinderBaseRadius 와 같은 값이지만 이름을 다르게 둔다 — 익명
	 *  네임스페이스 동명 상수가 유니티 빌드에서 충돌한 전례가 있다.) */
	constexpr float ExplosionPlaneBaseRadius = 50.f;

	/** 링 Z 스케일. **1.0 미만으로 두지 말 것** — XY 를 키운 상태에서 Z 를 낮추면
	 *  인스턴스가 화면에서 통째로 사라진다(마커에서 실RHI 스크린샷 이진탐색으로 확인:
	 *  0.02/0.15/0.4 전부 무렌더, 등방 2.0 은 정상. ISM 극단 비등방 스케일의 컬링/
	 *  바운즈 이슈로 추정, 엔진 레벨·원인 미상 — REArcRenderProcessor.cpp 참조).
	 *  코어와 연기는 등방 구체라 이 함정에 걸리지 않는다. */
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

	// 시각 검증용 스크린샷 — 지정한 실행 횟수에서 정확히 한 번 (#97).
	// ISM 확인보다 **먼저** 둔다. 뒤에 두면 통이 하나라도 없는 월드에서 카운터가 아예
	// 안 돌아 요청 시점이 오지 않는다 — 원래 이 훅이 탄환 프로세서에서 겪던 것과 같은 함정이다.
	{
		const int32 ShotAt = CVarDebugShotFrame.GetValueOnGameThread();
		static int32 ShotTick = 0;
		++ShotTick;
		if (ShotAt > 0 && ShotTick == ShotAt)
		{
			// 콘솔 HighResShot 은 -game 뷰포트에서 조용히 무시됐다(로그도 PNG도 안 남음).
			// 직접 요청이 확실하다 — 산출물은 Saved/Screenshots/ 아래.
			const bool bShowUI = CVarDebugShotUI.GetValueOnGameThread() != 0;
			FScreenshotRequest::RequestScreenshot(bShowUI);
			UE_LOG(LogREBullet, Log, TEXT("[RE] DebugScreenshot: 요청 (tick=%d explosions=%d ui=%d)"),
				ShotTick, Live.Num(), bShowUI ? 1 : 0);
		}
	}

	UREBulletRenderSubsystem* RS = World->GetSubsystem<UREBulletRenderSubsystem>();
	UInstancedStaticMeshComponent* CoreISM  = RS ? RS->GetExplosionCoreISM()  : nullptr;
	UInstancedStaticMeshComponent* RingISM  = RS ? RS->GetExplosionRingISM()  : nullptr;
	UInstancedStaticMeshComponent* SmokeISM = RS ? RS->GetExplosionSmokeISM() : nullptr;
	if (!CoreISM || !RingISM || !SmokeISM)
	{
		return;
	}

	const int32 M = Live.Num();
	TArray<FTransform> CoreXf;
	TArray<FTransform> RingXf;
	TArray<FTransform> SmokeXf;
	TArray<float> Cd;
	CoreXf.Reserve(M);
	RingXf.Reserve(M);
	SmokeXf.Reserve(M);
	Cd.Reserve(M);

	for (const REExplosionFx::FLiveExplosion& E : Live)
	{
		const float CoreR  = FMath::Lerp(ExplosionCoreRadiusStart,  ExplosionCoreRadiusEnd,  E.Progress);
		const float RingR  = FMath::Lerp(ExplosionRingRadiusStart,  ExplosionRingRadiusEnd,  E.Progress);
		const float SmokeR = FMath::Lerp(ExplosionSmokeRadiusStart, ExplosionSmokeRadiusEnd, E.Progress);

		// 구체는 등방 스케일이고 회전이 의미 없다. 카메라가 월드 고정(pitch -50 절대)이라
		// 빌보드 갱신도 필요 없다.
		const float CoreS = CoreR / REBulletGeometry::EngineSphereRadius;
		CoreXf.Add(FTransform(FRotator::ZeroRotator, E.Loc, FVector(CoreS)));

		// Plane 은 XY 평면(법선 +Z)이라 회전 없이 그대로 수평 원판이다.
		// Z 는 1.0 고정 — 위 ExplosionRingZScale 주석의 함정.
		const float RingS = RingR / ExplosionPlaneBaseRadius;
		RingXf.Add(FTransform(FRotator::ZeroRotator, E.Loc,
			FVector(RingS, RingS, ExplosionRingZScale)));

		// 연기도 등방 구체라 링과 달리 Z 스케일 함정에 걸리지 않는다 (#151).
		const float SmokeS = SmokeR / REBulletGeometry::EngineSphereRadius;
		SmokeXf.Add(FTransform(FRotator::ZeroRotator, E.Loc, FVector(SmokeS)));

		Cd.Add(E.Progress);
	}

	SyncExplosionISM(CoreISM,  CoreXf);
	SyncExplosionISM(RingISM,  RingXf);
	SyncExplosionISM(SmokeISM, SmokeXf);

	// 인스턴스 수를 맞춘 뒤라야 SetCustomData 의 인덱스 범위가 유효하다.
	// 세 층이 **같은** 진행도를 받고 각자 다르게 해석한다 — 커스텀데이터 슬롯은 1개다.
	if (M > 0)
	{
		CoreISM->SetCustomData(0, M - 1, Cd, /*bMarkRenderStateDirty=*/true);
		RingISM->SetCustomData(0, M - 1, Cd, /*bMarkRenderStateDirty=*/true);
		SmokeISM->SetCustomData(0, M - 1, Cd, /*bMarkRenderStateDirty=*/true);
	}
}
