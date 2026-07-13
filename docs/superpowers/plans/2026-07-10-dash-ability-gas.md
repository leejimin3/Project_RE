# Dash Ability (GAS 시간형) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 플레이어에 스페이스 대쉬(GAS 어빌리티, 커서방향 고정거리 시간형 이동 + GAS 쿨다운, 서버권위)를 추가한다.

**Architecture:** GAS `UGameplayAbility`(ServerOnly)가 `UAbilityTask_ApplyRootMotionConstantForce`로 고정거리 이동을 구동하고, `UGameplayEffect` 쿨다운으로 재사용을 막는다. 커서 방향은 로컬 `PlayerController`가 계산해 `Server_Dash` RPC로 전달한다. 클라 예측은 미사용(M4로 문서화).

**Tech Stack:** UE 5.8 C++, GameplayAbilities(GAS), GameplayTags(네이티브 태그), EnhancedInput.

## Global Constraints

- 엔진: UE 5.8, 타깃 `Project_REEditor` Win64 Development.
- 자동 테스트 인프라 없음 → 검증 게이트 = **에디터 빌드 성공(에러 0)** + **headless 런타임 프로브 로그 관측**.
- GAS 모듈(`GameplayAbilities`,`GameplayTags`,`GameplayTasks`)·플러그인은 #23에서 이미 활성 — 추가 활성 불필요.
- ASC = Pawn(`ARECharacterBase`) 소유, Mixed 복제 (기존, 변경 금지).
- 기존 이동/HP/카메라/GAS 셋업 **변경 금지** (Surgical). 한글 주석 스타일 유지.
- uasset 없이 **코드로** 어빌리티/GE/태그/입력 정의 (프로젝트 규약).
- 커밋: Conventional Commits, 태스크당 1커밋. 푸터 `Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>`.
- 튜닝값: 대쉬 거리 600uu / 시간 0.2s / 쿨다운 2.0s.
- 빌드 명령:
  ```
  "/e/UE_5.8/Engine/Build/BatchFiles/Build.bat" Project_REEditor Win64 Development -Project="E:\UnrealProjects\Project_RE\Project_RE.uproject" -WaitMutex -NoHotReload
  ```
  기대: `Result: Succeeded`, 에러 0. (에디터 열려 있으면 종료 후 실행 — 파일락 회피.)

## 브랜치
`dev`에서 분기: `feature/M2-dash-ability`.
```
git checkout dev && git pull && git checkout -b feature/M2-dash-ability
```
(이미 존재하면 체크아웃만.)

## 파일 구조

| 파일 | 책임 |
|---|---|
| `Source/Project_RE/Abilities/REGameplayTags.h/.cpp` (신규) | 네이티브 게임플레이 태그 선언·정의 (`Cooldown.Dash`, `State.Dashing`) |
| `Source/Project_RE/Abilities/REGE_DashCooldown.h/.cpp` (신규) | 대쉬 쿨다운 GameplayEffect (2.0s, `Cooldown.Dash` 부여) |
| `Source/Project_RE/Abilities/REGA_Dash.h/.cpp` (신규) | 대쉬 어빌리티 (ServerOnly, RootMotion, 쿨다운 커밋) |
| `Source/Project_RE/Core/RECharacterBase.h/.cpp` (수정) | 어빌리티 부여(GiveAbility) + `TryDash(Dir)` + `PendingDashDir` |
| `Source/Project_RE/Core/REPlayerController.h/.cpp` (수정) | Space 입력 → 커서방향 계산 → `Server_Dash` RPC + headless 대쉬 프로브 |

---

### Task 1: 네이티브 태그 + 쿨다운 GE

**Files:**
- Create: `Source/Project_RE/Abilities/REGameplayTags.h`
- Create: `Source/Project_RE/Abilities/REGameplayTags.cpp`
- Create: `Source/Project_RE/Abilities/REGE_DashCooldown.h`
- Create: `Source/Project_RE/Abilities/REGE_DashCooldown.cpp`

**Interfaces:**
- Produces:
  - `RETag_Cooldown_Dash` (extern `FNativeGameplayTag`) — 쿨다운 태그 `Cooldown.Dash`.
  - `RETag_State_Dashing` (extern `FNativeGameplayTag`) — 대쉬중 상태 태그 `State.Dashing`.
  - `class UREGE_DashCooldown : public UGameplayEffect` — 쿨다운 GE, `Cooldown.Dash`를 2.0s 부여.

- [ ] **Step 1: 태그 선언 헤더 작성**

`Source/Project_RE/Abilities/REGameplayTags.h`:
```cpp
// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "NativeGameplayTags.h"

// 대쉬 관련 네이티브 게임플레이 태그 (ini 편집 없이 C++로 정의)
UE_DECLARE_GAMEPLAY_TAG_EXTERN(RETag_Cooldown_Dash); // Cooldown.Dash — 대쉬 쿨다운 중
UE_DECLARE_GAMEPLAY_TAG_EXTERN(RETag_State_Dashing); // State.Dashing — 대쉬 이동 중 (#27 무적판정이 읽음)
```

- [ ] **Step 2: 태그 정의 cpp 작성**

`Source/Project_RE/Abilities/REGameplayTags.cpp`:
```cpp
// Copyright Epic Games, Inc. All Rights Reserved.

#include "REGameplayTags.h"

UE_DEFINE_GAMEPLAY_TAG(RETag_Cooldown_Dash, "Cooldown.Dash");
UE_DEFINE_GAMEPLAY_TAG(RETag_State_Dashing, "State.Dashing");
```

- [ ] **Step 3: 쿨다운 GE 헤더 작성**

`Source/Project_RE/Abilities/REGE_DashCooldown.h`:
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

- [ ] **Step 4: 쿨다운 GE cpp 작성**

`Source/Project_RE/Abilities/REGE_DashCooldown.cpp`:
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

- [ ] **Step 5: 빌드 검증**

Run:
```
"/e/UE_5.8/Engine/Build/BatchFiles/Build.bat" Project_REEditor Win64 Development -Project="E:\UnrealProjects\Project_RE\Project_RE.uproject" -WaitMutex -NoHotReload
```
기대: `Result: Succeeded`, 에러 0.

- [ ] **Step 6: 커밋**

```bash
git add Source/Project_RE/Abilities/REGameplayTags.h Source/Project_RE/Abilities/REGameplayTags.cpp Source/Project_RE/Abilities/REGE_DashCooldown.h Source/Project_RE/Abilities/REGE_DashCooldown.cpp
git commit -m "feat(M2): add native gameplay tags + dash cooldown GE (#25)

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

### Task 2: REGA_Dash 어빌리티

**Files:**
- Create: `Source/Project_RE/Abilities/REGA_Dash.h`
- Create: `Source/Project_RE/Abilities/REGA_Dash.cpp`

**Interfaces:**
- Consumes: `UREGE_DashCooldown` (Task 1), `RETag_State_Dashing` (Task 1), `ARECharacterBase::GetPendingDashDir()` (Task 3 — 이름/타입은 아래 명시. Task 3보다 먼저 구현 시 캐스트 대상만 전방선언).
- Produces: `class UREGA_Dash : public UGameplayAbility` — 서버권위 대쉬 어빌리티. `TryActivateAbilityByClass(UREGA_Dash::StaticClass())`로 활성.

> **참고:** 이 태스크는 `ARECharacterBase::GetPendingDashDir()`(반환 `FVector`)를 사용한다. Task 3에서 정의된다. 구현 순서상 Task 2를 먼저 빌드하려면 Task 3의 헤더 선언(멤버+접근자)이 필요하므로, **Task 2와 Task 3은 함께 빌드**한다(각자 커밋은 분리). Task 2 빌드 검증은 Task 3 헤더 반영 후 수행.

- [ ] **Step 1: 어빌리티 헤더 작성**

`Source/Project_RE/Abilities/REGA_Dash.h`:
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

- [ ] **Step 2: 어빌리티 cpp 작성**

`Source/Project_RE/Abilities/REGA_Dash.cpp`:
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

- [ ] **Step 3: 빌드 검증 (Task 3 헤더 반영 후)**

> Task 3의 `RECharacterBase.h` 변경(`GetPendingDashDir` 선언)을 먼저 적용한 뒤 빌드.

Run:
```
"/e/UE_5.8/Engine/Build/BatchFiles/Build.bat" Project_REEditor Win64 Development -Project="E:\UnrealProjects\Project_RE\Project_RE.uproject" -WaitMutex -NoHotReload
```
기대: `Result: Succeeded`, 에러 0.

- [ ] **Step 4: 커밋**

```bash
git add Source/Project_RE/Abilities/REGA_Dash.h Source/Project_RE/Abilities/REGA_Dash.cpp
git commit -m "feat(M2): add REGA_Dash root-motion dash ability (#25)

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

### Task 3: 어빌리티 부여 + Space 입력 배선

**Files:**
- Modify: `Source/Project_RE/Core/RECharacterBase.h`
- Modify: `Source/Project_RE/Core/RECharacterBase.cpp`
- Modify: `Source/Project_RE/Core/REPlayerController.h`
- Modify: `Source/Project_RE/Core/REPlayerController.cpp`

**Interfaces:**
- Consumes: `UREGA_Dash` (Task 2).
- Produces:
  - `ARECharacterBase::GetPendingDashDir() const` → `FVector` (Task 2가 사용).
  - `ARECharacterBase::TryDash(FVector Dir)` → `bool` (활성 성공 여부; 프로브가 사용).

- [ ] **Step 1: RECharacterBase.h — include + 멤버·메서드 선언 추가**

먼저 include 블록의 `#include "AbilitySystemInterface.h"` **뒤에** 추가 (멤버 `FGameplayAbilitySpecHandle`가 풀 정의 필요):
```cpp
#include "Abilities/GameplayAbilitySpecHandle.h"
```

이어서 전방선언 추가 — 기존:
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

`public:` 섹션의 `GetLifetimeReplicatedProps` 선언(40행 부근) **뒤에** 추가:
```cpp
	/** 서버: 대쉬 시도. Dir 저장 후 대쉬 어빌리티 활성. 활성 성공 시 true. */
	bool TryDash(FVector Dir);

	/** 대쉬 어빌리티가 읽을 목표 방향(로컬이 계산해 서버로 전달한 값). */
	FVector GetPendingDashDir() const { return PendingDashDir; }
```

`protected:` 섹션의 `AbilitySystemComponent` UPROPERTY(45행 부근) **뒤에** 추가:
```cpp
	/** 부여된 대쉬 어빌리티 스펙 핸들(서버). */
	FGameplayAbilitySpecHandle DashAbilityHandle;

	/** 대쉬 목표 방향. Server_Dash → TryDash에서 세팅, 어빌리티 ActivateAbility에서 소비. */
	FVector PendingDashDir = FVector::ForwardVector;
```

- [ ] **Step 2: RECharacterBase.cpp — include + GiveAbility + TryDash 구현**

include 블록(1~12행)의 `#include "AbilitySystemComponent.h"` **뒤에** 추가:
```cpp
#include "Abilities/GameplayAbilitySpec.h"
#include "Abilities/REGA_Dash.h"
```

`PossessedBy`(104행 부근)를 아래로 교체:
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

파일 끝(`GetAbilitySystemComponent`/`InitASCActorInfo`/`OnRep_PlayerState` 뒤, 마지막 `}` 다음)에 추가:
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

- [ ] **Step 3: REPlayerController.h — 입력·RPC 선언 추가**

`protected:` 섹션의 `Server_RequestMove` UFUNCTION 선언(31~32행) **뒤에** 추가:
```cpp
	/** 스페이스 핸들러: 커서 방향을 계산해 서버로 대쉬 요청 */
	void OnDash(const FInputActionValue& Value);

	/** 대쉬 요청 서버 RPC. 서버가 폰의 대쉬 어빌리티를 Dir 방향으로 활성. */
	UFUNCTION(Server, Reliable)
	void Server_Dash(FVector Dir);
```

`ClickMoveAction` UPROPERTY(34~35행) **뒤에** 추가:
```cpp
	UPROPERTY()
	UInputAction* DashAction;
```

- [ ] **Step 4: REPlayerController.cpp — IA 생성/매핑 + 핸들러 + RPC**

include 블록(3~14행)의 `#include "GameFramework/Pawn.h"` **뒤에** 추가:
```cpp
#include "Core/RECharacterBase.h"
```

`SetupInputComponent`의 ClickMove IA 생성부(26~31행) 뒤, `if (UEnhancedInputComponent* EIC ...)` **앞에** 추가:
```cpp
	// 스페이스 대쉬 IA (코드 생성, transient)
	DashAction = NewObject<UInputAction>(this, TEXT("IA_Dash"));
	DashAction->ValueType = EInputActionValueType::Boolean;
	TopDownMappingContext->MapKey(DashAction, EKeys::SpaceBar);
```

`EIC->BindAction(ClickMoveAction, ...)` 줄(35행) **뒤에** 추가:
```cpp
		EIC->BindAction(DashAction, ETriggerEvent::Started, this, &AREPlayerController::OnDash);
```

`OnClickMove` 구현(66~74행) **뒤에** 추가:
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

- [ ] **Step 5: 빌드 검증 (Task 2 파일 포함 전체 빌드)**

Run:
```
"/e/UE_5.8/Engine/Build/BatchFiles/Build.bat" Project_REEditor Win64 Development -Project="E:\UnrealProjects\Project_RE\Project_RE.uproject" -WaitMutex -NoHotReload
```
기대: `Result: Succeeded`, 에러 0.

정적 확인:
```bash
cd E:/UnrealProjects/Project_RE && grep -n "SpaceBar\|Server_Dash\|TryDash" Source/Project_RE/Core/REPlayerController.cpp Source/Project_RE/Core/RECharacterBase.cpp
```
기대: SpaceBar 1, Server_Dash 2(선언 호출+구현), TryDash 2(호출+구현).

- [ ] **Step 6: 커밋**

```bash
git add Source/Project_RE/Core/RECharacterBase.h Source/Project_RE/Core/RECharacterBase.cpp Source/Project_RE/Core/REPlayerController.h Source/Project_RE/Core/REPlayerController.cpp
git commit -m "feat(M2): grant dash ability + Space input wiring (#25)

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

### Task 4: Headless 대쉬 프로브

**Files:**
- Modify: `Source/Project_RE/Core/REPlayerController.h`
- Modify: `Source/Project_RE/Core/REPlayerController.cpp`

**Interfaces:**
- Consumes: `ARECharacterBase::TryDash(FVector)`, `RETag_State_Dashing`, `UAbilitySystemComponent::HasMatchingGameplayTag`.
- Produces: `[Dash]` 접두 로그 4종 (activate/dist/blocked/re-activate) — headless 검증 게이트.

- [ ] **Step 1: REPlayerController.h — 프로브 선언 추가**

`private:` 섹션의 `RunHeadlessMoveProbe();` 선언 **뒤에** 추가:
```cpp
	/** 헤드리스(-unattended) 대쉬 프로브. 서버 권위에서만 발동. */
	void RunHeadlessDashProbe();

	FTimerHandle ProbeDashTimer;
	FVector ProbeDashStart = FVector::ZeroVector;
```

- [ ] **Step 2: REPlayerController.cpp — include + BeginPlay 배선 + 프로브 구현**

include 블록에 추가(기존 `#include "Core/RECharacterBase.h"` 있으므로 아래만 추가):
```cpp
#include "AbilitySystemComponent.h"
#include "AbilitySystemGlobals.h"
#include "Abilities/REGameplayTags.h"
#include "HAL/PlatformMisc.h"
```

`BeginPlay`의 headless 프로브 발동부(60~63행) — 기존:
```cpp
	if (HasAuthority() && FApp::IsUnattended())
	{
		RunHeadlessMoveProbe();
	}
```
를 아래로 교체:
```cpp
	if (HasAuthority() && FApp::IsUnattended())
	{
		RunHeadlessMoveProbe();
		RunHeadlessDashProbe();
	}
```

파일 끝(`RunHeadlessMoveProbe` 구현 뒤)에 추가:
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

- [ ] **Step 3: 빌드 검증**

Run:
```
"/e/UE_5.8/Engine/Build/BatchFiles/Build.bat" Project_REEditor Win64 Development -Project="E:\UnrealProjects\Project_RE\Project_RE.uproject" -WaitMutex -NoHotReload
```
기대: `Result: Succeeded`, 에러 0.

- [ ] **Step 4: 프로브 실행 + 로그 관측**

Run (MSYS_NO_PATHCONV 필수 — 맵 경로 `/Game/...` 변형 방지):
```bash
MSYS_NO_PATHCONV=1 "/e/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" "E:\UnrealProjects\Project_RE\Project_RE.uproject" /Game/Level/Main -game -unattended -nullrhi -stdout -AllowStdOutLogVerbosity 2>&1 | grep -E "\[Dash\]"
```
기대 로그(순서대로):
```
[Dash] activate ok=1 State.Dashing=1
[Dash] immediate retry activated=0 (0=blocked by cooldown, 기대 0)
[Dash] dist=~600 ...          (대략 500~700 범위면 OK; 벗어나면 REGA_Dash::DashStrength 조정)
[Dash] re-activate ok=1 (기대 1, 쿨다운 만료)
[Dash] probe done — exiting
```
판정:
- `activate ok=1` + `State.Dashing=1` → 활성·태그 정상
- `immediate retry activated=0` → 쿨다운 차단 정상
- `dist` 500~700 → RootMotion 이동 정상 (벗어나면 `DashStrength` 조정 후 재실행)
- `re-activate ok=1` → 쿨다운 만료 후 재사용 정상

> 튜닝: `dist`가 600에서 크게 벗어나면 `REGA_Dash.h`의 `DashStrength`를 `600/실측dist*현재값`으로 조정하고 Step 3~4 재실행.

- [ ] **Step 5: 커밋**

```bash
git add Source/Project_RE/Core/REPlayerController.h Source/Project_RE/Core/REPlayerController.cpp
git commit -m "test(M2): headless dash probe (#25)

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

## 완료 기준
- 4개 태스크 커밋 완료.
- 최종 빌드 `Result: Succeeded`, 에러 0.
- 대쉬 프로브 로그 4종 기대대로 관측 (activate/blocked/dist~600/re-activate).
- 스코프 밖 미변경 확인: 이동/HP/카메라/기존 GAS 셋업 diff 없음.

## PR
- base=`dev`, 이슈 #25 메타(label C++, milestone M2, assignee, project) 미러링.
- 본문: 목적/변경/검증(프로브 로그 캡처)/스코프 경계(무적 #27·예측 M4).
