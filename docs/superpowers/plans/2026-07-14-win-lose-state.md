# 승패 상태 + 결과 화면 (#40) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 게임루프를 닫는다 — 플레이어 HP 0 → "DEFEAT", 보스 HP 0 → "VICTORY". 판정은 서버 권위, 결과는 화면 중앙 텍스트 한 줄, 탄막·자동사격·입력 전부 정지.

**Architecture:** 멈출 것 3개(탄막 타이머 / 자동사격 타이머 / 플레이어 입력)가 서로 다른 액터에 흩어져 있다 → 이걸 전부 아는 `AREGameMode`가 `EndGame(bool bVictory)`로 오케스트레이션. `AGameModeBase`는 서버에만 존재하므로 새 권위 경로 없음. 판정 호출부는 양쪽 `TakeDamage`의 **기존** `HasAuthority()` 가드 안, 보스의 기존 `bIsDead` 패턴 재사용. 결과 화면은 `AREPlayerController`의 Client RPC 경유 → M4 데디 전환 시 수정 0줄.

**Tech Stack:** UE 5.8 C++, UMG(`UUserWidget`/`UTextBlock`/`UOverlay` — `Build.cs`에 이미 존재), 스펙: `docs/superpowers/specs/2026-07-14-win-lose-state-design.md`

## Global Constraints

- 엔진 빌드: `"/e/UE_5.8/Engine/Build/BatchFiles/Build.bat" Project_REEditor Win64 Development -Project="E:\UnrealProjects\Project_RE\Project_RE.uproject" -WaitMutex -NoHotReload` → 기대 `Result: Succeeded`
- **`Build.cs` / `.uproject` 변경 금지** — `UMG`·`Slate` 모듈과 `Project_RE/UI` include 경로는 #29에서 이미 들어갔다.
- 자동화 테스트 인프라 없음 → 게이트 = **빌드 성공** + **headless 로그 프로브** + **실RHI 스크린샷** + **PIE 육안**
- 로그 접두어 `[RE]` 고정. 클래스/함수명은 스펙과 동일하게.
- Git Bash에서 UE 실행 시 `MSYS_NO_PATHCONV=1` 필수
- `-nullrhi`로는 위젯 렌더 검증 불가 — 스크린샷은 실RHI `-windowed`로만
- 브랜치: `feature/M2-win-lose-state` (dev에서 분기). PR base=dev
- YAGNI: 재시작/리스폰, `AREGameState` 상태기계, 사망 연출, 뜬 탄막 일괄 소멸 — 전부 스코프 밖

**사용 API (전부 기존/표준 — 신규 엔진 모듈 없음):**
- `UWidgetTree::ConstructWidget<T>()` — #29 `UREHealthBarWidget`과 동일 패턴
- `UUserWidget::AddToViewport()` — 화면공간 (HP바의 월드스페이스 `UWidgetComponent`와 대비)
- `UWorld::GetAuthGameMode<T>()` — 서버에서만 non-null
- `AActor::FindComponentByClass<T>()` — 캐릭터의 protected `AutoFireComponent`에 accessor 안 뚫고 접근
- `APlayerController::DisableInput(APlayerController*)`
- `FTimerManager::ClearTimer(FTimerHandle&)`

---

### Task 1: `UREResultWidget` — 순수 C++ 결과 텍스트 위젯

**Files:**
- Create: `Source/Project_RE/UI/REResultWidget.h`
- Create: `Source/Project_RE/UI/REResultWidget.cpp`

**Interfaces:**
- Consumes: 없음 (엔진 UMG만)
- Produces: `UREResultWidget::SetResult(bool bVictory)` — Task 3의 `AREPlayerController::Client_ShowResult`가 호출

- [ ] **Step 0: 브랜치 생성** (현재 브랜치는 `feature/M2-dash-iframe` — dev에서 새로 분기)

```bash
git fetch origin
git checkout -b feature/M2-win-lose-state origin/dev
```

- [ ] **Step 1: 헤더 작성**

`Source/Project_RE/UI/REResultWidget.h`:

```cpp
// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "REResultWidget.generated.h"

class UTextBlock;

/**
 *  순수 C++ 결과 화면 위젯 (#40). Initialize()에서 WidgetTree로 Overlay+TextBlock 생성 — BP 자산 불필요.
 *  HP바(#29)와 달리 월드스페이스가 아니라 화면공간 — AddToViewport()로 뷰포트에 직접 올린다.
 *  Overlay 루트: TextBlock 단독 루트는 뷰포트 슬롯에서 Fill 되어 세로 중앙 정렬이 보장되지 않는다.
 */
UCLASS()
class UREResultWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	/** 승/패 텍스트·색 적용. VICTORY=초록 / DEFEAT=빨강. */
	void SetResult(bool bVictory);

protected:
	virtual bool Initialize() override;

private:
	/** WidgetTree 소유 결과 텍스트. */
	UPROPERTY()
	TObjectPtr<UTextBlock> ResultText;
};
```

- [ ] **Step 2: 구현 작성**

`Source/Project_RE/UI/REResultWidget.cpp`:

```cpp
// Copyright Epic Games, Inc. All Rights Reserved.

#include "REResultWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/TextBlock.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Styling/CoreStyle.h"

bool UREResultWidget::Initialize()
{
	if (!Super::Initialize())
	{
		return false;
	}

	// CDO는 WidgetTree 트리 구성 대상 아님 — 인스턴스에서만 생성 (#29 동일).
	if (WidgetTree && !ResultText)
	{
		UOverlay* Root = WidgetTree->ConstructWidget<UOverlay>(UOverlay::StaticClass(), TEXT("Root"));

		ResultText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("ResultText"));
		ResultText->SetFont(FCoreStyle::GetDefaultFontStyle("Bold", 72));
		ResultText->SetJustification(ETextJustify::Center);

		// 화면 정중앙 고정 — 뷰포트 슬롯이 Overlay를 채우고, Overlay 슬롯이 텍스트를 가운데로.
		if (UOverlaySlot* Slot = Cast<UOverlaySlot>(Root->AddChild(ResultText)))
		{
			Slot->SetHorizontalAlignment(HAlign_Center);
			Slot->SetVerticalAlignment(VAlign_Center);
		}

		WidgetTree->RootWidget = Root;
	}
	return true;
}

void UREResultWidget::SetResult(bool bVictory)
{
	if (ResultText)
	{
		ResultText->SetText(FText::FromString(bVictory ? TEXT("VICTORY") : TEXT("DEFEAT")));
		ResultText->SetColorAndOpacity(FSlateColor(bVictory ? FLinearColor::Green : FLinearColor::Red));
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
git add Source/Project_RE/UI/REResultWidget.h Source/Project_RE/UI/REResultWidget.cpp
git commit -m "feat(M2): add pure C++ result screen widget, no BP asset (#40)"
```

---

### Task 2: 정지·표시 리프 API — `StopFiring()` + `Client_ShowResult()`

`EndGame`(Task 3)이 호출할 두 개의 말단 API를 먼저 만든다. 각각 자기 액터 안에서 자기 것만 멈춘다.

**Files:**
- Modify: `Source/Project_RE/Core/REAutoFireComponent.h` (public 섹션)
- Modify: `Source/Project_RE/Core/REAutoFireComponent.cpp`
- Modify: `Source/Project_RE/Core/REPlayerController.h` (protected 섹션)
- Modify: `Source/Project_RE/Core/REPlayerController.cpp`

**Interfaces:**
- Consumes: Task 1의 `UREResultWidget::SetResult(bool)`
- Produces:
  - `UREAutoFireComponent::StopFiring()` — public, 인자 없음, void
  - `AREPlayerController::Client_ShowResult(bool bVictory)` — `UFUNCTION(Client, Reliable)`, 구현 심볼은 `Client_ShowResult_Implementation`
  - 둘 다 Task 3의 `AREGameMode::EndGame`이 호출

- [ ] **Step 1: `REAutoFireComponent.h` — public `StopFiring()` 선언**

`UREAutoFireComponent()` 생성자 선언 **아래**, `protected:` **위**에 추가:

```cpp
	/**
	 *  발사 중지 (#40 게임 종료). 자동사격은 입력이 아니라 서버 타이머 구동이라
	 *  PlayerController의 DisableInput으로는 안 멈춘다 — 명시적 정지가 필요하다.
	 */
	void StopFiring();
```

- [ ] **Step 2: `REAutoFireComponent.cpp` — 구현 추가**

`EndPlay` 함수 **아래**, `Fire()` **위**에 추가:

```cpp
void UREAutoFireComponent::StopFiring()
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(FireTimer);
		UE_LOG(LogTemp, Log, TEXT("[RE] AutoFire stopped"));
	}
}
```

- [ ] **Step 3: `REPlayerController.h` — Client RPC 선언**

`Server_Dash` 선언 **아래** (같은 `protected:` 블록 안)에 추가:

```cpp
	/**
	 *  결과 화면 표시 + 입력 차단 (#40). 서버가 EndGame에서 호출, 오너 클라에서 실행.
	 *  싱글/리슨에서는 로컬 즉시 실행 — M4 데디 전환 시 수정 불필요.
	 */
	UFUNCTION(Client, Reliable)
	void Client_ShowResult(bool bVictory);
```

- [ ] **Step 4: `REPlayerController.cpp` — 구현 추가**

include 블록에 추가:

```cpp
#include "REResultWidget.h"
#include "Blueprint/UserWidget.h"
```

`Server_Dash_Implementation` 함수 **아래**에 추가:

```cpp
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
```

- [ ] **Step 5: 빌드 게이트**

```bash
"/e/UE_5.8/Engine/Build/BatchFiles/Build.bat" Project_REEditor Win64 Development -Project="E:\UnrealProjects\Project_RE\Project_RE.uproject" -WaitMutex -NoHotReload
```
기대: `Result: Succeeded`, 에러 0.

- [ ] **Step 6: 커밋**

```bash
git add Source/Project_RE/Core/REAutoFireComponent.h Source/Project_RE/Core/REAutoFireComponent.cpp Source/Project_RE/Core/REPlayerController.h Source/Project_RE/Core/REPlayerController.cpp
git commit -m "feat(M2): add AutoFire StopFiring + PC Client_ShowResult RPC (#40)"
```

---

### Task 3: `EndGame` 오케스트레이션 + 양쪽 사망 판정 배선

**Files:**
- Modify: `Source/Project_RE/Core/REGameMode.h` (public `EndGame` + private `bGameOver`)
- Modify: `Source/Project_RE/Core/REGameMode.cpp`
- Modify: `Source/Project_RE/Core/RECharacterBase.h` (`bIsDead`)
- Modify: `Source/Project_RE/Core/RECharacterBase.cpp:92-96` (`TakeDamage`)
- Modify: `Source/Project_RE/Core/REBossCharacter.cpp:82-86` (`TakeDamage`의 기존 `bIsDead` 블록)

**Interfaces:**
- Consumes: Task 2의 `UREAutoFireComponent::StopFiring()`, `AREPlayerController::Client_ShowResult(bool)`
- Produces: `AREGameMode::EndGame(bool bVictory)` — 양쪽 캐릭터의 `TakeDamage`가 호출

- [ ] **Step 1: `REGameMode.h` 수정**

`AREGameMode()` 생성자 선언 **아래** (`public:` 안)에 추가:

```cpp
	/**
	 *  승패 확정 (#40). 탄막 발사·자동사격 정지 + 결과 화면 표시.
	 *  GameModeBase는 서버에만 존재 → 이 함수 자체가 서버 권위. 중복 호출은 선착순 무시.
	 */
	void EndGame(bool bVictory);
```

`private:` 블록의 `DemoBoss` 멤버 **아래**에 추가:

```cpp
	/** 승패 확정 여부. 같은 프레임에 양쪽이 죽는 경우 선착순 처리. */
	bool bGameOver = false;
```

- [ ] **Step 2: `REGameMode.cpp` 수정 — include + `EndGame` 구현**

include 블록에 추가:

```cpp
#include "REAutoFireComponent.h"
```

(`RECharacterBase.h`, `REPlayerController.h`, `TimerManager.h`는 이미 있다.)

`Tick` 함수 **아래**(파일 끝)에 추가:

```cpp
void AREGameMode::EndGame(bool bVictory)
{
	if (bGameOver)
	{
		return;
	}
	bGameOver = true;

	UE_LOG(LogTemp, Log, TEXT("[RE] EndGame: %s"), bVictory ? TEXT("VICTORY") : TEXT("DEFEAT"));

	// 1) 탄막 발사 중지. 이미 뜬 탄환은 Lifetime 다할 때까지 계속 난다 (설계 합의 — 일괄 소멸 안 함).
	GetWorld()->GetTimerManager().ClearTimer(DemoFireTimer);

	APlayerController* PC = GetWorld()->GetFirstPlayerController();

	// 2) 자동사격 중지. 서버 타이머 구동이라 입력 차단으로는 안 멈춘다.
	//    AutoFireComponent는 캐릭터의 protected 멤버 — accessor 추가 대신 컴포넌트 조회.
	if (ARECharacterBase* Player = PC ? Cast<ARECharacterBase>(PC->GetPawn()) : nullptr)
	{
		if (UREAutoFireComponent* AutoFire = Player->FindComponentByClass<UREAutoFireComponent>())
		{
			AutoFire->StopFiring();
		}
	}

	// 3) 결과 화면 + 입력 차단 — 오너 클라 실행(싱글은 로컬 즉시).
	if (AREPlayerController* REPC = Cast<AREPlayerController>(PC))
	{
		REPC->Client_ShowResult(bVictory);
	}
}
```

- [ ] **Step 3: `RECharacterBase.h` 수정 — `bIsDead` 추가**

`protected:` 블록의 `MaxHealth` 멤버 **아래**에 추가:

```cpp
	/** 사망 여부. 서버 전용 — 클라 시각처리는 스코프 밖이라 비복제. (AREBossCharacter 동일 패턴) */
	bool bIsDead = false;
```

- [ ] **Step 4: `RECharacterBase.cpp` 수정 — 패배 판정**

include 블록에 추가:

```cpp
#include "REGameMode.h"
```

`TakeDamage` 본문에서 아래 두 줄을

```cpp
	OnRep_Health(); // 서버/싱글 경로 — 복제 OnRep은 원격 클라 전용이라 직접 호출
	// TODO M5: 서버권위 피격 판정/이펙트, 사망 처리
	return Applied;
```

이렇게 교체 (잘못된 `TODO M5` 주석 제거 — 플레이어 사망은 M5 기능이 아니라 M2 게임루프다):

```cpp
	OnRep_Health(); // 서버/싱글 경로 — 복제 OnRep은 원격 클라 전용이라 직접 호출

	// 패배 판정 — 이미 HasAuthority 가드 안. 사망 후에도 뜬 탄환이 계속 때리므로 bIsDead로 재진입 차단.
	if (Health <= 0.f && !bIsDead)
	{
		bIsDead = true;
		UE_LOG(LogTemp, Log, TEXT("[RE] Player died (Health<=0)"));
		if (AREGameMode* GM = GetWorld()->GetAuthGameMode<AREGameMode>())
		{
			GM->EndGame(/*bVictory=*/false);
		}
	}

	return Applied;
```

- [ ] **Step 5: `REBossCharacter.cpp` 수정 — 승리 판정**

include 블록에 추가:

```cpp
#include "REGameMode.h"
```

`TakeDamage`의 기존 `bIsDead` 블록을 교체:

```cpp
	if (Health <= 0.f && !bIsDead)
	{
		bIsDead = true;
		UE_LOG(LogTemp, Log, TEXT("[RE] Boss died (Health<=0)"));
		if (AREGameMode* GM = GetWorld()->GetAuthGameMode<AREGameMode>())
		{
			GM->EndGame(/*bVictory=*/true);
		}
	}
```

- [ ] **Step 6: 빌드 게이트**

```bash
"/e/UE_5.8/Engine/Build/BatchFiles/Build.bat" Project_REEditor Win64 Development -Project="E:\UnrealProjects\Project_RE\Project_RE.uproject" -WaitMutex -NoHotReload
```
기대: `Result: Succeeded`, 에러 0.

- [ ] **Step 7: 커밋**

```bash
git add Source/Project_RE/Core/REGameMode.h Source/Project_RE/Core/REGameMode.cpp Source/Project_RE/Core/RECharacterBase.h Source/Project_RE/Core/RECharacterBase.cpp Source/Project_RE/Core/REBossCharacter.cpp
git commit -m "feat(M2): server-authoritative win/lose decision via GameMode EndGame (#40)"
```

---

### Task 4: headless 로그 프로브 — 승리/패배 양쪽 경로

신규 커밋 코드 없음. `-game -nullrhi`로 두 번 돌려 판정·정지 로그를 관측한다. 위젯은 `-nullrhi`에서 안 그려지므로 여기선 **판정과 정지만** 본다 (렌더는 Task 5).

**Files:**
- Modify (임시, revert): `Source/Project_RE/Core/REBossCharacter.h` — 패배 경로 프로브용 `MaxHealth` 상향

- [ ] **Step 1: 승리 경로 실행 (코드 수정 없음)**

자동사격 10dmg / 0.25s → 보스 100HP → ~2.5초 사망. 30초면 충분.

```bash
MSYS_NO_PATHCONV=1 "/e/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" \
  "E:\UnrealProjects\Project_RE\Project_RE.uproject" /Game/Level/Main \
  -game -nullrhi -unattended -nosplash -stdout -NoSound -log=RE_probe40_win.log
```
30초쯤 뒤 수동 종료(Ctrl+C) 또는 프로세스 kill.

- [ ] **Step 2: 승리 로그 검증**

`Saved/Logs/RE_probe40_win.log`에서 확인:

```bash
grep -n "Boss died\|EndGame\|AutoFire stopped\|Client_ShowResult" Saved/Logs/RE_probe40_win.log
```
기대 (이 순서로 각 1회):
```
[RE] Boss died (Health<=0)
[RE] EndGame: VICTORY
[RE] AutoFire stopped
[RE] Client_ShowResult: VICTORY
```

탄막 정지도 확인 — `EndGame` 로그 **이후**에 `[RE] Boss Spiral` 줄이 더 나오면 안 된다:

```bash
grep -n "Boss Spiral\|EndGame" Saved/Logs/RE_probe40_win.log | tail -20
```
기대: 마지막 `Boss Spiral` 줄번호 < `EndGame` 줄번호.

- [ ] **Step 3: 패배 경로 — 보스 HP 임시 상향**

⚠️ 현 밸런스로는 자동사격이 보스를 먼저 녹여 **DEFEAT가 자연 발생하지 않는다.** 프로브 전용으로 보스만 불사에 가깝게 만든다.

`Source/Project_RE/Core/REBossCharacter.h`의 `MaxHealth` 기본값을 임시 변경:

```cpp
	/** 최대 체력. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Stats")
	float MaxHealth = 1000000.f;   // TEMP #40 probe — 커밋 금지 (원복: 100.f)
```

빌드:
```bash
"/e/UE_5.8/Engine/Build/BatchFiles/Build.bat" Project_REEditor Win64 Development -Project="E:\UnrealProjects\Project_RE\Project_RE.uproject" -WaitMutex -NoHotReload
```
기대: `Result: Succeeded`.

- [ ] **Step 4: 패배 경로 실행**

```bash
MSYS_NO_PATHCONV=1 "/e/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" \
  "E:\UnrealProjects\Project_RE\Project_RE.uproject" /Game/Level/Main \
  -game -nullrhi -unattended -nosplash -stdout -NoSound -log=RE_probe40_lose.log
```
보스가 안 죽으므로 플레이어가 탄막에 맞아 죽는다. 30초쯤 뒤 종료.

- [ ] **Step 5: 패배 로그 검증**

```bash
grep -n "Player died\|EndGame\|AutoFire stopped\|Client_ShowResult\|Boss died" Saved/Logs/RE_probe40_lose.log
```
기대:
```
[RE] Player died (Health<=0)
[RE] EndGame: DEFEAT
[RE] AutoFire stopped
[RE] Client_ShowResult: DEFEAT
```
`Boss died` 줄은 없어야 한다(HP 100만). `EndGame` 줄은 **정확히 1회** — 사망 후 뜬 탄환이 계속 때려도 `bIsDead` 가드가 재진입을 막는지 보는 게 이 프로브의 핵심이다.

```bash
grep -c "EndGame" Saved/Logs/RE_probe40_lose.log
```
기대: `1`

실패 시: superpowers:systematic-debugging으로 원인 규명 후 수정.

- [ ] **Step 6: 임시 코드 revert + 재빌드**

```bash
git checkout -- Source/Project_RE/Core/REBossCharacter.h
git status --short   # REBossCharacter.h 변경 없음 확인
"/e/UE_5.8/Engine/Build/BatchFiles/Build.bat" Project_REEditor Win64 Development -Project="E:\UnrealProjects\Project_RE\Project_RE.uproject" -WaitMutex -NoHotReload
```
기대: `git status` 깨끗, `Result: Succeeded`.

---

### Task 5: 실RHI 스크린샷 프로브 — VICTORY / DEFEAT 화면

신규 커밋 코드 없음. 위젯 렌더는 로그로 검증 불가(#29에서 배움) → 실RHI 창모드 PNG로 육안 대체.

**Files:**
- Modify (임시, revert): `Source/Project_RE/Core/REGameMode.cpp` — 스크린샷/종료 타이머
- Modify (임시, revert): `Source/Project_RE/Core/REBossCharacter.h` — DEFEAT 샷 촬영 시 `MaxHealth` 상향

- [ ] **Step 1: 임시 프로브 코드 삽입**

`REGameMode.cpp` include 블록에 추가:

```cpp
// TEMP #40 probe — 커밋 금지
#include "UnrealClient.h"
#include "Misc/Paths.h"
#include "Engine/Engine.h"
```

`BeginPlay()` 끝에 추가:

```cpp
	// TEMP #40 probe — 커밋 금지: 8s 스크린샷, 11s 종료
	FTimerHandle ShotT, QuitT;
	GetWorld()->GetTimerManager().SetTimer(ShotT, FTimerDelegate::CreateLambda([]()
	{
		FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir() / TEXT("Result.png"), false, false);
	}), 8.f, false);
	GetWorld()->GetTimerManager().SetTimer(QuitT, FTimerDelegate::CreateLambda([this]()
	{
		GEngine->Exec(GetWorld(), TEXT("quit"));
	}), 11.f, false);
```

- [ ] **Step 2: 빌드 + VICTORY 샷 실행**

```bash
"/e/UE_5.8/Engine/Build/BatchFiles/Build.bat" Project_REEditor Win64 Development -Project="E:\UnrealProjects\Project_RE\Project_RE.uproject" -WaitMutex -NoHotReload
MSYS_NO_PATHCONV=1 "/e/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" \
  "E:\UnrealProjects\Project_RE\Project_RE.uproject" /Game/Level/Main \
  -game -windowed -ResX=1280 -ResY=720 -nosplash -stdout -NoSound -log=RE_shot40_win.log
```
(셰이더 컴파일로 첫 실행 오래 걸림 — timeout 300s. 11초 quit 타이머로 자체 종료.)

보스는 ~2.5초에 죽으므로 8초 샷에는 VICTORY가 떠 있다.

- [ ] **Step 3: VICTORY PNG 검증 + 보관**

`Saved/Result.png`를 Read 툴로 확인: 화면 **정중앙 초록 "VICTORY"** 텍스트.

```bash
mv Saved/Result.png Saved/Result_victory.png
```

실패 시(텍스트 없음/구석에 붙음/색 틀림): superpowers:systematic-debugging. 정렬·폰트 튜닝은 Task 1 파일에서 수정 → 이 태스크 안에서 반복.

- [ ] **Step 4: DEFEAT 샷 — 보스 HP 임시 상향 후 실행**

`Source/Project_RE/Core/REBossCharacter.h`의 `MaxHealth`를 다시 임시 변경:

```cpp
	float MaxHealth = 1000000.f;   // TEMP #40 probe — 커밋 금지 (원복: 100.f)
```

```bash
"/e/UE_5.8/Engine/Build/BatchFiles/Build.bat" Project_REEditor Win64 Development -Project="E:\UnrealProjects\Project_RE\Project_RE.uproject" -WaitMutex -NoHotReload
MSYS_NO_PATHCONV=1 "/e/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" \
  "E:\UnrealProjects\Project_RE\Project_RE.uproject" /Game/Level/Main \
  -game -windowed -ResX=1280 -ResY=720 -nosplash -stdout -NoSound -log=RE_shot40_lose.log
```

플레이어가 8초 안에 안 죽어 샷에 DEFEAT가 안 잡히면, Task 4의 `RE_probe40_lose.log`에서 `Player died` 시각을 확인해 스크린샷 타이머(8s)와 quit 타이머(11s)를 그 뒤로 올린다.

- [ ] **Step 5: DEFEAT PNG 검증**

`Saved/Result.png`를 Read 툴로 확인: 화면 **정중앙 빨강 "DEFEAT"** 텍스트.

```bash
mv Saved/Result.png Saved/Result_defeat.png
```

- [ ] **Step 6: 임시 코드 전부 revert + 재빌드**

```bash
git checkout -- Source/Project_RE/Core/REGameMode.cpp Source/Project_RE/Core/REBossCharacter.h
git status --short   # 두 파일 변경 없음 확인
"/e/UE_5.8/Engine/Build/BatchFiles/Build.bat" Project_REEditor Win64 Development -Project="E:\UnrealProjects\Project_RE\Project_RE.uproject" -WaitMutex -NoHotReload
```
기대: `git status` 깨끗(Saved/ PNG 제외), `Result: Succeeded`.

---

### Task 6: 푸시 + PR

- [ ] **Step 1: 푸시**

```bash
git push -u origin feature/M2-win-lose-state
```

- [ ] **Step 2: PR 생성**

컨벤션: base=dev, 이슈 #40 메타 미러링 — label `C++`,`UI`, milestone `M2: 플레이어 게임루프`, assignee `leejimin3`, project 연결. Reviewer 생략. 본문 6개 필드 전부 (요약 / 변경사항 / 이슈링크 `Closes #40` / 검증 / 스코프 제외 / 참고).

```bash
gh pr create --base dev \
  --title "feat(M2): 승패 상태 + 결과 화면 (#40)" \
  --label "C++" --label "UI" \
  --milestone "M2: 플레이어 게임루프" \
  --assignee leejimin3 \
  --body "..."
```

검증 필드에 Task 4 로그 발췌 + Task 5 PNG 2장(`Result_victory.png` / `Result_defeat.png`) 첨부.

- [ ] **Step 3: PIE 육안 최종 확인 요청**

사용자에게 PIE 1회 확인 요청:
- 보스 죽을 때까지 대기 → VICTORY 표시 + 탄막/입력 정지
- (선택) 보스 HP 올려 패배 유도 → DEFEAT 표시

이후 `/검증` 스킬로 머지 진행.

---

## 후속 (이 계획 밖)

좌클릭 공격 (로스트아크식 — 이동 중단 후 커서 방향 발사, `UREAutoFireComponent` 대체) → #40 머지 후 별도 이슈로 생성.
