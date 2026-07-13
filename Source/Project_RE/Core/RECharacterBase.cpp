// Copyright Epic Games, Inc. All Rights Reserved.

#include "RECharacterBase.h"
#include "GameFramework/SpringArmComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Camera/CameraComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Animation/AnimInstance.h"
#include "Engine/SkeletalMesh.h"
#include "UObject/ConstructorHelpers.h"
#include "Net/UnrealNetwork.h"
#include "AbilitySystemComponent.h"
#include "REAutoFireComponent.h"
#include "GameplayAbilitySpec.h"
#include "Abilities/REGA_Dash.h"

ARECharacterBase::ARECharacterBase()
{
	PrimaryActorTick.bCanEverTick = false;

	// 폰이 컨트롤러 회전을 따라가지 않음 — 탑뷰 카메라 고정 유지
	bUseControllerRotationYaw = false;
	bUseControllerRotationPitch = false;
	bUseControllerRotationRoll = false;

	// 체력 초기화 — MaxHealth 조정 시 정합 유지
	Health = MaxHealth;

	// GAS: ASC 부착 — Pawn 소유, 복제 켜고 Mixed 모드(오너 클라만 GE 복제).
	AbilitySystemComponent = CreateDefaultSubobject<UAbilitySystemComponent>(TEXT("AbilitySystemComponent"));
	AbilitySystemComponent->SetIsReplicated(true);
	AbilitySystemComponent->SetReplicationMode(EGameplayEffectReplicationMode::Mixed);

	// 자동사격 부착 — 컴포넌트가 BeginPlay에서 서버 여부를 스스로 판단.
	AutoFireComponent = CreateDefaultSubobject<UREAutoFireComponent>(TEXT("AutoFire"));

	// 이동 방향으로 캐릭터가 회전 (탑뷰 클릭 이동 시 진행 방향을 바라봄)
	GetCharacterMovement()->bOrientRotationToMovement = true;
	GetCharacterMovement()->RotationRate = FRotator(0.f, 640.f, 0.f);

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
}

float ARECharacterBase::TakeDamage(float DamageAmount, const FDamageEvent& DamageEvent,
                                   AController* EventInstigator, AActor* DamageCauser)
{
	// 서버 권위 가드 — 게임상태(Health) 변경은 서버에서만
	if (!HasAuthority())
	{
		return 0.f;
	}

	const float Applied = Super::TakeDamage(DamageAmount, DamageEvent, EventInstigator, DamageCauser);
	Health = FMath::Clamp(Health - Applied, 0.f, MaxHealth);
	// TODO M5: 서버권위 피격 판정/이펙트, 사망 처리
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
