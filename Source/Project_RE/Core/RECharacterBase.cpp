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

// 치트: 1이면 플레이어 무적(TakeDamage 무피해). 데브 전용, 클라 로컬(ECVF_Cheat).
static TAutoConsoleVariable<int32> CVarPlayerInvincible(
	TEXT("re.Cheat.PlayerInvincible"),
	0,
	TEXT("1 = player takes no damage (dev cheat, client-local)"),
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

	// 마네킹 메시 로드 (ConstructorHelpers, 실패해도 크래시 없이 진행)
	static ConstructorHelpers::FObjectFinder<USkeletalMesh> MeshAsset(
		TEXT("/Game/Characters/Mannequins/Meshes/SKM_Manny_Simple.SKM_Manny_Simple"));
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
}

void ARECharacterBase::Multicast_PlayDashMontage_Implementation()
{
	// 데디 서버는 화면이 없으므로 코스메틱 재생을 생략한다 (#74 Multicast_PlayFireMontage와 동일 패턴).
	// "데디는 AnimInstance가 null이라 자연 no-op"으로 봤던 초안 가정은 실측으로 반증됐다 —
	// 데디 서버 로그에 `[Dash] anim len=0.97 (role=ROLE_Authority)`가 찍혔다. 가드가 없으면
	// 서버가 IgnoreRootMotion으로 전환한 채 몽타주를 돌려 서버 권위 이동 경로에 개입한다.
	if (!DashAnim || IsNetMode(NM_DedicatedServer))
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
			GM->EndGame(/*bVictory=*/false);
		}
	}

	return Applied;
}

void ARECharacterBase::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ARECharacterBase, Health);
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

void ARECharacterBase::Multicast_PlayFireMontage_Implementation(UAnimMontage* Montage)
{
	// Multicast는 서버에서도 실행된다. 데디 서버는 화면이 없으므로 코스메틱 재생을 생략한다.
	// 리슨서버/싱글은 여기서 딱 한 번 재생 — 컴포넌트의 직접 재생을 제거했으므로 이중 재생 경로가 없다.
	if (!Montage || IsNetMode(NM_DedicatedServer))
	{
		return;
	}

	if (UAnimInstance* AnimInst = GetMesh() ? GetMesh()->GetAnimInstance() : nullptr)
	{
		const float Len = AnimInst->Montage_Play(Montage, 1.0f);
		UE_LOG(LogTemp, Log, TEXT("[Attack] fire montage len=%.2f"), Len);
	}
}

void ARECharacterBase::OnRep_Health()
{
	if (HealthBar)
	{
		HealthBar->SetHealthPercent(MaxHealth > 0.f ? Health / MaxHealth : 0.f);
	}
}
