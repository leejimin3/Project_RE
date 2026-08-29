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
#include "Navigation/PathFollowingComponent.h"
#include "Misc/App.h"
#include "TimerManager.h"
#include "AbilitySystemComponent.h"
#include "AbilitySystemGlobals.h"
#include "Abilities/REGameplayTags.h"
#include "HAL/PlatformMisc.h"
#include "REResultWidget.h"
#include "UI/REPlayerHudWidget.h"
#include "HAL/IConsoleManager.h"
#include "RECheatPanelWidget.h"
#include "Blueprint/UserWidget.h"
#include "Core/REAttackComponent.h"
#include "Core/REBossCharacter.h"
#include "Core/REGameMode.h"
#include "Debug/REHeadlessProbeComponent.h"                // 헤드리스 프로브 (#141)
#include "GameFramework/PawnMovementComponent.h"
#include "EngineUtils.h"
#include "Project_RE.h"                              // LogRE / LogREBullet / LogRENet

namespace
{
	/**
	 *  WBP 위젯 클래스를 경로로 해석한다. 없으면 C++ 클래스로 폴백한다 (#121).
	 *
	 *  WBP 는 디자이너에서 룩을 만지기 위한 껍데기이고, 트리를 아직 안 만들었으면
	 *  C++ 쪽 Initialize() 가 예전 트리를 그대로 구성한다 — 전환 도중에도 화면이 비지 않는다.
	 *  LoadClass 는 첫 호출에서만 디스크를 친다. HUD 는 possess 시 1회, 결과는 게임당 1회다.
	 */
	UClass* ResolveWidgetClass(const TCHAR* WbpPath, UClass* Fallback)
	{
		UClass* Loaded = LoadClass<UUserWidget>(nullptr, WbpPath);
		if (!Loaded)
		{
			UE_LOG(LogRE, Log, TEXT("[UI] WBP 없음 - C++ 폴백 사용: %s"), WbpPath);
			return Fallback;
		}
		return Loaded;
	}
}

namespace
{
	/**
	 *  치트 패널 표시 허용 (#100). 0 이면 토글 키를 눌러도 뜨지 않는다.
	 *  패널은 원래도 키를 눌러야 뜨지만, 촬영 중 실수로 한 번 누르면 그대로 찍힌다.
	 */
	/**
	 *  클라 이동 입력을 끊는 목표 근접 반경 (#112). 서버가 bHasMoveTarget 을 내리기까지의
	 *  공백에 도착 지점에서 좌우로 떠는 것을 막는다.
	 */
	constexpr float MoveInputStopRadius = 60.f;

	/**
	 *  이동 재요청을 허용하는 최소 목표 변화량 (#126). 홀드 중 커서가 이만큼 움직여야 다시 보낸다.
	 *  ponytail: 드래그가 빠르면 이 값을 넘겨 재요청이 나가고 그 순간 속도가 한 번 리셋된다.
	 *  완전히 없애려면 SimpleMoveToLocation 대신 PathFollowingComponent::RequestMove 를
	 *  EPathFollowingVelocityMode::Keep 으로 직접 호출해야 한다 — 필요해지면 그때 가라.
	 */
	constexpr float MoveRequestMinDelta = 100.f;

	static TAutoConsoleVariable<int32> CVarCheatPanel(
		TEXT("re.Debug.CheatPanel"),
		1,
		TEXT("치트 패널 토글 허용 (0=끔, 영상 촬영용)."),
		ECVF_Cheat);

	/**
	 *  엔진 온스크린 메시지 표시 (#100). 0 이면 끈다.
	 *
	 *  실측 스크린샷에 "Multiple directional lights are competing..." 경고가 화면 좌상단에
	 *  그대로 찍혔다. 영상(#99)에 들어가면 안 된다. 경고의 원인(레벨 조명 설정)은 별건이고,
	 *  여기서는 표시만 끈다.
	 */
	static TAutoConsoleVariable<int32> CVarScreenMessages(
		TEXT("re.Debug.ScreenMessages"),
		1,
		TEXT("엔진 온스크린 디버그 메시지 표시 (0=끔, 영상 촬영용)."),
		FConsoleVariableDelegate::CreateStatic([](IConsoleVariable* Var)
		{
			GAreScreenMessagesEnabled = Var->GetInt() != 0;
		}),
		ECVF_Cheat);
}

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

		// 플레이어 상태 HUD (#100). HP·대쉬 쿨다운·투사체 수.
		// 로컬 컨트롤러 안이라 데디서버에서는 만들어지지 않는다.
		if (!PlayerHud)
		{
			PlayerHud = CreateWidget<UREPlayerHudWidget>(this,
				ResolveWidgetClass(TEXT("/Game/UI/WBP_PlayerHud.WBP_PlayerHud_C"),
					UREPlayerHudWidget::StaticClass()));
			if (PlayerHud)
			{
				PlayerHud->AddToViewport();
			}
		}

		// 준비 완료를 서버에 알린다 — 싱글/리슨에서는 권한 보유라 즉시 로컬 실행된다.
		Server_NotifyReady();
	}

	// 헤드리스(-unattended) 서버권위 검증 프로브. 실플레이(PIE/에디터)엔 무발동.
	// 본문은 UREHeadlessProbeComponent 로 나갔다 (#141) — 쉬핑 빌드에서는 그 본문이
	// 컴파일 아웃되어 이 경로가 no-op 다. 전에는 런타임 게이트뿐이라 -unattended 를
	// 커맨드라인으로 넘기면 출시 빌드에서도 프로브가 켜졌다.
	if (HasAuthority() && FApp::IsUnattended())
	{
		UREHeadlessProbeComponent* Probe = NewObject<UREHeadlessProbeComponent>(this);
		Probe->RegisterComponent();
		Probe->StartProbes();
	}
}

void AREPlayerController::PlayerTick(float DeltaTime)
{
	Super::PlayerTick(DeltaTime);

	// 오너 클라 전용 회전 구동 (#79). 서버는 CMC의 bOrientRotationToMovement가 그대로 담당한다.
	if (HasAuthority())
	{
		// 서버: 패스팔로잉이 끝나거나 끊기면 목표를 내린다 (#112).
		// 안 내리면 오너 클라가 목표를 향해 계속 입력을 넣어 서버 보정과 싸운다.
		// 상태로 판정하므로 도달·발사정지·사망·경로실패를 한 곳에서 덮는다.
		if (ARECharacterBase* RC = Cast<ARECharacterBase>(GetPawn()))
		{
			// 서버에도 목표 방향 입력을 넣는다 (#126).
			//
			// 이동 자체는 패스팔로잉이 이미 처리하므로 이 입력은 이동을 만들기 위한 것이 아니다.
			// CMC 의 Acceleration 멤버는 오직 입력 벡터에서만 채워지고(CharacterMovementComponent.cpp:6451
			// Acceleration = ScaleInputAcceleration(ConstrainInputAcceleration(InputVector))),
			// 패스팔로잉은 RequestedAcceleration 이라는 별도 변수를 쓰며 멤버 Acceleration 은 0으로 남긴다.
			// ABP_Unarmed 의 ShouldMove 는 GroundSpeed 와 함께 GetCurrentAcceleration() != 0 을 보므로,
			// 입력이 없으면 속도가 600 이어도 Idle 상태에 머물러 캐릭터가 포즈 고정인 채 미끄러진다.
			//
			// 클라(아래 :215)가 이미 같은 입력을 넣고 있었다 — 서버만 빠져 있었다.
			if (RC->HasMoveTarget())
			{
				const FVector To = RC->GetMoveTarget() - RC->GetActorLocation();
				const FVector Dir = FVector(To.X, To.Y, 0.f);
				if (Dir.SizeSquared() > FMath::Square(MoveInputStopRadius))
				{
					RC->AddMovementInput(Dir.GetSafeNormal());
				}
			}

			if (RC->HasMoveTarget())
			{
				const UPathFollowingComponent* PFC = FindComponentByClass<UPathFollowingComponent>();
				if (!PFC || PFC->GetStatus() != EPathFollowingStatus::Moving)
				{
					RC->ClearMoveTarget();
				}
			}
		}
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

	// 서버가 준 이동 목표로 클라도 입력을 넣는다 (#112).
	//
	// 클라 예측은 직선이고 서버 패스팔로잉은 곡선 nav 경로를 탈 수 있다 — 둘이 어긋나면
	// 서버 보정이 정정한다. 최종 위치는 서버가 정하므로 권위는 그대로다. 이 아레나는 장애물이
	// 없어 실질적으로 항상 직선이다.
	if (ARECharacterBase* RC = Cast<ARECharacterBase>(Char))
	{
		if (RC->HasMoveTarget())
		{
			const FVector To = RC->GetMoveTarget() - Char->GetActorLocation();
			const FVector Dir = FVector(To.X, To.Y, 0.f);
			// 목표 근처에서는 입력을 끊는다. 안 그러면 도착 지점에서 좌우로 떤다.
			// 서버가 bHasMoveTarget 을 내릴 때까지의 공백을 여기서 메운다.
			if (Dir.SizeSquared() > FMath::Square(MoveInputStopRadius))
			{
				Char->AddMovementInput(Dir.GetSafeNormal());
			}
		}
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
			UE_LOG(LogRE, Verbose, TEXT("[Facing] reverted: cur=%.1f expected=%.1f"), Cur.Yaw, FacingLockYaw);
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
	if (!GetHitResultUnderCursor(ECC_Visibility, false, Hit) || !Hit.bBlockingHit)
	{
		return;
	}

	// 홀드 시 Triggered가 매 프레임 오므로 목표 변화량으로 페이싱한다 (OnFire의 시간 페이싱과 같은 취지).
	// 매 프레임 재요청하면 SimpleMoveToLocation이 진행 중인 패스팔로잉을 Abort하고 새로 시작하는데,
	// 그때마다 Velocity가 0으로 리셋돼 ABP가 Idle을 출력한다 — 애니메이션 없이 미끄러진다 (#126).
	// 이동 중일 때만 억제한다. 도착한 뒤 같은 지점을 다시 클릭하는 경로까지 막으면 안 된다.
	const ARECharacterBase* RC = Cast<ARECharacterBase>(GetPawn());
	if (RC && RC->HasMoveTarget()
		&& FVector::DistSquared2D(Hit.ImpactPoint, LastMoveRequest) < FMath::Square(MoveRequestMinDelta))
	{
		return;
	}

	LastMoveRequest = Hit.ImpactPoint;
	Server_RequestMove(Hit.ImpactPoint);
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
		UE_LOG(LogRE, Log, TEXT("[Dash] input sent (local)"));
		Server_Dash(Dir);
	}
}

bool AREPlayerController::IsPawnAlive() const
{
	const ARECharacterBase* Char = Cast<ARECharacterBase>(GetPawn());
	return Char && Char->IsAlive();
}

void AREPlayerController::Server_Dash_Implementation(FVector Dir)
{
	// 서버 권위 — 죽은 폰의 RPC는 무시 (#85). 클라 DisableInput은 지연·조작에 뚫린다.
	if (!IsPawnAlive())
	{
		return;
	}
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
		GM->NotifyPlayerReady(this);
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
	UClass* ResultClass = ResolveWidgetClass(TEXT("/Game/UI/WBP_Result.WBP_Result_C"),
		UREResultWidget::StaticClass());
	if (UREResultWidget* Result = CreateWidget<UREResultWidget>(this, ResultClass))
	{
		Result->SetResult(bVictory);
		Result->AddToViewport();
	}

	// 이동/대쉬 입력 차단 — 입력은 클라 소유물이라 여기가 제자리.
	DisableInput(this);

	UE_LOG(LogRENet, Log, TEXT("[RE] Client_ShowResult: %s"), bVictory ? TEXT("VICTORY") : TEXT("DEFEAT"));
}

void AREPlayerController::Client_NotifyDeath_Implementation()
{
	// 입력만 끊는다. 결과 화면은 게임이 끝날 때 Client_ShowResult가 따로 띄운다.
	DisableInput(this);

	UE_LOG(LogRENet, Log, TEXT("[RE] Client_NotifyDeath: input disabled (spectating)"));
}

void AREPlayerController::OnToggleCheatPanel()
{
	if (!IsLocalPlayerController())
	{
		return;
	}
	// 촬영 중 오조작 차단 (#100). 패널은 기본적으로 키를 눌러야 뜨지만, 영상 녹화 중
	// 실수로 한 번 누르면 그대로 찍힌다. 0 이면 아예 뜨지 않게 한다.
	if (CVarCheatPanel.GetValueOnGameThread() == 0)
	{
		if (CheatPanel)
		{
			CheatPanel->SetVisibility(ESlateVisibility::Collapsed);
		}
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
	// 서버 권위 — 죽은 폰의 이동 요청 무시 (#85). 클라 DisableInput은 지연·조작에 뚫린다.
	if (!IsPawnAlive())
	{
		UE_LOG(LogRENet, Warning, TEXT("[Move] rejected: pawn dead"));
		return;
	}

	// 사격 모션 중에는 이동 요청을 받지 않는다. 발사 시점의 StopMovement 는 '지금 속도'만
	// 끊을 뿐이라, 바로 다음 클릭이 모션 중간에 걸어나가게 만든다.
	if (MoveLockUntil >= 0.0 && GetWorld()->GetTimeSeconds() < MoveLockUntil)
	{
		UE_LOG(LogRENet, Log, TEXT("[Move] rejected: fire lock (%.2fs 남음)"),
			MoveLockUntil - GetWorld()->GetTimeSeconds());
		return;
	}

	// 서버 권위 — nav 검증 후 패스팔로잉 구동.
	UNavigationSystemV1* NavSys = UNavigationSystemV1::GetCurrent(GetWorld());
	FNavLocation NavLoc;
	if (!NavSys || !NavSys->ProjectPointToNavigation(Target, NavLoc))
	{
		UE_LOG(LogRENet, Warning, TEXT("[Move] rejected: off-navmesh %s"), *Target.ToString());
		return;
	}

	UE_LOG(LogRENet, Log, TEXT("[Move] Server_RequestMove recv target=%s"), *NavLoc.Location.ToString());
	UAIBlueprintHelperLibrary::SimpleMoveToLocation(this, NavLoc.Location);

	// 오너 클라에 목표를 알려 클라도 같은 방향으로 입력을 넣게 한다 (#112).
	// 이게 없으면 데디에서 서버가 폰을 거의 못 움직인다 — RECharacterBase::MoveTarget 주석 참조.
	if (ARECharacterBase* Char = Cast<ARECharacterBase>(GetPawn()))
	{
		Char->SetMoveTarget(NavLoc.Location);
	}
}

void AREPlayerController::Server_RequestFire_Implementation(FVector Dir)
{
	// 게임오버 후 잔여 RPC 무시 — 구 AutoFire StopFiring의 대체.
	AREGameMode* GM = GetWorld()->GetAuthGameMode<AREGameMode>();
	if (GM && GM->IsGameOver())
	{
		return;
	}

	// 서버 권위 — 죽은 폰의 발사 요청 무시 (#85). 클라 DisableInput은 지연·조작에 뚫린다.
	if (!IsPawnAlive())
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
				UE_LOG(LogRENet, Log, TEXT("[Attack] blocked: dashing"));
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
		// 사격 모션이 끝날 때까지 이동 잠금. 연사 중에는 매 발사가 락을 갱신해 계속 잠긴다.
		MoveLockUntil = GetWorld()->GetTimeSeconds() + Attack->GetFireLockSec();

		// 클라 예측도 같이 끊는다 (#112). 안 끊으면 서버는 섰는데 클라만 계속 밀고 가서
		// 매 프레임 보정으로 되돌려지는 최악의 조합이 된다.
		if (ARECharacterBase* RC = Cast<ARECharacterBase>(P))
		{
			RC->ClearMoveTarget();
		}
		P->SetActorRotation(FRotator(0.f, Dir2D.Rotation().Yaw, 0.f));  // 커서 방향 회전
	}
}

