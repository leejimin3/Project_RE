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
#include "GameFramework/PawnMovementComponent.h"

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
	// #85 협동 인원. ready가 이 수를 채우면 보스 발사 시작(RPG 던전 입장 모델).
	// ini가 아니라 CVar인 이유: 스테이징 Config는 pak 안에 들어가서 ini면 인원을 바꿀 때마다
	// 재쿡해야 한다. CVar면 서버 커맨드라인(-ExecCmds)으로 넘길 수 있어 데디 검증이 재쿡 없이 돈다.
	static TAutoConsoleVariable<int32> CVarExpectedPlayers(
		TEXT("re.Coop.ExpectedPlayers"),
		1,
		TEXT("협동 시작에 필요한 준비 완료 플레이어 수. 기본 1(싱글 동작 유지)."),
		ECVF_Default);
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
		// #64: 발사 주체를 Boss로 이관. 발사 시작은 클라 준비 후 (#84) — 여기서 켜지 않는다.
		DemoBoss = Boss;
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

	// 준비 신호가 이미 와 있었다면 여기서 켜진다 — PC BeginPlay와 GameMode BeginPlay는 순서가 보장되지 않는다.
	TryStartBossFiring();
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

	// 2) 결과 화면 + 입력 차단 — 전 클라에 보낸다 (#85).
	//    이미 죽어서 입력이 차단된 플레이어도 결과 화면은 받아야 하므로 필터하지 않는다.
	//    FConstPlayerControllerIterator는 약참조를 주므로 역참조 전에 유효성을 본다.
	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		if (!It->IsValid())
		{
			continue;
		}
		if (AREPlayerController* REPC = Cast<AREPlayerController>(It->Get()))
		{
			REPC->Client_ShowResult(bVictory);
		}
	}
}

void AREGameMode::NotifyPlayerReady(APlayerController* PC)
{
	// TSet은 카운트 중복을 막아주지만 로그는 아니다 — 클라가 RPC를 연타하면 무한정 찍힌다.
	// 실제로 새로 추가됐을 때만 로그.
	bool bAlreadyInSet = false;
	if (PC)
	{
		ReadyPlayers.Add(PC, &bAlreadyInSet);
	}
	const int32 Expected = FMath::Max(1, CVarExpectedPlayers.GetValueOnGameThread());
	if (PC && !bAlreadyInSet)
	{
		UE_LOG(LogTemp, Log, TEXT("[RE] Player ready %d/%d"), ReadyPlayers.Num(), Expected);
	}
	TryStartBossFiring();
}

void AREGameMode::NotifyPlayerDied(APlayerController* PC)
{
	if (!PC || bGameOver)
	{
		return;
	}
	// 사망 시점에 아직 ReadyPlayers에 없을 수 있다 — 스폰이 ready RPC 왕복보다 먼저 끝나는 레이스.
	// 죽었다는 사실 자체가 참가자라는 증거이므로 분모에도 넣는다: DeadPlayers는 항상 ReadyPlayers의
	// 부분집합이어야 한다는 불변식을 지킨다. TryStartBossFiring()은 여기서 부르지 않는다 —
	// 이 함수가 매치를 시작시키는 부작용을 가져서는 안 된다.
	ReadyPlayers.Add(PC);
	DeadPlayers.Add(PC);

	if (AREPlayerController* REPC = Cast<AREPlayerController>(PC))
	{
		REPC->Client_NotifyDeath();
	}
	// 서버측: 마지막 이동 명령이 남아 시체가 계속 미끄러지는 것을 막는다.
	PC->StopMovement();                                        // 우클릭 이동 패스팔로잉 중단
	if (UPawnMovementComponent* Move = PC->GetPawn() ? PC->GetPawn()->GetMovementComponent() : nullptr)
	{
		Move->StopMovementImmediately();                       // 잔여 속도 제거
	}

	UE_LOG(LogTemp, Log, TEXT("[RE] Player died %d/%d"), DeadPlayers.Num(), ReadyPlayers.Num());

	// 분모는 ExpectedPlayers가 아니라 실제 접속자 수다 — 중간에 나간 사람이 있으면
	// 고정 분모로는 남은 사람이 다 죽어도 게임이 끝나지 않는다.
	if (DeadPlayers.Num() >= ReadyPlayers.Num())
	{
		UE_LOG(LogTemp, Log, TEXT("[RE] All %d players dead"), ReadyPlayers.Num());
		EndGame(/*bVictory=*/false);
	}
}

void AREGameMode::Logout(AController* Exiting)
{
	if (APlayerController* PC = Cast<APlayerController>(Exiting))
	{
		ReadyPlayers.Remove(PC);
		DeadPlayers.Remove(PC);

		UE_LOG(LogTemp, Log, TEXT("[RE] Player left — ready=%d dead=%d"),
			ReadyPlayers.Num(), DeadPlayers.Num());

		// 분모가 줄었으니 지금이 종료 시점일 수 있다. 남은 사람이 이미 다 죽어 있던 경우다.
		// ReadyPlayers.Num() > 0 가드가 없으면 마지막 한 명이 나갈 때 0 >= 0 으로 DEFEAT가 떠서
		// 받을 클라도 없는 상태로 게임이 끝난 것으로 기록된다.
		if (!bGameOver && ReadyPlayers.Num() > 0 && DeadPlayers.Num() >= ReadyPlayers.Num())
		{
			EndGame(/*bVictory=*/false);
		}
	}
	Super::Logout(Exiting);
}

APawn* AREGameMode::SpawnDefaultPawnAtTransform_Implementation(AController* NewPlayer,
                                                               const FTransform& SpawnTransform)
{
	// 맵에 PlayerStart가 하나뿐이라 N인이면 같은 자리에 겹친다 — 인덱스별로 흩는다 (#85).
	// +Y인 이유(#56): 보스가 +X 600에 있어 +X로 흩으면 플레이어를 탄막 레인에 밀어넣는다.
	// 1인이면 Half=0, SpawnedPawnCount=0 → 오프셋이 정확히 0이라 싱글 스폰 좌표가 불변이다.
	const int32 Expected = FMath::Max(1, CVarExpectedPlayers.GetValueOnGameThread());
	const float Half = (Expected - 1) * 0.5f;
	const FVector Offset(0.f, (SpawnedPawnCount - Half) * SpawnSpacing, 0.f);
	++SpawnedPawnCount;

	FTransform Adjusted = SpawnTransform;
	Adjusted.AddToTranslation(Offset);

	UE_LOG(LogTemp, Log, TEXT("[RE] Spawn player idx=%d offsetY=%.0f loc=%s"),
		SpawnedPawnCount - 1, Offset.Y, *Adjusted.GetLocation().ToString());

	return Super::SpawnDefaultPawnAtTransform_Implementation(NewPlayer, Adjusted);
}

void AREGameMode::TryStartBossFiring()
{
	const int32 Expected = FMath::Max(1, CVarExpectedPlayers.GetValueOnGameThread());
	if (bFiringStarted || !DemoBoss || ReadyPlayers.Num() < Expected)
	{
		return;
	}
	bFiringStarted = true;
	// 시드는 서버 전용 PhaseRng 초기화용 — 네트워크에 나가지 않는다 (#84).
	DemoBoss->StartFiring(/*Seed=*/FMath::Rand());
	UE_LOG(LogTemp, Log, TEXT("[RE] Boss firing started (%d/%d ready)"), ReadyPlayers.Num(), Expected);
}
