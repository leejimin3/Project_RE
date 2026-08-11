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
#include "GameFramework/GameStateBase.h"

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

	// 완전 랜덤 로테이션. 같은 패턴 2연속 금지, 첫 페이즈 무제약(Spiral 고정 없음).
	// enum 순서(Spiral=0,Fan=1,Homing=2,Artillery=3)와 로테이션 인덱스가 다르므로 풀 배열로 매핑.
	// Homing은 백로그 스텁이라 풀에서 제외.
	static const EBulletPattern Pool[3] = {
		EBulletPattern::Spiral, EBulletPattern::Fan, EBulletPattern::Artillery };
	EBulletPattern NewPattern;
	do
	{
		NewPattern = Pool[PhaseRng.RandRange(0, 2)];
	} while (!bFirstPhase && NewPattern == CurrentPhasePattern);
	bFirstPhase = false;
	CurrentPhasePattern = NewPattern;
	if (CurrentPhasePattern == EBulletPattern::Artillery)
	{
		CurrentArtilleryShape = (EArtilleryShape)PhaseRng.RandRange(
			(int32)EArtilleryShape::Ring, (int32)EArtilleryShape::Random);
	}

	float PhaseSec = SpiralPhaseSec;
	float FireInterval = REBulletPattern::FireIntervalSec();
	const TCHAR* PhaseName = TEXT("Spiral");
	switch (CurrentPhasePattern)
	{
	case EBulletPattern::Fan:
		PhaseSec = FanPhaseSec; FireInterval = FanFireIntervalSec; PhaseName = TEXT("Fan"); break;
	case EBulletPattern::Artillery:
		PhaseSec = ArtilleryPhaseSec; FireInterval = ArtilleryFireInterval; PhaseName = TEXT("Artillery"); break;
	default: break;   // Spiral 기본값
	}

	UE_LOG(LogTemp, Log, TEXT("[RE] Boss Phase: %s %.1fs"), PhaseName, PhaseSec);

	GetWorldTimerManager().SetTimer(FireTimer, this,
		&AREBossCharacter::FireCurrentPattern, FireInterval, /*bLoop=*/true);
	GetWorldTimerManager().SetTimer(PhaseTimer, this,
		&AREBossCharacter::EndPhase, PhaseSec, /*bLoop=*/false);
}

void AREBossCharacter::FireCurrentPattern()
{
	if (bIsDead)
	{
		return;
	}
	if (CurrentPhasePattern == EBulletPattern::Artillery)
	{
		FireArtillery();
		return;
	}
	if (CurrentPhasePattern == EBulletPattern::Homing)
	{
		UE_LOG(LogTemp, Warning, TEXT("[RE] Boss: Homing 미구현 (#67)"));
		return;
	}

	// 여기까지가 서버 전용 결정이다. 클라는 로테이션도 PhaseRng도 돌리지 않는다 (#84).
	float AngleDeg = 0.f;
	int32 Count    = 0;

	if (CurrentPhasePattern == EBulletPattern::Spiral)
	{
		Count    = ResolveSpiralCount();
		AngleDeg = SpiralBaseAngleDeg;
		SpiralBaseAngleDeg += SpiralRotationStepDeg;   // 다음 발사에 회전
	}
	else   // Fan
	{
		Count = REBulletPattern::FFanParams().Count;
		// 플레이어 방향 조준. 폰 없으면 0°(기존 기본) 폴백.
		// 클라는 이 각을 유도할 수 없다(복제 위치가 서버와 다름) → 페이로드로 보낸다.
		// TODO: 멀티는 타깃 선택 정책 필요 — 지금은 첫 플레이어 고정 (#85).
		if (const APlayerController* PC = GetWorld()->GetFirstPlayerController())
		{
			if (const APawn* Target = PC->GetPawn())
			{
				const FVector D = Target->GetActorLocation() - GetActorLocation();
				AngleDeg = FMath::RadiansToDegrees(FMath::Atan2(D.Y, D.X));
			}
		}
	}

	Multicast_FireDirect(CurrentPhasePattern, GetActorLocation(), AngleDeg, Count, GetServerNow());
}

void AREBossCharacter::FireArtillery()
{
	if (bIsDead)
	{
		return;
	}

	const FVector BossLoc = GetActorLocation();

	// 조준점. 폰 없으면 보스 앞쪽 폴백. 클라는 이 값을 유도할 수 없다 → 페이로드로 보낸다.
	// TODO: 멀티는 타깃 선택 정책 필요 — 지금은 첫 플레이어 고정 (#85).
	FVector PlayerLoc = BossLoc + FVector(300.f, 0.f, 0.f);
	if (const APlayerController* PC = GetWorld()->GetFirstPlayerController())
	{
		if (const APawn* P = PC->GetPawn())
		{
			PlayerLoc = P->GetActorLocation();
		}
	}

	// Random 모양 전용 시드. 서버 스트림에서 1회 뽑아 넘긴다 —
	// 클라는 PhaseRng가 없으므로 이 시드로 로컬 스트림을 만들어 같은 착지점을 얻는다.
	const int32 CallSeed = (int32)PhaseRng.GetUnsignedInt();

	Multicast_FireArtillery(CurrentArtilleryShape, BossLoc, PlayerLoc, CallSeed, GetServerNow());
}

void AREBossCharacter::Multicast_FireArtillery_Implementation(EArtilleryShape Shape, FVector_NetQuantize Origin,
                                                              FVector_NetQuantize AimLoc, int32 CallSeed, float ServerTime)
{
	UREBulletSpawnSubsystem* Spawner = GetWorld() ? GetWorld()->GetSubsystem<UREBulletSpawnSubsystem>() : nullptr;
	if (!Spawner)
	{
		UE_LOG(LogTemp, Warning, TEXT("[RE] Boss::FireArtillery: Spawner NULL"));
		return;
	}

	const FVector BossLoc  = Origin;
	const FVector PlayerLoc = AimLoc;
	const float   GroundZ  = BossLoc.Z + MarkerGroundOffset;   // 착지 평면(보스 캡슐 바닥 근사)

	// 서버가 넘긴 시드로 만든 로컬 스트림 — 양쪽이 같은 난수열을 본다.
	FRandomStream CallRng(CallSeed);

	TArray<FVector> Targets;
	switch (Shape)
	{
	case EArtilleryShape::Ring:
		Targets = REBulletPattern::GenRing(BossLoc, /*Radius=*/500.f, ArtilleryCount, GroundZ);
		break;
	case EArtilleryShape::Line:
		Targets = REBulletPattern::GenLine(BossLoc, PlayerLoc, /*WallLen=*/900.f, ArtilleryCount, GroundZ);
		break;
	case EArtilleryShape::Grid:
		Targets = REBulletPattern::GenGrid(BossLoc, /*ExtentX=*/600.f, /*ExtentY=*/600.f, /*Cols=*/4, /*Rows=*/3, GroundZ);
		break;
	case EArtilleryShape::Spiral:
		Targets = REBulletPattern::GenArcSpiral(BossLoc, /*MaxRadius=*/600.f, ArtilleryCount, GroundZ);
		break;
	case EArtilleryShape::PlayerAimed:
		Targets = REBulletPattern::GenPlayerCluster(PlayerLoc, /*ClusterRadius=*/150.f, /*RingN=*/4, GroundZ);
		break;
	case EArtilleryShape::Random:
		Targets = REBulletPattern::GenRandom(BossLoc, /*ArenaRadius=*/800.f, ArtilleryCount, CallRng, GroundZ);
		break;
	}

	// 지연 보정 — 이미 착지한 탄은 스폰하지 않는다.
	const float Elapsed = GetElapsedSince(ServerTime);
	if (Elapsed >= ArtilleryFlightTime)
	{
		UE_LOG(LogTemp, Log, TEXT("[RE] Boss FireArtillery: skipped (Elapsed=%.3f >= FlightTime=%.2f)"),
			Elapsed, ArtilleryFlightTime);
		return;
	}

	TArray<REBulletPattern::FArcBulletSpawnParams> Shots;
	Shots.Reserve(Targets.Num());
	for (const FVector& T : Targets)
	{
		REBulletPattern::FArcBulletSpawnParams P;
		P.Start      = BossLoc;
		P.Target     = T;
		P.FlightTime = ArtilleryFlightTime;
		P.MaxHeight  = ArtilleryMaxHeight;
		P.Damage     = ArtilleryDamage;
		P.Radius     = ArtilleryRadius;
		P.Elapsed    = Elapsed;
		Shots.Add(P);
	}
	Spawner->SpawnArcBulletBatch(Shots);

	UE_LOG(LogTemp, Log, TEXT("[RE] Boss FireArtillery: Shape=%d N=%d Elapsed=%.3f role=%s"),
		(int32)Shape, Shots.Num(), Elapsed, *UEnum::GetValueAsString(GetLocalRole()));
}

void AREBossCharacter::EndPhase()
{
	GetWorldTimerManager().ClearTimer(FireTimer);
	UE_LOG(LogTemp, Log, TEXT("[RE] Boss Phase: Rest %.1fs"), RestSec);
	GetWorldTimerManager().SetTimer(PhaseTimer, this,
		&AREBossCharacter::BeginPhase, RestSec, /*bLoop=*/false);
}

float AREBossCharacter::GetServerNow() const
{
	const AGameStateBase* GS = GetWorld() ? GetWorld()->GetGameState() : nullptr;
	return GS ? GS->GetServerWorldTimeSeconds() : 0.f;
}

float AREBossCharacter::GetElapsedSince(float ServerTime) const
{
	// 접속 직후 GameState 복제 전이면 GetServerWorldTimeSeconds가 0을 반환할 수 있다 →
	// ServerTime을 그대로 빼면 큰 음수가 나오므로 Max로 막는다(보정 없음으로 폴백).
	return FMath::Max(0.f, GetServerNow() - ServerTime);
}

int32 AREBossCharacter::ResolveSpiralCount()
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
	return Count;
}

void AREBossCharacter::Multicast_FireDirect_Implementation(EBulletPattern Pattern, FVector_NetQuantize Origin,
                                                           float AngleDeg, int32 Count, float ServerTime)
{
	UREBulletSpawnSubsystem* Spawner = GetWorld() ? GetWorld()->GetSubsystem<UREBulletSpawnSubsystem>() : nullptr;
	if (!Spawner)
	{
		UE_LOG(LogTemp, Warning, TEXT("[RE] Boss::FireDirect: UREBulletSpawnSubsystem NULL"));
		return;
	}

	// Origin을 쓴다 — GetActorLocation()이 아니다. 클라의 보스 위치는 복제 지연으로 서버와 다를 수 있다.
	TArray<FBulletSpawnParams> Params;
	if (Pattern == EBulletPattern::Spiral)
	{
		const REBulletPattern::FSpiralParams SP = REBulletPattern::MakeSpiralRing(Count, AngleDeg);
		Params = REBulletPattern::GenerateSpiral(Origin, SP);
	}
	else if (Pattern == EBulletPattern::Fan)
	{
		REBulletPattern::FFanParams FP;
		FP.Count          = Count;
		FP.CenterAngleDeg = AngleDeg;
		Params = REBulletPattern::GenerateFan(Origin, FP);
	}
	else
	{
		return;
	}

	// 지연 보정. 서버는 발사 시각이 곧 현재라 Elapsed≈0 → 같은 코드가 무보정으로 동작한다.
	const float Elapsed = GetElapsedSince(ServerTime);
	if (Elapsed > 0.f)
	{
		for (int32 i = Params.Num() - 1; i >= 0; --i)
		{
			if (Elapsed >= Params[i].Lifetime)
			{
				Params.RemoveAtSwap(i);   // 이미 수명이 다한 탄 — 스폰하지 않는다
				continue;
			}
			Params[i].Location += Params[i].Velocity * Elapsed;
			Params[i].Lifetime -= Elapsed;
		}
	}

	Spawner->SpawnBulletBatch(Params);

	// role이 판정의 핵심 신호다 — 서버=ROLE_Authority, 클라=ROLE_SimulatedProxy 양쪽에 찍혀야 한다.
	UE_LOG(LogTemp, Log, TEXT("[RE] Boss FireDirect: Pattern=%d Angle=%.1f N=%d Elapsed=%.3f role=%s"),
		(int32)Pattern, AngleDeg, Params.Num(), Elapsed, *UEnum::GetValueAsString(GetLocalRole()));
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
