# HP바 월드스페이스 위젯 (#29) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 플레이어·보스 머리 위 월드스페이스 HP바 — 피격 시 실시간 갱신, M4 데디 전환 시 무수정 동작.

**Architecture:** 순수 C++ `UUserWidget`(WidgetTree로 `UProgressBar` 생성, BP 자산 0개) + 셋업 캡슐화한 `UWidgetComponent` 서브클래스. 캐릭터는 `ReplicatedUsing=OnRep_Health`로 전환하고 서버 경로(TakeDamage)와 클라 경로(OnRep)가 같은 갱신 함수를 호출.

**Tech Stack:** UE 5.8, UMG(UWidgetComponent/UProgressBar — Build.cs에 이미 존재), 스펙: `docs/superpowers/specs/2026-07-13-hp-bar-widget-design.md`

## Global Constraints

- 엔진 빌드: `"/e/UE_5.8/Engine/Build/BatchFiles/Build.bat" Project_REEditor Win64 Development -Project="E:\UnrealProjects\Project_RE\Project_RE.uproject" -WaitMutex -NoHotReload` → 기대 `Result: Succeeded`
- 자동화 테스트 인프라 없음 → 게이트 = **빌드 성공** + **실RHI 스크린샷 프로브** + **PIE 육안**
- 로그 접두어 `[RE]` 고정. 브랜치 `feature/M2-hp-bar`, PR base=dev
- Git Bash에서 UE 실행 시 `MSYS_NO_PATHCONV=1` 필수
- `-nullrhi`로는 렌더 검증 불가 — 스크린샷 프로브는 실RHI `-windowed`로만

---

### Task 1: UREHealthBarWidget — 순수 C++ 프로그레스바 위젯

**Files:**
- Create: `Source/Project_RE/UI/REHealthBarWidget.h`
- Create: `Source/Project_RE/UI/REHealthBarWidget.cpp`
- Modify: `Source/Project_RE/Project_RE.Build.cs` (PublicIncludePaths에 `Project_RE/UI` 추가)

**Interfaces:**
- Produces: `UREHealthBarWidget::SetPercent(float)` — 0~1 클램프 후 프로그레스바 반영. `SetBarColor(FLinearColor)` — 채움색. Task 2가 소비.

- [ ] **Step 1: 헤더 작성**

`Source/Project_RE/UI/REHealthBarWidget.h`:

```cpp
// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "REHealthBarWidget.generated.h"

class UProgressBar;

/**
 *  순수 C++ HP바 위젯 (#29). Initialize()에서 WidgetTree로 UProgressBar 루트 생성 — BP 자산 불필요.
 *  NativeConstruct는 슬레이트 트리 구축 후에 불려서 늦다 — RootWidget은 Initialize()에서 세팅해야 한다.
 */
UCLASS()
class UREHealthBarWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	/** 프로그레스바 percent 설정 (0~1 클램프). */
	void SetPercent(float InPercent);

	/** 바 채움 색 설정. */
	void SetBarColor(FLinearColor InColor);

protected:
	virtual bool Initialize() override;

private:
	/** WidgetTree 소유 프로그레스바 루트. */
	UPROPERTY()
	TObjectPtr<UProgressBar> Bar;
};
```

- [ ] **Step 2: 구현 작성**

`Source/Project_RE/UI/REHealthBarWidget.cpp`:

```cpp
// Copyright Epic Games, Inc. All Rights Reserved.

#include "REHealthBarWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/ProgressBar.h"

bool UREHealthBarWidget::Initialize()
{
	if (!Super::Initialize())
	{
		return false;
	}

	// CDO는 WidgetTree 트리 구성 대상 아님 — 인스턴스에서만 생성.
	if (WidgetTree && !Bar)
	{
		Bar = WidgetTree->ConstructWidget<UProgressBar>(UProgressBar::StaticClass(), TEXT("Bar"));
		Bar->SetPercent(1.f);
		WidgetTree->RootWidget = Bar;
	}
	return true;
}

void UREHealthBarWidget::SetPercent(float InPercent)
{
	if (Bar)
	{
		Bar->SetPercent(FMath::Clamp(InPercent, 0.f, 1.f));
	}
}

void UREHealthBarWidget::SetBarColor(FLinearColor InColor)
{
	if (Bar)
	{
		Bar->SetFillColorAndOpacity(InColor);
	}
}
```

- [ ] **Step 3: Build.cs에 UI 경로 추가**

`Source/Project_RE/Project_RE.Build.cs`의 `PublicIncludePaths`에서 `"Project_RE/Mass",` 다음 줄에 추가:

```csharp
			"Project_RE/UI",
```

- [ ] **Step 4: 빌드 게이트**

```bash
"/e/UE_5.8/Engine/Build/BatchFiles/Build.bat" Project_REEditor Win64 Development -Project="E:\UnrealProjects\Project_RE\Project_RE.uproject" -WaitMutex -NoHotReload
```
기대: `Result: Succeeded`, 에러 0.

- [ ] **Step 5: 커밋**

```bash
git add Source/Project_RE/UI/REHealthBarWidget.h Source/Project_RE/UI/REHealthBarWidget.cpp Source/Project_RE/Project_RE.Build.cs
git commit -m "feat(M2): add pure C++ HP bar widget, no BP asset (#29)"
```

---

### Task 2: UREHealthBarComponent — 셋업 캡슐화 위젯 컴포넌트

**Files:**
- Create: `Source/Project_RE/UI/REHealthBarComponent.h`
- Create: `Source/Project_RE/UI/REHealthBarComponent.cpp`

**Interfaces:**
- Consumes: Task 1의 `UREHealthBarWidget::SetPercent(float)` / `SetBarColor(FLinearColor)`
- Produces: `UREHealthBarComponent::SetHealthPercent(float)` + `BarColor` 멤버(`FLinearColor`, 기본 Green). Task 3이 소비.

- [ ] **Step 1: 헤더 작성**

`Source/Project_RE/UI/REHealthBarComponent.h`:

```cpp
// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/WidgetComponent.h"
#include "REHealthBarComponent.generated.h"

/**
 *  머리 위 월드스페이스 HP바 컴포넌트 (#29). 셋업 전부 캡슐화 — 소유 캐릭터는 SetHealthPercent만 호출.
 *  절대회전 고정: 캐릭터가 bOrientRotationToMovement로 회전해도 바는 탑뷰 카메라(피치 -50)를 계속 정면으로 본다.
 */
UCLASS()
class UREHealthBarComponent : public UWidgetComponent
{
	GENERATED_BODY()

public:
	UREHealthBarComponent();

	/** HP 비율(0~1) 반영. 위젯 미생성 시점 호출이면 캐시 후 InitWidget에서 적용. */
	void SetHealthPercent(float Percent);

	//~ 위젯 생성 직후 색/캐시 percent 적용.
	virtual void InitWidget() override;

	/** 바 채움색. 캐릭터 생성자에서 지정 (플레이어 초록, 보스 빨강). */
	UPROPERTY(EditAnywhere, Category = "HealthBar")
	FLinearColor BarColor = FLinearColor::Green;

private:
	/** 위젯 생성 전 SetHealthPercent 호출 대비 캐시. */
	float CachedPercent = 1.f;
};
```

- [ ] **Step 2: 구현 작성**

`Source/Project_RE/UI/REHealthBarComponent.cpp`:

```cpp
// Copyright Epic Games, Inc. All Rights Reserved.

#include "REHealthBarComponent.h"
#include "REHealthBarWidget.h"

UREHealthBarComponent::UREHealthBarComponent()
{
	SetWidgetSpace(EWidgetSpace::World);
	SetWidgetClass(UREHealthBarWidget::StaticClass());
	SetDrawSize(FVector2D(100.f, 10.f));

	// (피치 +50, 요 180) = 카메라 붐(피치 -50, 요 0)의 정반대 방향 — 항상 카메라 정면.
	// 절대회전 필수: 캐릭터 요 회전에 끌려가면 바가 옆면으로 사라진다.
	SetUsingAbsoluteRotation(true);
	SetRelativeRotation(FRotator(50.f, 180.f, 0.f));
	SetRelativeLocation(FVector(0.f, 0.f, 120.f));

	// 자동사격 ECC_Pawn 라인트레이스 차단 방지.
	SetCollisionEnabled(ECollisionEnabled::NoCollision);
}

void UREHealthBarComponent::InitWidget()
{
	Super::InitWidget();

	// 데디 서버는 엔진이 위젯 생성 스킵 → Cast 실패로 자연 no-op.
	if (UREHealthBarWidget* Bar = Cast<UREHealthBarWidget>(GetUserWidgetObject()))
	{
		Bar->SetBarColor(BarColor);
		Bar->SetPercent(CachedPercent);
	}
}

void UREHealthBarComponent::SetHealthPercent(float Percent)
{
	CachedPercent = FMath::Clamp(Percent, 0.f, 1.f);
	if (UREHealthBarWidget* Bar = Cast<UREHealthBarWidget>(GetUserWidgetObject()))
	{
		Bar->SetPercent(CachedPercent);
	}
}
```

- [ ] **Step 3: 빌드 게이트**

```bash
"/e/UE_5.8/Engine/Build/BatchFiles/Build.bat" Project_REEditor Win64 Development -Project="E:\UnrealProjects\Project_RE\Project_RE.uproject" -WaitMutex -NoHotReload
```
기대: `Result: Succeeded`, 에러 0.

- [ ] **Step 4: 커밋**

```bash
git add Source/Project_RE/UI/REHealthBarComponent.h Source/Project_RE/UI/REHealthBarComponent.cpp
git commit -m "feat(M2): add world-space health bar widget component (#29)"
```

---

### Task 3: 플레이어·보스 배선 — OnRep_Health + 컴포넌트 부착

**Files:**
- Modify: `Source/Project_RE/Core/RECharacterBase.h` (Health UPROPERTY, OnRep 선언, HealthBar 멤버, 전방선언)
- Modify: `Source/Project_RE/Core/RECharacterBase.cpp` (include, 생성자 부착, OnRep 구현, TakeDamage 호출)
- Modify: `Source/Project_RE/Core/REBossCharacter.h` (동일)
- Modify: `Source/Project_RE/Core/REBossCharacter.cpp` (동일, 색만 빨강)

**Interfaces:**
- Consumes: Task 2의 `UREHealthBarComponent::SetHealthPercent(float)`, `BarColor`
- Produces: `OnRep_Health()` — 서버 경로(TakeDamage)와 클라 경로(복제 OnRep) 공용 갱신 함수

- [ ] **Step 1: RECharacterBase.h 수정**

전방선언 블록(`class UREGA_Dash;` 아래)에 추가:

```cpp
class UREHealthBarComponent;
```

`Health` UPROPERTY를 `ReplicatedUsing`으로 교체:

```cpp
	/** 현재 체력. 서버 권위, 클라 복제. 변경 시 OnRep_Health로 HP바 갱신. */
	UPROPERTY(ReplicatedUsing = OnRep_Health, VisibleAnywhere, BlueprintReadOnly, Category = "Stats")
	float Health = 100.f;
```

`protected:` 섹션에 추가 (AutoFireComponent 멤버 아래):

```cpp
	/** 머리 위 HP바 (#29). 셋업은 컴포넌트가 자체 처리. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "UI", meta = (AllowPrivateAccess = "true"))
	UREHealthBarComponent* HealthBar;

	/** Health 복제 도착(클라) / 서버 직접 호출 공용 — HP바 갱신. */
	UFUNCTION()
	void OnRep_Health();
```

- [ ] **Step 2: RECharacterBase.cpp 수정**

include 추가 (`#include "Abilities/REGA_Dash.h"` 아래):

```cpp
#include "REHealthBarComponent.h"
```

생성자에서 AutoFireComponent 생성 다음에 추가:

```cpp
	// HP바 (#29) — 플레이어 초록. 회전/사이즈/위젯클래스는 컴포넌트 생성자가 처리.
	HealthBar = CreateDefaultSubobject<UREHealthBarComponent>(TEXT("HealthBar"));
	HealthBar->SetupAttachment(RootComponent);
	HealthBar->BarColor = FLinearColor::Green;
```

`TakeDamage`의 `Health = FMath::Clamp(...)` 직후에 추가:

```cpp
	OnRep_Health(); // 서버/싱글 경로 — 복제 OnRep은 원격 클라 전용이라 직접 호출
```

파일 끝에 구현 추가:

```cpp
void ARECharacterBase::OnRep_Health()
{
	if (HealthBar)
	{
		HealthBar->SetHealthPercent(MaxHealth > 0.f ? Health / MaxHealth : 0.f);
	}
}
```

- [ ] **Step 3: REBossCharacter.h 수정**

`#include "REBossCharacter.generated.h"` 위에 전방선언 추가:

```cpp
class UREHealthBarComponent;
```

`Health` UPROPERTY 교체:

```cpp
	/** 현재 체력. 서버 권위, 클라 복제. 변경 시 OnRep_Health로 HP바 갱신. */
	UPROPERTY(ReplicatedUsing = OnRep_Health, VisibleAnywhere, BlueprintReadOnly, Category = "Stats")
	float Health = 100.f;
```

`protected:` 섹션에 추가 (MaxHealth 아래):

```cpp
	/** 머리 위 HP바 (#29). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "UI", meta = (AllowPrivateAccess = "true"))
	UREHealthBarComponent* HealthBar;

	/** Health 복제 도착(클라) / 서버 직접 호출 공용 — HP바 갱신. */
	UFUNCTION()
	void OnRep_Health();
```

- [ ] **Step 4: REBossCharacter.cpp 수정**

include 추가 (`#include "Net/UnrealNetwork.h"` 아래):

```cpp
#include "REHealthBarComponent.h"
```

생성자 끝에 추가:

```cpp
	// HP바 (#29) — 보스 빨강.
	HealthBar = CreateDefaultSubobject<UREHealthBarComponent>(TEXT("HealthBar"));
	HealthBar->SetupAttachment(RootComponent);
	HealthBar->BarColor = FLinearColor::Red;
```

`TakeDamage`의 `Health = FMath::Clamp(...)` 직후(사망 체크 위)에 추가:

```cpp
	OnRep_Health(); // 서버/싱글 경로 — 복제 OnRep은 원격 클라 전용이라 직접 호출
```

파일 끝에 구현 추가:

```cpp
void AREBossCharacter::OnRep_Health()
{
	if (HealthBar)
	{
		HealthBar->SetHealthPercent(MaxHealth > 0.f ? Health / MaxHealth : 0.f);
	}
}
```

- [ ] **Step 5: 빌드 게이트**

```bash
"/e/UE_5.8/Engine/Build/BatchFiles/Build.bat" Project_REEditor Win64 Development -Project="E:\UnrealProjects\Project_RE\Project_RE.uproject" -WaitMutex -NoHotReload
```
기대: `Result: Succeeded`, 에러 0.

- [ ] **Step 6: 커밋**

```bash
git add Source/Project_RE/Core/RECharacterBase.h Source/Project_RE/Core/RECharacterBase.cpp Source/Project_RE/Core/REBossCharacter.h Source/Project_RE/Core/REBossCharacter.cpp
git commit -m "feat(M2): wire HP bar to player/boss via OnRep_Health (#29)"
```

---

### Task 4: 실RHI 스크린샷 프로브 검증

신규 커밋 코드 없음. `REGameMode::BeginPlay`에 **임시(커밋 금지)** 스크린샷 타이머를 넣고 실RHI 창모드로 돌려 바 표시+감소를 PNG로 검증한 뒤 임시 코드를 되돌린다.

**검증 시나리오:** 보스는 0.1s 주기 Spiral 탄막 발사(기존 데모) → 플레이어 피격 → 플레이어바 감소. 자동사격은 최근접 보스 자동 조준 → 보스바 감소. 5초 시점 Shot1, 12초 시점 Shot2 비교.

**Files:**
- Modify (임시, revert): `Source/Project_RE/Core/REGameMode.cpp`

- [ ] **Step 1: 임시 프로브 코드 삽입**

`REGameMode.cpp` include 블록에 추가:

```cpp
// TEMP #29 probe — 커밋 금지
#include "UnrealClient.h"
#include "Misc/Paths.h"
#include "Engine/Engine.h"
```

`BeginPlay()` 끝에 추가:

```cpp
	// TEMP #29 probe — 커밋 금지: 5s/12s 스크린샷, 15s 종료
	FTimerHandle ShotT1, ShotT2, QuitT;
	GetWorld()->GetTimerManager().SetTimer(ShotT1, FTimerDelegate::CreateLambda([]()
	{
		FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir() / TEXT("Shot1.png"), false, false);
	}), 5.f, false);
	GetWorld()->GetTimerManager().SetTimer(ShotT2, FTimerDelegate::CreateLambda([]()
	{
		FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir() / TEXT("Shot2.png"), false, false);
	}), 12.f, false);
	GetWorld()->GetTimerManager().SetTimer(QuitT, FTimerDelegate::CreateLambda([this]()
	{
		GEngine->Exec(GetWorld(), TEXT("quit"));
	}), 15.f, false);
```

- [ ] **Step 2: 빌드**

```bash
"/e/UE_5.8/Engine/Build/BatchFiles/Build.bat" Project_REEditor Win64 Development -Project="E:\UnrealProjects\Project_RE\Project_RE.uproject" -WaitMutex -NoHotReload
```
기대: `Result: Succeeded`.

- [ ] **Step 3: 실RHI 창모드 실행** (Git Bash, 셰이더 컴파일로 첫 실행 오래 걸림 — timeout 300s)

```bash
MSYS_NO_PATHCONV=1 "/e/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" \
  "E:\UnrealProjects\Project_RE\Project_RE.uproject" /Game/Level/Main \
  -game -windowed -ResX=1280 -ResY=720 -nosplash -stdout -NoSound -log=RE_probe29.log
```
15초 quit 타이머로 자체 종료. 스크린샷은 async write — 종료 직후 없으면 잠깐 뒤 재확인.

- [ ] **Step 4: PNG 검증**

`Saved/Shot1.png`, `Saved/Shot2.png`를 Read 툴로 확인:
- Shot1: 플레이어 머리 위 초록 바 + 보스 머리 위 빨강 바 표시
- Shot2: 두 바 모두 Shot1보다 감소 (보스 조기 사망 시 보스바 0도 통과)

실패 시: superpowers:systematic-debugging으로 원인 규명 후 수정 (위치/회전/DrawSize 튜닝은 이 태스크 안에서 반복).

- [ ] **Step 5: 임시 코드 revert**

```bash
git checkout -- Source/Project_RE/Core/REGameMode.cpp
git status --short   # REGameMode.cpp 변경 없음 확인
```

- [ ] **Step 6: 재빌드 (revert 반영)**

```bash
"/e/UE_5.8/Engine/Build/BatchFiles/Build.bat" Project_REEditor Win64 Development -Project="E:\UnrealProjects\Project_RE\Project_RE.uproject" -WaitMutex -NoHotReload
```
기대: `Result: Succeeded`.

---

### Task 5: 푸시 + PR

- [ ] **Step 1: 푸시**

```bash
git push -u origin feature/M2-hp-bar
```

- [ ] **Step 2: PR 생성** (컨벤션: base=dev, 이슈 #29 메타 미러링 — label `C++`,`UI`, milestone M2, assignee leejimin3, project 연결. Reviewer 생략. 본문 6개 필드 전부.)

```bash
gh pr create --base dev --title "feat(M2): HP바 월드스페이스 위젯 (플레이어+보스) (#29)" --label "C++" --label "UI" --milestone "M2: 플레이어 게임루프" --assignee leejimin3 --body "..."
```
본문: 요약 / 변경사항 / 이슈링크(Closes #29) / 검증(빌드+스크린샷 프로브 결과 PNG 첨부) / 스코프 제외 / 참고 — 6개 필드 채움.

- [ ] **Step 3: PIE 육안 최종 확인 요청**

사용자에게 PIE 1회 육안 확인 요청 (바 표시 + 피격 감소). 이후 /검증 스킬로 머지 진행.
