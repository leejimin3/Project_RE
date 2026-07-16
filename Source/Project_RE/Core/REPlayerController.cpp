// Copyright Epic Games, Inc. All Rights Reserved.

#include "REPlayerController.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "InputActionValue.h"
#include "Engine/LocalPlayer.h"
#include "GameFramework/Pawn.h"
#include "Core/RECharacterBase.h"
#include "Blueprint/AIBlueprintHelperLibrary.h"
#include "NavigationSystem.h"
#include "Misc/App.h"
#include "TimerManager.h"
#include "AbilitySystemComponent.h"
#include "AbilitySystemGlobals.h"
#include "Abilities/REGameplayTags.h"
#include "HAL/PlatformMisc.h"
#include "REResultWidget.h"
#include "Blueprint/UserWidget.h"

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

	// 스페이스 대쉬 IA (코드생성, transient)
	DashAction = NewObject<UInputAction>(this, TEXT("IA_Dash"));
	DashAction->ValueType = EInputActionValueType::Boolean;
	TopDownMappingContext->MapKey(DashAction, EKeys::SpaceBar);

	if (UEnhancedInputComponent* EIC = Cast<UEnhancedInputComponent>(InputComponent))
	{
		EIC->BindAction(ClickMoveAction, ETriggerEvent::Triggered, this, &AREPlayerController::OnClickMove);
		EIC->BindAction(DashAction, ETriggerEvent::Started, this, &AREPlayerController::OnDash);
	}
}

void AREPlayerController::BeginPlay()
{
	Super::BeginPlay();

	if (IsLocalPlayerController())
	{
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

	// 헤드리스(-unattended) 서버권위 이동 프로브. 실플레이(PIE/에디터)엔 무발동.
	if (HasAuthority() && FApp::IsUnattended())
	{
		RunHeadlessMoveProbe();
		RunHeadlessDashProbe();
	}
}

void AREPlayerController::OnClickMove(const FInputActionValue& Value)
{
	// 클릭 검출은 로컬(커서/카메라는 로컬 전용). 해석된 월드 좌표만 서버로.
	FHitResult Hit;
	if (GetHitResultUnderCursor(ECC_Visibility, false, Hit) && Hit.bBlockingHit)
	{
		Server_RequestMove(Hit.ImpactPoint);
	}
}

void AREPlayerController::OnDash(const FInputActionValue& Value)
{
	// 커서 아래 지점 방향을 로컬에서 계산(폰→커서 XY). 서버로 방향만 전달.
	APawn* P = GetPawn();
	FHitResult Hit;
	if (!P || !GetHitResultUnderCursor(ECC_Visibility, false, Hit) || !Hit.bBlockingHit)
	{
		return;
	}
	const FVector Dir = (Hit.ImpactPoint - P->GetActorLocation()).GetSafeNormal2D();
	if (!Dir.IsNearlyZero())
	{
		Server_Dash(Dir);
	}
}

void AREPlayerController::Server_Dash_Implementation(FVector Dir)
{
	// 서버 권위 — 폰의 대쉬 어빌리티 활성(쿨다운은 어빌리티가 검사).
	if (ARECharacterBase* Char = Cast<ARECharacterBase>(GetPawn()))
	{
		Char->TryDash(Dir);
	}
}

void AREPlayerController::Client_ShowResult_Implementation(bool bVictory)
{
	if (UREResultWidget* Result = CreateWidget<UREResultWidget>(this, UREResultWidget::StaticClass()))
	{
		Result->SetResult(bVictory);
		Result->AddToViewport();
	}

	// 이동/대쉬 입력 차단 — 입력은 클라 소유물이라 여기가 제자리.
	DisableInput(this);

	UE_LOG(LogTemp, Log, TEXT("[RE] Client_ShowResult: %s"), bVictory ? TEXT("VICTORY") : TEXT("DEFEAT"));
}

void AREPlayerController::Server_RequestMove_Implementation(FVector Target)
{
	// 서버 권위 — nav 검증 후 패스팔로잉 구동.
	UNavigationSystemV1* NavSys = UNavigationSystemV1::GetCurrent(GetWorld());
	FNavLocation NavLoc;
	if (!NavSys || !NavSys->ProjectPointToNavigation(Target, NavLoc))
	{
		UE_LOG(LogTemp, Warning, TEXT("[Move] rejected: off-navmesh %s"), *Target.ToString());
		return;
	}

	UE_LOG(LogTemp, Log, TEXT("[Move] Server_RequestMove recv target=%s"), *NavLoc.Location.ToString());
	UAIBlueprintHelperLibrary::SimpleMoveToLocation(this, NavLoc.Location);
}

void AREPlayerController::RunHeadlessMoveProbe()
{
	// 폰 possess 완료(BeginPlay 직후 possess 타이밍 여유) 후 1.0s에 자기이동 1회 + 오프메시 거부 1회.
	FTimerDelegate MoveDel = FTimerDelegate::CreateLambda([this]()
	{
		APawn* P = GetPawn();
		if (!P)
		{
			UE_LOG(LogTemp, Warning, TEXT("[Move] probe: no pawn"));
			return;
		}
		// 시작점에서 +Y 500 만큼 떨어진 목표(nav 위 예상).
		// +Y인 이유(#56): 보스가 +X 600에 있음 — +X 목표는 탄막 정면 진입(사망)이고,
		// 대쉬 프로브(+X)도 보스 캡슐에 막혀 거리 게이트가 무효화됨. 이동을 +Y로 빼면
		// 대쉬(+X)가 Y≈450에서 발사돼 보스와 안 겹침.
		ProbeTarget = P->GetActorLocation() + FVector(0.f, 500.f, 0.f);
		UE_LOG(LogTemp, Log, TEXT("[Move] probe start: pawn=%s target=%s"),
			*P->GetActorLocation().ToString(), *ProbeTarget.ToString());

		// 정상 이동 요청.
		Server_RequestMove(ProbeTarget);

		// 오프메시 거부 검증: 맵 밖 좌표 1회.
		Server_RequestMove(FVector(100000.f, 100000.f, 0.f));

		// 0.5s마다 목표까지 거리 로그(수렴 관측), 5s간.
		FTimerDelegate LogDel = FTimerDelegate::CreateLambda([this]()
		{
			if (APawn* Pn = GetPawn())
			{
				const float Dist = FVector::Dist2D(Pn->GetActorLocation(), ProbeTarget);
				UE_LOG(LogTemp, Log, TEXT("[Move] probe dist=%.1f loc=%s"),
					Dist, *Pn->GetActorLocation().ToString());
			}
		});
		GetWorld()->GetTimerManager().SetTimer(ProbeLogTimer, LogDel, 0.5f, /*bLoop=*/true);
	});
	GetWorld()->GetTimerManager().SetTimer(ProbeMoveTimer, MoveDel, 1.0f, /*bLoop=*/false);
}

void AREPlayerController::RunHeadlessDashProbe()
{
	// t=2.0s: 대쉬 1회 + 즉시 재시도(쿨다운 차단 확인). 이후 거리 측정, 2.1s 후 재활성, 종료.
	FTimerDelegate DashDel = FTimerDelegate::CreateLambda([this]()
	{
		ARECharacterBase* Char = Cast<ARECharacterBase>(GetPawn());
		if (!Char)
		{
			UE_LOG(LogTemp, Warning, TEXT("[Dash] probe: no pawn"));
			return;
		}
		UAbilitySystemComponent* ASC = UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(Char);
		ProbeDashStart = Char->GetActorLocation();

		// 1) 정상 대쉬(+X 방향).
		const bool bFirst = Char->TryDash(FVector::ForwardVector);
		const bool bDashingTag = ASC && ASC->HasMatchingGameplayTag(RETag_State_Dashing);
		UE_LOG(LogTemp, Log, TEXT("[Dash] activate ok=%d State.Dashing=%d"), bFirst, bDashingTag);

		// 2) 즉시 재시도 → 쿨다운 차단 기대.
		const bool bBlocked = Char->TryDash(FVector::ForwardVector);
		UE_LOG(LogTemp, Log, TEXT("[Dash] immediate retry activated=%d (0=blocked by cooldown, 기대 0)"), bBlocked);

		// 3) 0.3s 후 이동거리 측정(RootMotion 완료 뒤).
		FTimerHandle DistTimer;
		FTimerDelegate DistDel = FTimerDelegate::CreateLambda([this]()
		{
			if (APawn* Pn = GetPawn())
			{
				const float Dist = FVector::Dist2D(Pn->GetActorLocation(), ProbeDashStart);
				UE_LOG(LogTemp, Log, TEXT("[Dash] dist=%.1f (기대 ~600)"), Dist);
			}
		});
		GetWorld()->GetTimerManager().SetTimer(DistTimer, DistDel, 0.3f, false);

		// 4) 2.1s 후(쿨다운 만료) 재활성 → 성공 기대. 그 뒤 종료.
		FTimerHandle ReTimer;
		FTimerDelegate ReDel = FTimerDelegate::CreateLambda([this]()
		{
			bool bReactivated = false;
			if (ARECharacterBase* Pn = Cast<ARECharacterBase>(GetPawn()))
			{
				bReactivated = Pn->TryDash(FVector::ForwardVector);
			}
			UE_LOG(LogTemp, Log, TEXT("[Dash] re-activate ok=%d (기대 1, 쿨다운 만료)"), bReactivated);
			UE_LOG(LogTemp, Log, TEXT("[Dash] probe done — exiting"));
			// headless 프로세스 자체 종료(결정적 실행).
			FPlatformMisc::RequestExit(false);
		});
		GetWorld()->GetTimerManager().SetTimer(ReTimer, ReDel, 2.1f, false);
	});
	GetWorld()->GetTimerManager().SetTimer(ProbeDashTimer, DashDel, 2.0f, false);
}
