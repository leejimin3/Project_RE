// Copyright Epic Games, Inc. All Rights Reserved.

#include "REPlayerController.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "InputActionValue.h"
#include "Engine/LocalPlayer.h"
#include "GameFramework/Pawn.h"

void AREPlayerController::SetupInputComponent()
{
	Super::SetupInputComponent();

	// 입력은 로컬 컨트롤러에만 배선 (M4 데디 대비)
	if (!IsLocalPlayerController())
	{
		return;
	}

	// uasset 없이 코드로 IA/IMC 생성 (transient — 매 실행 생성)
	ClickMoveAction = NewObject<UInputAction>(this, TEXT("IA_ClickMove"));
	ClickMoveAction->ValueType = EInputActionValueType::Boolean;

	TopDownMappingContext = NewObject<UInputMappingContext>(this, TEXT("IMC_TopDown"));
	TopDownMappingContext->MapKey(ClickMoveAction, EKeys::RightMouseButton);

	if (UEnhancedInputComponent* EIC = Cast<UEnhancedInputComponent>(InputComponent))
	{
		EIC->BindAction(ClickMoveAction, ETriggerEvent::Triggered, this, &AREPlayerController::OnClickMove);
	}
}

void AREPlayerController::BeginPlay()
{
	Super::BeginPlay();

	if (!IsLocalPlayerController())
	{
		return;
	}

	// 탑뷰 클릭 이동 — 마우스 커서 표시
	bShowMouseCursor = true;
	DefaultMouseCursor = EMouseCursor::Default;

	if (UEnhancedInputLocalPlayerSubsystem* Subsystem =
			ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(GetLocalPlayer()))
	{
		if (TopDownMappingContext)
		{
			Subsystem->AddMappingContext(TopDownMappingContext, 0);
		}
	}
}

void AREPlayerController::OnClickMove(const FInputActionValue& Value)
{
	UE_LOG(LogTemp, Log, TEXT("[RE] OnClickMove triggered"));

	// 커서 아래 월드 지점을 이동 목표로 설정
	// TODO M2: 로컬 이동을 Server RPC 이동 요청 + NavMesh 패스파인딩으로 교체
	FHitResult Hit;
	if (GetHitResultUnderCursor(ECC_Visibility, false, Hit) && Hit.bBlockingHit)
	{
		MoveTarget = Hit.ImpactPoint;
		bMoveToTarget = true;
	}
}

void AREPlayerController::PlayerTick(float DeltaTime)
{
	Super::PlayerTick(DeltaTime);

	if (!bMoveToTarget)
	{
		return;
	}

	APawn* ControlledPawn = GetPawn();
	if (!ControlledPawn)
	{
		bMoveToTarget = false;
		return;
	}

	FVector ToTarget = MoveTarget - ControlledPawn->GetActorLocation();
	ToTarget.Z = 0.f; // 수평 이동만

	if (ToTarget.SizeSquared() <= AcceptanceRadius * AcceptanceRadius)
	{
		bMoveToTarget = false; // 도달
		return;
	}

	ControlledPawn->AddMovementInput(ToTarget.GetSafeNormal());
}
