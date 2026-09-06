# 구현 목표: M2 #40 — 승패 상태 (플레이어 사망=패배 / 보스 사망=승리) + 결과 화면

## 컨텍스트

UE 5.8 C++ 탑뷰 탄막 회피 게임(Project_RE). Mass Entity 탄막 + GAS 대쉬 + 자동사격이 이미 동작한다.

이슈 **#40** / 마일스톤 **M2: 플레이어 게임루프**.

이 goal이 하는 것: **게임루프를 닫는다.** 지금은 HP가 0이 돼도 아무 일도 안 일어난다 — HP바만 비고 계속 플레이된다. 플레이어 HP 0 → "DEFEAT", 보스 HP 0 → "VICTORY", 그리고 탄막·자동사격·입력이 전부 멈춘다.

**스코프 밖 (손대지 말 것):** 재시작/리스폰, `AREGameState` 상태기계, 사망 연출, 뜬 탄막 일괄 소멸, 좌클릭 공격(#26 재작업 — 별도 후속 이슈).

설계 스펙: `docs/superpowers/specs/2026-07-14-win-lose-state-design.md`
상세 플랜: `docs/superpowers/plans/2026-07-14-win-lose-state.md`
(참고 가능. 단 **아래 코드가 최종 정본.**)

## 브랜치

`dev`에서 분기: `feature/M2-win-lose-state`

```bash
git fetch origin
git checkout -b feature/M2-win-lose-state origin/dev
```

(현재 브랜치가 `feature/M2-dash-iframe`일 수 있다 — 반드시 `origin/dev`에서 새로 분기.)

## 전역 제약

- 엔진 빌드: `"/e/UE_5.8/Engine/Build/BatchFiles/Build.bat" Project_REEditor Win64 Development -Project="E:\UnrealProjects\Project_RE\Project_RE.uproject" -WaitMutex -NoHotReload` → 기대 `Result: Succeeded`
- **`Build.cs` / `.uproject` 변경 금지** — `UMG`·`Slate` 모듈과 `Project_RE/UI` include 경로는 #29에서 이미 들어갔다.
- 자동화 테스트 인프라 없음 → 게이트 = **빌드 성공** + **headless 로그 프로브** + **실RHI 스크린샷** + **PIE 육안**
- 로그 접두어 `[RE]` 고정.
- Git Bash에서 UE 실행 시 `MSYS_NO_PATHCONV=1` 필수.
- `-nullrhi`로는 위젯 렌더 검증 불가 — 스크린샷은 실RHI `-windowed`로만.
- PR base=dev.
- YAGNI: 스펙에 없는 기능·추상화·설정값 추가 금지.

## 검증된 API (실물 확인됨)

- `UWidgetTree::ConstructWidget<T>(UClass*, FName)` — `Blueprint/WidgetTree.h`. #29 `UREHealthBarWidget`이 이미 이 패턴 사용.
- `UUserWidget::Initialize()` override — 슬레이트 트리 구축 **전**. `NativeConstruct`는 늦다(루트 세팅 불가).
- `UUserWidget::AddToViewport()` — 화면공간. (HP바의 월드스페이스 `UWidgetComponent`와 대비.)
- `UOverlay::AddChild(UWidget*)` → `UPanelSlot*`, `Cast<UOverlaySlot>` → `SetHorizontalAlignment(HAlign_Center)` / `SetVerticalAlignment(VAlign_Center)`. 헤더: `Components/Overlay.h`, `Components/OverlaySlot.h`.
- `UTextBlock::SetFont(FSlateFontInfo)` / `SetJustification(ETextJustify::Center)` / `SetText(FText)` / `SetColorAndOpacity(FSlateColor)`. 헤더: `Components/TextBlock.h`.
- `FCoreStyle::GetDefaultFontStyle("Bold", 72)` → `FSlateFontInfo`. 헤더: `Styling/CoreStyle.h`.
- `UWorld::GetAuthGameMode<T>()` — 서버에서만 non-null.
- `AActor::FindComponentByClass<T>()` — 캐릭터의 protected `AutoFireComponent`에 accessor 안 뚫고 접근.
- `APlayerController::DisableInput(APlayerController*)`.
- `FTimerManager::ClearTimer(FTimerHandle&)` — `TimerManager.h`.
- `UFUNCTION(Client, Reliable)` → 구현 심볼은 `<Name>_Implementation`.

## 기존 파일 현황 (변경 대상)

**`Core/RECharacterBase.cpp:92-96`** — `TakeDamage`가 `HasAuthority()` 가드 안에서 Health 차감 + `OnRep_Health()` 호출. 그 뒤에 `// TODO M5: 서버권위 피격 판정/이펙트, 사망 처리` 주석만 있고 **사망 처리 없음**. 이 라벨은 틀렸다(플레이어 사망은 M5 협동 멀티 기능이 아니라 M2 게임루프) → 제거 대상. `bIsDead` 멤버 **없음**.

**`Core/REBossCharacter.cpp:82-86`** — `TakeDamage`에 이미 `bIsDead` 블록 존재:
```cpp
if (Health <= 0.f && !bIsDead)
{
    bIsDead = true;
    UE_LOG(LogTemp, Log, TEXT("[RE] Boss died (Health<=0)"));
}
```
`bIsDead`는 private 멤버(비복제), `TriggerBulletPattern` 진입 가드(`REBossCharacter.cpp:24`)가 읽는다. `MaxHealth = 100.f` (`REBossCharacter.h`, `EditDefaultsOnly`).

**`Core/REGameMode.cpp:66`** — `DemoFireTimer`(private `FTimerHandle`)가 0.1초 루프로 `DemoBoss->TriggerBulletPattern(Spiral)` 발사. `EndGame` 없음.

**`Core/REAutoFireComponent`** — `FireTimer`(private)가 서버에서 `FireInterval=0.25s`, `Damage=10.f`로 최근접 보스 라인트레이스 발사. 정지 API **없음** (`EndPlay`에서만 Clear).

**`Core/REPlayerController`** — `Server_RequestMove`/`Server_Dash` Server RPC 보유. Client RPC 없음.

**`UI/`** — `REHealthBarWidget.{h,cpp}`(순수 C++ ProgressBar), `REHealthBarComponent.{h,cpp}` 존재. 결과 위젯 없음.

================================================================
## TASK 1: `UREResultWidget` — 순수 C++ 결과 텍스트 위젯
================================================================

### 1-1. `Source/Project_RE/UI/REResultWidget.h` (신규)

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

### 1-2. `Source/Project_RE/UI/REResultWidget.cpp` (신규)

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

### 1-3. 빌드 게이트

```bash
"/e/UE_5.8/Engine/Build/BatchFiles/Build.bat" Project_REEditor Win64 Development -Project="E:\UnrealProjects\Project_RE\Project_RE.uproject" -WaitMutex -NoHotReload
```
기대: `Result: Succeeded`, 에러 0.

### 1-4. 커밋

```bash
git add Source/Project_RE/UI/REResultWidget.h Source/Project_RE/UI/REResultWidget.cpp
git commit -m "feat(M2): add pure C++ result screen widget, no BP asset (#40)"
```

================================================================
## TASK 2: 정지·표시 리프 API — `StopFiring()` + `Client_ShowResult()`
================================================================

TASK 3의 `EndGame`이 호출할 말단 API 2개. 각자 자기 액터 안에서 자기 것만 멈춘다.

### 2-1. `Source/Project_RE/Core/REAutoFireComponent.h` (수정)

`UREAutoFireComponent();` 생성자 선언 **아래**, `protected:` **위**에 추가:

```cpp
	/**
	 *  발사 중지 (#40 게임 종료). 자동사격은 입력이 아니라 서버 타이머 구동이라
	 *  PlayerController의 DisableInput으로는 안 멈춘다 — 명시적 정지가 필요하다.
	 */
	void StopFiring();
```

### 2-2. `Source/Project_RE/Core/REAutoFireComponent.cpp` (수정)

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

### 2-3. `Source/Project_RE/Core/REPlayerController.h` (수정)

`Server_Dash` 선언 **아래** (같은 `protected:` 블록 안)에 추가:

```cpp
	/**
	 *  결과 화면 표시 + 입력 차단 (#40). 서버가 EndGame에서 호출, 오너 클라에서 실행.
	 *  싱글/리슨에서는 로컬 즉시 실행 — M4 데디 전환 시 수정 불필요.
	 */
	UFUNCTION(Client, Reliable)
	void Client_ShowResult(bool bVictory);
```

### 2-4. `Source/Project_RE/Core/REPlayerController.cpp` (수정)

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

### 2-5. 빌드 게이트

```bash
"/e/UE_5.8/Engine/Build/BatchFiles/Build.bat" Project_REEditor Win64 Development -Project="E:\UnrealProjects\Project_RE\Project_RE.uproject" -WaitMutex -NoHotReload
```
기대: `Result: Succeeded`, 에러 0.

### 2-6. 커밋

```bash
git add Source/Project_RE/Core/REAutoFireComponent.h Source/Project_RE/Core/REAutoFireComponent.cpp Source/Project_RE/Core/REPlayerController.h Source/Project_RE/Core/REPlayerController.cpp
git commit -m "feat(M2): add AutoFire StopFiring + PC Client_ShowResult RPC (#40)"
```

================================================================
## TASK 3: `EndGame` 오케스트레이션 + 양쪽 사망 판정 배선
================================================================

멈출 것 3개(탄막 타이머 / 자동사격 타이머 / 입력)가 서로 다른 액터에 있다 → 이걸 전부 아는 `AREGameMode`가 오케스트레이션. `AGameModeBase`는 서버에만 존재하므로 **새 권위 경로 없음**. 판정 호출부는 양쪽 `TakeDamage`의 **기존** `HasAuthority()` 가드 안.

### 3-1. `Source/Project_RE/Core/REGameMode.h` (수정)

`AREGameMode();` 생성자 선언 **아래** (`public:` 안)에 추가:

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

### 3-2. `Source/Project_RE/Core/REGameMode.cpp` (수정)

include 블록에 추가 (`RECharacterBase.h`, `REPlayerController.h`, `TimerManager.h`는 이미 있다):

```cpp
#include "REAutoFireComponent.h"
```

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

### 3-3. `Source/Project_RE/Core/RECharacterBase.h` (수정)

`protected:` 블록의 `MaxHealth` 멤버 **아래**에 추가:

```cpp
	/** 사망 여부. 서버 전용 — 클라 시각처리는 스코프 밖이라 비복제. (AREBossCharacter 동일 패턴) */
	bool bIsDead = false;
```

### 3-4. `Source/Project_RE/Core/RECharacterBase.cpp` (수정 — 패배 판정)

include 블록에 추가:

```cpp
#include "REGameMode.h"
```

`TakeDamage` 함수를 아래 **전문**으로 교체 (잘못된 `TODO M5` 주석 제거 — 플레이어 사망은 M5 기능이 아니라 M2 게임루프):

```cpp
float ARECharacterBase::TakeDamage(float DamageAmount, const FDamageEvent& DamageEvent,
                                   AController* EventInstigator, AActor* DamageCauser)
{
	// 서버 권위 가드 — 게임상태(Health) 변경은 서버에서만
	if (!HasAuthority())
	{
		return 0.f;
	}

	const float Applied = Super::TakeDamage(DamageAmount, DamageEvent, EventInstigator, DamageCauser);
	Health = FMath::Clamp(Health - Applied, 0.f, MaxHealth);
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
}
```

### 3-5. `Source/Project_RE/Core/REBossCharacter.cpp` (수정 — 승리 판정)

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

### 3-6. 빌드 게이트

```bash
"/e/UE_5.8/Engine/Build/BatchFiles/Build.bat" Project_REEditor Win64 Development -Project="E:\UnrealProjects\Project_RE\Project_RE.uproject" -WaitMutex -NoHotReload
```
기대: `Result: Succeeded`, 에러 0.

### 3-7. 커밋

```bash
git add Source/Project_RE/Core/REGameMode.h Source/Project_RE/Core/REGameMode.cpp Source/Project_RE/Core/RECharacterBase.h Source/Project_RE/Core/RECharacterBase.cpp Source/Project_RE/Core/REBossCharacter.cpp
git commit -m "feat(M2): server-authoritative win/lose decision via GameMode EndGame (#40)"
```

================================================================
## TASK 4: headless 로그 프로브 — 승리/패배 양쪽 경로
================================================================

**커밋할 코드 없음.** `-game -nullrhi`로 두 번 돌려 판정·정지 로그를 관측한다. 위젯은 `-nullrhi`에서 안 그려지므로 여기선 판정과 정지만 본다(렌더는 TASK 5).

### 4-1. 승리 경로 실행 (코드 수정 없음)

자동사격 10dmg / 0.25s → 보스 100HP → ~2.5초 사망.

```bash
MSYS_NO_PATHCONV=1 "/e/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" \
  "E:\UnrealProjects\Project_RE\Project_RE.uproject" /Game/Level/Main \
  -game -nullrhi -unattended -nosplash -stdout -NoSound -log=RE_probe40_win.log
```
30초쯤 뒤 종료(Ctrl+C / kill).

### 4-2. 승리 로그 게이트

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

탄막 정지 확인 — `EndGame` **이후** `[RE] Boss Spiral` 줄이 더 나오면 안 된다:
```bash
grep -n "Boss Spiral\|EndGame" Saved/Logs/RE_probe40_win.log | tail -20
```
기대: 마지막 `Boss Spiral` 줄번호 < `EndGame` 줄번호.

### 4-3. 패배 경로 — 보스 HP 임시 상향 (⚠️ 커밋 금지)

현 밸런스로는 자동사격이 보스를 먼저 녹여 **DEFEAT가 자연 발생하지 않는다.** 프로브 전용으로 보스만 불사에 가깝게 만든다.

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

### 4-4. 패배 경로 실행

```bash
MSYS_NO_PATHCONV=1 "/e/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" \
  "E:\UnrealProjects\Project_RE\Project_RE.uproject" /Game/Level/Main \
  -game -nullrhi -unattended -nosplash -stdout -NoSound -log=RE_probe40_lose.log
```
보스가 안 죽으므로 플레이어가 탄막에 맞아 죽는다. 30초쯤 뒤 종료.

### 4-5. 패배 로그 게이트

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
`Boss died` 줄은 **없어야** 한다(HP 100만).

재진입 차단 확인 — 사망 후에도 뜬 탄환이 계속 때리지만 `bIsDead` 가드가 막아야 한다:
```bash
grep -c "EndGame" Saved/Logs/RE_probe40_lose.log
```
기대: `1`

실패 시: superpowers:systematic-debugging으로 원인 규명 후 수정.

### 4-6. 임시 코드 revert + 재빌드

```bash
git checkout -- Source/Project_RE/Core/REBossCharacter.h
git status --short
"/e/UE_5.8/Engine/Build/BatchFiles/Build.bat" Project_REEditor Win64 Development -Project="E:\UnrealProjects\Project_RE\Project_RE.uproject" -WaitMutex -NoHotReload
```
기대: `REBossCharacter.h` 변경 없음, `Result: Succeeded`.

================================================================
## TASK 5: 실RHI 스크린샷 프로브 — VICTORY / DEFEAT 화면
================================================================

**커밋할 코드 없음.** 위젯 렌더는 로그로 검증 불가(#29에서 배움) → 실RHI 창모드 PNG로 확인.

### 5-1. 임시 프로브 코드 삽입 (⚠️ 커밋 금지)

`Source/Project_RE/Core/REGameMode.cpp` include 블록에 추가:

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

### 5-2. 빌드 + VICTORY 샷 실행

```bash
"/e/UE_5.8/Engine/Build/BatchFiles/Build.bat" Project_REEditor Win64 Development -Project="E:\UnrealProjects\Project_RE\Project_RE.uproject" -WaitMutex -NoHotReload
MSYS_NO_PATHCONV=1 "/e/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" \
  "E:\UnrealProjects\Project_RE\Project_RE.uproject" /Game/Level/Main \
  -game -windowed -ResX=1280 -ResY=720 -nosplash -stdout -NoSound -log=RE_shot40_win.log
```
셰이더 컴파일로 첫 실행 오래 걸림 — timeout 300s. 11초 quit 타이머로 자체 종료. 보스는 ~2.5초에 죽으므로 8초 샷에 VICTORY가 떠 있다.

### 5-3. VICTORY PNG 게이트

`Saved/Result.png`를 Read 툴로 확인: 화면 **정중앙 초록 "VICTORY"** 텍스트.

```bash
mv Saved/Result.png Saved/Result_victory.png
```

실패 시(텍스트 없음 / 구석에 붙음 / 색 틀림): superpowers:systematic-debugging. 정렬·폰트 튜닝은 TASK 1 파일에서 수정 → 이 태스크 안에서 반복.

### 5-4. DEFEAT 샷 — 보스 HP 임시 상향 후 실행 (⚠️ 커밋 금지)

`Source/Project_RE/Core/REBossCharacter.h`:

```cpp
	float MaxHealth = 1000000.f;   // TEMP #40 probe — 커밋 금지 (원복: 100.f)
```

```bash
"/e/UE_5.8/Engine/Build/BatchFiles/Build.bat" Project_REEditor Win64 Development -Project="E:\UnrealProjects\Project_RE\Project_RE.uproject" -WaitMutex -NoHotReload
MSYS_NO_PATHCONV=1 "/e/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" \
  "E:\UnrealProjects\Project_RE\Project_RE.uproject" /Game/Level/Main \
  -game -windowed -ResX=1280 -ResY=720 -nosplash -stdout -NoSound -log=RE_shot40_lose.log
```

플레이어가 8초 안에 안 죽어 샷에 DEFEAT가 안 잡히면, TASK 4의 `RE_probe40_lose.log`에서 `Player died` 시각을 확인해 스크린샷(8s)/quit(11s) 타이머를 그 뒤로 올린다.

### 5-5. DEFEAT PNG 게이트

`Saved/Result.png`를 Read 툴로 확인: 화면 **정중앙 빨강 "DEFEAT"** 텍스트.

```bash
mv Saved/Result.png Saved/Result_defeat.png
```

### 5-6. 임시 코드 전부 revert + 재빌드

```bash
git checkout -- Source/Project_RE/Core/REGameMode.cpp Source/Project_RE/Core/REBossCharacter.h
git status --short
"/e/UE_5.8/Engine/Build/BatchFiles/Build.bat" Project_REEditor Win64 Development -Project="E:\UnrealProjects\Project_RE\Project_RE.uproject" -WaitMutex -NoHotReload
```
기대: 두 파일 변경 없음(`Saved/` PNG 제외), `Result: Succeeded`.

## 완료 후

### 푸시 + PR

```bash
git push -u origin feature/M2-win-lose-state
gh pr create --base dev \
  --title "feat(M2): 승패 상태 + 결과 화면 (#40)" \
  --label "C++" --label "UI" \
  --milestone "M2: 플레이어 게임루프" \
  --assignee leejimin3 \
  --body "..."
```

PR 규칙: **base=dev**. 이슈 #40 메타 미러링 — label `C++`,`UI` / milestone `M2: 플레이어 게임루프` / assignee `leejimin3` / project 연결. **Reviewer 생략.** 본문 6개 필드 전부:
1. 요약
2. 변경사항
3. 이슈링크 (`Closes #40`)
4. 검증 — TASK 4 로그 발췌 + TASK 5 PNG 2장(`Result_victory.png` / `Result_defeat.png`)
5. 스코프 제외
6. 참고

### PIE 육안 최종

사용자에게 PIE 1회 확인 요청 — 보스 죽을 때까지 대기 → VICTORY 표시 + 탄막/입력 정지 확인.

### 남는 의도된 TODO (후속 이슈 몫 — 건드리지 말 것)

- `REBossCharacter.cpp` `TriggerBulletPattern`의 `// TODO M5: Multicast_TriggerPattern RPC로 교체` — M5 데디 몫.

## 하지 말 것 (스코프 밖)

- **재시작 / 리스폰** — 필요해지면 별도 이슈.
- **`AREGameState` 상태기계** (Waiting/Playing/Won/Lost) — 상태 4개뿐이라 지금은 과함. M4 데디 전환 시 재검토.
- **사망 연출** (페이드/이펙트/보스 액터 `Destroy()`) — M6 폴리싱. 보스는 죽어도 그 자리에 서 있는 게 현재 의도된 동작.
- **뜬 탄막 일괄 소멸** — 새 발사만 중지. 이미 스폰된 Mass 엔티티는 `Lifetime` 다할 때까지 계속 난다 (설계 합의).
- **좌클릭 공격** (로스트아크식 이동중단+커서방향 발사, `UREAutoFireComponent` 대체) — #26 재작업, #40 머지 후 별도 이슈.
- **`Build.cs` / `.uproject` 수정** — 필요 없다. 필요하다고 느끼면 잘못 가고 있는 것.
- **밸런스 조정** — 보스 `MaxHealth` 상향은 프로브 전용 임시 변경. 반드시 revert.
