# 치트 패널 (무적 토글) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** F1로 여는 인게임 치트 패널에 플레이어 무적 토글을 넣는다.

**Architecture:** 치트 상태는 CVar `re.Cheat.PlayerInvincible` 백엔드. `RECharacterBase::TakeDamage`가 CVar를 읽어 무피해 처리. 순수 C++ `UUserWidget` 패널(체크박스 1행)이 CVar를 토글. `REPlayerController`가 F1 입력으로 패널 표시/숨김.

**Tech Stack:** UE 5.8, C++ (UMG WidgetTree 코드 구성, EnhancedInput 코드생성 IA/IMC, CVar).

## Global Constraints

- UI는 uasset 없이 순수 C++ (`WidgetTree->ConstructWidget`) — `REResultWidget` idiom 준수.
- 입력은 코드생성 transient IA/IMC — `REPlayerController` 기존 패턴 준수.
- 빌드 타깃: `Project_REEditor Win64 Development`, `-Project="E:\UnrealProjects\Project_RE\Project_RE.uproject"`.
- 빌드 게이트(Git Bash, `MSYS_NO_PATHCONV=1` 필수):
  ```bash
  MSYS_NO_PATHCONV=1 "/e/UE_5.8/Engine/Build/BatchFiles/Build.bat" \
    Project_REEditor Win64 Development \
    -Project="E:\UnrealProjects\Project_RE\Project_RE.uproject" -WaitMutex -NoHotReload
  ```
- 커밋 메시지 끝에 `Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>`.
- 이 프로젝트엔 유닛테스트 하니스 없음 — 각 태스크 검증 = 빌드 게이트, 최종 통합 = PIE 육안.

---

### Task 1: CVar + TakeDamage 무적 가드

**Files:**
- Modify: `Source/Project_RE/Core/RECharacterBase.cpp`

**Interfaces:**
- Produces: CVar `re.Cheat.PlayerInvincible` (int32, 기본 0). Task 2 위젯이 이 이름으로 조회.

- [ ] **Step 1: CVar 선언 추가**

`RECharacterBase.cpp` 상단 include 블록 바로 아래(첫 함수 정의 위) 파일 스코프에 추가.
`IConsoleManager.h` include가 없으면 include 블록에 추가:

```cpp
#include "HAL/IConsoleManager.h"
```

```cpp
// 치트: 1이면 플레이어 무적(TakeDamage 무피해). 데브 전용, 클라 로컬(ECVF_Cheat).
static TAutoConsoleVariable<int32> CVarPlayerInvincible(
	TEXT("re.Cheat.PlayerInvincible"),
	0,
	TEXT("1 = player takes no damage (dev cheat, client-local)"),
	ECVF_Cheat);
```

- [ ] **Step 2: TakeDamage 가드 추가**

`RECharacterBase::TakeDamage` 의 `HasAuthority()` 가드 **직후**, `Super::TakeDamage` 호출 **전**에 삽입:

```cpp
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
```

- [ ] **Step 3: 빌드 게이트**

Run (Global Constraints의 빌드 명령).
Expected: `BUILD SUCCESSFUL`, 컴파일 에러 0.

- [ ] **Step 4: 커밋**

```bash
git add Source/Project_RE/Core/RECharacterBase.cpp
git commit -m "feat(cheat): re.Cheat.PlayerInvincible CVar + TakeDamage 무적 가드"
```

---

### Task 2: RECheatPanelWidget

**Files:**
- Create: `Source/Project_RE/UI/RECheatPanelWidget.h`
- Create: `Source/Project_RE/UI/RECheatPanelWidget.cpp`

**Interfaces:**
- Consumes: CVar `re.Cheat.PlayerInvincible` (Task 1).
- Produces: `URECheatPanelWidget` (UUserWidget 파생). Task 3이 `CreateWidget`으로 생성.

- [ ] **Step 1: 헤더 작성**

`Source/Project_RE/UI/RECheatPanelWidget.h`:

```cpp
// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "RECheatPanelWidget.generated.h"

class UCheckBox;

/**
 *  인게임 치트 패널 (SRDebugger-lite). 순수 C++ UUserWidget — Initialize()에서 트리 구성.
 *  치트 상태는 CVar 백엔드. 지금은 무적 토글 1개. 치트 추가 = 행 빌드 블록 복사.
 */
UCLASS()
class URECheatPanelWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	virtual bool Initialize() override;

private:
	/** 무적 체크박스 상태 변경 → re.Cheat.PlayerInvincible CVar 세팅. */
	UFUNCTION()
	void OnInvincibleChanged(bool bIsChecked);

	UPROPERTY()
	UCheckBox* InvincibleCheck = nullptr;
};
```

- [ ] **Step 2: cpp 작성**

`Source/Project_RE/UI/RECheatPanelWidget.cpp`:

```cpp
// Copyright Epic Games, Inc. All Rights Reserved.

#include "RECheatPanelWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/VerticalBox.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/CheckBox.h"
#include "Components/TextBlock.h"
#include "Styling/CoreStyle.h"
#include "Styling/SlateTypes.h"
#include "HAL/IConsoleManager.h"

bool URECheatPanelWidget::Initialize()
{
	if (!Super::Initialize())
	{
		return false;
	}

	// CDO는 트리 구성 대상 아님 — 인스턴스에서만 (REResultWidget 동일).
	if (!WidgetTree || InvincibleCheck)
	{
		return true;
	}

	UBorder* Root = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("Root"));
	Root->SetBrushColor(FLinearColor(0.f, 0.f, 0.f, 0.6f));
	Root->SetPadding(FMargin(12.f));

	UVerticalBox* Box = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("Box"));
	Root->AddChild(Box);

	// 타이틀
	UTextBlock* Title = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("Title"));
	Title->SetText(FText::FromString(TEXT("CHEATS")));
	Title->SetFont(FCoreStyle::GetDefaultFontStyle("Bold", 24));
	Box->AddChild(Title);

	// 무적 행: [체크박스] Player Invincible
	UHorizontalBox* Row = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("InvincibleRow"));
	Box->AddChild(Row);

	InvincibleCheck = WidgetTree->ConstructWidget<UCheckBox>(UCheckBox::StaticClass(), TEXT("InvincibleCheck"));
	// C++ 생성 UCheckBox는 스타일이 비어 안 보임 → 엔진 기본 체크박스 스타일 주입.
	InvincibleCheck->WidgetStyle = FCoreStyle::Get().GetWidgetStyle<FCheckBoxStyle>("Checkbox");
	// 초기 상태 = CVar 현재값.
	static IConsoleVariable* Inv = IConsoleManager::Get().FindConsoleVariable(TEXT("re.Cheat.PlayerInvincible"));
	InvincibleCheck->SetIsChecked(Inv && Inv->GetInt() != 0);
	InvincibleCheck->OnCheckStateChanged.AddDynamic(this, &URECheatPanelWidget::OnInvincibleChanged);
	Row->AddChild(InvincibleCheck);

	UTextBlock* Label = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("InvincibleLabel"));
	Label->SetText(FText::FromString(TEXT("Player Invincible")));
	Label->SetFont(FCoreStyle::GetDefaultFontStyle("Regular", 18));
	if (UHorizontalBoxSlot* LabelSlot = Cast<UHorizontalBoxSlot>(Row->AddChild(Label)))
	{
		LabelSlot->SetPadding(FMargin(8.f, 0.f, 0.f, 0.f));
		LabelSlot->SetVerticalAlignment(VAlign_Center);
	}

	WidgetTree->RootWidget = Root;
	return true;
}

void URECheatPanelWidget::OnInvincibleChanged(bool bIsChecked)
{
	static IConsoleVariable* Inv = IConsoleManager::Get().FindConsoleVariable(TEXT("re.Cheat.PlayerInvincible"));
	if (Inv)
	{
		Inv->Set(bIsChecked ? 1 : 0);
	}
}
```

- [ ] **Step 3: 빌드 게이트**

Run (Global Constraints의 빌드 명령).
Expected: `BUILD SUCCESSFUL`. UMG 심볼(UBorder/UCheckBox 등)은 UMG 모듈 — 이미 `Project_RE.Build.cs`에 UMG 있음(HP바 위젯 존재). 링크 에러 시 `Build.cs`의 `PublicDependencyModuleNames`에 `"UMG"` 확인.

- [ ] **Step 4: 커밋**

```bash
git add Source/Project_RE/UI/RECheatPanelWidget.h Source/Project_RE/UI/RECheatPanelWidget.cpp
git commit -m "feat(cheat): RECheatPanelWidget — 무적 체크박스 패널"
```

---

### Task 3: F1 토글 입력 배선

**Files:**
- Modify: `Source/Project_RE/Core/REPlayerController.h`
- Modify: `Source/Project_RE/Core/REPlayerController.cpp`

**Interfaces:**
- Consumes: `URECheatPanelWidget` (Task 2).

- [ ] **Step 1: 헤더에 forward + 멤버 추가**

`REPlayerController.h` 의 forward 선언부(`struct FInputActionValue;` 아래)에:

```cpp
class URECheatPanelWidget;
```

`protected:` UPROPERTY 멤버 블록(`TopDownMappingContext` 아래)에:

```cpp
	UPROPERTY()
	UInputAction* CheatPanelAction;
```

`private:` 블록 끝에:

```cpp
	/** 치트 패널 토글 (F1). 위젯 1회 생성 후 표시/숨김. */
	void OnToggleCheatPanel();

	UPROPERTY()
	URECheatPanelWidget* CheatPanel = nullptr;
```

- [ ] **Step 2: cpp include 추가**

`REPlayerController.cpp` include 블록(`#include "REResultWidget.h"` 근처)에:

```cpp
#include "RECheatPanelWidget.h"
```

- [ ] **Step 3: SetupInputComponent에 IA 생성 + 매핑 + 바인드**

`FireAction` 매핑 블록 뒤(`if (UEnhancedInputComponent* EIC ...)` 앞)에:

```cpp
	// F1 치트 패널 토글 IA (코드생성, transient).
	CheatPanelAction = NewObject<UInputAction>(this, TEXT("IA_CheatPanel"));
	CheatPanelAction->ValueType = EInputActionValueType::Boolean;
	TopDownMappingContext->MapKey(CheatPanelAction, EKeys::F1);
```

`EIC->BindAction(...)` 블록 안, `FireAction` 바인드 줄 뒤에:

```cpp
		EIC->BindAction(CheatPanelAction, ETriggerEvent::Started, this, &AREPlayerController::OnToggleCheatPanel);
```

- [ ] **Step 4: 토글 핸들러 구현**

`REPlayerController.cpp` 아무 함수 정의 뒤(예: `Client_ShowResult_Implementation` 뒤)에:

```cpp
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
```

- [ ] **Step 5: 빌드 게이트**

Run (Global Constraints의 빌드 명령).
Expected: `BUILD SUCCESSFUL`.

- [ ] **Step 6: 커밋**

```bash
git add Source/Project_RE/Core/REPlayerController.h Source/Project_RE/Core/REPlayerController.cpp
git commit -m "feat(cheat): F1 치트 패널 토글 입력 배선"
```

---

### Task 4: PIE 통합 검증

**Files:** 없음 (수동 검증).

- [ ] **Step 1: 에디터에서 PIE 실행**

프로젝트 열고 Play(PIE). 보스 탄막이 나오는 레벨.

- [ ] **Step 2: 패널 열기**

`F1` → 화면에 "CHEATS" + `[ ] Player Invincible` 체크박스 표시 확인.

- [ ] **Step 3: 무적 켜고 검증**

체크박스 클릭(체크) → 플레이어를 보스 탄막 속에 두기 → HP 100 유지 확인.
체크 해제 → 탄 맞으면 HP 감소 확인.

- [ ] **Step 4: 토글 재확인**

`F1` 다시 → 패널 숨김. `F1` → 다시 표시.

검증 통과 = 완료. 실패 시 로그 확인 후 해당 태스크로 회귀.
```