// Copyright Epic Games, Inc. All Rights Reserved.

#include "REBossCharacter.h"
#include "REBulletSpawnSubsystem.h"
#include "REBulletPatternGenerator.h"
#include "Net/UnrealNetwork.h"
#include "REHealthBarComponent.h"
#include "REGameMode.h"
#include "RECharacterBase.h"
#include "HAL/IConsoleManager.h"
#include "REBulletRenderSubsystem.h"                        // 라이브 카운트(ISM) 조회 (#51)
#include "ProfilingDebugging/CsvProfiler.h"                 // 채움 완료 시점에 캡처 시작 신호 (#88)
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Animation/AnimSequence.h"
#include "Materials/MaterialInstanceDynamic.h"
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
	3.6f,     // 무차원 루프게인(코드에서 N^2 으로 나눈다, N=수명/발사간격). 설정을 바꿔도 안정성 불변.
	          // 3.6 = 07-16 실측 확정값 0.004 를 당시 설정(3.0/0.1, N=30)에서 환산한 것: 30^2*0.004.
	          // 당시 측정: 1000/5000 모두 ±0.5%, 100 -6%(짧은 창). 배증(7.2)은 진동.
	TEXT("스폰 적분 루프게인(무차원). 라이브 카운트 목표 수렴 속도/안정성."),
	ECVF_Cheat);

AREBossCharacter::AREBossCharacter()
{
	// 틱은 외관 램프(#130) 전용이라 기본 정지 — BeginPlay가 비-데디에서만 켠다.
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = false;

	// 체력 초기화 — Settings 단일 출처 (M3.5 ③, RECharacterBase 동일 패턴).
	MaxHealth = GetDefault<UREStatsSettings>()->BossMaxHealth;
	Health = MaxHealth;

	// HP바 (#29) — 보스 빨강.
	HealthBar = CreateDefaultSubobject<UREHealthBarComponent>(TEXT("HealthBar"));
	HealthBar->SetupAttachment(RootComponent);
	HealthBar->BarColor = FLinearColor::Red;

	// 보스 HP바는 골렘 머리 위로 올린다 — 컴포넌트 기본값(Z 120)은 사람 키 기준이라
	// 골렘(높이 328) 가슴에 박힌다. 컴포넌트는 플레이어와 공유하므로 여기서만 덮는다.
	HealthBar->SetRelativeLocation(FVector(0.f, 0.f, 270.f));

	// 보스 가시화 (M3.5 ②, #118에서 Quinn → Stone Golem) — 실패해도 크래시 없이 진행.
	// 스케일은 건드리지 않는다: 원저작 높이 328uu 가 캡슐(HalfHeight 88 → 176uu)의 약 1.9배라
	// 그대로 두면 의도한 "캡슐보다 큰 보스"가 된다. 캡슐은 불변이다 — 자동사격 트레이스 Z+20,
	// 스폰 좌표 (600,0,90), 곡사탄 착지 평면이 전부 이 지오메트리에서 파생됐다(#54/#55/#56/#57).
	// 판정은 캡슐이 하므로 메시가 더 커도 `hit boss` 게이트는 영향받지 않는다.
	static ConstructorHelpers::FObjectFinder<USkeletalMesh> MeshAsset(
		TEXT("/Game/Stone_Golem/mesh/SKM_Stone_Golem.SKM_Stone_Golem"));
	if (MeshAsset.Succeeded())
	{
		GetMesh()->SetSkeletalMesh(MeshAsset.Object);
		GetMesh()->SetRelativeLocation(FVector(0.f, 0.f, -90.f));   // 메시 원점=발 → 캡슐 바닥에 접지
		// 마네킹에서 물려받은 -90은 골렘을 화면 위쪽으로 돌려세웠다. 보스는 화면 아래
		// (플레이어 스폰 방향)를 봐야 하므로 180도 돌린다.
		GetMesh()->SetRelativeRotation(FRotator(0.f, 90.f, 0.f));
	}

	// 로코모션 — 고정형 보스라 idle 재생이 목적. 애님BP를 쓰지 않는다:
	// Stone Golem은 자체 스켈레톤(SK_Stone_Golem_Skeleton)이라 플레이어와 공유하던
	// ABP_Unarmed가 붙지 않고, 팩 애님이 이 스켈레톤에 네이티브로 붙어 리타겟이 불필요하다.
	static ConstructorHelpers::FObjectFinder<UAnimSequence> IdleAsset(
		TEXT("/Game/Stone_Golem/demo/animations/ThirdPersonIdle.ThirdPersonIdle"));
	if (IdleAsset.Succeeded())
	{
		IdleAnim = IdleAsset.Object;
	}
	static ConstructorHelpers::FObjectFinder<UAnimSequence> LeapAsset(
		TEXT("/Game/Stone_Golem/demo/animations/ThirdPersonJump_Start.ThirdPersonJump_Start"));
	if (LeapAsset.Succeeded())
	{
		LeapAnim = LeapAsset.Object;
	}
}

void AREBossCharacter::BeginPlay()
{
	Super::BeginPlay();

	// 데디 서버는 화면이 없다 — MID도 애님도 코스메틱이라 통째로 생략한다.
	// BodyMID가 nullptr로 남고 틱도 꺼진 채라 외관 경로가 전부 자연 no-op이 된다.
	if (IsNetMode(NM_DedicatedServer))
	{
		return;
	}

	// 슬롯 0 단일 — SKM_Stone_Golem은 머티리얼 슬롯이 하나다.
	BodyMID = GetMesh() ? GetMesh()->CreateDynamicMaterialInstance(0) : nullptr;
	PlayIdle();

	// 시작 외관은 램프 없이 즉시 — 첫 페이즈 전에는 전환할 이전 상태가 없다.
	LookFrom = LookTo = LookForPattern(LookPattern);
	LookAlpha = 1.f;
	ApplyLook(LookTo);

	SetActorTickEnabled(true);   // 외관 램프 전용 (데디는 위에서 반환)
}

AREBossCharacter::FBossLook AREBossCharacter::LookForPattern(EBulletPattern Pattern)
{
	FBossLook Look;   // 기본값 = Spiral(회색 화강암 + 팩 기본 빨강 이미시브)
	switch (Pattern)
	{
	case EBulletPattern::Fan:
		// Snow 스칼라만으로는 하얘지지 않는다 — 그 값은 균열 이미시브를 증폭할 뿐이고
		// 색은 Color Emis 가 쥔다. 안 덮으면 적열이 되어 빨강+흰색 탄막에 섞이고
		// 주황 용암(Artillery)과도 계열이 겹친다. 청록으로 가른다.
		Look.Snow = FanSnowAmount;
		Look.Emis = FLinearColor(0.15f, 0.70f, 1.0f, 1.0f);
		break;
	case EBulletPattern::Artillery:
		Look.Lava = ArtilleryLavaAmount;
		break;
	default: break;
	}
	return Look;
}

void AREBossCharacter::ApplyLook(const FBossLook& Look)
{
	if (!BodyMID)
	{
		return;
	}
	BodyMID->SetScalarParameterValue(TEXT("Snow"), Look.Snow);
	BodyMID->SetScalarParameterValue(TEXT("Lava"), Look.Lava);
	BodyMID->SetVectorParameterValue(TEXT("Color Emis"), Look.Emis);
}

void AREBossCharacter::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (LookAlpha >= 1.f)
	{
		return;   // 전환 완료 — 매 프레임 같은 값을 다시 쓰지 않는다
	}

	LookAlpha = FMath::Min(1.f, LookAlpha + DeltaSeconds / LookIntroSec);
	FBossLook Now;
	Now.Snow = FMath::Lerp(LookFrom.Snow, LookTo.Snow, LookAlpha);
	Now.Lava = FMath::Lerp(LookFrom.Lava, LookTo.Lava, LookAlpha);
	Now.Emis = FMath::Lerp(LookFrom.Emis, LookTo.Emis, LookAlpha);
	ApplyLook(Now);
}

void AREBossCharacter::PlayIdle()
{
	if (IdleAnim && GetMesh())
	{
		GetMesh()->PlayAnimation(IdleAnim, /*bLooping=*/true);
	}
}

void AREBossCharacter::PlayLeap()
{
	if (!LeapAnim || !GetMesh() || IsNetMode(NM_DedicatedServer))
	{
		return;
	}

	GetMesh()->PlayAnimation(LeapAnim, /*bLooping=*/false);
	// single-node에는 상태기계가 없다 — 재생 길이만큼 뒤에 손으로 idle로 되돌린다.
	GetWorldTimerManager().SetTimer(LeapTimer, this, &AREBossCharacter::PlayIdle,
		LeapAnim->GetPlayLength(), /*bLoop=*/false);
}

void AREBossCharacter::StartPatternLook(EBulletPattern Pattern)
{
	if (!BodyMID || Pattern == LookPattern)
	{
		return;   // 데디 서버(MID 없음) 또는 이미 그 외관 — 램프를 다시 시작하지 않는다
	}

	// 진행 중이던 램프의 현재 값을 시작점으로 굳힌다. 페이즈가 램프보다 빨리 바뀌어도
	// 색이 이전 목표로 튀지 않고 보이던 자리에서 이어진다.
	FBossLook Now;
	Now.Snow = FMath::Lerp(LookFrom.Snow, LookTo.Snow, LookAlpha);
	Now.Lava = FMath::Lerp(LookFrom.Lava, LookTo.Lava, LookAlpha);
	Now.Emis = FMath::Lerp(LookFrom.Emis, LookTo.Emis, LookAlpha);

	LookPattern = Pattern;
	LookFrom = Now;
	LookTo = LookForPattern(Pattern);
	LookAlpha = 0.f;
}

void AREBossCharacter::Multicast_BeginPhaseLook_Implementation(EBulletPattern Pattern)
{
	StartPatternLook(Pattern);

	// Artillery만 도약으로 예고한다. 발사는 이 인트로가 끝난 뒤 시작한다(BeginPhase).
	if (Pattern == EBulletPattern::Artillery)
	{
		PlayLeap();
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

	// 외관/애님 인트로를 페이즈 시작에 알린다 (#130). 발사 Multicast가 패턴을 싣고 있지만
	// 그건 첫 탄이 나간 뒤에야 도착한다 — "전환이 끝난 뒤 발사"를 하려면 전환 시작을
	// 따로 알려야 한다. 페이즈당 1회라 대역폭은 무시할 수준.
	Multicast_BeginPhaseLook(CurrentPhasePattern);

	// 첫 발사를 인트로 뒤로 민다. PhaseSec은 '발사 구간' 길이라 인트로만큼 늘려
	// 페이즈당 발사 시간을 보존한다 — 안 늘리면 패턴마다 볼리가 줄어든다.
	GetWorldTimerManager().SetTimer(FireTimer, this,
		&AREBossCharacter::FireCurrentPattern, FireInterval, /*bLoop=*/true, /*InFirstDelay=*/LookIntroSec);
	GetWorldTimerManager().SetTimer(PhaseTimer, this,
		&AREBossCharacter::EndPhase, PhaseSec + LookIntroSec, /*bLoop=*/false);
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
		// 최근접 생존자를 조준한다 (#85). 전원 사망이면 0° 폴백 — 곧 EndGame이 발사를 끊는다.
		if (const APawn* Target = FindNearestLivingPlayerPawn())
		{
			const FVector D = Target->GetActorLocation() - GetActorLocation();
			AngleDeg = FMath::RadiansToDegrees(FMath::Atan2(D.Y, D.X));
		}
	}

	Multicast_FireDirect(CurrentPhasePattern, GetActorLocation(), AngleDeg, Count, GetServerNow());
}

const APawn* AREBossCharacter::FindNearestLivingPlayerPawn() const
{
	const UWorld* W = GetWorld();
	if (!W)
	{
		return nullptr;
	}
	const FVector BossLoc = GetActorLocation();
	const APawn* Best = nullptr;
	float BestDistSq = TNumericLimits<float>::Max();

	for (FConstPlayerControllerIterator It = W->GetPlayerControllerIterator(); It; ++It)
	{
		if (!It->IsValid())
		{
			continue;
		}
		const ARECharacterBase* P = Cast<ARECharacterBase>(It->Get()->GetPawn());
		if (!P || !P->IsAlive())
		{
			continue;   // 사망자를 빼지 않으면 시체를 조준한다
		}
		const float D = FVector::DistSquared2D(P->GetActorLocation(), BossLoc);
		if (D < BestDistSq)
		{
			BestDistSq = D;
			Best = P;
		}
	}
	return Best;
}

void AREBossCharacter::FireArtillery()
{
	if (bIsDead)
	{
		return;
	}

	const FVector BossLoc = GetActorLocation();

	// 조준점. 폰 없으면 보스 앞쪽 폴백. 클라는 이 값을 유도할 수 없다 → 페이로드로 보낸다.
	// 최근접 생존자를 조준한다 (#85). 전원 사망이면 보스 앞쪽 폴백.
	FVector PlayerLoc = BossLoc + FVector(300.f, 0.f, 0.f);
	if (const APawn* P = FindNearestLivingPlayerPawn())
	{
		PlayerLoc = P->GetActorLocation();
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

	// 지연 보정 — 이미 착지한 탄은 스폰하지 않는다. 착지점 생성·난수 뽑기보다 먼저 검사해 헛수고를 막는다.
	const float Elapsed = GetElapsedSince(ServerTime);
	if (Elapsed >= ArtilleryFlightTime)
	{
		UE_LOG(LogTemp, Log, TEXT("[RE] Boss FireArtillery: skipped (Elapsed=%.3f >= FlightTime=%.2f)"),
			Elapsed, ArtilleryFlightTime);
		return;
	}

	// 외관 보정 — 페이즈 도중 접속한 클라 따라잡기용. 도약은 페이즈 시작에서만 재생한다.
	StartPatternLook(EBulletPattern::Artillery);

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
	return GS ? static_cast<float>(GS->GetServerWorldTimeSeconds()) : 0.f;
}

float AREBossCharacter::GetElapsedSince(float ServerTime) const
{
	// 접속 직후 GameState 복제 전이면 GetServerWorldTimeSeconds가 0을 반환할 수 있다 →
	// ServerTime을 그대로 빼면 큰 음수가 나오므로 하한 0으로 막는다(보정 없음으로 폴백).
	// 상한 1초 — 클라 시각 추정치가 아직 EMA 수렴 중이거나 서버 재시작으로 시각이 리셋된
	// 직후면 큰 양수가 나올 수 있다. 막지 않으면 볼리의 모든 탄이 "이미 수명 다함"으로
	// 스킵되어 N=0으로 조용히 빈 화면이 된다 — 이 브랜치가 고친 원래 버그와 같은 증상이다.
	return FMath::Clamp(GetServerNow() - ServerTime, 0.f, 1.f);
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
			//
			// 1수명당 발사 수 N = 이 루프의 데드타임(발사한 탄이 소멸로 되돌아오기까지 걸리는 샷 수)이자
			// 스폰율→라이브 카운트의 정상상태 이득(live = rate * N)이다. 그래서 원시 게인의 루프게인은
			// N^2 에 비례해 커진다 — Ki 를 N^2 으로 나눠 무차원화해야 설정과 무관하게 안정성이 고정된다.
			// 안 하면 Lifetime/Interval 을 바꾸는 순간 조용히 진동한다: 실제로 3.0/0.1(N=30, 루프게인 3.6)
			// 에서 튜닝한 값이 15.0/0.15(N=100) 로 바뀌며 루프게인 40 이 되어 388~1802 리밋사이클에 빠졌고,
			// 프로파일 측정이 통째로 무의미해졌다 (#88).
			const float ShotsPerLife = REBulletPattern::BulletLifetimeSec() / REBulletPattern::FireIntervalSec();
			const int32 FillShots = FMath::CeilToInt(ShotsPerLife);
			if (SpiralShotCount < FillShots)
			{
				SpiralSpawnRate = FeedFwd;
			}
			else
			{
				// 채움 완료 = 정상상태 진입. 프로파일 캡처는 이 이벤트에서 시작한다(-csvStartOnEvent, #88).
				// SpiralShotCount 는 매 호출 증가하므로 정확히 한 번만 발화한다.
				if (SpiralShotCount == FillShots)
				{
					CSV_EVENT_GLOBAL(TEXT("REBulletsFilled"));
				}
				SpiralSpawnRate += CVarSpawnKi.GetValueOnGameThread() / (ShotsPerLife * ShotsPerLife) * (TargetLive - CurrentLive);
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
	// 외관 보정 — 정상 경로에서는 Multicast_BeginPhaseLook이 이미 램프를 걸어 no-op다.
	// 페이즈 도중 접속한 클라만 여기서 따라잡는다.
	StartPatternLook(Pattern);

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
		REBulletPattern::FSpiralParams SP = REBulletPattern::MakeSpiralRing(Count, AngleDeg);
		// 발사 회차 패리티를 ServerTime 에서 뽑는다 — 이 함수는 서버와 클라 양쪽에서
		// 같은 ServerTime 으로 실행되므로 별도 복제 없이 색이 일치한다 (#97).
		// 카운터를 따로 두면 멀티캐스트 유실 시 클라마다 색이 어긋난다.
		const float Interval = REBulletPattern::FireIntervalSec();
		SP.ShotParity = (Interval > 0.f) ? (FMath::FloorToInt(ServerTime / Interval) & 1) : 0;
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
