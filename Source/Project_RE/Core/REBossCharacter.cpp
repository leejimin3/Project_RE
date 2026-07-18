// Copyright Epic Games, Inc. All Rights Reserved.

#include "REBossCharacter.h"
#include "REBulletSpawnSubsystem.h"
#include "REBulletPatternGenerator.h"
#include "Net/UnrealNetwork.h"
#include "REHealthBarComponent.h"
#include "REGameMode.h"
#include "HAL/IConsoleManager.h"
#include "REBulletRenderSubsystem.h"                        // 라이브 카운트(ISM) 조회 (#51)
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Animation/AnimInstance.h"
#include "Engine/SkeletalMesh.h"
#include "UObject/ConstructorHelpers.h"
#include "REStatsSettings.h"
#include "TimerManager.h"
#include "GameFramework/PlayerController.h"

/**
 *  측정용 클로즈드루프 오버라이드. 발사 시점 조회 — 재시작 없이 다음 발사부터 반영.
 *  -1(기본) = 오픈루프: Settings(BulletsPerShot) 고정 발수 — 게임플레이 경로, 패턴 균일.
 *  0 이상 = 이 값을 목표 동시 탄수로 클로즈드루프(#51) 가동 — M3 측정 하네스 전제
 *  (0은 Mass off 스위치 — scripts/profile.ps1 Actor 비교군 경로가 의존, 유효값이라 센티널로 못 쓴다).
 */
static TAutoConsoleVariable<int32> CVarBulletCount(
	TEXT("re.Bullets.Count"),
	-1,
	TEXT("측정용: -1=오픈루프(Settings BulletsPerShot 고정), 0 이상=목표 동시 탄수 클로즈드루프."),
	ECVF_Cheat);

// #51 클로즈드루프 적분 게인. 수명 지연(수명/발사주기 ≈ 30발) 대비 크면 진동한다.
// 튜닝 노브(리빌드 없이 -ExecCmds로 스윕). 기본값은 실측으로 확정.
static TAutoConsoleVariable<float> CVarSpawnKi(
	TEXT("re.Bullets.SpawnKi"),
	0.004f,   // 실측 확정: fill-phase 안티와인드업과 함께 1000/5000 모두 ±0.5%, 100 -6%(짧은 창). 0.008은 진동.
	TEXT("스폰 적분 게인 Ki. 라이브 카운트 목표 수렴 속도/안정성."),
	ECVF_Cheat);

AREBossCharacter::AREBossCharacter()
{
	PrimaryActorTick.bCanEverTick = false;

	// 체력 초기화 — Settings 단일 출처 (M3.5 ③, RECharacterBase 동일 패턴).
	MaxHealth = GetDefault<UREStatsSettings>()->BossMaxHealth;
	Health = MaxHealth;

	// HP바 (#29) — 보스 빨강.
	HealthBar = CreateDefaultSubobject<UREHealthBarComponent>(TEXT("HealthBar"));
	HealthBar->SetupAttachment(RootComponent);
	HealthBar->BarColor = FLinearColor::Red;

	// 보스 가시화 (M3.5 ②) — Quinn 메시 로드 (실패해도 크래시 없이 진행).
	static ConstructorHelpers::FObjectFinder<USkeletalMesh> MeshAsset(
		TEXT("/Game/Characters/Mannequins/Meshes/SKM_Quinn_Simple.SKM_Quinn_Simple"));
	if (MeshAsset.Succeeded())
	{
		GetMesh()->SetSkeletalMesh(MeshAsset.Object);
		GetMesh()->SetRelativeLocation(FVector(0.f, 0.f, -90.f));
		GetMesh()->SetRelativeRotation(FRotator(0.f, -90.f, 0.f));
	}

	// 로코모션 애님BP — 고정형 보스라 idle 상태 재생이 목적.
	static ConstructorHelpers::FClassFinder<UAnimInstance> AnimAsset(
		TEXT("/Game/Characters/Mannequins/Anims/Unarmed/ABP_Unarmed"));
	if (AnimAsset.Succeeded())
	{
		GetMesh()->SetAnimInstanceClass(AnimAsset.Class);
	}
}

void AREBossCharacter::StartFiring(int32 Seed)
{
	PhaseRng.Initialize(Seed);
	bFirstPhase = true;
	BeginPhase();   // 즉시 1회, 이후 타이머로 재진입
}

void AREBossCharacter::StopFiring()
{
	GetWorldTimerManager().ClearTimer(FireTimer);
	GetWorldTimerManager().ClearTimer(PhaseTimer);
}

void AREBossCharacter::BeginPhase()
{
	// KeepFiring 측정 모드: 로테이션/대기 우회, Spiral 연속 발사.
	// StartFiring 시점(BeginPlay)엔 ExecCmds가 아직 CVar를 안 세팅했을 수 있어
	// 여기(타이머 재진입 콜백)에서 매번 조회한다. 첫 페이즈 Spiral 고정이
	// profiling 시작 오염 창을 닫는다.
	static IConsoleVariable* KeepFiring = IConsoleManager::Get().FindConsoleVariable(TEXT("re.Profiling.KeepFiring"));
	if (KeepFiring && KeepFiring->GetInt() != 0)
	{
		CurrentPhasePattern = EBulletPattern::Spiral;
		GetWorldTimerManager().SetTimer(FireTimer, this,
			&AREBossCharacter::FireCurrentPattern, REBulletPattern::FireIntervalSec(), /*bLoop=*/true);
		return;   // PhaseTimer 예약 안 함 → 페이즈 종료/대기 없음
	}

	if (bFirstPhase)
	{
		CurrentPhasePattern = EBulletPattern::Spiral;   // 오프닝 시그니처 + 오염 창 차단
		bFirstPhase = false;
	}
	else
	{
		CurrentPhasePattern = (PhaseRng.RandRange(0, 1) == 0)
			? EBulletPattern::Spiral : EBulletPattern::Fan;
	}

	const bool bSpiral = (CurrentPhasePattern == EBulletPattern::Spiral);
	const float PhaseSec  = bSpiral ? SpiralPhaseSec : FanPhaseSec;
	const float PhaseFireInterval = bSpiral ? REBulletPattern::FireIntervalSec() : FanFireIntervalSec;

	UE_LOG(LogTemp, Log, TEXT("[RE] Boss Phase: %s %.1fs"),
		bSpiral ? TEXT("Spiral") : TEXT("Fan"), PhaseSec);

	GetWorldTimerManager().SetTimer(FireTimer, this,
		&AREBossCharacter::FireCurrentPattern, PhaseFireInterval, /*bLoop=*/true);
	GetWorldTimerManager().SetTimer(PhaseTimer, this,
		&AREBossCharacter::EndPhase, PhaseSec, /*bLoop=*/false);
}

void AREBossCharacter::FireCurrentPattern()
{
	TriggerBulletPattern(CurrentPhasePattern, /*Seed=*/12345, /*StartTime=*/0.f);
}

void AREBossCharacter::EndPhase()
{
	GetWorldTimerManager().ClearTimer(FireTimer);
	UE_LOG(LogTemp, Log, TEXT("[RE] Boss Phase: Rest %.1fs"), RestSec);
	GetWorldTimerManager().SetTimer(PhaseTimer, this,
		&AREBossCharacter::BeginPhase, RestSec, /*bLoop=*/false);
}

void AREBossCharacter::TriggerBulletPattern(EBulletPattern Pattern, int32 Seed, float StartTime)
{
	if (bIsDead)
	{
		return;
	}

	// TODO M5: Multicast_TriggerPattern RPC로 교체 (서버→클라 시드 브로드캐스트, 총알 자체는 미전송).
	//          현재는 싱글 로컬 직접 스폰 경로.

	UREBulletSpawnSubsystem* Spawner = GetWorld() ? GetWorld()->GetSubsystem<UREBulletSpawnSubsystem>() : nullptr;
	if (!Spawner)
	{
		UE_LOG(LogTemp, Warning, TEXT("[RE] Boss::TriggerBulletPattern: UREBulletSpawnSubsystem NULL"));
		return;
	}

	TArray<FBulletSpawnParams> Params;
	switch (Pattern)
	{
	case EBulletPattern::Spiral:
	{
		// 발사 시점 조회 — CVar 변경이 재시작/타이머 재설정 없이 다음 발사부터 반영된다.
		// CVar ≥ 0 = 측정용 클로즈드루프(동시 탄수 유지 — M3 하네스 전제),
		// -1(기본) = 오픈루프: Settings(BulletsPerShot) 고정 발수 → 링이 매 발사 균일.
		const int32 CVarCount = CVarBulletCount.GetValueOnGameThread();
		int32 Count;
		if (CVarCount < 0)
		{
			// 게임플레이 경로 — 발사당 탄수 고정. 동시 탄수는 발수×수명/주기로 자연 결정(제한 없음).
			Count = GetDefault<UREStatsSettings>()->BulletsPerShot;
		}
		else
		{
			const int32 TargetLive = CVarCount;

			// 라이브 탄환 수 = ISM 인스턴스 수(렌더 프로세서가 매 프레임 엔티티 수로 동기화).
			// 관측 불가(데디서버 등 ISM 없음)면 CurrentLive=-1 → 피드포워드 폴백.
			int32 CurrentLive = -1;
			if (const UREBulletRenderSubsystem* RS = GetWorld()->GetSubsystem<UREBulletRenderSubsystem>())
			{
				if (const UInstancedStaticMeshComponent* ISM = RS->GetISM())
				{
					CurrentLive = ISM->GetInstanceCount();
				}
			}

			const float FeedFwd = TargetLive * REBulletPattern::FireIntervalSec() / REBulletPattern::BulletLifetimeSec();
			if (CurrentLive >= 0 && TargetLive > 0)
			{
				// 클로즈드루프(적분 제어): 스폰율을 오차만큼 램프. 소멸률이 얼마든 라이브=목표에서 램프가 멎어 정상상태 오차 0.
				// 단, 첫 1수명 동안은 아직 탄환이 채워지는 중이라 오차가 크게 양수 → 적분하면 와인드업으로 대폭 오버슈트한다.
				// 그 구간은 피드포워드로 채우기만 하고, 채워진 뒤(정상상태 근처)부터 적분으로 소멸분을 보정한다.
				const int32 FillShots = FMath::CeilToInt(REBulletPattern::BulletLifetimeSec() / REBulletPattern::FireIntervalSec());
				if (SpiralShotCount < FillShots)
				{
					SpiralSpawnRate = FeedFwd;
				}
				else
				{
					SpiralSpawnRate += CVarSpawnKi.GetValueOnGameThread() * (TargetLive - CurrentLive);
					SpiralSpawnRate = FMath::Clamp(SpiralSpawnRate, 0.f, (float)TargetLive);  // anti-windup 상한
				}
				// 소수부 이월 내림 — 소형 타깃(rate~3.3)에서 round()가 매 발사 4로 올려 +20% 오버슛하는 것 방지.
				SpiralSpawnAccum += SpiralSpawnRate;
				Count = FMath::FloorToInt(SpiralSpawnAccum);
				SpiralSpawnAccum -= Count;
			}
			else
			{
				Count = FMath::RoundToInt(FeedFwd);  // 라이브 관측 불가(데디서버 등) → 오픈루프 폴백
			}
			++SpiralShotCount;

			UE_LOG(LogTemp, Log, TEXT("[RE] Boss Spiral: Target=%d Live=%d Rate=%.1f"),
				TargetLive, CurrentLive, SpiralSpawnRate);
		}

		const REBulletPattern::FSpiralParams SP = REBulletPattern::MakeSpiralRing(Count, SpiralBaseAngleDeg);
		Params = REBulletPattern::GenerateSpiral(GetActorLocation(), SP);
		UE_LOG(LogTemp, Log, TEXT("[RE] Boss Spiral: N=%d"), Params.Num());
		SpiralBaseAngleDeg += SpiralRotationStepDeg;  // 다음 호출 시 회전
		break;
	}
	case EBulletPattern::Fan:
	{
		REBulletPattern::FFanParams FP;
		// 플레이어 방향 조준. 폰 없으면 0°(기존 기본) 폴백.
		// TODO M5: 멀티는 타깃 선택 필요 — 지금은 첫 플레이어 고정.
		if (APlayerController* PC = GetWorld()->GetFirstPlayerController())
		{
			if (const APawn* Target = PC->GetPawn())
			{
				const FVector D = Target->GetActorLocation() - GetActorLocation();
				FP.CenterAngleDeg = FMath::RadiansToDegrees(FMath::Atan2(D.Y, D.X));
			}
		}
		Params = REBulletPattern::GenerateFan(GetActorLocation(), FP);
		UE_LOG(LogTemp, Log, TEXT("[RE] Boss Fan: Center=%.1f Spread=%.1f -> N=%d"),
			FP.CenterAngleDeg, FP.SpreadDeg, Params.Num());
		break;
	}
	case EBulletPattern::Homing:
		// M1 범위 밖 — 슬롯만 유지, 미구현. 스폰 없이 종료.
		UE_LOG(LogTemp, Warning, TEXT("[RE] Boss: Homing 미구현 (M1 범위 밖)"));
		return;
	}
	Spawner->SpawnBulletBatch(Params);

	UE_LOG(LogTemp, Log, TEXT("[RE] Boss::TriggerBulletPattern: Pattern=%d Seed=%d Start=%.2f -> spawned %d entities at %s"),
		(int32)Pattern, Seed, StartTime, Params.Num(), *GetActorLocation().ToString());
}

float AREBossCharacter::TakeDamage(float DamageAmount, const FDamageEvent& DamageEvent,
                                    AController* EventInstigator, AActor* DamageCauser)
{
	// #46 측정 모드: 플레이어 자동사격(10dmg/0.25s)이 보스를 ~2.5s에 죽인다 →
	// 발사가 끊겨 Mass 탄환이 목표 수까지 못 찬다. 프로파일링 중에는 보스를 무적으로.
	static IConsoleVariable* KeepFiring = IConsoleManager::Get().FindConsoleVariable(TEXT("re.Profiling.KeepFiring"));
	if (KeepFiring && KeepFiring->GetInt() != 0)
	{
		return 0.f;
	}

	// 서버 권위 가드 — 게임상태(Health) 변경은 서버에서만
	if (!HasAuthority())
	{
		return 0.f;
	}

	const float Applied = Super::TakeDamage(DamageAmount, DamageEvent, EventInstigator, DamageCauser);
	Health = FMath::Clamp(Health - Applied, 0.f, MaxHealth);
	OnRep_Health(); // 서버/싱글 경로 — 복제 OnRep은 원격 클라 전용이라 직접 호출

	if (Health <= 0.f && !bIsDead)
	{
		bIsDead = true;
		UE_LOG(LogTemp, Log, TEXT("[RE] Boss died (Health<=0)"));
		if (AREGameMode* GM = GetWorld()->GetAuthGameMode<AREGameMode>())
		{
			GM->EndGame(/*bVictory=*/true);
		}
	}

	return Applied;
}

void AREBossCharacter::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AREBossCharacter, Health);
}

void AREBossCharacter::OnRep_Health()
{
	if (HealthBar)
	{
		HealthBar->SetHealthPercent(MaxHealth > 0.f ? Health / MaxHealth : 0.f);
	}
}
