# 구현 목표: [M2 #25] 스페이스 대쉬 어빌리티 (GAS 시간형)

## 컨텍스트
UE 5.8 C++ 탑뷰 탄막(bullet-hell) 프로젝트. 이슈 **#25** (마일스톤 **M2: 플레이어 게임루프**).
이 goal이 하는 것: 플레이어에 **스페이스 대쉬**를 추가한다 — GAS 어빌리티로 **커서 방향 고정거리 시간형(RootMotion) 이동** + **GAS 쿨다운(GameplayEffect)**, 서버권위. 클라 예측은 미사용(M4로 문서화).

토대(#23 완료): `ARECharacterBase`에 `UAbilitySystemComponent`(Pawn 소유, Mixed 복제) 부착됨. ActorInfo는 서버(`PossessedBy`)+클라(`OnRep_PlayerState`) 초기화됨. **어빌리티 0개, 입력 바인딩 없음** — 이 goal이 첫 실전 어빌리티.

스코프 밖(손대지 말 것): 무적/i-frame 로직(#27), 클라 예측(M4), HP의 GAS AttributeSet 전환(HP는 기존 `Replicated float` 유지), Cost GE(마나 등).

설계 스펙: `docs/superpowers/specs/2026-07-10-dash-ability-gas-design.md`
상세 플랜: `docs/superpowers/plans/2026-07-10-dash-ability-gas.md`
(참고 가능. 단 **아래 코드가 최종 정본.**)

## 브랜치
`dev`에서 분기: `feature/M2-dash-ability`
```
git checkout dev && git pull && git checkout -b feature/M2-dash-ability
```
(이미 존재하면 체크아웃만.)

## 전역 제약
- 엔진: UE 5.8, 타깃 `Project_REEditor` Win64 Development.
- 자동 테스트 인프라 없음 → 게이트 = **에디터 빌드 성공(에러 0)** + **headless 런타임 프로브 로그 관측**.
- GAS 모듈(`GameplayAbilities`,`GameplayTags`,`GameplayTasks`)·플러그인은 #23에서 이미 활성 — 추가 활성 불필요.
- ASC = Pawn(`ARECharacterBase`) 소유, Mixed 복제 (기존, 변경 금지).
- 기존 이동(`Server_RequestMove`)/HP(`Health`,`TakeDamage`)/카메라/GAS 셋업(ASC 부착·ActorInfo) **변경 금지** (Surgical). 한글 주석 스타일 유지.
- uasset 없이 **코드로** 어빌리티/GE/태그/입력 정의 (프로젝트 규약).
- 커밋: Conventional Commits, 태스크당 1커밋. 푸터 `Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>`.
- 튜닝값: 대쉬 거리 600uu / 시간 0.2s / 쿨다운 2.0s.
- **빌드 커맨드 (공통):**
  ```
  "/e/UE_5.8/Engine/Build/BatchFiles/Build.bat" Project_REEditor Win64 Development -Project="E:\UnrealProjects\Project_RE\Project_RE.uproject" -WaitMutex -NoHotReload
  ```
  기대: `Result: Succeeded`, 에러 0. (에디터 열려 있으면 종료 후 실행 — 파일락 회피.)

## 검증된 API (실물 확인됨)
- 네이티브 태그 매크로 — 헤더 `#include "NativeGameplayTags.h"`. 헤더 선언 `UE_DECLARE_GAMEPLAY_TAG_EXTERN(TagName)`, cpp 정의 `UE_DEFINE_GAMEPLAY_TAG(TagName, "A.B")`. (cpp에서만 DEFINE 가능.)
- `UGameplayEffect` (헤더 `#include "GameplayEffect.h"`): 멤버 `EGameplayEffectDurationType DurationPolicy`(값 `EGameplayEffectDurationType::HasDuration`), `FGameplayEffectModifierMagnitude DurationMagnitude`(생성 `FGameplayEffectModifierMagnitude(FScalableFloat(2.0f))`), `FInheritedTagContainer InheritableOwnedTagsContainer`(멤버 `.Added`(FGameplayTagContainer), `.CombinedTags`(FGameplayTagContainer); `.AddTag(FGameplayTag)`).
- `UGameplayAbility` (헤더 `#include "Abilities/GameplayAbility.h"`): 멤버 `NetExecutionPolicy`(`EGameplayAbilityNetExecutionPolicy::ServerOnly`), `InstancingPolicy`(`EGameplayAbilityInstancingPolicy::InstancedPerActor`), `TSubclassOf<UGameplayEffect> CooldownGameplayEffectClass`, `FGameplayTagContainer ActivationOwnedTags`. 메서드 `bool CommitAbility(Handle, ActorInfo, ActivationInfo, OptionalRelevantTags=nullptr)`, `virtual void ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData)`, `void EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled)`. 인스턴스 멤버 `CurrentSpecHandle`/`CurrentActorInfo`/`CurrentActivationInfo`. `UObject* GetAvatarActorFromActorInfo()`.
- `UAbilityTask_ApplyRootMotionConstantForce` (헤더 `#include "Abilities/Tasks/AbilityTask_ApplyRootMotionConstantForce.h"`). 팩토리:
  `ApplyRootMotionConstantForce(UGameplayAbility* OwningAbility, FName TaskInstanceName, FVector WorldDirection, float Strength, float Duration, bool bIsAdditive, UCurveFloat* StrengthOverTime, ERootMotionFinishVelocityMode VelocityOnFinishMode, FVector SetVelocityOnFinish, float ClampVelocityOnFinish, bool bEnableGravity)`. 델리게이트 `FApplyRootMotionConstantForceDelegate OnFinish`(`AddDynamic`). `Task->ReadyForActivation()`.
- `ERootMotionFinishVelocityMode` — 헤더 `#include "GameFramework/RootMotionSource.h"`. 값 `ClearVelocity`.
- `UAbilitySystemComponent` (헤더 `#include "AbilitySystemComponent.h"`): `FGameplayAbilitySpecHandle GiveAbility(FGameplayAbilitySpec)`, `bool TryActivateAbilityByClass(TSubclassOf<UGameplayAbility>)`, `bool HasMatchingGameplayTag(FGameplayTag)`.
- `FGameplayAbilitySpec(TSubclassOf<UGameplayAbility>, int32 Level, int32 InputID, UObject* SourceObject)` — 헤더 `#include "Abilities/GameplayAbilitySpec.h"`. `FGameplayAbilitySpecHandle` — 헤더 `#include "Abilities/GameplayAbilitySpecHandle.h"` (멤버 타입 풀 정의용).
- `UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(AActor*)` — 헤더 `#include "AbilitySystemGlobals.h"`.
- `FPlatformMisc::RequestExit(bool)` — 헤더 `#include "HAL/PlatformMisc.h"`.
- EnhancedInput (기존 코드에 배선됨): `UInputAction`(멤버 `ValueType=EInputActionValueType::Boolean`), `UInputMappingContext::MapKey(IA, EKeys::SpaceBar)`, `UEnhancedInputComponent::BindAction(IA, ETriggerEvent::Started, this, &Handler)`. `EKeys::SpaceBar`.
- `APlayerController::GetHitResultUnderCursor(ECC_Visibility, false, FHitResult&)` (기존 이동코드에서 사용중).

## 기존 파일 현황 (변경 대상)
- `Source/Project_RE/Core/RECharacterBase.h` — `class ARECharacterBase : public ACharacter, public IAbilitySystemInterface`. include에 `#include "AbilitySystemInterface.h"` 존재. 전방선언 블록 = `USpringArmComponent`/`UCameraComponent`/`UAbilitySystemComponent`. public에 `GetAbilitySystemComponent`/`PossessedBy`/`OnRep_PlayerState`/`TakeDamage`/`GetLifetimeReplicatedProps` 선언. protected에 `AbilitySystemComponent` UPROPERTY, `InitASCActorInfo()`, `Health`(Replicated)/`MaxHealth`, 카메라 멤버.
- `Source/Project_RE/Core/RECharacterBase.cpp` — include 1~12행(끝=`#include "AbilitySystemComponent.h"`). 생성자에 ASC 부착/카메라/메시. `TakeDamage`/`GetLifetimeReplicatedProps`/`GetAbilitySystemComponent`/`InitASCActorInfo`/`PossessedBy`/`OnRep_PlayerState` 구현. `PossessedBy`(104행 부근)는 `Super::PossessedBy` + `InitASCActorInfo()`만. 파일 끝 = `OnRep_PlayerState` 닫는 `}`.
- `Source/Project_RE/Core/REPlayerController.h` — `AREPlayerController : public APlayerController`. protected에 `BeginPlay`/`SetupInputComponent`/`OnClickMove`/`Server_RequestMove`(UFUNCTION Server Reliable)/`ClickMoveAction`/`TopDownMappingContext`. private에 `RunHeadlessMoveProbe()`/`ProbeMoveTimer`/`ProbeLogTimer`/`ProbeTarget`.
- `Source/Project_RE/Core/REPlayerController.cpp` — include 3~14행(`GameFramework/Pawn.h` 포함). `SetupInputComponent`(16~37행): IA/IMC 코드생성, ClickMove만 매핑, `EIC->BindAction(ClickMoveAction, Triggered, ...)`. `BeginPlay`(39~64행): 커서표시+IMC추가, 60~63행에 headless 프로브 발동 `if (HasAuthority() && FApp::IsUnattended()) { RunHeadlessMoveProbe(); }`. `OnClickMove`(66~74행). `Server_RequestMove_Implementation`(76~89행). `RunHeadlessMoveProbe`(91~126행, 파일 끝).

================================================================
## TASK 1: 네이티브 태그 + 쿨다운 GE
================================================================

### 1-1. `Source/Project_RE/Abilities/REGameplayTags.h` (신규)
```cpp
// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "NativeGameplayTags.h"

// 대쉬 관련 네이티브 게임플레이 태그 (ini 편집 없이 C++로 정의)
UE_DECLARE_GAMEPLAY_TAG_EXTERN(RETag_Cooldown_Dash); // Cooldown.Dash — 대쉬 쿨다운 중
UE_DECLARE_GAMEPLAY_TAG_EXTERN(RETag_State_Dashing); // State.Dashing — 대쉬 이동 중 (#27 무적판정이 읽음)
```

### 1-2. `Source/Project_RE/Abilities/REGameplayTags.cpp` (신규)
```cpp
// Copyright Epic Games, Inc. All Rights Reserved.

#include "REGameplayTags.h"

UE_DEFINE_GAMEPLAY_TAG(RETag_Cooldown_Dash, "Cooldown.Dash");
UE_DEFINE_GAMEPLAY_TAG(RETag_State_Dashing, "State.Dashing");
```

### 1-3. `Source/Project_RE/Abilities/REGE_DashCooldown.h` (신규)
```cpp
// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayEffect.h"
#include "REGE_DashCooldown.generated.h"

/**
 *  대쉬 쿨다운 GameplayEffect. 2.0초간 Cooldown.Dash 태그를 부여한다.
 *  REGA_Dash가 CooldownGameplayEffectClass로 참조 → CommitAbility 시 적용.
 *  코드로 구성(uasset 없음).
 */
UCLASS()
class UREGE_DashCooldown : public UGameplayEffect
{
	GENERATED_BODY()

public:
	UREGE_DashCooldown();
};
```

### 1-4. `Source/Project_RE/Abilities/REGE_DashCooldown.cpp` (신규)
```cpp
// Copyright Epic Games, Inc. All Rights Reserved.

#include "REGE_DashCooldown.h"
#include "REGameplayTags.h"

UREGE_DashCooldown::UREGE_DashCooldown()
{
	// 지속형(HasDuration) — 2.0초 후 자동 소멸.
	DurationPolicy = EGameplayEffectDurationType::HasDuration;
	DurationMagnitude = FGameplayEffectModifierMagnitude(FScalableFloat(2.0f));

	// 이 GE가 소유하는 태그 = Cooldown.Dash. GAS가 GetCooldownTags로 읽어 재활성 차단.
	// CDO 생성 시점엔 CombinedTags가 자동 재계산되지 않으므로 Added/CombinedTags 둘 다 세팅.
	InheritableOwnedTagsContainer.Added.AddTag(RETag_Cooldown_Dash);
	InheritableOwnedTagsContainer.CombinedTags.AddTag(RETag_Cooldown_Dash);
}
```

### 1-5. 빌드/검증 게이트
```
"/e/UE_5.8/Engine/Build/BatchFiles/Build.bat" Project_REEditor Win64 Development -Project="E:\UnrealProjects\Project_RE\Project_RE.uproject" -WaitMutex -NoHotReload
```
기대: `Result: Succeeded`, 에러 0.

### 1-6. 커밋
```bash
git add Source/Project_RE/Abilities/REGameplayTags.h Source/Project_RE/Abilities/REGameplayTags.cpp Source/Project_RE/Abilities/REGE_DashCooldown.h Source/Project_RE/Abilities/REGE_DashCooldown.cpp
git commit -m "feat(M2): add native gameplay tags + dash cooldown GE (#25)

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

================================================================
## TASK 2: REGA_Dash 어빌리티 (코드 작성만 — 빌드는 TASK 3에서)
================================================================
> 이 어빌리티는 `ARECharacterBase::GetPendingDashDir()`(TASK 3에서 추가)를 사용한다. 따라서 **TASK 2 파일 작성 → TASK 3 파일 수정 → TASK 3에서 함께 빌드**한다. 커밋은 분리.

### 2-1. `Source/Project_RE/Abilities/REGA_Dash.h` (신규)
```cpp
// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Abilities/GameplayAbility.h"
#include "REGA_Dash.generated.h"

/**
 *  스페이스 대쉬 어빌리티 (GAS, 시간형 RootMotion, 서버권위).
 *  아바타(ARECharacterBase)의 PendingDashDir 방향으로 고정거리(600uu / 0.2s) 이동.
 *  쿨다운은 UREGE_DashCooldown(2.0s) 커밋. 활성 동안 State.Dashing 태그(#27이 읽음).
 *
 *  NetExecutionPolicy = ServerOnly (클라 예측 미사용).
 *  TODO M4: 데디 원격클라 지연 측정 후 LocalPredicted 전환. 지금은 리슨서버라 지연 0 → 예측 이득 0.
 */
UCLASS()
class UREGA_Dash : public UGameplayAbility
{
	GENERATED_BODY()

public:
	UREGA_Dash();

	virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle,
	                             const FGameplayAbilityActorInfo* ActorInfo,
	                             const FGameplayAbilityActivationInfo ActivationInfo,
	                             const FGameplayEventData* TriggerEventData) override;

private:
	/** RootMotion 태스크 완료 콜백 → EndAbility. */
	UFUNCTION()
	void OnDashFinished();

	/** 대쉬 속도(uu/s). 거리≈Strength*Duration. 프로브 실측으로 600uu에 맞춰 조정. */
	float DashStrength = 3000.f;

	/** 대쉬 지속(s). */
	float DashDuration = 0.2f;
};
```

### 2-2. `Source/Project_RE/Abilities/REGA_Dash.cpp` (신규)
```cpp
// Copyright Epic Games, Inc. All Rights Reserved.

#include "REGA_Dash.h"
#include "REGE_DashCooldown.h"
#include "REGameplayTags.h"
#include "Core/RECharacterBase.h"
#include "Abilities/Tasks/AbilityTask_ApplyRootMotionConstantForce.h"
#include "GameFramework/RootMotionSource.h"

UREGA_Dash::UREGA_Dash()
{
	// 서버권위 실행 — 클라 예측 미사용(M4에서 LocalPredicted 전환 검토).
	NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::ServerOnly;
	// 인스턴스 필요(멤버 상태·RootMotion 태스크 보유).
	InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;

	// 쿨다운 GE 연결 — CommitAbility가 이 GE를 적용.
	CooldownGameplayEffectClass = UREGE_DashCooldown::StaticClass();

	// 활성 동안 소유자에 State.Dashing 부여(#27 무적판정 계약). EndAbility 시 자동 해제.
	ActivationOwnedTags.AddTag(RETag_State_Dashing);
}

void UREGA_Dash::ActivateAbility(const FGameplayAbilitySpecHandle Handle,
                                 const FGameplayAbilityActorInfo* ActorInfo,
                                 const FGameplayAbilityActivationInfo ActivationInfo,
                                 const FGameplayEventData* TriggerEventData)
{
	// 쿨다운/코스트 커밋 — 쿨다운 중이면 실패 → 대쉬 취소.
	if (!CommitAbility(Handle, ActorInfo, ActivationInfo))
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, /*bReplicateEndAbility=*/true, /*bWasCancelled=*/true);
		return;
	}

	// 아바타에서 대쉬 방향 획득(로컬이 계산해 서버 TryDash로 넣어둔 값).
	ARECharacterBase* Char = Cast<ARECharacterBase>(GetAvatarActorFromActorInfo());
	if (!Char)
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}
	const FVector Dir = Char->GetPendingDashDir();

	UE_LOG(LogTemp, Log, TEXT("[Dash] activate ok dir=%s"), *Dir.ToString());

	// 시간형 RootMotion — 고정속도×고정시간 → 마찰 무관 고정거리. 종료 시 속도 클리어.
	UAbilityTask_ApplyRootMotionConstantForce* Task =
		UAbilityTask_ApplyRootMotionConstantForce::ApplyRootMotionConstantForce(
			this,
			FName("Dash"),
			Dir,
			DashStrength,
			DashDuration,
			/*bIsAdditive=*/false,
			/*StrengthOverTime=*/nullptr,
			ERootMotionFinishVelocityMode::ClearVelocity,
			/*SetVelocityOnFinish=*/FVector::ZeroVector,
			/*ClampVelocityOnFinish=*/0.f,
			/*bEnableGravity=*/false);

	Task->OnFinish.AddDynamic(this, &UREGA_Dash::OnDashFinished);
	Task->ReadyForActivation();
}

void UREGA_Dash::OnDashFinished()
{
	// RootMotion 종료 → 어빌리티 종료(State.Dashing 자동 해제).
	EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, /*bReplicateEndAbility=*/true, /*bWasCancelled=*/false);
}
```

### 2-3. 커밋 (빌드는 TASK 3에서 함께 — 여기선 파일만 스테이징 후 커밋)
```bash
git add Source/Project_RE/Abilities/REGA_Dash.h Source/Project_RE/Abilities/REGA_Dash.cpp
git commit -m "feat(M2): add REGA_Dash root-motion dash ability (#25)

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

================================================================
## TASK 3: 어빌리티 부여 + Space 입력 배선 (여기서 TASK 2 포함 빌드)
================================================================

### 3-1. `Source/Project_RE/Core/RECharacterBase.h` (수정)

**(a) include 보강** — `#include "AbilitySystemInterface.h"` **뒤에** 추가 (멤버 `FGameplayAbilitySpecHandle` 풀 정의용):
```cpp
#include "Abilities/GameplayAbilitySpecHandle.h"
```

**(b) 전방선언** — 기존:
```cpp
class USpringArmComponent;
class UCameraComponent;
class UAbilitySystemComponent;
```
를 아래로 교체:
```cpp
class USpringArmComponent;
class UCameraComponent;
class UAbilitySystemComponent;
class UREGA_Dash;
```

**(c) public** — `GetLifetimeReplicatedProps` 선언 **뒤에** 추가:
```cpp
	/** 서버: 대쉬 시도. Dir 저장 후 대쉬 어빌리티 활성. 활성 성공 시 true. */
	bool TryDash(FVector Dir);

	/** 대쉬 어빌리티가 읽을 목표 방향(로컬이 계산해 서버로 전달한 값). */
	FVector GetPendingDashDir() const { return PendingDashDir; }
```

**(d) protected** — `AbilitySystemComponent` UPROPERTY **뒤에** 추가:
```cpp
	/** 부여된 대쉬 어빌리티 스펙 핸들(서버). */
	FGameplayAbilitySpecHandle DashAbilityHandle;

	/** 대쉬 목표 방향. Server_Dash → TryDash에서 세팅, 어빌리티 ActivateAbility에서 소비. */
	FVector PendingDashDir = FVector::ForwardVector;
```

### 3-2. `Source/Project_RE/Core/RECharacterBase.cpp` (수정)

**(a) include** — `#include "AbilitySystemComponent.h"` **뒤에** 추가:
```cpp
#include "Abilities/GameplayAbilitySpec.h"
#include "Abilities/REGA_Dash.h"
```

**(b) `PossessedBy` 교체** — 기존 `PossessedBy` 전체를 아래로:
```cpp
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
```

**(c) 파일 끝에 `TryDash` 추가** (`OnRep_PlayerState` 닫는 `}` 다음):
```cpp

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
```

### 3-3. `Source/Project_RE/Core/REPlayerController.h` (수정)

**(a) protected** — `Server_RequestMove` UFUNCTION 선언 **뒤에** 추가:
```cpp
	/** 스페이스 핸들러: 커서 방향을 계산해 서버로 대쉬 요청 */
	void OnDash(const FInputActionValue& Value);

	/** 대쉬 요청 서버 RPC. 서버가 폰의 대쉬 어빌리티를 Dir 방향으로 활성. */
	UFUNCTION(Server, Reliable)
	void Server_Dash(FVector Dir);
```

**(b) protected** — `ClickMoveAction` UPROPERTY **뒤에** 추가:
```cpp
	UPROPERTY()
	UInputAction* DashAction;
```

### 3-4. `Source/Project_RE/Core/REPlayerController.cpp` (수정)

**(a) include** — `#include "GameFramework/Pawn.h"` **뒤에** 추가:
```cpp
#include "Core/RECharacterBase.h"
```

**(b) `SetupInputComponent`** — ClickMove IA 생성부 뒤, `if (UEnhancedInputComponent* EIC ...)` **앞에** 추가:
```cpp
	// 스페이스 대쉬 IA (코드 생성, transient)
	DashAction = NewObject<UInputAction>(this, TEXT("IA_Dash"));
	DashAction->ValueType = EInputActionValueType::Boolean;
	TopDownMappingContext->MapKey(DashAction, EKeys::SpaceBar);
```

**(c) `SetupInputComponent`** — `EIC->BindAction(ClickMoveAction, ...)` 줄 **뒤에** 추가:
```cpp
		EIC->BindAction(DashAction, ETriggerEvent::Started, this, &AREPlayerController::OnDash);
```

**(d) `OnClickMove` 구현 뒤에 추가:**
```cpp
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
```

### 3-5. 빌드/검증 게이트 (TASK 2 파일 포함 전체 빌드)
```
"/e/UE_5.8/Engine/Build/BatchFiles/Build.bat" Project_REEditor Win64 Development -Project="E:\UnrealProjects\Project_RE\Project_RE.uproject" -WaitMutex -NoHotReload
```
기대: `Result: Succeeded`, 에러 0.

정적 확인:
```bash
cd E:/UnrealProjects/Project_RE && grep -n "SpaceBar\|Server_Dash\|TryDash" Source/Project_RE/Core/REPlayerController.cpp Source/Project_RE/Core/RECharacterBase.cpp
```
기대: SpaceBar 1, Server_Dash 2, TryDash 2.

### 3-6. 커밋
```bash
git add Source/Project_RE/Core/RECharacterBase.h Source/Project_RE/Core/RECharacterBase.cpp Source/Project_RE/Core/REPlayerController.h Source/Project_RE/Core/REPlayerController.cpp
git commit -m "feat(M2): grant dash ability + Space input wiring (#25)

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

================================================================
## TASK 4: Headless 대쉬 프로브
================================================================

### 4-1. `Source/Project_RE/Core/REPlayerController.h` (수정)
`private:`의 `RunHeadlessMoveProbe();` 선언 **뒤에** 추가:
```cpp
	/** 헤드리스(-unattended) 대쉬 프로브. 서버 권위에서만 발동. */
	void RunHeadlessDashProbe();

	FTimerHandle ProbeDashTimer;
	FVector ProbeDashStart = FVector::ZeroVector;
```

### 4-2. `Source/Project_RE/Core/REPlayerController.cpp` (수정)

**(a) include** — 추가 (기존 `#include "Core/RECharacterBase.h"` 있음):
```cpp
#include "AbilitySystemComponent.h"
#include "AbilitySystemGlobals.h"
#include "Abilities/REGameplayTags.h"
#include "HAL/PlatformMisc.h"
```

**(b) `BeginPlay` headless 발동부 교체** — 기존:
```cpp
	if (HasAuthority() && FApp::IsUnattended())
	{
		RunHeadlessMoveProbe();
	}
```
를 아래로:
```cpp
	if (HasAuthority() && FApp::IsUnattended())
	{
		RunHeadlessMoveProbe();
		RunHeadlessDashProbe();
	}
```

**(c) 파일 끝(`RunHeadlessMoveProbe` 구현 뒤)에 추가:**
```cpp

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
```

### 4-3. 빌드 게이트
```
"/e/UE_5.8/Engine/Build/BatchFiles/Build.bat" Project_REEditor Win64 Development -Project="E:\UnrealProjects\Project_RE\Project_RE.uproject" -WaitMutex -NoHotReload
```
기대: `Result: Succeeded`, 에러 0.

### 4-4. 프로브 실행 + 로그 관측 게이트
```bash
MSYS_NO_PATHCONV=1 "/e/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" "E:\UnrealProjects\Project_RE\Project_RE.uproject" /Game/Level/Main -game -unattended -nullrhi -stdout -AllowStdOutLogVerbosity 2>&1 | grep -E "\[Dash\]"
```
기대 로그(순서대로):
```
[Dash] activate ok=1 State.Dashing=1
[Dash] immediate retry activated=0 (0=blocked by cooldown, 기대 0)
[Dash] dist=... (기대 ~600; 500~700 범위면 OK)
[Dash] re-activate ok=1 (기대 1, 쿨다운 만료)
[Dash] probe done — exiting
```
판정:
- `activate ok=1` + `State.Dashing=1` → 활성·태그 정상
- `immediate retry activated=0` → 쿨다운 차단 정상
- `dist` 500~700 → RootMotion 이동 정상 (벗어나면 `REGA_Dash.h`의 `DashStrength`를 `600/실측dist*3000`으로 조정 후 4-3~4-4 재실행)
- `re-activate ok=1` → 쿨다운 만료 후 재사용 정상

### 4-5. 커밋
```bash
git add Source/Project_RE/Core/REPlayerController.h Source/Project_RE/Core/REPlayerController.cpp
git commit -m "test(M2): headless dash probe (#25)

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

## 완료 후
- PR 생성: base=`dev`. 이슈 **#25** 메타 미러링 — label(`C++`), milestone(`M2: 플레이어 게임루프`), assignee(leejimin3), project 동일 배정.
- PR 본문 6필드: 목적 / 변경사항(태그+GE / 어빌리티 / 부여+입력 / 프로브) / 검증(프로브 로그 4종 캡처 + 빌드 Succeeded) / 스코프 경계(무적 #27·예측 M4) / 관련이슈(#25) / 체크리스트.
- 의도된 잔여 TODO(후속 이슈 몫): `REGA_Dash` 헤더의 `TODO M4: LocalPredicted 전환`.

## 하지 말 것 (스코프 밖)
- 무적/i-frame 로직 → **#27**. 이번엔 `State.Dashing` 태그 부여만. 데미지 무시 로직 넣지 말 것.
- 클라 예측(LocalPredicted/TargetData) → **M4**. `NetExecutionPolicy`는 `ServerOnly` 유지.
- HP의 GAS AttributeSet 전환 → HP는 기존 `Replicated float Health` 그대로. `TakeDamage` 손대지 말 것.
- Cost GE(마나 등) → 없음. 쿨다운만.
- 이동(`Server_RequestMove`)/카메라/기존 ASC 셋업(생성자 ASC 부착, `InitASCActorInfo`) → 변경 금지.
