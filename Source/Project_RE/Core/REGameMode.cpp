// Copyright Epic Games, Inc. All Rights Reserved.

#include "REGameMode.h"
#include "RECharacterBase.h"
#include "REPlayerController.h"
#include "MassEntitySubsystem.h"
#include "MassEntityManager.h"
#include "REBulletSimProcessor.h"
#include "REBulletRenderProcessor.h"
#include "REBossCharacter.h"
#include "REBulletSpawnSubsystem.h"
#include "REBulletPatternGenerator.h"
#include "Mass/EntityFragments.h"  // FTransformFragment
#include "TimerManager.h"
#include "REAutoFireComponent.h"

AREGameMode::AREGameMode()
{
	DefaultPawnClass = ARECharacterBase::StaticClass();
	PlayerControllerClass = AREPlayerController::StaticClass();
	PrimaryActorTick.bCanEverTick = true;
}

void AREGameMode::BeginPlay()
{
	Super::BeginPlay();

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
	// AlwaysSpawn: 원점 캡슐 충돌로 스폰 실패하는 것 방지.
	// Z=90: 탄환이 보스 위치에서 스폰되므로 바닥(Z=0) 위로 띄워 매몰/z-fighting 방지 (#17 데모 가시성).
	FActorSpawnParameters BossSpawnParams;
	BossSpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	if (AREBossCharacter* Boss = GetWorld()->SpawnActor<AREBossCharacter>(
			AREBossCharacter::StaticClass(), FVector(0.f, 0.f, 90.f), FRotator::ZeroRotator, BossSpawnParams))
	{
		Boss->TriggerBulletPattern(EBulletPattern::Spiral, 12345, 0.f);
		Boss->TriggerBulletPattern(EBulletPattern::Spiral, 12345, 0.f);  // #16 프로브: BaseAngle 누적 확인

		// #17 데모: 0.1초마다 Spiral 발사 → 회전 나선 탄막 지속(영상 소스 + ISM 카운트 추종 검증).
		DemoBoss = Boss;
		FTimerDelegate FireDel = FTimerDelegate::CreateLambda([this]()
		{
			if (DemoBoss)
			{
				DemoBoss->TriggerBulletPattern(EBulletPattern::Spiral, 12345, 0.f);
			}
		});
		GetWorld()->GetTimerManager().SetTimer(DemoFireTimer, FireDel, 0.1f, /*bLoop=*/true);
	}

	// #15 프로브: nonzero velocity/lifetime 탄환 1발 → SimProcessor 이동/파괴 관측용.
	// MassGameplay 플러그인의 페이즈 매니저가 SimProcessor를 매 프레임 자동 구동(PrePhysics).
	if (UREBulletSpawnSubsystem* Spawner = GetWorld()->GetSubsystem<UREBulletSpawnSubsystem>())
	{
		ProbeBullet = Spawner->SpawnBullet(FVector::ZeroVector, FVector(100.f, 0.f, 0.f), 0.5f);
		UE_LOG(LogTemp, Log, TEXT("[RE] SimProbe spawn: Vel=(100,0,0) Life=0.50"));
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

void AREGameMode::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (!ProbeBullet.IsSet())
	{
		return;
	}

	ProbeElapsed += DeltaSeconds;

	UMassEntitySubsystem* Mass = GetWorld()->GetSubsystem<UMassEntitySubsystem>();
	if (!Mass)
	{
		return;
	}
	FMassEntityManager& EM = Mass->GetMutableEntityManager();

	if (EM.IsEntityValid(ProbeBullet))
	{
		const FVector Loc = EM.GetFragmentDataChecked<FTransformFragment>(ProbeBullet).GetTransform().GetLocation();
		UE_LOG(LogTemp, Log, TEXT("[RE] SimProbe: t=%.2f Loc=%s Alive=1"), ProbeElapsed, *Loc.ToString());
	}
	else
	{
		UE_LOG(LogTemp, Log, TEXT("[RE] SimProbe: t=%.2f Alive=0 (destroyed)"), ProbeElapsed);
		ProbeBullet.Reset();  // 파괴 확인 후 로그 종료.
	}
}

void AREGameMode::EndGame(bool bVictory)
{
	if (bGameOver)
	{
		return;
	}
	bGameOver = true;

	UE_LOG(LogTemp, Log, TEXT("[RE] EndGame: %s"), bVictory ? TEXT("VICTORY") : TEXT("DEFEAT"));

	// 1) 탄막 발사 중지. 이미 뜬 탄환은 Lifetime 다할 때까지 계속 난다 (설계 합의 — 일괄 소멸 안 함).
	GetWorld()->GetTimerManager().ClearTimer(DemoFireTimer);

	APlayerController* PC = GetWorld()->GetFirstPlayerController();

	// 2) 자동사격 중지. 서버 타이머 구동이라 입력 차단으로는 안 멈춘다.
	//    AutoFireComponent는 캐릭터의 protected 멤버 — accessor 추가 대신 컴포넌트 조회.
	if (ARECharacterBase* Player = PC ? Cast<ARECharacterBase>(PC->GetPawn()) : nullptr)
	{
		if (UREAutoFireComponent* AutoFire = Player->FindComponentByClass<UREAutoFireComponent>())
		{
			AutoFire->StopFiring();
		}
	}

	// 3) 결과 화면 + 입력 차단 — 오너 클라 실행(싱글은 로컬 즉시).
	if (AREPlayerController* REPC = Cast<AREPlayerController>(PC))
	{
		REPC->Client_ShowResult(bVictory);
	}
}
