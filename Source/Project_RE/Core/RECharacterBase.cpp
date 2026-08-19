// Copyright Epic Games, Inc. All Rights Reserved.

#include "RECharacterBase.h"
#include "GameFramework/SpringArmComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Camera/CameraComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimMontage.h"
#include "Engine/SkeletalMesh.h"
#include "TimerManager.h"
#include "UObject/ConstructorHelpers.h"
#include "Net/UnrealNetwork.h"
#include "AbilitySystemComponent.h"
#include "REAttackComponent.h"
#include "GameplayAbilitySpec.h"
#include "Abilities/REGA_Dash.h"
#include "REHealthBarComponent.h"
#include "REGameMode.h"
#include "REStatsSettings.h"
#include "HAL/IConsoleManager.h"
#include "NiagaraSystem.h"
#include "NiagaraFunctionLibrary.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInterface.h"
#include "REExplosionFx.h"

// 치트: 1이면 플레이어 무적(TakeDamage 무피해). 데브 전용, 클라 로컬(ECVF_Cheat).
static TAutoConsoleVariable<int32> CVarPlayerInvincible(
	TEXT("re.Cheat.PlayerInvincible"),
	0,
	TEXT("1 = player takes no damage (dev cheat, client-local)"),
	ECVF_Cheat);

// 대쉬 잔상 VFX 스폰 보정 (#116). 기본값은 육안으로 맞춘 실측값이다.
//
// 콘솔에 남겨두는 이유: 이 셋은 "메시 원점이 어디냐"와 "애셋이 자기 원점 기준 어디로
// 뻗느냐"에 종속된 값이다. M7 에서 캐릭터 모델(#117)과 대쉬 애셋이 바뀌면 전부 다시
// 맞춰야 한다. 그때 리빌드 없이 PIE 에서 조절하기 위한 노브다.
// 코스메틱 전용이라 클라 로컬(ECVF_Cheat)로 충분하다.

// 500 = 대쉬 거리(678uu)의 대부분. 잔상이 도착 지점 쪽에서 지나온 길로 흐르게 된다.
static TAutoConsoleVariable<float> CVarDashVfxOffsetFwd(
	TEXT("re.Debug.DashVfxOffsetFwd"),
	500.f,
	TEXT("대쉬 잔상 VFX 스폰 오프셋 — 대쉬 진행방향(uu). 음수=뒤."),
	ECVF_Cheat);

// 100 = 발밑 기준으로 캐릭터 몸통 높이까지 올림.
static TAutoConsoleVariable<float> CVarDashVfxOffsetUp(
	TEXT("re.Debug.DashVfxOffsetUp"),
	100.f,
	TEXT("대쉬 잔상 VFX 스폰 오프셋 — 월드 상하(uu). 음수=아래."),
	ECVF_Cheat);

// 1 로 두는 이유: 이 애셋에서는 스케일이 균등하게 먹지 않는다. 값을 줄이면 파티클이
// 작아지는 게 아니라 잔상 사이 간격만 좁아진다(실측). 이미터가 파티클 크기를 월드
// 단위로 들고 있어 컴포넌트 스케일이 위치 오프셋에만 적용되는 것으로 보인다.
// 크기를 정말 줄이려면 애셋(NS_Dash_Ghost) 쪽을 손대야 한다.
// 노브는 남긴다 — 애셋을 갈면 다시 시도해볼 값이다.
static TAutoConsoleVariable<float> CVarDashVfxScale(
	TEXT("re.Debug.DashVfxScale"),
	1.f,
	TEXT("대쉬 잔상 VFX 균등 스케일. 1=원본. 이 애셋에선 간격만 변한다 — 주석 참조."),
	ECVF_Cheat);

/**
 *  발사 이펙트 노출 시간(s) 오버라이드 (#120). 0 이하 = 코드 기본값(FireFxSec).
 *
 *  기본 0.06s 는 발사 간격(0.25s)에 겹치지 않게 잡은 값인데, 그만큼 짧아서 스크린샷으로
 *  잡으려면 4프레임 안에 셔터를 맞춰야 한다 — 실제로 계속 1프레임씩 빗나갔다.
 *  검증할 때 이 값을 크게 주면 아무 프레임에서나 찍힌다. 코스메틱이라 클라 로컬로 충분하다.
 */
static TAutoConsoleVariable<float> CVarFireFxSec(
	TEXT("re.Debug.FireFxSec"),
	0.f,
	TEXT("발사 빔/섬광 노출 시간(s). 0 이하 = 기본값. 스크린샷 검증용."),
	ECVF_Cheat);

ARECharacterBase::ARECharacterBase()
{
	PrimaryActorTick.bCanEverTick = false;

	// 폰이 컨트롤러 회전을 따라가지 않음 — 탑뷰 카메라 고정 유지
	bUseControllerRotationYaw = false;
	bUseControllerRotationPitch = false;
	bUseControllerRotationRoll = false;

	// 체력 초기화 — Settings 단일 출처 (M3.5 ③).
	MaxHealth = GetDefault<UREStatsSettings>()->PlayerMaxHealth;
	Health = MaxHealth;

	// GAS: ASC 부착 — Pawn 소유, 복제 켜고 Mixed 모드(오너 클라만 GE 복제).
	AbilitySystemComponent = CreateDefaultSubobject<UAbilitySystemComponent>(TEXT("AbilitySystemComponent"));
	AbilitySystemComponent->SetIsReplicated(true);
	AbilitySystemComponent->SetReplicationMode(EGameplayEffectReplicationMode::Mixed);

	// 수동공격 부착 — 발사는 컨트롤러 Server_RequestFire → FireInDirection 경유.
	AttackComponent = CreateDefaultSubobject<UREAttackComponent>(TEXT("Attack"));

	// HP바 (#29) — 플레이어 초록. 회전/사이즈/위젯클래스는 컴포넌트 생성자가 처리.
	HealthBar = CreateDefaultSubobject<UREHealthBarComponent>(TEXT("HealthBar"));
	HealthBar->SetupAttachment(RootComponent);
	HealthBar->BarColor = FLinearColor::Green;

	// 이동 방향으로 캐릭터가 회전 (탑뷰 클릭 이동 시 진행 방향을 바라봄)
	GetCharacterMovement()->bOrientRotationToMovement = true;
	GetCharacterMovement()->RotationRate = FRotator(0.f, 640.f, 0.f);

	// 데디 보정 스로틀 해제 (#72) — 클릭 이동은 입력 예측형이 아니라 클라 예측(Accel=0=브레이크)이
	// 서버 패스팔로잉과 매 프레임 어긋난다. 기본 0.10s 스로틀이면 100ms마다 ~30uu가 밀린 뒤
	// 하드 텔레포트로 보정된다(오너 클라는 SmoothCorrection 대상이 아님) — 캡슐에 붙은 카메라까지 튄다.
	// 매 무브 보정하면 점프량이 1프레임 이동량(~10uu)으로 줄어든다.
	// 근거: docs/superpowers/specs/2026-08-10-dedi-move-replication-design.md
	// ponytail: 플레이어 1명 전제(보정 RPC 1개/무브). 다인전이면 되돌리고 이동목표 복제+클라 예측으로 가라.
	GetCharacterMovement()->NetworkMinTimeBetweenClientAdjustments = 0.f;
	GetCharacterMovement()->NetworkMinTimeBetweenClientAdjustmentsLargeCorrection = 0.f;

	// SpringArm: 절대 하향(-50) 고정, 길이 1500, 상속/폰회전 사용 안 함
	CameraBoom = CreateDefaultSubobject<USpringArmComponent>(TEXT("CameraBoom"));
	CameraBoom->SetupAttachment(RootComponent);
	CameraBoom->TargetArmLength = 1500.f;
	CameraBoom->SetRelativeRotation(FRotator(-50.f, 0.f, 0.f));
	CameraBoom->bUsePawnControlRotation = false;
	CameraBoom->bInheritPitch = false;
	CameraBoom->bInheritYaw = false;
	CameraBoom->bInheritRoll = false;
	CameraBoom->bDoCollisionTest = false; // 벽에 카메라가 튕기지 않도록

	// Camera: 원근 FOV 90
	TopDownCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("TopDownCamera"));
	TopDownCamera->SetupAttachment(CameraBoom, USpringArmComponent::SocketName);
	TopDownCamera->bUsePawnControlRotation = false;
	TopDownCamera->SetFieldOfView(90.f);

	// 플레이어 메시 로드 (#117) — 유료 애셋(Fab, .gitignore 대상)이라 없는 환경이 정상 경로다.
	// SKEL_Sarah 는 UE5 Manny 와 본 이름·계층이 동일하고, 스켈레톤 에셋에 Manny 가
	// Compatible Skeleton 으로 등록돼 있어야 아래 ABP_Unarmed/MM_Dash 가 그대로 재생된다.
	static ConstructorHelpers::FObjectFinder<USkeletalMesh> MeshAsset(
		TEXT("/Game/Adventure_Pack/Characters/Sarah/Mesh/SK_Sarah.SK_Sarah"));
	if (MeshAsset.Succeeded())
	{
		GetMesh()->SetSkeletalMesh(MeshAsset.Object);
		GetMesh()->SetRelativeLocation(FVector(0.f, 0.f, -90.f));
		GetMesh()->SetRelativeRotation(FRotator(0.f, -90.f, 0.f));
	}

	// 애님BP 로드
	static ConstructorHelpers::FClassFinder<UAnimInstance> AnimAsset(
		TEXT("/Game/Characters/Mannequins/Anims/Unarmed/ABP_Unarmed"));
	if (AnimAsset.Succeeded())
	{
		GetMesh()->SetAnimInstanceClass(AnimAsset.Class);
	}

	// 대쉬 모션 — 실패해도 크래시 없이 진행(모션만 생략).
	// 어빌리티(UREGA_Dash)가 아니라 캐릭터가 들고 있다: 재생이 Multicast RPC로 옮겨갔고,
	// 여기 두면 모든 프로세스가 CDO에서 같은 애셋을 로드해 RPC로 오브젝트 참조를 보낼 필요가 없다.
	static ConstructorHelpers::FObjectFinder<UAnimSequence> DashAnimAsset(
		TEXT("/Game/Characters/Mannequins/Anims/Unarmed/Jump/MM_Dash.MM_Dash"));
	if (DashAnimAsset.Succeeded())
	{
		DashAnim = DashAnimAsset.Object;
	}

	// 대쉬 잔상 VFX — DashAnim과 같은 이유로 여기서 로드한다(모든 프로세스가 CDO에서 동일 애셋 확보).
	// 유료 애셋(Fab, .gitignore 대상)이라 없는 환경이 정상 경로다 — 실패하면 VFX만 생략된다.
	static ConstructorHelpers::FObjectFinder<UNiagaraSystem> DashVfxAsset(
		TEXT("/Game/BlinkDash/VFX_Niagara/NS_Dash_Ghost.NS_Dash_Ghost"));
	if (DashVfxAsset.Succeeded())
	{
		DashVfx = DashVfxAsset.Object;
	}

	//~ 발사 표현 (#120) — 전부 코스메틱. 충돌은 모두 끈다:
	//  자동사격이 ECC_Pawn 라인트레이스라 오너 부착물이 판정을 가리면 안 된다(HP바와 같은 이유).

	// 총기 — Sarah 스켈레톤이 Pistol_Socket 을 이미 들고 있다(팩 저작). 소켓이 없는 환경에서는
	// 메시 루트에 붙어 위치만 어긋난다(크래시 없음).
	WeaponMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Weapon"));
	WeaponMesh->SetupAttachment(GetMesh(), TEXT("Pistol_Socket"));
	WeaponMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	WeaponMesh->SetCastShadow(false);   // 탑다운 시점에서 총 그림자는 노이즈다
	static ConstructorHelpers::FObjectFinder<UStaticMesh> PistolAsset(
		TEXT("/Game/Adventure_Pack/Characters/Shared/Props/Pistol/SM_Pistol.SM_Pistol"));
	if (PistolAsset.Succeeded())
	{
		WeaponMesh->SetStaticMesh(PistolAsset.Object);
	}

	// 빔/섬광 — 엔진 기본 도형 + 자체 저작 이미시브 머티리얼(scripts/make_beam_material.py).
	static ConstructorHelpers::FObjectFinder<UStaticMesh> CylinderAsset(
		TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> SphereAsset(
		TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> BeamMatAsset(
		TEXT("/Game/Materials/M_REBeam.M_REBeam"));

	BeamMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("FireBeam"));
	BeamMesh->SetupAttachment(RootComponent);
	BeamMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	BeamMesh->SetCastShadow(false);
	BeamMesh->SetVisibility(false);
	// 절대 트랜스폼 — 빔은 월드에 그은 선이다. 캡슐에 상대로 두면 발사 직후 플레이어가
	// 움직이거나 회전할 때 남은 프레임 동안 빔이 같이 끌려가 휜다.
	BeamMesh->SetUsingAbsoluteLocation(true);
	BeamMesh->SetUsingAbsoluteRotation(true);
	BeamMesh->SetUsingAbsoluteScale(true);
	if (CylinderAsset.Succeeded())
	{
		BeamMesh->SetStaticMesh(CylinderAsset.Object);
	}

	MuzzleFlash = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("MuzzleFlash"));
	MuzzleFlash->SetupAttachment(RootComponent);
	MuzzleFlash->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	MuzzleFlash->SetCastShadow(false);
	MuzzleFlash->SetVisibility(false);
	MuzzleFlash->SetUsingAbsoluteLocation(true);
	MuzzleFlash->SetUsingAbsoluteScale(true);
	if (SphereAsset.Succeeded())
	{
		MuzzleFlash->SetStaticMesh(SphereAsset.Object);
	}
	MuzzleFlash->SetWorldScale3D(FVector(0.22f));   // 구 기본 지름 100uu → 22uu (≈11픽셀, 빔 굵기 주석 참조)

	if (BeamMatAsset.Succeeded())
	{
		BeamMesh->SetMaterial(0, BeamMatAsset.Object);
		MuzzleFlash->SetMaterial(0, BeamMatAsset.Object);
	}
}

// SM_Pistol 바운즈 X 범위 [-18.8, +4.2] 에서 +X 끝이 총구 쪽이다. 소켓이 없어 오프셋으로 잡는다.
const FVector ARECharacterBase::MuzzleLocal(4.f, 0.f, 2.f);

void ARECharacterBase::ShowFireFx(const FVector& BeamEnd)
{
	if (!BeamMesh || !WeaponMesh)
	{
		return;
	}

	const FVector Start = WeaponMesh->GetComponentTransform().TransformPosition(MuzzleLocal);
	const FVector Delta = BeamEnd - Start;
	const float Len = Delta.Size();
	if (Len < 1.f)
	{
		return;   // 끝점이 총구에 겹침 — 방향을 못 뽑는다
	}

	// 실린더 기본형: 높이 100uu·지름 100uu, 원점 중앙, 축 = +Z.
	BeamMesh->SetWorldLocation(Start + Delta * 0.5f);
	BeamMesh->SetWorldRotation(FRotationMatrix::MakeFromZ(Delta / Len).Rotator());
	BeamMesh->SetWorldScale3D(FVector(BeamThicknessUU / 100.f, BeamThicknessUU / 100.f, Len / 100.f));
	BeamMesh->SetVisibility(true);

	MuzzleFlash->SetWorldLocation(Start);
	MuzzleFlash->SetVisibility(true);

	// 빔은 0.06초만 뜨고 화면으로만 판정할 수 있다 — 스크린샷이 흐릿할 때 기하가 틀린 건지
	// 룩이 약한 건지 가르려면 이 값이 필요하다(#120 검증에서 실제로 갈랐다).
	UE_LOG(LogTemp, Log, TEXT("[Attack] beam start=%s end=%s len=%.1f"),
		*Start.ToCompactString(), *BeamEnd.ToCompactString(), Len);

	const float Override = CVarFireFxSec.GetValueOnGameThread();
	GetWorldTimerManager().SetTimer(FireFxTimer, this, &ARECharacterBase::HideFireFx,
		Override > 0.f ? Override : FireFxSec, false);
}

void ARECharacterBase::HideFireFx()
{
	if (BeamMesh)
	{
		BeamMesh->SetVisibility(false);
	}
	if (MuzzleFlash)
	{
		MuzzleFlash->SetVisibility(false);
	}
}

void ARECharacterBase::Multicast_PlayDashMontage_Implementation(FVector DashDir)
{
	// 데디 서버는 화면이 없으므로 코스메틱 재생을 생략한다 (#74 Multicast_PlayFire와 동일 패턴).
	// "데디는 AnimInstance가 null이라 자연 no-op"으로 봤던 초안 가정은 실측으로 반증됐다 —
	// 데디 서버 로그에 `[Dash] anim len=0.97 (role=ROLE_Authority)`가 찍혔다. 가드가 없으면
	// 서버가 IgnoreRootMotion으로 전환한 채 몽타주를 돌려 서버 권위 이동 경로에 개입한다.
	if (IsNetMode(NM_DedicatedServer))
	{
		return;
	}

	// 잔상 VFX(#116) — 애님과 독립적으로 스폰한다.
	// 애셋 유무를 각자 판단하는 이유: DashAnim은 UE5 템플릿 마네킹 경로(.gitignore 대상)이고
	// DashVfx는 유료 Fab 애셋(.gitignore 대상)이라, 한쪽만 없는 환경이 실제로 존재한다.
	// 하나로 묶어 return하면 애님이 없다는 이유로 VFX까지 사라진다.
	// 대쉬 이동은 UREGA_Dash의 RootMotion이 담당하므로 여기서 무엇을 스폰하든 이동에 영향 없다.
	//
	// 월드 스페이스 스폰이다 — 메시에 부착하면 안 된다. 부착했을 때 두 가지가 깨졌다:
	// (1) 잔상이 플레이어를 따라다녀 "지나간 자리에 남는다"는 표현 자체가 성립하지 않고,
	// (2) 캐릭터 메시의 회전(Yaw 오프셋 포함)을 물려받아 대쉬 방향과 어긋난다.
	// 위치는 메시 컴포넌트 원점(발밑)을 쓴다 — 캡슐 중심(GetActorLocation)은 90uu 떠 있다.
	// 회전은 대쉬 방향의 역방향이다. 애셋(NS_Dash_Ghost)이 자기 +X 로 뻗도록 만들어져 있어
	// DashDir 을 그대로 주면 이펙트가 진행 방향으로 앞서 나간다 — 잔상은 지나온 쪽으로
	// 흘러야 하므로 뒤집는다. 실측으로 확인한 값이다(육안).
	if (DashVfx && GetMesh())
	{
		// 기준점은 메시 컴포넌트 원점(발밑). 캡슐 중심(GetActorLocation)은 90uu 떠 있다.
		// 거기서 전후(대쉬 방향) · 상하(월드 Z) 로 보정한다 — 둘 다 콘솔로 조절한다.
		const FVector SpawnLoc = GetMesh()->GetComponentLocation()
			+ DashDir * CVarDashVfxOffsetFwd.GetValueOnGameThread()
			+ FVector::UpVector * CVarDashVfxOffsetUp.GetValueOnGameThread();

		const float Scale = CVarDashVfxScale.GetValueOnGameThread();

		UNiagaraFunctionLibrary::SpawnSystemAtLocation(
			GetWorld(),
			DashVfx,
			SpawnLoc,
			(-DashDir).Rotation(),
			FVector(Scale));
	}

	if (!DashAnim)
	{
		return;
	}
	UAnimInstance* AnimInst = GetMesh() ? GetMesh()->GetAnimInstance() : nullptr;
	if (!AnimInst)
	{
		return;
	}

	// MM_Dash는 루트모션 포함 AnimSequence(bEnableRootMotion — 애셋 자체 플래그, 공용이라 미변경).
	// UAnimMontage::bEnableRootMotionTranslation/Rotation은 4.5부터 PostLoad 동기화 전용 deprecated
	// 필드라 런타임 생성 다이나믹 몽타주에는 효과 없음 — 실제 추출은 AnimSequence 플래그가 좌우한다.
	// 기본 RootMotionMode(RootMotionFromMontagesOnly)에서 그 루트모션이 CharacterMovement에 그대로 먹혀
	// ApplyRootMotionConstantForce와 충돌, 대쉬 거리가 짧아지는 회귀(680→405)가 났다 (PR #59).
	// 몽타주 재생 구간만 IgnoreRootMotion(추출은 하되 적용은 안 함)으로 전환해 "코스메틱 독립"을 실제로 성립시킨다.
	// 이 함수 자체가 서버·클라 공통 경로이므로 이 가드는 클라 재생에도 그대로 걸린다.
	AnimInst->SetRootMotionMode(ERootMotionMode::IgnoreRootMotion);

	// PlaySlotAnimationAsDynamicMontage는 float가 아니라 UAnimMontage*를 반환 — 길이는 GetPlayLength()로 조회.
	UAnimMontage* PlayedMontage = AnimInst->PlaySlotAnimationAsDynamicMontage(
		DashAnim, FName("DefaultSlot"), /*BlendInTime=*/0.1f, /*BlendOutTime=*/0.1f);
	if (!PlayedMontage)
	{
		// 재생 실패 — 억제할 루트모션도 없으므로 즉시 기본 모드 복귀.
		AnimInst->SetRootMotionMode(ERootMotionMode::RootMotionFromMontagesOnly);
		return;
	}

	const float Len = PlayedMontage->GetPlayLength();
	UE_LOG(LogTemp, Log, TEXT("[Dash] anim len=%.2f (role=%s)"), Len, *UEnum::GetValueAsString(GetLocalRole()));

	// 몽타주 재생이 끝나면 기본 모드로 복귀.
	// 약참조로 캡처한다: raw 포인터를 캡처하면 월드 정리(접속 종료/레벨 전환) 중 타이머가 돌 때
	// 이미 파괴된 AnimInstance를 IsValid()가 역참조해 UObjectArray.h:1083 assert로 죽는다.
	// 데디 클라 실측 크래시(#75) — 서버가 먼저 종료되자 클라 타이머가 이 경로로 터졌다.
	TWeakObjectPtr<UAnimInstance> WeakAnimInst(AnimInst);
	FTimerHandle RestoreRootMotionTimer;
	GetWorldTimerManager().SetTimer(RestoreRootMotionTimer, FTimerDelegate::CreateLambda([WeakAnimInst]()
	{
		if (UAnimInstance* Anim = WeakAnimInst.Get())
		{
			Anim->SetRootMotionMode(ERootMotionMode::RootMotionFromMontagesOnly);
		}
	}), Len, false);
}

float ARECharacterBase::TakeDamage(float DamageAmount, const FDamageEvent& DamageEvent,
                                   AController* EventInstigator, AActor* DamageCauser)
{
	// 서버 권위 가드 — 게임상태(Health) 변경은 서버에서만
	if (!HasAuthority())
	{
		return 0.f;
	}

	// ponytail: CVar는 클라 로컬 — PIE/단일프로세스만 유효, 실 데디 서버 미지원(데브 치트)
	if (CVarPlayerInvincible.GetValueOnGameThread() != 0)
	{
		return 0.f;
	}

	const float Applied = Super::TakeDamage(DamageAmount, DamageEvent, EventInstigator, DamageCauser);
	Health = FMath::Clamp(Health - Applied, 0.f, MaxHealth);
	OnRep_Health(); // 서버/싱글 경로 — 복제 OnRep은 원격 클라 전용이라 직접 호출

	// 패배 판정 — 이미 HasAuthority 가드 안. 사망 후에도 뜬 탄환이 계속 때리므로 bIsDead로 재진입 차단.
	if (Health <= 0.f && !bIsDead)
	{
		bIsDead = true;
		UE_LOG(LogTemp, Log, TEXT("[RE] Player died (Health<=0)"));
		if (AREGameMode* GM = GetWorld()->GetAuthGameMode<AREGameMode>())
		{
			// 전원 사망이어야 패배다 — 판정은 GameMode가 한다 (#85).
			GM->NotifyPlayerDied(Cast<APlayerController>(GetController()));
		}
	}

	return Applied;
}

void ARECharacterBase::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ARECharacterBase, Health);

	// 이동 목표는 본인만 필요하다 (#112) — 남의 폰 예측에는 쓰지 않는다.
	DOREPLIFETIME_CONDITION(ARECharacterBase, MoveTarget, COND_AutonomousOnly);
	DOREPLIFETIME_CONDITION(ARECharacterBase, bHasMoveTarget, COND_AutonomousOnly);
}

void ARECharacterBase::SetMoveTarget(const FVector& InTarget)
{
	if (!HasAuthority())
	{
		return;   // 서버 권위 — 클라가 스스로 목표를 세우지 못한다
	}
	MoveTarget = InTarget;
	bHasMoveTarget = true;
}

void ARECharacterBase::ClearMoveTarget()
{
	if (!HasAuthority())
	{
		return;
	}
	bHasMoveTarget = false;
}

UAbilitySystemComponent* ARECharacterBase::GetAbilitySystemComponent() const
{
	return AbilitySystemComponent;
}

void ARECharacterBase::InitASCActorInfo()
{
	// OwnerActor=AvatarActor=this (Pawn 소유). 오너 클라 판정은 Pawn→Controller 소유 체인으로 엔진이 처리.
	AbilitySystemComponent->InitAbilityActorInfo(this, this);
	UE_LOG(LogTemp, Log, TEXT("[GAS] ASC ActorInfo set (role=%s)"), *UEnum::GetValueAsString(GetLocalRole()));
}

void ARECharacterBase::PossessedBy(AController* NewController)
{
	Super::PossessedBy(NewController);
	// 서버 권위 경로 — Possess 즉시 ActorInfo 세팅.
	InitASCActorInfo();

	// 서버에서 대쉬 어빌리티 부여(1회). 클라는 부여받은 스펙이 복제됨.
	if (HasAuthority() && AbilitySystemComponent && !DashAbilityHandle.IsValid())
	{
		DashAbilityHandle = AbilitySystemComponent->GiveAbility(
			FGameplayAbilitySpec(UREGA_Dash::StaticClass(), /*Level=*/1, /*InputID=*/INDEX_NONE, this));
	}
}

void ARECharacterBase::OnRep_PlayerState()
{
	Super::OnRep_PlayerState();
	// 클라 경로 — PlayerState 복제 도착 후 ActorInfo 세팅.
	InitASCActorInfo();
}

bool ARECharacterBase::TryDash(FVector Dir)
{
	if (!AbilitySystemComponent)
	{
		return false;
	}
	// 방향 저장(어빌리티가 소비) 후 클래스로 활성 시도. 쿨다운 중이면 false.
	PendingDashDir = Dir.GetSafeNormal2D();
	return AbilitySystemComponent->TryActivateAbilityByClass(UREGA_Dash::StaticClass());
}

void ARECharacterBase::Multicast_PlayFire_Implementation(UAnimSequence* FireAnim, FVector_NetQuantize BeamEnd, bool bHit)
{
	// Multicast는 서버에서도 실행된다. 데디 서버는 화면이 없으므로 코스메틱 재생을 생략한다.
	// 리슨서버/싱글은 여기서 딱 한 번 재생 — 컴포넌트의 직접 재생을 제거했으므로 이중 재생 경로가 없다.
	if (IsNetMode(NM_DedicatedServer))
	{
		return;
	}

	// 애님과 이펙트는 각자 판단한다 — 애님 애셋은 UE5 템플릿 경로(.gitignore 대상)라
	// 없는 환경이 실제로 존재한다. 묶어서 return하면 총이 없다는 이유로 빔까지 사라진다.
	if (FireAnim)
	{
		if (UAnimInstance* AnimInst = GetMesh() ? GetMesh()->GetAnimInstance() : nullptr)
		{
			// DefaultSlot 다이나믹 몽타주 — MM_Dash와 같은 경로다 (#132).
			// 원래는 MM_Pistol_Fire_Montage(Arms 슬롯)를 재생했는데, ABP_Unarmed에 Arms 슬롯
			// 노드가 없어 포즈를 받아 줄 곳이 없었다. 슬롯이 없어도 재생 자체는 성공하므로
			// Montage_Play 반환값도 Montage_IsPlaying도 정상이었다 — 로그로는 안 잡혔다.
			//
			// 상체 블렌드가 아니라 전신으로 두는 이유: 이 게임은 발사하면 이동이 멈춘다
			// (REPlayerController::Server_RequestFire의 StopMovementImmediately). 하체가
			// 따로 돌 일이 없으므로 상체 블렌드를 위해 애님BP를 손댈 이유가 없다.
			//
			// MM_Pistol_Fire는 루트모션이 없다(실측) — 대쉬처럼 IgnoreRootMotion으로 감쌀 필요가 없다.
			UAnimMontage* Played = AnimInst->PlaySlotAnimationAsDynamicMontage(
				FireAnim, FName("DefaultSlot"), /*BlendInTime=*/0.05f, /*BlendOutTime=*/0.1f);
			// 로그 문구를 "montage"로 유지한다 — scripts/dedi-verify.ps1 이 이 문자열로
			// "클라 재생 / 서버 생략"을 판정한다. 재생 방식이 바뀌었어도 검증하는 사실은
			// 그대로이므로, 코드와 검증기를 같은 커밋에서 함께 바꿔 게이트를 느슨하게 만들지 않는다.
			// (다이나믹 몽타주도 실제로 UAnimMontage 다 — 문구가 틀린 것도 아니다.)
			UE_LOG(LogTemp, Log, TEXT("[Attack] fire montage len=%.2f"),
				Played ? Played->GetPlayLength() : -1.f);
		}
	}

	ShowFireFx(BeamEnd);

	// 임팩트는 막힌 지점에만. 빗나간 발의 BeamEnd 는 허공(사거리 끝)이라 거기서 터지면 거짓이다.
	if (bHit)
	{
		REExplosionFx::SpawnBulletExplosion(GetWorld(), BeamEnd);
	}
}

void ARECharacterBase::OnRep_Health()
{
	if (HealthBar)
	{
		HealthBar->SetHealthPercent(MaxHealth > 0.f ? Health / MaxHealth : 0.f);
	}
}
