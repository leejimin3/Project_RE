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

/**
 *  검증/튜닝용 패턴 고정. -1(기본) = 정상 랜덤 로테이션.
 *  인덱스는 BeginPhase 의 Pool 순서다(enum 순서가 아니다) — 전체 목록은 아래 헬프 문자열에 있다.
 *  (Homing 은 백로그 스텁이라 Pool 에 없다.)
 *  페이즈 진입 시점 조회 — 재시작 없이 다음 페이즈부터 반영된다.
 */
static TAutoConsoleVariable<int32> CVarBossPattern(
	TEXT("re.Debug.BossPattern"),
	-1,
	TEXT("검증용: -1=정상 로테이션, 0=Spiral 1=Fan 2=Artillery 3=ArtilleryStorm 4=RoseEnvelope "
	     "5=Cardioid 6=LissajousStorm 7=BezierVortex "
	     "8=StarBloom 9=LemniscateBloom 10=SuperformulaBloom "
	     "11=RoseField 12=AerialDome 13=Spirograph 14=MicroMissile 고정."),
	ECVF_Cheat);

namespace REBoss
{
	/**
	 *  곡사(포물선 착지 + 마커) 계열인가. 발사 라우팅과 RPC 구현 양쪽이 이걸로 갈린다 —
	 *  두 곳에 각각 나열하면 패턴을 늘릴 때 한쪽만 고쳐 조용히 어긋난다.
	 */
	static bool IsArcPattern(EBulletPattern P)
	{
		return P == EBulletPattern::Artillery
			|| P == EBulletPattern::ArtilleryStorm
			|| P == EBulletPattern::LissajousStorm
			|| P == EBulletPattern::BezierVortex
			|| P == EBulletPattern::RoseField
			|| P == EBulletPattern::AerialDome
			|| P == EBulletPattern::Spirograph
			|| P == EBulletPattern::MicroMissile;
	}

	/**
	 *  곡선 블룸 계열인가. 발사 입력(각 미사용·발수 상수)과 클라 생성 분기가 이걸로 갈린다.
	 */
	static bool IsBloomPattern(EBulletPattern P)
	{
		return P == EBulletPattern::StarBloom
			|| P == EBulletPattern::LemniscateBloom
			|| P == EBulletPattern::SuperformulaBloom;
	}
}

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
	case EBulletPattern::ArtilleryStorm:
		// 용암은 Artillery 와 같은 값이라 그것만으론 두 곡사 페이즈가 구분되지 않는다.
		// 이미시브를 금색으로 올려 가른다 — Fan(청록)/Spiral(빨강)과도 안 겹친다.
		Look.Lava = StormLavaAmount;
		Look.Emis = FLinearColor(1.0f, 0.85f, 0.2f, 1.0f);
		break;
	case EBulletPattern::RoseEnvelope:
		// 질감 축(Snow/Lava)에는 남는 조합이 없다 — Fan 이 Snow, 곡사 둘이 Lava 를 쓴다.
		// Spiral 과 같은 화강암에 이미시브만 보라로 가른다(빨강/청록/주황/금색과 안 겹친다).
		Look.Emis = FLinearColor(0.70f, 0.10f, 1.0f, 1.0f);
		break;
	//~ 아래 다섯은 (질감, 색) 쌍이 위와 겹치지 않도록 배분했다 — 색만으로는 10개가 안 갈린다.
	case EBulletPattern::Cardioid:
		// 색만 주황으로 가르면 Spiral(회색 화강암 + 빨강)과 화면에서 잘 안 갈렸다 —
		// 실제로 그랬다. 질감까지 흰 화강암으로 바꾼다(흰 화강암 + 주황은 남는 조합이다).
		Look.Snow = CardioidSnowAmount;
		Look.Emis = FLinearColor(1.0f, 0.45f, 0.05f, 1.0f);
		break;
	case EBulletPattern::LissajousStorm:
		Look.Lava = LissaLavaAmount;                          // 용암 + 연두
		Look.Emis = FLinearColor(0.55f, 1.0f, 0.20f, 1.0f);
		break;
	case EBulletPattern::BezierVortex:
		Look.Snow = VortexSnowAmount;                         // 흰 화강암 + 진파랑
		Look.Emis = FLinearColor(0.10f, 0.25f, 1.0f, 1.0f);
		break;
	//~ 블룸 3종은 질감을 안 쓴다(회색 화강암) — 색만으로 가른다. 위에서 회색 화강암을 쓰는 건
	//  Spiral(빨강)과 Rose(보라)뿐이라 아래 셋과 안 겹친다.
	case EBulletPattern::StarBloom:
		Look.Emis = FLinearColor(1.0f, 0.90f, 0.40f, 1.0f);   // 회색 화강암 + 담금색
		break;
	case EBulletPattern::LemniscateBloom:
		Look.Emis = FLinearColor(0.10f, 0.90f, 0.90f, 1.0f);  // 회색 화강암 + 청록
		break;
	case EBulletPattern::SuperformulaBloom:
		Look.Emis = FLinearColor(0.30f, 1.0f, 0.40f, 1.0f);   // 회색 화강암 + 연녹
		break;
	case EBulletPattern::RoseField:
		Look.Lava = RoseFieldLavaAmount;                      // 용암 + 진파랑
		Look.Emis = FLinearColor(0.20f, 0.35f, 1.0f, 1.0f);
		break;
	case EBulletPattern::AerialDome:
		Look.Snow = DomeSnowAmount;                           // 흰 화강암 + 자홍
		Look.Emis = FLinearColor(1.0f, 0.20f, 0.70f, 1.0f);
		break;
	case EBulletPattern::MicroMissile:
		Look.Snow = MicroSnowAmount;                          // 흰 화강암 + 경고등 빨강
		Look.Emis = FLinearColor(1.0f, 0.15f, 0.10f, 1.0f);
		break;
	case EBulletPattern::Spirograph:
		Look.Lava = SpiroLavaAmount;                          // 용암 + 청록
		Look.Emis = FLinearColor(0.15f, 0.80f, 0.95f, 1.0f);
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

	// 곡사 계열만 도약으로 예고한다. 발사는 이 인트로가 끝난 뒤 시작한다(BeginPhase).
	if (REBoss::IsArcPattern(Pattern))
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
	static const EBulletPattern Pool[15] = {
		EBulletPattern::Spiral, EBulletPattern::Fan, EBulletPattern::Artillery,
		EBulletPattern::ArtilleryStorm, EBulletPattern::RoseEnvelope, EBulletPattern::Cardioid,
		EBulletPattern::LissajousStorm, EBulletPattern::BezierVortex,
		EBulletPattern::StarBloom, EBulletPattern::LemniscateBloom, EBulletPattern::SuperformulaBloom,
		EBulletPattern::RoseField, EBulletPattern::AerialDome, EBulletPattern::Spirograph,
		EBulletPattern::MicroMissile };
	EBulletPattern NewPattern;
	const int32 Forced = CVarBossPattern.GetValueOnGameThread();
	if (Forced >= 0)
	{
		// 검증/튜닝 경로 — 로테이션도 no-repeat도 건너뛴다. PhaseRng는 뽑지 않는다
		// (Artillery의 CallSeed가 같은 스트림을 쓰므로 여기서 뽑으면 고정 모드마다 수열이 달라진다).
		NewPattern = Pool[FMath::Clamp(Forced, 0, UE_ARRAY_COUNT(Pool) - 1)];
	}
	else
	{
		do
		{
			NewPattern = Pool[PhaseRng.RandRange(0, UE_ARRAY_COUNT(Pool) - 1)];
		} while (!bFirstPhase && NewPattern == CurrentPhasePattern);
	}
	bFirstPhase = false;
	CurrentPhasePattern = NewPattern;
	if (CurrentPhasePattern == EBulletPattern::Artillery)
	{
		CurrentArtilleryShape = (EArtilleryShape)PhaseRng.RandRange(
			(int32)EArtilleryShape::Ring, (int32)EArtilleryShape::Random);
	}
	else if (CurrentPhasePattern == EBulletPattern::ArtilleryStorm)
	{
		// 폭풍의 착지 모양은 시간의 함수라 Shape 열거로 표현되지 않는다 — 로그 표기용으로만 고정한다.
		CurrentArtilleryShape = EArtilleryShape::Spiral;
		PhaseVolleyIdx = 0;   // 스윕은 페이즈마다 안쪽에서 다시 시작한다
	}
	else if (CurrentPhasePattern == EBulletPattern::MicroMissile)
	{
		PhaseVolleyIdx = 0;   // 방향 순환도 페이즈마다 동쪽에서 다시 시작한다
	}

	float PhaseSec = SpiralPhaseSec;
	const float FireInterval = FireIntervalFor(CurrentPhasePattern);   // 단일 출처
	const TCHAR* PhaseName = TEXT("Spiral");
	switch (CurrentPhasePattern)
	{
	case EBulletPattern::Fan:
		PhaseSec = FanPhaseSec; PhaseName = TEXT("Fan"); break;
	case EBulletPattern::Artillery:
		PhaseSec = ArtilleryPhaseSec; PhaseName = TEXT("Artillery"); break;
	case EBulletPattern::ArtilleryStorm:
		PhaseSec = StormPhaseSec; PhaseName = TEXT("ArtilleryStorm"); break;
	case EBulletPattern::RoseEnvelope:
		PhaseSec = RosePhaseSec; PhaseName = TEXT("RoseEnvelope"); break;
	case EBulletPattern::Cardioid:
		PhaseSec = CardioidPhaseSec; PhaseName = TEXT("Cardioid"); break;
	case EBulletPattern::LissajousStorm:
		PhaseSec = LissaPhaseSec; PhaseName = TEXT("LissajousStorm"); break;
	case EBulletPattern::BezierVortex:
		PhaseSec = VortexPhaseSec; PhaseName = TEXT("BezierVortex"); break;
	case EBulletPattern::StarBloom:
		PhaseSec = StarBloomPhaseSec; PhaseName = TEXT("StarBloom"); break;
	case EBulletPattern::LemniscateBloom:
		PhaseSec = LemniPhaseSec; PhaseName = TEXT("LemniscateBloom"); break;
	case EBulletPattern::SuperformulaBloom:
		PhaseSec = SuperPhaseSec; PhaseName = TEXT("SuperformulaBloom"); break;
	case EBulletPattern::RoseField:
		PhaseSec = RoseFieldPhaseSec; PhaseName = TEXT("RoseField"); break;
	case EBulletPattern::AerialDome:
		PhaseSec = DomePhaseSec; PhaseName = TEXT("AerialDome"); break;
	case EBulletPattern::Spirograph:
		PhaseSec = SpiroPhaseSec; PhaseName = TEXT("Spirograph"); break;
	case EBulletPattern::MicroMissile:
		PhaseSec = MicroPhaseSec; PhaseName = TEXT("MicroMissile"); break;
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

float AREBossCharacter::FireIntervalFor(EBulletPattern P)
{
	switch (P)
	{
	case EBulletPattern::Fan:               return FanFireIntervalSec;
	case EBulletPattern::Artillery:         return ArtilleryFireInterval;
	case EBulletPattern::ArtilleryStorm:    return StormFireInterval;
	case EBulletPattern::LissajousStorm:    return LissaFireInterval;
	case EBulletPattern::BezierVortex:      return VortexFireInterval;
	case EBulletPattern::RoseField:         return RoseFieldFireInterval;
	case EBulletPattern::AerialDome:        return DomeFireInterval;
	case EBulletPattern::Spirograph:        return SpiroFireInterval;
	case EBulletPattern::MicroMissile:      return MicroFireInterval;
	//~ 블룸 3종은 전용 간격을 쓴다 — 복사본 간 반경 간격이 이 값에 비례해서,
	//  Spiral 의 0.15 를 쓰면 간격이 탄 지름보다 좁아져 도형이 뭉갠다(BloomFireInterval 주석).
	case EBulletPattern::StarBloom:
	case EBulletPattern::LemniscateBloom:
	case EBulletPattern::SuperformulaBloom: return BloomFireInterval;
	//~ Spiral / RoseEnvelope / Cardioid 는 링 지오메트리가 같아 ini 기본 주기를 공유한다.
	default:                                return REBulletPattern::FireIntervalSec();
	}
}

void AREBossCharacter::FireCurrentPattern()
{
	if (bIsDead)
	{
		return;
	}
	if (REBoss::IsArcPattern(CurrentPhasePattern))
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

	if (CurrentPhasePattern == EBulletPattern::Spiral
		|| CurrentPhasePattern == EBulletPattern::RoseEnvelope)
	{
		// 장미도 균등 링이라 서버가 정할 게 Spiral과 같다 — 링 시작각과 발사당 탄 수뿐이다.
		// 로브 위상은 ServerTime 에서 순수 유도되므로 페이로드에 실을 값이 없다 (#84).
		Count    = ResolveSpiralCount();
		AngleDeg = SpiralBaseAngleDeg;
		SpiralBaseAngleDeg += SpiralRotationStepDeg;   // 다음 발사에 회전
	}
	else if (REBoss::IsBloomPattern(CurrentPhasePattern))
	{
		// 블룸은 모양을 스폰 위치로 직접 그린다 — 서버가 정할 건 표본 수뿐이다.
		// 회전·변태 위상은 클라가 ServerTime 에서 순수 유도하므로 각 자리는 비워 보낸다 (#84).
		switch (CurrentPhasePattern)
		{
		case EBulletPattern::StarBloom:         Count = StarBloomVerts * StarBloomSegPerEdge; break;
		case EBulletPattern::LemniscateBloom:   Count = LemniCount; break;
		default:                                Count = SuperCount; break;   // SuperformulaBloom
		}
	}
	else   // Fan / Cardioid — 둘 다 페이로드의 각을 '플레이어 조준'에 쓴다
	{
		Count = (CurrentPhasePattern == EBulletPattern::Cardioid)
			? CardioidCount : REBulletPattern::FFanParams().Count;
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

	// 볼리 일련번호. 프레임률과 무관하게 볼리마다 정확히 1씩 는다 — 폭풍은 스윕 진행도의
	// 분자로, 마이크로 미사일은 발사 방향 순환 인덱스로 쓴다.
	// 서버 전용 상태이므로 클라는 유도할 수 없다 → 페이로드로 보낸다 (#84).
	//
	// 마이크로 미사일에서 ServerTime 파생을 안 쓰는 이유: 타이머가 ±5ms 흔들려
	// floor(ServerTime/Interval) 이 인덱스를 건너뛰거나 반복한다(실측 간격 104/98/103/97/105ms).
	// 그러면 동→서→북→남 순환이 깨져 연속한 두 발이 인접 방향에서 오기도 한다.
	// 카운터는 지터와 무관하게 정확히 1씩 는다.
	int32 SweepIdx = 0;
	if (CurrentPhasePattern == EBulletPattern::ArtilleryStorm
		|| CurrentPhasePattern == EBulletPattern::MicroMissile)
	{
		SweepIdx = PhaseVolleyIdx++;
	}

	Multicast_FireArtillery(CurrentPhasePattern, CurrentArtilleryShape, BossLoc, PlayerLoc, CallSeed,
	                        SweepIdx, GetServerNow());
}

void AREBossCharacter::Multicast_FireArtillery_Implementation(EBulletPattern Pattern, EArtilleryShape Shape,
                                                              FVector_NetQuantize Origin,
                                                              FVector_NetQuantize AimLoc, int32 CallSeed,
                                                              int32 SweepIdx, float ServerTime)
{
	UREBulletSpawnSubsystem* Spawner = GetWorld() ? GetWorld()->GetSubsystem<UREBulletSpawnSubsystem>() : nullptr;
	if (!Spawner)
	{
		UE_LOG(LogTemp, Warning, TEXT("[RE] Boss::FireArtillery: Spawner NULL"));
		return;
	}

	// 곡사 계열 네 패턴이 이 경로를 공유한다 — 갈리는 것은 체공/고도/발수와 착지 지오메트리뿐이다.
	// 클라는 페이즈 로직을 안 돌리므로 어느 쪽인지 페이로드로 받아야 한다 (#84).
	const bool bStorm  = (Pattern == EBulletPattern::ArtilleryStorm);
	const bool bLissa  = (Pattern == EBulletPattern::LissajousStorm);
	const bool bVortex = (Pattern == EBulletPattern::BezierVortex);
	const bool bRoseF  = (Pattern == EBulletPattern::RoseField);
	const bool bDome   = (Pattern == EBulletPattern::AerialDome);
	const bool bSpiro  = (Pattern == EBulletPattern::Spirograph);
	const bool bMicro  = (Pattern == EBulletPattern::MicroMissile);
	// 아래 셋은 바닥에 그래프를 그리고 3차 제어점으로 가는 길을 성형한다.
	const bool bShaped = bRoseF || bDome || bSpiro;

	float FlightTime = ArtilleryFlightTime;
	float MaxHeight  = ArtilleryMaxHeight;   // 소용돌이는 탄마다 덮어쓴다(층 만들기)
	int32 Count      = ArtilleryCount;
	if (bStorm)
	{
		FlightTime = StormFlightTime;  MaxHeight = StormMaxHeight;  Count = StormCount;
	}
	else if (bLissa)
	{
		FlightTime = LissaFlightTime;  MaxHeight = LissaMaxHeight;  Count = LissaCount;
	}
	else if (bVortex)
	{
		FlightTime = VortexFlightTime; MaxHeight = VortexMaxHeight; Count = VortexCount;
	}
	else if (bRoseF)
	{
		FlightTime = RoseFieldFlightTime; MaxHeight = RoseFieldMaxHeight; Count = RoseFieldCount;
	}
	else if (bDome)
	{
		FlightTime = DomeFlightTime;      MaxHeight = DomeMaxHeight;      Count = DomeCount;
	}
	else if (bSpiro)
	{
		FlightTime = SpiroFlightTime;     MaxHeight = SpiroMaxHeight;     Count = SpiroCount;
	}
	else if (bMicro)
	{
		FlightTime = MicroFlightTime;     MaxHeight = MicroMaxHeight;     Count = MicroCount;
	}

	// 지연 보정 — 이미 착지한 탄은 스폰하지 않는다. 착지점 생성·난수 뽑기보다 먼저 검사해 헛수고를 막는다.
	const float Elapsed = GetElapsedSince(ServerTime);
	if (Elapsed >= FlightTime)
	{
		UE_LOG(LogTemp, Log, TEXT("[RE] Boss FireArtillery: skipped (Elapsed=%.3f >= FlightTime=%.2f)"),
			Elapsed, FlightTime);
		return;
	}

	// 외관 보정 — 페이즈 도중 접속한 클라 따라잡기용. 도약은 페이즈 시작에서만 재생한다.
	StartPatternLook(Pattern);

	const FVector BossLoc  = Origin;
	const FVector PlayerLoc = AimLoc;
	const float   GroundZ  = BossLoc.Z + MarkerGroundOffset;   // 착지 평면(보스 캡슐 바닥 근사)

	// 서버가 넘긴 시드로 만든 로컬 스트림 — 양쪽이 같은 난수열을 본다.
	FRandomStream CallRng(CallSeed);

	TArray<FVector> Targets;
	if (bStorm)
	{
		// 폭풍: 단일 나선을 한 발씩 이어 그린다. 이 볼리가 맡은 구간은 전체 발수 중
		// [SweepIdx*Count, SweepIdx*Count + Count-1] 번째 발이다. Clamp 는 타이머가
		// 예상보다 더 돌았을 때 바깥 끝에 머물게 한다.
		const float Denom = (float)FMath::Max(StormShotsPerSweep - 1, 1);
		const float T0 = FMath::Clamp((SweepIdx * Count)             / Denom, 0.f, 1.f);
		const float T1 = FMath::Clamp((SweepIdx * Count + Count - 1) / Denom, 0.f, 1.f);
		Targets = REBulletPattern::GenSweepSpiral(BossLoc, StormMinRadius, StormMaxRadius,
		                                          StormSweepTurns, T0, T1, Count, StormArms, GroundZ);
	}
	else if (bLissa)
	{
		// 볼리 하나가 매듭 전체를 그린다. 위상을 ServerTime 에서 유도해 복제 없이 양쪽이 맞춘다.
		Targets = REBulletPattern::GenLissajous(BossLoc, LissaExtent, LissaExtent,
		                                        LissaFreqX, LissaFreqY,
		                                        LissaDeltaDegPerSec * ServerTime, Count, GroundZ);
	}
	else if (bVortex)
	{
		// 착지 링을 볼리마다 돌린다 — 안 돌리면 같은 자리에 계속 떨어져 소용돌이가 안 이어진다.
		// 반경은 톱니로 팽창한다 — 고정 링이면 그 띠 밖은 영영 안전하다. AerialDome 과 같은
		// 수법이고 위상을 ServerTime 에서 뽑으므로 페이로드도 서버 상태도 필요 없다 (#84).
		const float VPhase  = FMath::Frac(ServerTime / VortexExpandSec);
		const float VRadius = FMath::Lerp(VortexMinRadius, VortexMaxRadius, VPhase);
		Targets = REBulletPattern::GenRing(BossLoc, VRadius, Count, GroundZ,
		                                   VortexSpinDegPerSec * ServerTime);
	}
	else if (bRoseF)
	{
		Targets = REBulletPattern::GenRoseCurve(BossLoc, RoseFieldRadius, RoseFieldPetals,
		                                        RoseFieldSpinDegPerSec * ServerTime, Count, GroundZ);
	}
	else if (bDome)
	{
		// 링 반경이 톱니로 팽창한다 — 고정 링이면 가운데와 바깥이 영영 안전하다.
		// 위상을 ServerTime 에서 뽑으므로 페이로드도 서버 상태도 필요 없다 (#84).
		const float Phase  = FMath::Frac(ServerTime / DomeExpandSec);
		const float Radius = FMath::Lerp(DomeMinRadius, DomeMaxRadius, Phase);
		Targets = REBulletPattern::GenRing(BossLoc, Radius, Count, GroundZ,
		                                   DomeSpinDegPerSec * ServerTime);
	}
	else if (bSpiro)
	{
		Targets = REBulletPattern::GenHypotrochoid(BossLoc, SpiroRadius, SpiroBigR, SpiroSmallR,
		                                           SpiroD, SpiroSpinDegPerSec * ServerTime,
		                                           Count, GroundZ);
	}
	else if (bMicro)
	{
		// 목표는 **발사 순간의** 플레이어 위치다(유도가 아니다). 그 자리를 중심으로 좁은
		// 원판에 균등하게 뿌려 착탄이 카펫처럼 깔리게 한다.
		// 산포는 서버가 보낸 시드로 만든 스트림을 쓴다 — 양쪽이 같은 난수열을 본다.
		Targets = REBulletPattern::GenRandom(PlayerLoc, MicroSpread, Count, CallRng, GroundZ);
	}
	else
	{
		switch (Shape)
		{
		case EArtilleryShape::Ring:
			Targets = REBulletPattern::GenRing(BossLoc, /*Radius=*/ArenaRadius * 0.75f, Count, GroundZ);
			break;
		case EArtilleryShape::Line:
			Targets = REBulletPattern::GenLine(BossLoc, PlayerLoc, /*WallLen=*/ArenaRadius * 2.f, Count, GroundZ);
			break;
		case EArtilleryShape::Grid:
			Targets = REBulletPattern::GenGrid(BossLoc, ArenaRadius, ArenaRadius, /*Cols=*/4, /*Rows=*/3, GroundZ);
			break;
		case EArtilleryShape::Spiral:
			Targets = REBulletPattern::GenArcSpiral(BossLoc, /*MaxRadius=*/ArenaRadius, Count, GroundZ);
			break;
		case EArtilleryShape::PlayerAimed:
			Targets = REBulletPattern::GenPlayerCluster(PlayerLoc, /*ClusterRadius=*/150.f, /*RingN=*/4, GroundZ);
			break;
		case EArtilleryShape::Random:
			Targets = REBulletPattern::GenRandom(BossLoc, ArenaRadius, Count, CallRng, GroundZ);
			break;
		}
	}

	TArray<REBulletPattern::FArcBulletSpawnParams> Shots;
	Shots.Reserve(Targets.Num());
	for (int32 i = 0; i < Targets.Num(); ++i)
	{
		REBulletPattern::FArcBulletSpawnParams P;
		P.Start      = BossLoc;
		P.Target     = Targets[i];
		P.FlightTime = FlightTime;
		P.MaxHeight  = MaxHeight;
		// 마이크로 미사일은 발수가 10배라 발당 데미지를 낮춰야 초당 데미지가 유지된다.
		P.Damage     = bMicro ? MicroDamage : ArtilleryDamage;
		// 판정·마커 반경은 패턴의 지오메트리 축척을 따라간다 — 곡선을 그리는 패턴에서
		// 반경이 크면 선 굵기가 무늬 자체를 뭉갠다.
		P.Radius     = bLissa ? LissaRadius : (bMicro ? MicroRadius : ArtilleryRadius);
		P.Elapsed    = Elapsed;
		if (bMicro)
		{
			// 초기 접선을 원주 등분각으로 고정한다 — 목표가 어디든 먼저 그 방향으로 뻗어
			// 볼리 하나가 부채꼴로 터진다. 볼리마다 부채꼴 전체가 MicroBaseStepDeg 씩 돈다.
			const float MAng = FMath::DegreesToRadians(
				SweepIdx * MicroBaseStepDeg + i * (360.f / FMath::Max(Count, 1)));
			const FVector MDir(FMath::Cos(MAng), FMath::Sin(MAng), 0.f);
			const REBulletPattern::FArcShapeOffsets Sh =
				REBulletPattern::ArcCompassLob(P.Start, P.Target, MDir,
				                               MicroOutDist, MicroRise1, MicroRise2);
			P.Ctrl1Offset = Sh.Ctrl1;
			P.Ctrl2Offset = Sh.Ctrl2;
		}
		if (bShaped)
		{
			// 궤적 성형. 착지점·착지 시각은 제어점과 무관하므로 회피 규칙은 일반 곡사와 같다 —
			// 바뀌는 건 가는 길뿐이다.
			REBulletPattern::FArcShapeOffsets Sh;
			if (bRoseF)
			{
				Sh = REBulletPattern::ArcSpiralColumn(P.Start, P.Target, RoseFieldSwirl, RoseFieldRise);
			}
			else if (bDome)
			{
				Sh = REBulletPattern::ArcDomeShell(P.Start, P.Target, DomeRise);
			}
			else   // Spirograph
			{
				// 이웃끼리 부호를 뒤집어야 궤적이 엇갈린다 — 같은 부호면 전부 나란히 휜다.
				const float Swing = ((i & 1) ? 1.f : -1.f) * SpiroSwing;
				Sh = REBulletPattern::ArcSCurve(P.Start, P.Target, Swing, SpiroRise);
			}
			P.Ctrl1Offset = Sh.Ctrl1;
			P.Ctrl2Offset = Sh.Ctrl2;

			// 어긋내기는 **돔에만** 건다. 폭풍은 Interval(0.05)이 FlightTime 보다 훨씬 작아
			// 어긋냄이 무시할 수준이지만, 장미밭·스피로그래프는 겹수를 1로 맞추려고 Interval 을
			// 1.2 까지 올린 탓에 어긋냄이 비행의 79%(1.19/1.5)를 먹었다 — 탄이 보스에서
			// 출발하지 않고 **착지점 근처에서 튀어나왔다.** 동시에 쏘면 전부 보스에서 뻗는다.
			if (bDome)
			{
				P.Elapsed += DomeFireInterval * (float)(Count - 1 - i) / Count;
				if (P.Elapsed >= FlightTime)
				{
					continue;
				}
			}
		}
		if (bVortex)
		{
			// 고도를 탄마다 어긋내 층을 만든다. 궤적이 정규화 보간이라 높이를 바꿔도 착지
			// 타이밍은 안 변한다 — 층이 져도 링은 여전히 동시에 떨어진다.
			const float f = (Targets.Num() > 1) ? ((float)i / (Targets.Num() - 1)) : 0.f;
			P.MaxHeight  = FMath::Lerp(VortexMinHeight, VortexMaxHeight, f);
			// 제어점을 접선으로 밀어 직선 대신 휘감아 들어가게 한다. 끝점은 그대로다.
			P.CtrlOffset = REBulletPattern::ArcSwirlOffset(P.Start, P.Target, VortexSwirl);
			// 폭풍과 같은 어긋내기. 안 하면 Count 발이 한 프레임에 통째로 나가 소용돌이가
			// 이어진 리본이 아니라 덩어리로 보인다(실제로 그랬다). 마커는 Target 기반이라
			// 어긋내기가 안 먹지만 스케일 램프가 Elapsed 를 쓰므로 링도 순차로 자란다.
			P.Elapsed += VortexFireInterval * (float)(Count - 1 - i) / Count;
			if (P.Elapsed >= FlightTime)
			{
				continue;   // 지연이 커서 이미 착지했을 발은 버린다
			}
		}
		if (bStorm)
		{
			// 이 볼리의 슬롯들은 '지난 발사 주기 동안 한 발씩 나간' 것이다. 슬롯 0 이 가장 먼저
			// 나갔으므로 그만큼 비행이 진행돼 있어야 한다 — 안 어긋내면 한 점에서 동시에 출발해
			// 일제사로 보인다. 어긋내면 화면상 초당 Count/주기 발 단발 사격이 된다.
			// 나누는 단위는 발이 아니라 **슬롯**이다: 같은 슬롯의 팔들은 동시 발사다
			// (GenSweepSpiral 이 슬롯 우선으로 채우므로 i/StormArms 가 슬롯 인덱스다).
			const int32 Slot = i / StormArms;
			P.Elapsed += StormFireInterval * (float)(Count - 1 - Slot) / Count;
			if (P.Elapsed >= FlightTime)
			{
				continue;   // 지연이 커서 이미 착지했을 서브샷은 버린다
			}
		}
		Shots.Add(P);
	}
	Spawner->SpawnArcBulletBatch(Shots);

	UE_LOG(LogTemp, Log, TEXT("[RE] Boss FireArtillery: Pattern=%d Shape=%d N=%d Flight=%.2f Sweep=%d Elapsed=%.3f role=%s"),
		(int32)Pattern, (int32)Shape, Shots.Num(), FlightTime, SweepIdx, Elapsed,
		*UEnum::GetValueAsString(GetLocalRole()));
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
	else if (Pattern == EBulletPattern::RoseEnvelope)
	{
		REBulletPattern::FRoseParams RP;
		RP.Count        = Count;
		RP.BaseAngleDeg = AngleDeg;
		RP.Lobes        = RoseLobes;
		RP.Amp          = RoseAmp;
		// 로브 위상을 ServerTime 에서 뽑는다 — ShotParity 와 같은 이유다 (#97):
		// 이 함수는 서버와 클라가 **같은 ServerTime** 으로 실행하므로 복제 없이 무늬가 일치한다.
		// 로컬 시계나 자체 카운터를 쓰면 멀티캐스트 유실 시 클라마다 로브가 어긋난다.
		RP.PhaseDeg     = RoseSpinDegPerSec * ServerTime;
		Params = REBulletPattern::GenerateRose(Origin, RP);
	}
	else if (Pattern == EBulletPattern::Cardioid)
	{
		REBulletPattern::FCardioidParams CdP;
		CdP.Count       = Count;
		CdP.AimAngleDeg = AngleDeg;
		CdP.Amp         = CardioidAmp;
		// 링 회전은 ServerTime 에서 유도한다 — 페이로드의 각 자리는 조준이 쓰고 있다 (#84).
		CdP.RingBaseDeg = CardioidRingSpinDegPerSec * ServerTime;
		Params = REBulletPattern::GenerateCardioid(Origin, CdP);
	}
	else if (REBoss::IsBloomPattern(Pattern))
	{
		// 모양을 곡선으로 그린 뒤 한 수법으로 부풀린다 — 곡선만 갈린다.
		// 회전·변태 위상은 상수와 ServerTime 에서 순수 유도한다(복제 불필요, #84/#97).
		TArray<FVector2D> Curve;
		if (Pattern == EBulletPattern::StarBloom)
		{
			Curve = REBulletPattern::GenStarPolygon(StarBloomVerts, StarBloomSkip, StarBloomRadius,
			                                        StarBloomSpinDegPerSec * ServerTime, StarBloomSegPerEdge);
		}
		else if (Pattern == EBulletPattern::LemniscateBloom)
		{
			Curve = REBulletPattern::GenLemniscate(Count, LemniA, LemniSpinDegPerSec * ServerTime);
		}
		else   // SuperformulaBloom
		{
			// m 을 삼각함수로 왕복시킨다 — 톱니로 감으면 주기마다 모양이 튄다.
			const float Cycle = FMath::Sin(2.f * PI * ServerTime / SuperMorphPeriodSec) * 0.5f + 0.5f;
			const float M     = FMath::Lerp(SuperMMin, SuperMMax, Cycle);
			Curve = REBulletPattern::GenSuperformula(Count, M, SuperN1, SuperN2, SuperN3,
			                                         SuperRadius, SuperSpinDegPerSec * ServerTime);
		}
		REBulletPattern::FCurveBloomParams BP;
		BP.ScaleRate = BloomScaleRate;
		// ini 기본 수명(15초)을 쓰면 도형이 맵 밖까지 부풀어 화면에는 이미 가장자리를 지나간
		// 잔해만 남는다 — 아레나 가장자리에 닿는 순간 소멸하도록 잘라낸다.
		BP.Lifetime  = BloomLifetime;
		Params = REBulletPattern::GenerateCurveBloom(Origin, Curve, BP);
	}
	else
	{
		return;
	}

	// 볼리 안에서 한 발씩 나가게 어긋낸다. 안 하면 N발이 같은 프레임에 생기고 **같은
	// 프레임에 죽는다** — 속력이 제각각인 패턴에서 파면 한 줄이 통째로 증발해 눈에 거슬린다.
	//
	// 위치까지 미리 보내면(등속 직선이라 Location += Velocity·Dt 로 정확하다) 볼리가 시간축으로
	// 번져 발사가 이어져 보인다. 다만 **블룸은 그 번짐이 곧 도형의 축척 차이**라 모양이
	// 뭉갠다(복사본 간 반경 간격 = R₀·ScaleRate·간격) — 블룸은 위치를 안 건드리고 수명만 흩뜨린다.
	{
		const bool  bBloomHere = REBoss::IsBloomPattern(Pattern);
		const float Interval   = FireIntervalFor(Pattern);
		const int32 N          = Params.Num();
		for (int32 i = 0; i < N; ++i)
		{
			const float Frac = (N > 1) ? ((float)i / (N - 1)) : 0.f;
			if (!bBloomHere)
			{
				const float Dt = Interval * Frac;
				Params[i].Location += Params[i].Velocity * Dt;
				Params[i].Lifetime -= Dt;
			}
			// 소멸 시각을 벌린다. 위치 어긋냄만으로는 폭이 발사 주기(0.15s ≈ 9프레임)뿐이라
			// 여전히 한꺼번에 사라지는 것처럼 보인다.
			// 하한을 둔다 — 폭이 수명을 넘으면 0수명 탄이 조용히 스폰된다.
			Params[i].Lifetime = FMath::Max(Params[i].Lifetime - VolleyDeathSpreadSec * Frac, 0.1f);
		}
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
