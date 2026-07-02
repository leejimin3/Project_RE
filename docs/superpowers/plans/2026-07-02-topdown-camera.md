# M0 #1 탑뷰 카메라 + PlayerController 뼈대 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal (최종 목표 — Claude Code goal):** PIE 실행 시 탑뷰 쿼터뷰(약 −50° 하향) 카메라로 마네킹 캐릭터가 화면에 보이고, 우클릭 시 Output Log에 `[RE] OnClickMove triggered` 로그가 출력된다. C++ 빌드 에러 0.

**검증 가능한 완료 조건 (Acceptance):**
1. `Project_REEditor` 빌드 성공, 에러 0.
2. PIE 실행 → 마네킹이 위에서 내려다보는 쿼터뷰로 보임.
3. 게임 뷰포트서 우클릭 → Output Log에 `[RE] OnClickMove triggered` 출력.

**Architecture:** 신규 C++ 3종(`ARECharacterBase`/`AREPlayerController`/`AREGameMode`)을 `Source/Project_RE/Core/`에 생성. 카메라는 캐릭터 부착 SpringArm+Camera로 절대회전 고정. 입력 IA/IMC는 uasset 없이 C++ `NewObject`로 코드 생성. 폰 비주얼은 생성자 `ConstructorHelpers`로 마네킹 로드. 기존 `AProject_RE*` 템플릿은 참고만 하고 방치.

**Tech Stack:** UE 5.8 C++, EnhancedInput(이미 Build.cs 의존에 존재).

## Global Constraints

- 엔진 빌드 경로: `E:\UE_5.8\Engine\Build\BatchFiles\Build.bat`
- 빌드 타깃: `Project_REEditor Win64 Development`, `-project="E:\UnrealProjects\Project_RE\Project_RE.uproject"`
- 모든 설정은 C++/ini 텍스트로 유지(관리 용이성 최우선). BP/Input uasset 신규 생성 금지.
- 클래스는 게임 모듈 단일이므로 API 매크로(`PROJECT_RE_API`) 불필요 — 기존 `AProject_RE*` 스타일과 동일.
- 에셋 경로(ConstructorHelpers): 메시 `/Game/Characters/Mannequins/Meshes/SKM_Manny_Simple.SKM_Manny_Simple`, 애님BP `/Game/Characters/Mannequins/Anims/Unarmed/ABP_Unarmed`.
- 브랜치: `feature/M0-topdown-camera`.

**참고 — 테스트 전략:** 이 작업은 카메라/입력/설정 배선으로 런타임 관측(빌드+PIE)이 유일한 실질 검증이며, 프로젝트에 자동화 테스트 인프라가 없다. 따라서 pytest식 단위 TDD 대신 각 태스크는 **빌드 성공**을 게이트로, 최종 태스크는 **PIE 관측**을 게이트로 삼는다. (YAGNI: 생성자 값 검증용 UE Automation Spec은 M0 범위 밖.)

---

### Task 1: `ARECharacterBase` — 탑뷰 카메라 폰

**Files:**
- Modify: `Source/Project_RE/Project_RE.Build.cs` (PublicIncludePaths에 `"Project_RE/Core"` 추가)
- Create: `Source/Project_RE/Core/RECharacterBase.h`
- Create: `Source/Project_RE/Core/RECharacterBase.cpp`

**Interfaces:**
- Consumes: 없음(엔진만).
- Produces: `class ARECharacterBase : public ACharacter` — Task 3의 GameMode `DefaultPawnClass`가 참조.

- [ ] **Step 1: Build.cs에 Core 인클루드 경로 추가**

`Source/Project_RE/Project_RE.Build.cs`의 `PublicIncludePaths.AddRange(...)` 배열 첫 항목 `"Project_RE",` 다음 줄에 추가:

```csharp
			"Project_RE/Core",
```

- [ ] **Step 2: `RECharacterBase.h` 작성**

```cpp
// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "RECharacterBase.generated.h"

class USpringArmComponent;
class UCameraComponent;

/**
 *  탑뷰 쿼터뷰 플레이어 폰 베이스.
 *  캐릭터 부착 SpringArm + Camera를 절대회전으로 고정한다.
 *  (HP/복제는 M0 #3에서 추가)
 */
UCLASS()
class ARECharacterBase : public ACharacter
{
	GENERATED_BODY()

public:
	ARECharacterBase();

protected:
	/** 탑뷰 카메라 붐 (절대 하향 고정) */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Camera", meta = (AllowPrivateAccess = "true"))
	USpringArmComponent* CameraBoom;

	/** 탑뷰 카메라 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Camera", meta = (AllowPrivateAccess = "true"))
	UCameraComponent* TopDownCamera;
};
```

- [ ] **Step 3: `RECharacterBase.cpp` 작성**

```cpp
// Copyright Epic Games, Inc. All Rights Reserved.

#include "RECharacterBase.h"
#include "GameFramework/SpringArmComponent.h"
#include "Camera/CameraComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Animation/AnimInstance.h"
#include "Engine/SkeletalMesh.h"
#include "UObject/ConstructorHelpers.h"

ARECharacterBase::ARECharacterBase()
{
	PrimaryActorTick.bCanEverTick = false;

	// 폰이 컨트롤러 회전을 따라가지 않음 — 탑뷰 카메라 고정 유지
	bUseControllerRotationYaw = false;
	bUseControllerRotationPitch = false;
	bUseControllerRotationRoll = false;

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
```

- [ ] **Step 4: 빌드**

Run:
```bash
"E:/UE_5.8/Engine/Build/BatchFiles/Build.bat" Project_REEditor Win64 Development -project="E:/UnrealProjects/Project_RE/Project_RE.uproject" -waitmutex
```
Expected: `Build succeeded`, 에러 0.

- [ ] **Step 5: 커밋**

```bash
git add Source/Project_RE/Project_RE.Build.cs Source/Project_RE/Core/RECharacterBase.h Source/Project_RE/Core/RECharacterBase.cpp
git commit -m "feat(M0): add ARECharacterBase top-down camera pawn"
```

---

### Task 2: `AREPlayerController` — 우클릭 입력 뼈대

**Files:**
- Create: `Source/Project_RE/Core/REPlayerController.h`
- Create: `Source/Project_RE/Core/REPlayerController.cpp`

**Interfaces:**
- Consumes: 없음(엔진 EnhancedInput만).
- Produces: `class AREPlayerController : public APlayerController` — Task 3의 GameMode `PlayerControllerClass`가 참조.

- [ ] **Step 1: `REPlayerController.h` 작성**

```cpp
// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "REPlayerController.generated.h"

class UInputAction;
class UInputMappingContext;
struct FInputActionValue;

/**
 *  탑뷰 PlayerController. 우클릭 입력을 받을 뼈대만 세운다.
 *  IA/IMC는 uasset 없이 코드로 생성(transient). 실제 이동은 M2.
 */
UCLASS()
class AREPlayerController : public APlayerController
{
	GENERATED_BODY()

protected:
	virtual void BeginPlay() override;
	virtual void SetupInputComponent() override;

	/** 우클릭 핸들러 (M0에선 로그만) */
	void OnClickMove(const FInputActionValue& Value);

	UPROPERTY()
	UInputAction* ClickMoveAction;

	UPROPERTY()
	UInputMappingContext* TopDownMappingContext;
};
```

- [ ] **Step 2: `REPlayerController.cpp` 작성**

주의: `SetupInputComponent()`가 `BeginPlay()`보다 먼저 호출된다. 따라서 IA/IMC 생성과 BindAction은 `SetupInputComponent()`에서 하고, MappingContext 등록만 `BeginPlay()`에서 한다.

```cpp
// Copyright Epic Games, Inc. All Rights Reserved.

#include "REPlayerController.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "InputActionValue.h"
#include "Engine/LocalPlayer.h"

void AREPlayerController::SetupInputComponent()
{
	Super::SetupInputComponent();

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
	// TODO M2: 커서 히트 결과 계산 → Server RPC 이동 요청으로 교체
}
```

- [ ] **Step 3: 빌드**

Run:
```bash
"E:/UE_5.8/Engine/Build/BatchFiles/Build.bat" Project_REEditor Win64 Development -project="E:/UnrealProjects/Project_RE/Project_RE.uproject" -waitmutex
```
Expected: `Build succeeded`, 에러 0.

- [ ] **Step 4: 커밋**

```bash
git add Source/Project_RE/Core/REPlayerController.h Source/Project_RE/Core/REPlayerController.cpp
git commit -m "feat(M0): add AREPlayerController right-click input skeleton"
```

---

### Task 3: `AREGameMode` + 기본맵/게임모드 배선 (최종 통합)

**Files:**
- Create: `Source/Project_RE/Core/REGameMode.h`
- Create: `Source/Project_RE/Core/REGameMode.cpp`
- Modify: `Config/DefaultEngine.ini:2-4`

**Interfaces:**
- Consumes: `ARECharacterBase`(Task 1), `AREPlayerController`(Task 2).
- Produces: `class AREGameMode : public AGameModeBase` — DefaultEngine.ini의 `GlobalDefaultGameMode`가 참조.

- [ ] **Step 1: `REGameMode.h` 작성**

```cpp
// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "REGameMode.generated.h"

/**
 *  탑뷰 게임모드. 기본 폰/컨트롤러를 RE 클래스로 지정.
 *  (서버 전용 로직은 M0 #3에서 추가)
 */
UCLASS()
class AREGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	AREGameMode();
};
```

- [ ] **Step 2: `REGameMode.cpp` 작성**

```cpp
// Copyright Epic Games, Inc. All Rights Reserved.

#include "REGameMode.h"
#include "RECharacterBase.h"
#include "REPlayerController.h"

AREGameMode::AREGameMode()
{
	DefaultPawnClass = ARECharacterBase::StaticClass();
	PlayerControllerClass = AREPlayerController::StaticClass();
}
```

- [ ] **Step 3: `DefaultEngine.ini` 기본맵/게임모드 교체**

`Config/DefaultEngine.ini` 2~4행을 아래로 교체:

```ini
GameDefaultMap=/Game/Level/Main.Main
EditorStartupMap=/Game/Level/Main.Main
GlobalDefaultGameMode=/Script/Project_RE.REGameMode
```

- [ ] **Step 4: 빌드**

Run:
```bash
"E:/UE_5.8/Engine/Build/BatchFiles/Build.bat" Project_REEditor Win64 Development -project="E:/UnrealProjects/Project_RE/Project_RE.uproject" -waitmutex
```
Expected: `Build succeeded`, 에러 0.

- [ ] **Step 5: PIE 런타임 검증 (최종 Acceptance)**

에디터로 프로젝트 열기(사용자 수동) → `/Game/Level/Main` 자동 로드 → PIE 재생.
확인:
1. 마네킹 캐릭터가 위에서 내려다보는 쿼터뷰(약 −50°)로 보인다.
2. 게임 뷰포트서 **우클릭** → Output Log(`Window > Output Log`)에 `[RE] OnClickMove triggered` 출력.

둘 다 만족하면 이슈 #1 완료기준 충족.

- [ ] **Step 6: 커밋**

```bash
git add Source/Project_RE/Core/REGameMode.h Source/Project_RE/Core/REGameMode.cpp Config/DefaultEngine.ini
git commit -m "feat(M0): wire AREGameMode + set Main map as default (closes #1)"
```

---

## Self-Review

- **Spec coverage:** 스펙의 3 클래스 전부(ARECharacterBase→T1, AREPlayerController→T2, AREGameMode→T3), 카메라 값(−50/1500/90)→T1, 코드생성 입력→T2, ini 배선→T3, Build.cs→T1. 갭 없음.
- **Placeholder scan:** 코드 내 `// TODO M2`는 의도된 M2 마커(스펙 명시)로 유지. 그 외 미완 항목 없음.
- **Type consistency:** `ARECharacterBase`/`AREPlayerController`/`AREGameMode`, `ClickMoveAction`/`TopDownMappingContext`/`OnClickMove` 명칭 태스크 간 일치. GameMode가 참조하는 두 클래스명 T1/T2 정의와 동일.
- **주의(실행 시 확인):** 빌드 전 새 파일이 UBT에 잡히도록, 실패 시 `Project_RE.uproject` 우클릭 → "Generate Visual Studio project files" 후 재빌드. `Project_REEditor` 타깃명이 다르면 `Source/*.Target.cs` 확인.
