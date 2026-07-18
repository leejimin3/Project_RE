// Copyright Epic Games, Inc. All Rights Reserved.

#include "REGameMode.h"
#include "RECharacterBase.h"
#include "REPlayerController.h"
#include "MassEntitySubsystem.h"
#include "MassEntityManager.h"
#include "REBulletSimProcessor.h"
#include "REBulletRenderProcessor.h"
#include "REBossCharacter.h"
#include "REBulletPatternGenerator.h"
#include "TimerManager.h"
#include "HAL/IConsoleManager.h"
#include "REStatsSettings.h"

namespace
{
	// #46 측정 전용: 1이면 EndGame을 무력화 → 승패 확정이 보스 DemoFireTimer를 끄지 못하게 막는다.
	// (자동사격이 보스를 ~2.5s에 죽이거나(VICTORY) 정지 플레이어가 탄막에 죽으면(DEFEAT)
	//  발사가 중단돼 Mass 탄환이 목표 수까지 못 차 측정이 무효화됨.)
	// 프로파일링에서만 켠다(scripts/profile.ps1). 기본 0 = 게임 플레이 영향 없음.
	static TAutoConsoleVariable<int32> CVarProfilingKeepFiring(
		TEXT("re.Profiling.KeepFiring"),
		0,
		TEXT("측정 전용: 1이면 게임오버를 무시하고 보스 탄막 발사를 계속 유지."),
		ECVF_Cheat);
}

AREGameMode::AREGameMode()
{
	DefaultPawnClass = ARECharacterBase::StaticClass();
	PlayerControllerClass = AREPlayerController::StaticClass();
}

void AREGameMode::BeginPlay()
{
	Super::BeginPlay();

	// M3.5 ③: 스탯 로드 확인 — ini 반영 검증 프로브 (재시작 반영 원칙의 관측점).
	{
		const UREStatsSettings* Stats = GetDefault<UREStatsSettings>();
		UE_LOG(LogTemp, Log, TEXT("[Stats] Dmg=%.1f AtkInt=%.2f Range=%.0f PHP=%.0f BHP=%.0f BSpd=%.0f BLife=%.1f BInt=%.2f PerShot=%d"),
			Stats->AttackDamage, Stats->AttackInterval, Stats->AttackRange, Stats->PlayerMaxHealth,
			Stats->BossMaxHealth, Stats->BulletSpeed, Stats->BulletLifetime, Stats->BossFireInterval, Stats->BulletsPerShot);
	}

	// Mass 스모크 테스트: 서브시스템 얻고 엔티티 1개 생성 → 로그.
	// GameMode는 서버 권위라 HasAuthority 가드 불필요.
	if (UMassEntitySubsystem* Mass = GetWorld()->GetSubsystem<UMassEntitySubsystem>())
	{
		FMassEntityManager& EM = Mass->GetMutableEntityManager();
		FMassArchetypeHandle Arch = EM.CreateArchetype({ FRETestFragment::StaticStruct() });
		FMassEntityHandle E = EM.CreateEntity(Arch);
		UE_LOG(LogTemp, Log, TEXT("[RE] Mass entity created: Index=%d Serial=%d"), E.Index, E.SerialNumber);
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("[RE] UMassEntitySubsystem NULL"));
	}

	// #4 검증: 두 Processor CDO의 ExecutionFlags 확인. Sim=7(AllNetModes), Render=5(Standalone|Client).
	const uint8 SimFlags    = (uint8)GetDefault<UREBulletSimProcessor>()->GetExecutionFlags();
	const uint8 RenderFlags = (uint8)GetDefault<UREBulletRenderProcessor>()->GetExecutionFlags();
	UE_LOG(LogTemp, Log, TEXT("[RE] SimProcessor flags=%d  RenderProcessor flags=%d"), SimFlags, RenderFlags);

	// #5 검증: 보스 스폰 후 탄막 트리거 → 싱글 경로 스폰 카운트 실증.
	// AlwaysSpawn: 캡슐 충돌로 스폰 실패하는 것 방지.
	// Z=90: 탄환이 보스 위치에서 스폰되므로 바닥(Z=0) 위로 띄워 매몰/z-fighting 방지 (#17 데모 가시성).
	// X=600: PlayerStart(원점 부근)와 이격 — 겹치면 스폰 즉시 피격으로 프레임 3에 즉사 DEFEAT (#54).
	//        탄속 300×수명 3s = 사거리 900 안쪽이라 위협은 유지, 도달까지 ~2s 회피 여유.
	//        REActorBulletSpawner::SpawnOrigin(측정 비교군)과 반드시 동일 좌표 유지.
	FActorSpawnParameters BossSpawnParams;
	BossSpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	if (AREBossCharacter* Boss = GetWorld()->SpawnActor<AREBossCharacter>(
			AREBossCharacter::StaticClass(), FVector(600.f, 0.f, 90.f), FRotator::ZeroRotator, BossSpawnParams))
	{
		// #64: 발사 주체를 Boss로 이관 — 랜덤 패턴 페이즈 로테이션(M5 RPC 확장 대비).
		DemoBoss = Boss;
		// 서버 권위 지점에서 랜덤 시드 1개 생성 → M5에서 클라 replicate하면 결정적 동기화.
		Boss->StartFiring(/*Seed=*/FMath::Rand());
	}

	// #16 프로브: 패턴 제너레이터 수학 단위 검증 (순수 함수, 프레임 무관).
	{
		using namespace REBulletPattern;
		const TArray<FBulletSpawnParams> Sp = GenerateSpiral(FVector::ZeroVector, FSpiralParams{});
		const float SA0 = FMath::RadiansToDegrees(FMath::Atan2(Sp[0].Velocity.Y, Sp[0].Velocity.X));
		const float SA1 = FMath::RadiansToDegrees(FMath::Atan2(Sp[1].Velocity.Y, Sp[1].Velocity.X));
		UE_LOG(LogTemp, Log, TEXT("[RE] SpiralProbe: N=%d |V0|=%.1f ang0=%.1f ang1=%.1f"),
			Sp.Num(), Sp[0].Velocity.Size(), SA0, SA1);

		const TArray<FBulletSpawnParams> Fn = GenerateFan(FVector::ZeroVector, FFanParams{});
		const float FA0 = FMath::RadiansToDegrees(FMath::Atan2(Fn[0].Velocity.Y, Fn[0].Velocity.X));
		const float FAL = FMath::RadiansToDegrees(FMath::Atan2(Fn.Last().Velocity.Y, Fn.Last().Velocity.X));
		UE_LOG(LogTemp, Log, TEXT("[RE] FanProbe: N=%d ang_first=%.1f ang_last=%.1f"), Fn.Num(), FA0, FAL);
	}
}

void AREGameMode::EndGame(bool bVictory)
{
	// #46 측정 모드: 게임오버를 무시하고 탄막을 계속 유지 (근거는 CVar 정의 주석).
	if (CVarProfilingKeepFiring.GetValueOnGameThread() != 0)
	{
		return;
	}

	if (bGameOver)
	{
		return;
	}
	bGameOver = true;

	UE_LOG(LogTemp, Log, TEXT("[RE] EndGame: %s"), bVictory ? TEXT("VICTORY") : TEXT("DEFEAT"));

	// 1) 탄막 발사 중지. 이미 뜬 탄환은 Lifetime 다할 때까지 계속 난다 (설계 합의 — 일괄 소멸 안 함).
	if (DemoBoss)
	{
		DemoBoss->StopFiring();
	}

	APlayerController* PC = GetWorld()->GetFirstPlayerController();

	// 2) 결과 화면 + 입력 차단 — 오너 클라 실행(싱글은 로컬 즉시).
	if (AREPlayerController* REPC = Cast<AREPlayerController>(PC))
	{
		REPC->Client_ShowResult(bVictory);
	}
}
