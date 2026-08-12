// Copyright Epic Games, Inc. All Rights Reserved.

#include "REPlayerController.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "InputActionValue.h"
#include "Engine/LocalPlayer.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
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
#include "RECheatPanelWidget.h"
#include "Blueprint/UserWidget.h"
#include "Core/REAttackComponent.h"
#include "Core/REBossCharacter.h"
#include "Core/REGameMode.h"
#include "GameFramework/PawnMovementComponent.h"
#include "EngineUtils.h"

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

	// 좌클릭 공격 IA (코드생성, transient). Boolean+트리거 없음 = 홀드 동안 매 프레임 Triggered.
	FireAction = NewObject<UInputAction>(this, TEXT("IA_Fire"));
	FireAction->ValueType = EInputActionValueType::Boolean;
	TopDownMappingContext->MapKey(FireAction, EKeys::LeftMouseButton);

	// F1 치트 패널 토글 IA (코드생성, transient).
	CheatPanelAction = NewObject<UInputAction>(this, TEXT("IA_CheatPanel"));
	CheatPanelAction->ValueType = EInputActionValueType::Boolean;
	TopDownMappingContext->MapKey(CheatPanelAction, EKeys::F1);

	if (UEnhancedInputComponent* EIC = Cast<UEnhancedInputComponent>(InputComponent))
	{
		EIC->BindAction(ClickMoveAction, ETriggerEvent::Triggered, this, &AREPlayerController::OnClickMove);
		EIC->BindAction(DashAction, ETriggerEvent::Started, this, &AREPlayerController::OnDash);
		EIC->BindAction(FireAction, ETriggerEvent::Triggered, this, &AREPlayerController::OnFire);
		EIC->BindAction(CheatPanelAction, ETriggerEvent::Started, this, &AREPlayerController::OnToggleCheatPanel);
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

		// 준비 완료를 서버에 알린다 — 싱글/리슨에서는 권한 보유라 즉시 로컬 실행된다.
		Server_NotifyReady();
	}

	// 헤드리스(-unattended) 서버권위 이동 프로브. 실플레이(PIE/에디터)엔 무발동.
	if (HasAuthority() && FApp::IsUnattended())
	{
		RunHeadlessMoveProbe();
		RunHeadlessFireProbe();
		RunHeadlessDashProbe();
	}
}

void AREPlayerController::PlayerTick(float DeltaTime)
{
	Super::PlayerTick(DeltaTime);

	// 오너 클라 전용 회전 구동 (#79). 서버는 CMC의 bOrientRotationToMovement가 그대로 담당한다.
	if (HasAuthority())
	{
		return;
	}

	ACharacter* Char = Cast<ACharacter>(GetPawn());
	if (!Char)
	{
		return;
	}
	UCharacterMovementComponent* Move = Char->GetCharacterMovement();
	if (!Move)
	{
		return;
	}

	// 폰이 새로 잡히면 1회 설정. 오너 클라의 CMC 회전 경로는 죽어 있다 —
	// 이 프로젝트의 이동은 입력 예측형이 아니라 서버 패스팔로잉이므로 클라는 Acceleration=0,
	// bHasRequestedVelocity=false → ComputeOrientToMovementRotation이 CurrentRotation을 그대로 반환한다.
	// 꺼도 잃는 것이 없고, 동시에 서버 보정이 회전을 되돌리는 분기의 조건이 불성립해진다
	// (ClientAdjustPosition_Implementation: bUseLastGoodRotationDuringCorrection && bOrientRotationToMovement).
	if (FacingPawn.Get() != Char)
	{
		FacingPawn = Char;
		Move->bOrientRotationToMovement = false;
	}

	// 발사 직후 락 구간에는 커서 회전을 매 틱 다시 세운다.
	// 한 번만 세우고 손을 놓으면, 그 사이 서버 보정이 회전을 되돌렸을 때 복구하지 못해
	// "돌았다가 되돌아옴"이 된다 (#79 실측). 회전은 코스메틱이므로 재적용 비용은 무시할 만하다.
	const double Now = GetWorld()->GetTimeSeconds();
	if (FacingLockUntil >= 0.0 && Now < FacingLockUntil)
	{
		// 서버 보정이 회전을 덮은 흔적. 실측상 1분 플레이에 100회 넘게 발생하므로 Verbose로 둔다
		// (기본 출력 안 됨). 회전 문제가 재발하면 `Log LogTemp Verbose`로 켜서 관측한다.
		const FRotator Cur = Char->GetActorRotation();
		if (FMath::Abs(FRotator::NormalizeAxis(Cur.Yaw - FacingLockYaw)) > 2.f)
		{
			UE_LOG(LogTemp, Verbose, TEXT("[Facing] reverted: cur=%.1f expected=%.1f"), Cur.Yaw, FacingLockYaw);
		}
		Char->SetActorRotation(FRotator(0.f, FacingLockYaw, 0.f));
		return;
	}

	// 이동 중에는 속도 방향을 본다. Velocity는 서버 보정으로 갱신되므로 서버 결과와 수렴한다.
	// 정지 상태(속도 ~0)에서는 현재 회전을 유지 — 서버 CMC도 같은 조건에서 회전하지 않는다.
	const FVector Vel = Char->GetVelocity();
	if (Vel.SizeSquared2D() < 1.f)
	{
		return;
	}

	// 보간 속도는 서버와 동일 출처(RotationRate.Yaw)를 쓴다 — 상수 중복을 만들지 않는다.
	const FRotator Target(0.f, Vel.Rotation().Yaw, 0.f);
	Char->SetActorRotation(
		FMath::RInterpConstantTo(Char->GetActorRotation(), Target, DeltaTime, Move->RotationRate.Yaw));
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
		// 입력 시각 기준점 — 서버 [Dash] activate ok / 클라 [Dash] anim 로그와의 타임스탬프 차가 곧 입력→대쉬 지연(#75 측정).
		UE_LOG(LogTemp, Log, TEXT("[Dash] input sent (local)"));
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

void AREPlayerController::Server_NotifyReady_Implementation()
{
	if (AREGameMode* GM = GetWorld() ? GetWorld()->GetAuthGameMode<AREGameMode>() : nullptr)
	{
		GM->NotifyPlayerReady();
	}
}

void AREPlayerController::OnFire(const FInputActionValue& Value)
{
	// 홀드 연사 — Triggered가 매 프레임 오므로 로컬에서 AttackInterval 주기로 페이싱.
	// 서버도 rate limit을 재검증하므로 이 페이싱은 RPC 트래픽 절약용.
	APawn* P = GetPawn();
	if (!P)
	{
		return;
	}
	UREAttackComponent* Attack = P->FindComponentByClass<UREAttackComponent>();
	if (!Attack)
	{
		return;
	}
	const double Now = GetWorld()->GetTimeSeconds();
	if (LastFireRequestTime >= 0.0 && Now - LastFireRequestTime < Attack->GetAttackInterval())
	{
		return;
	}

	// 커서 방향 계산은 로컬(커서/카메라는 로컬 전용) — 방향만 서버로 (OnDash 동일 패턴).
	FHitResult Hit;
	if (!GetHitResultUnderCursor(ECC_Visibility, false, Hit) || !Hit.bBlockingHit)
	{
		return;
	}
	const FVector Dir = (Hit.ImpactPoint - P->GetActorLocation()).GetSafeNormal2D();
	if (Dir.IsNearlyZero())
	{
		return;
	}
	LastFireRequestTime = Now;

	// 데디 회전 보정 (#72) — 폰 회전은 오너 클라에 복제되지 않는다
	// (ReplicatedMovement=COND_SimulatedOrPhysics, CMC::ShouldCorrectRotation()=false).
	// 서버가 Server_RequestFire에서 하는 커서 방향 회전을 내 화면에서도 보이게 로컬로 같이 돈다.
	// 리슨/싱글에서는 서버가 같은 값을 다시 넣으므로 무해.
	P->SetActorRotation(FRotator(0.f, Dir.Rotation().Yaw, 0.f));

	// 발사 후 짧은 락 (#79) — 서버 StopMovement가 도달하기 전 남은 속도 때문에
	// PlayerTick의 속도 기준 회전이 방금 세운 커서 회전을 덮는 것을 막는다.
	// PlayerTick이 이 각도를 매 틱 다시 세우므로 서버 보정이 되돌려도 복구된다.
	FacingLockYaw = Dir.Rotation().Yaw;
	FacingLockUntil = Now + Attack->GetAttackInterval();

	Server_RequestFire(Dir);
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

void AREPlayerController::OnToggleCheatPanel()
{
	if (!IsLocalPlayerController())
	{
		return;
	}
	if (!CheatPanel)
	{
		CheatPanel = CreateWidget<URECheatPanelWidget>(this, URECheatPanelWidget::StaticClass());
		if (CheatPanel)
		{
			CheatPanel->AddToViewport(100);   // 생성 시 표시 상태
		}
		return;
	}
	const bool bVisible = CheatPanel->GetVisibility() == ESlateVisibility::Visible;
	CheatPanel->SetVisibility(bVisible ? ESlateVisibility::Collapsed : ESlateVisibility::Visible);
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

void AREPlayerController::Server_RequestFire_Implementation(FVector Dir)
{
	// 게임오버 후 잔여 RPC 무시 — 구 AutoFire StopFiring의 대체.
	AREGameMode* GM = GetWorld()->GetAuthGameMode<AREGameMode>();
	if (GM && GM->IsGameOver())
	{
		return;
	}

	APawn* P = GetPawn();
	if (!P)
	{
		return;
	}
	// 클라 입력 신뢰 금지 — 서버에서 재정규화.
	const FVector Dir2D = Dir.GetSafeNormal2D();
	if (Dir2D.IsNearlyZero())
	{
		return;
	}
	UREAttackComponent* Attack = P->FindComponentByClass<UREAttackComponent>();
	if (!Attack)
	{
		return;
	}

	// 대쉬 중 발사 금지 (#80). 상태는 UREGA_Dash의 ActivationOwnedTags(State.Dashing)가 이미 표현한다 —
	// 별도 플래그를 만들지 않는다. 클라 입력은 신뢰 대상이 아니므로 차단은 서버에 둔다.
	// 대쉬가 0.2s로 짧아 입력은 큐에 넣지 않고 버린다.
	if (const ARECharacterBase* Char = Cast<ARECharacterBase>(P))
	{
		if (const UAbilitySystemComponent* ASC = Char->GetAbilitySystemComponent())
		{
			if (ASC->HasMatchingGameplayTag(RETag_State_Dashing))
			{
				UE_LOG(LogTemp, Log, TEXT("[Attack] blocked: dashing"));
				return;
			}
		}
	}

	if (Attack->FireInDirection(Dir2D))
	{
		// 발사 성공 시에만 정지+회전 — rate limit에 걸린 스팸 RPC가 이동을 끊지 못하게.
		StopMovement();                                        // 우클릭 이동 패스팔로잉 중단
		if (UPawnMovementComponent* Move = P->GetMovementComponent())
		{
			Move->StopMovementImmediately();                   // 잔여 속도 제거 (로아 평타 정지)
		}
		P->SetActorRotation(FRotator(0.f, Dir2D.Rotation().Yaw, 0.f));  // 커서 방향 회전
	}
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

void AREPlayerController::RunHeadlessFireProbe()
{
	// t=1.5s: 보스 방향 발사 1회(hit 기대) + 즉시 재발사(rate limit 차단 기대).
	// 이동 프로브(1.0s, +Y 이동)와 대쉬 프로브(2.0s, +X) 사이 — 서로 간섭 없음.
	// 발사 성공 시 StopMovement가 이동 프로브를 끊지만 dist 로그는 계속 나옴(수렴만 중단) — 게이트 아님.
	FTimerDelegate FireDel = FTimerDelegate::CreateLambda([this]()
	{
		APawn* P = GetPawn();
		if (!P)
		{
			UE_LOG(LogTemp, Warning, TEXT("[Attack] probe: no pawn"));
			return;
		}
		AREBossCharacter* Boss = nullptr;
		for (TActorIterator<AREBossCharacter> It(GetWorld()); It; ++It)
		{
			Boss = *It;
			break;
		}
		if (!Boss)
		{
			UE_LOG(LogTemp, Warning, TEXT("[Attack] probe: no boss"));
			return;
		}
		const FVector Dir = (Boss->GetActorLocation() - P->GetActorLocation()).GetSafeNormal2D();
		UE_LOG(LogTemp, Log, TEXT("[Attack] probe fire dir=%s"), *Dir.ToString());
		Server_RequestFire(Dir);   // 1발 — hit boss 기대
		Server_RequestFire(Dir);   // 즉시 재발사 — rate-limited 기대
	});
	GetWorld()->GetTimerManager().SetTimer(ProbeFireTimer, FireDel, 1.5f, false);
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
