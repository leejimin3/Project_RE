# 보스 탄막 패턴 로테이션 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 보스가 Spiral 단일 반복 대신 랜덤 패턴(Spiral/Fan) 페이즈 로테이션으로 발사하게 한다.

**Architecture:** 발사 주체를 GameMode 타이머에서 `AREBossCharacter` 내부 페이즈 상태머신으로 이관한다. `FRandomStream(Seed)`가 페이즈 패턴을 결정(시드 결정성 = M5 시드 동기화 선행). Fan은 플레이어를 조준하고, Spiral 회전 스텝을 비정합 값으로 바꿔 직선 방사를 나선으로 개선한다.

**Tech Stack:** UE5.8 C++, `FTimerManager`, `FRandomStream`, 기존 `REBulletPatternGenerator` 순수 함수.

## Global Constraints

- **테스트 하네스 없음** — 이 프로젝트는 in-engine 유닛 테스트를 안 쓴다. 검증은 ① `Build.bat Project_REEditor` 게이트, ② headless `-game -nullrhi` 로그 프로브(`[[headless-runtime-probe]]`), ③ 실RHI 창모드 스크린샷(`[[ue_visual_verify_screenshot]]`)이다.
- **headless 프로브 실행 (Git Bash):** `export MSYS_NO_PATHCONV=1 MSYS2_ARG_CONV_EXCL="*"` 필수. 안 하면 `/Game/...` 경로 깨져 엉뚱한 맵 로드.
- **Mass 프로세서 게임스레드** — 이 작업은 프로세서 안 건드림. 스폰 경로(`SpawnBulletBatch`)만 사용.
- **측정 하네스 보존** — `scripts/profile.ps1`은 `re.Profiling.KeepFiring 1` + `re.Bullets.Count N`으로 Spiral 클로즈드루프 연속 발사를 전제한다. 회귀 금지.
- **Gitflow** — 브랜치 `feature/M5-pattern-rotation` (이미 분기됨). dev로 PR.
- **빌드 명령:** `"/e/UE_5.8/Engine/Build/BatchFiles/Build.bat" Project_REEditor Win64 Development -Project="E:\UnrealProjects\Project_RE\Project_RE.uproject" -WaitMutex`

## 파일 구조

| 파일 | 책임 | 변경 |
|------|------|------|
| `Source/Project_RE/Core/REBossCharacter.h` | 발사 진입점 + 페이즈 상태 선언 | 수정 |
| `Source/Project_RE/Core/REBossCharacter.cpp` | 페이즈 상태머신, Fan 조준, KeepFiring 우회 | 수정 |
| `Source/Project_RE/Core/REGameMode.cpp` | 보스 스폰 후 발사 위임(StartFiring/StopFiring) | 수정 |
| `Source/Project_RE/Core/REGameMode.h` | `DemoFireTimer` 멤버 제거 | 수정 |
| `docs/superpowers/specs/2026-07-18-boss-pattern-rotation-design.md` | 확정 회전스텝값 기록 | 수정(Task 3) |

---

### Task 1: 보스 페이즈 스케줄러 + GameMode 발사 위임

발사 주체를 GameMode→Boss로 옮기고 랜덤 페이즈 로테이션 상태머신을 구현한다. KeepFiring 측정 모드는 로테이션을 우회한다. 이 태스크 하나로 "보스가 Spiral↔Fan 페이즈를 대기 끼워 번갈아 발사"가 동작한다.

**Files:**
- Modify: `Source/Project_RE/Core/REBossCharacter.h`
- Modify: `Source/Project_RE/Core/REBossCharacter.cpp`
- Modify: `Source/Project_RE/Core/REGameMode.h`
- Modify: `Source/Project_RE/Core/REGameMode.cpp`

**Interfaces:**
- Consumes: `REBulletPattern::FireIntervalSec()` (기존), `EBulletPattern::{Spiral,Fan}`, `TriggerBulletPattern(EBulletPattern, int32, float)` (기존).
- Produces:
  - `void AREBossCharacter::StartFiring(int32 Seed)` — 페이즈 로테이션 발사 시작.
  - `void AREBossCharacter::StopFiring()` — 발사 정지(뜬 탄은 수명까지 유지).

- [ ] **Step 1: 헤더 — 발사 API + 페이즈 상태 선언**

`REBossCharacter.h`의 `public:` 블록, `TriggerBulletPattern` 선언 아래에 추가:

```cpp
	/** 페이즈 로테이션 발사 시작. Seed로 패턴 순서 결정(M5 시드 동기화 선행). */
	void StartFiring(int32 Seed);
	/** 발사 정지. 이미 뜬 탄은 수명까지 유지(일괄 소멸 안 함). */
	void StopFiring();
```

`private:` 블록, `bool bIsDead = false;` 아래에 추가:

```cpp
	//~ 패턴 로테이션 페이즈 스케줄러 (#64). 발사 주체 = Boss (M5 RPC 확장 대비).
	void BeginPhase();          // 다음 패턴 선택 + 발사 타이머 세팅 + 페이즈 종료 예약
	void FireCurrentPattern();  // 현재 페이즈 패턴 1회 발사 (FireTimer 콜백)
	void EndPhase();            // 발사 정지 + RestSec 뒤 BeginPhase 예약

	FRandomStream PhaseRng;
	EBulletPattern CurrentPhasePattern = EBulletPattern::Spiral;
	bool bFirstPhase = true;    // 첫 페이즈 Spiral 고정 (오프닝 + profiling 오염 창 차단)
	FTimerHandle FireTimer;     // 페이즈 내 발사 반복
	FTimerHandle PhaseTimer;    // 페이즈 종료/대기 전환

	static constexpr float SpiralPhaseSec     = 5.f;
	static constexpr float FanPhaseSec        = 3.f;
	static constexpr float RestSec            = 1.f;
	static constexpr float FanFireIntervalSec = 0.5f;
```

- [ ] **Step 2: cpp — 스케줄러 구현**

`REBossCharacter.cpp`의 `TriggerBulletPattern` 함수 **위**에 추가 (include `TimerManager.h`는 GameMode에만 있으므로 이 파일 상단 include에 `#include "TimerManager.h"` 추가):

```cpp
void AREBossCharacter::StartFiring(int32 Seed)
{
	PhaseRng.Initialize(Seed);
	bFirstPhase = true;
	BeginPhase();   // 즉시 1회, 이후 타이머로 재진입
}

void AREBossCharacter::StopFiring()
{
	GetWorldTimerManager().ClearTimer(FireTimer);
	GetWorldTimerManager().ClearTimer(PhaseTimer);
}

void AREBossCharacter::BeginPhase()
{
	// KeepFiring 측정 모드: 로테이션/대기 우회, Spiral 연속 발사.
	// StartFiring 시점(BeginPlay)엔 ExecCmds가 아직 CVar를 안 세팅했을 수 있어
	// 여기(타이머 재진입 콜백)에서 매번 조회한다. 첫 페이즈 Spiral 고정이
	// profiling 시작 오염 창을 닫는다.
	static IConsoleVariable* KeepFiring = IConsoleManager::Get().FindConsoleVariable(TEXT("re.Profiling.KeepFiring"));
	if (KeepFiring && KeepFiring->GetInt() != 0)
	{
		CurrentPhasePattern = EBulletPattern::Spiral;
		GetWorldTimerManager().SetTimer(FireTimer, this,
			&AREBossCharacter::FireCurrentPattern, REBulletPattern::FireIntervalSec(), /*bLoop=*/true);
		return;   // PhaseTimer 예약 안 함 → 페이즈 종료/대기 없음
	}

	if (bFirstPhase)
	{
		CurrentPhasePattern = EBulletPattern::Spiral;   // 오프닝 시그니처 + 오염 창 차단
		bFirstPhase = false;
	}
	else
	{
		CurrentPhasePattern = (PhaseRng.RandRange(0, 1) == 0)
			? EBulletPattern::Spiral : EBulletPattern::Fan;
	}

	const bool bSpiral = (CurrentPhasePattern == EBulletPattern::Spiral);
	const float PhaseSec  = bSpiral ? SpiralPhaseSec : FanPhaseSec;
	const float FireInterval = bSpiral ? REBulletPattern::FireIntervalSec() : FanFireIntervalSec;

	UE_LOG(LogTemp, Log, TEXT("[RE] Boss Phase: %s %.1fs"),
		bSpiral ? TEXT("Spiral") : TEXT("Fan"), PhaseSec);

	GetWorldTimerManager().SetTimer(FireTimer, this,
		&AREBossCharacter::FireCurrentPattern, FireInterval, /*bLoop=*/true);
	GetWorldTimerManager().SetTimer(PhaseTimer, this,
		&AREBossCharacter::EndPhase, PhaseSec, /*bLoop=*/false);
}

void AREBossCharacter::FireCurrentPattern()
{
	TriggerBulletPattern(CurrentPhasePattern, /*Seed=*/12345, /*StartTime=*/0.f);
}

void AREBossCharacter::EndPhase()
{
	GetWorldTimerManager().ClearTimer(FireTimer);
	UE_LOG(LogTemp, Log, TEXT("[RE] Boss Phase: Rest %.1fs"), RestSec);
	GetWorldTimerManager().SetTimer(PhaseTimer, this,
		&AREBossCharacter::BeginPhase, RestSec, /*bLoop=*/false);
}
```

`IConsoleManager`/`IConsoleVariable`는 이미 include된 `HAL/IConsoleManager.h`에 있음. `FRandomStream`은 `CoreMinimal` 포함.

- [ ] **Step 3: GameMode.h — DemoFireTimer 제거**

`REGameMode.h`에서 삭제:

```cpp
	/** 데모: 주기적 Spiral 발사로 지속 탄막(영상 소스). */
	FTimerHandle DemoFireTimer;
```

`DemoBoss` 멤버는 유지. `#include "Engine/TimerHandle.h"`는 다른 용도 없으면 제거 가능하나 — 확인: 이 헤더에서 `FTimerHandle` 다른 사용 없으므로 include도 제거.

- [ ] **Step 4: GameMode.cpp — 발사 위임**

`REGameMode.cpp` BeginPlay의 보스 스폰 `if` 블록 본문을 교체. 기존:

```cpp
		Boss->TriggerBulletPattern(EBulletPattern::Spiral, 12345, 0.f);
		Boss->TriggerBulletPattern(EBulletPattern::Spiral, 12345, 0.f);  // #16 프로브: BaseAngle 누적 확인

		// #17 데모: 0.1초마다 Spiral 발사 → 회전 나선 탄막 지속(영상 소스 + ISM 카운트 추종 검증).
		DemoBoss = Boss;
		FTimerDelegate FireDel = FTimerDelegate::CreateLambda([this]()
		{
			if (DemoBoss)
			{
				DemoBoss->TriggerBulletPattern(EBulletPattern::Spiral, 12345, 0.f);
			}
		});
		GetWorld()->GetTimerManager().SetTimer(DemoFireTimer, FireDel, REBulletPattern::FireIntervalSec(), /*bLoop=*/true);
```

교체 후:

```cpp
		// #64: 발사 주체를 Boss로 이관 — 랜덤 패턴 페이즈 로테이션(M5 RPC 확장 대비).
		DemoBoss = Boss;
		Boss->StartFiring(/*Seed=*/12345);
```

`EndGame`에서 기존:

```cpp
	// 1) 탄막 발사 중지. 이미 뜬 탄환은 Lifetime 다할 때까지 계속 난다 (설계 합의 — 일괄 소멸 안 함).
	GetWorld()->GetTimerManager().ClearTimer(DemoFireTimer);
```

교체 후:

```cpp
	// 1) 탄막 발사 중지. 이미 뜬 탄환은 Lifetime 다할 때까지 계속 난다 (설계 합의 — 일괄 소멸 안 함).
	if (DemoBoss)
	{
		DemoBoss->StopFiring();
	}
```

`#16 프로브(SpiralProbe/FanProbe)` 순수 함수 검증 블록(BeginPlay 하단)은 **유지**. `FTimerDelegate` include가 GameMode.cpp에서 더는 안 쓰이면 그대로 둬도 무방(TimerManager.h가 제공).

- [ ] **Step 5: 빌드 게이트**

Run: `"/e/UE_5.8/Engine/Build/BatchFiles/Build.bat" Project_REEditor Win64 Development -Project="E:\UnrealProjects\Project_RE\Project_RE.uproject" -WaitMutex`
Expected: `Result: Succeeded`

- [ ] **Step 6: headless 프로브 — 페이즈 전환 + 결정성**

Run (Git Bash):
```bash
export MSYS_NO_PATHCONV=1 MSYS2_ARG_CONV_EXCL="*"
timeout 90 "/e/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" "E:\UnrealProjects\Project_RE\Project_RE.uproject" "/Game/Level/Main" -game -nullrhi -unattended -nosound -nosplash -stdout -abslog="E:\UnrealProjects\Project_RE\Saved\Logs\phase-probe.log" >/dev/null 2>&1
grep -aF "Boss Phase" "E:\UnrealProjects\Project_RE\Saved\Logs\phase-probe.log" | head -20
```
Expected: `Boss Phase: Spiral 5.0s` 먼저, 이후 `Boss Phase: Rest 1.0s` / `Boss Phase: Fan 3.0s` / `Boss Phase: Spiral 5.0s` 교차 출현. 첫 페이즈는 항상 Spiral.

주의: headless는 정지 플레이어가 탄에 맞아 ~2s에 DEFEAT → StopFiring으로 발사 조기 종료될 수 있음. 페이즈 전환을 넉넉히 보려면 로그 앞부분(첫 Spiral→Rest→다음)까지만 확인하거나, DEFEAT 후 `Boss Phase` 미출현이 정상. 최소 확인: 첫 `Spiral 5.0s` + `Rest 1.0s` 1회 이상.

- [ ] **Step 7: 측정 하네스 회귀 확인**

Run (Git Bash) — KeepFiring 모드로 로테이션 우회 확인:
```bash
export MSYS_NO_PATHCONV=1 MSYS2_ARG_CONV_EXCL="*"
timeout 60 "/e/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" "E:\UnrealProjects\Project_RE\Project_RE.uproject" "/Game/Level/Main" -game -nullrhi -unattended -nosound -nosplash -stdout -ExecCmds="re.Profiling.KeepFiring 1,re.Bullets.Count 1000" -abslog="E:\UnrealProjects\Project_RE\Saved\Logs\keepfire-probe.log" >/dev/null 2>&1
grep -aF "Boss Phase" "E:\UnrealProjects\Project_RE\Saved\Logs\keepfire-probe.log" | head -5
grep -aF "RenderProbe" "E:\UnrealProjects\Project_RE\Saved\Logs\keepfire-probe.log" | tail -5
```
Expected: `Boss Phase: Fan` / `Rest` **미출현**(우회됨 — Spiral만). `RenderProbe live=` 카운트가 목표(1000) 방향으로 증가(기존 클로즈드루프 동작 유지). Fan 페이즈가 섞이지 않아야 함.

- [ ] **Step 8: Commit**

```bash
git add Source/Project_RE/Core/REBossCharacter.h Source/Project_RE/Core/REBossCharacter.cpp Source/Project_RE/Core/REGameMode.h Source/Project_RE/Core/REGameMode.cpp
git commit -m "feat(M5): 보스 발사 주체 Boss 이관 + 랜덤 패턴 페이즈 로테이션 (#64)"
```

---

### Task 2: Fan 플레이어 조준

로테이션에 편입된 Fan이 발사 시점 플레이어 방향을 조준하게 한다. 폰 없으면 기존 0° 폴백.

**Files:**
- Modify: `Source/Project_RE/Core/REBossCharacter.cpp` (`TriggerBulletPattern`의 `Fan` case)

**Interfaces:**
- Consumes: `REBulletPattern::FFanParams`, `REBulletPattern::GenerateFan` (기존).
- Produces: 없음 (내부 동작 변경).

- [ ] **Step 1: Fan case에 조준 계산 추가**

`REBossCharacter.cpp`의 `TriggerBulletPattern` `case EBulletPattern::Fan:` 블록 교체. 기존:

```cpp
	case EBulletPattern::Fan:
	{
		REBulletPattern::FFanParams FP;
		Params = REBulletPattern::GenerateFan(GetActorLocation(), FP);
		UE_LOG(LogTemp, Log, TEXT("[RE] Boss Fan: Spread=%.1f -> N=%d"), FP.SpreadDeg, Params.Num());
		break;
	}
```

교체 후:

```cpp
	case EBulletPattern::Fan:
	{
		REBulletPattern::FFanParams FP;
		// 플레이어 방향 조준. 폰 없으면 0°(기존 기본) 폴백.
		// TODO M5: 멀티는 타깃 선택 필요 — 지금은 첫 플레이어 고정.
		if (APlayerController* PC = GetWorld()->GetFirstPlayerController())
		{
			if (const APawn* Target = PC->GetPawn())
			{
				const FVector D = Target->GetActorLocation() - GetActorLocation();
				FP.CenterAngleDeg = FMath::RadiansToDegrees(FMath::Atan2(D.Y, D.X));
			}
		}
		Params = REBulletPattern::GenerateFan(GetActorLocation(), FP);
		UE_LOG(LogTemp, Log, TEXT("[RE] Boss Fan: Center=%.1f Spread=%.1f -> N=%d"),
			FP.CenterAngleDeg, FP.SpreadDeg, Params.Num());
		break;
	}
```

`APlayerController`/`APawn`는 `GameFramework/Character.h`(헤더 상속) 경유 가시. 안전을 위해 `.cpp` 상단에 `#include "GameFramework/PlayerController.h"` 추가.

- [ ] **Step 2: 빌드 게이트**

Run: `"/e/UE_5.8/Engine/Build/BatchFiles/Build.bat" Project_REEditor Win64 Development -Project="E:\UnrealProjects\Project_RE\Project_RE.uproject" -WaitMutex`
Expected: `Result: Succeeded`

- [ ] **Step 3: headless 프로브 — Fan 조준각 확인**

Fan 페이즈가 나오려면 첫 Spiral 페이즈(5s) + Rest(1s) 후이므로, 정지 플레이어가 그 전에 죽지 않게 KeepFiring 없이 짧게는 안 잡힐 수 있음. 대신 첫 페이즈를 강제로 관측하기 위해, Fan 조준 수학은 `#16 FanProbe`가 아닌 실제 발사 로그로 확인한다. 보스(600,0), 정지 플레이어(원점 부근)면 방향 D=(−600,−y) → Center≈180°.

Run (Git Bash):
```bash
export MSYS_NO_PATHCONV=1 MSYS2_ARG_CONV_EXCL="*"
timeout 90 "/e/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" "E:\UnrealProjects\Project_RE\Project_RE.uproject" "/Game/Level/Main" -game -nullrhi -unattended -nosound -nosplash -stdout -abslog="E:\UnrealProjects\Project_RE\Saved\Logs\fan-probe.log" >/dev/null 2>&1
grep -aF "Boss Fan: Center" "E:\UnrealProjects\Project_RE\Saved\Logs\fan-probe.log" | head -5
```
Expected: `Boss Fan: Center=180.0` 근처(플레이어 위치에 따라 ±). Fan 페이즈가 DEFEAT 전에 안 잡히면, 임시로 헤더 `SpiralPhaseSec=0.5f`로 낮춰 Fan을 앞당겨 관측 후 원복 — 또는 Task 3 실RHI 스크린샷에서 Fan 페이즈 캡처로 대체 확인.

- [ ] **Step 4: Commit**

```bash
git add Source/Project_RE/Core/REBossCharacter.cpp
git commit -m "feat(M5): Fan 패턴 플레이어 조준 (#64)"
```

---

### Task 3: Spiral 회전 스텝 튜닝 (직선 방사 → 나선)

`SpiralRotationStepDeg`를 링 간격(22.5°)과 비정합한 값으로 바꿔 직선 방사를 나선으로 만든다. 실RHI 스크린샷으로 후보 비교 후 확정.

**Files:**
- Modify: `Source/Project_RE/Core/REBossCharacter.h` (`SpiralRotationStepDeg` 값)
- Modify: `docs/superpowers/specs/2026-07-18-boss-pattern-rotation-design.md` (확정값 기록)

**Interfaces:**
- Consumes: 없음. Produces: 없음.

- [ ] **Step 1: 임시 스크린샷 프로브 삽입**

`REGameMode.cpp` BeginPlay `#16 프로브` 블록 위에 임시 삽입(검증 후 Step 6에서 제거):

```cpp
	// TEMP-PROBE: 스파이럴 곡선감 스크린샷 (검증 후 제거)
	{
		FTimerHandle S1, S2, Q;
		GetWorld()->GetTimerManager().SetTimer(S1, FTimerDelegate::CreateLambda([]()
		{ FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir() / TEXT("SpiralTune3s.png"), false, false); }), 3.0f, false);
		GetWorld()->GetTimerManager().SetTimer(S2, FTimerDelegate::CreateLambda([]()
		{ FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir() / TEXT("SpiralTune5s.png"), false, false); }), 5.0f, false);
		GetWorld()->GetTimerManager().SetTimer(Q, FTimerDelegate::CreateLambda([this]()
		{ GEngine->Exec(GetWorld(), TEXT("quit")); }), 7.0f, false);
	}
```

`.cpp` 상단 include 추가: `#include "UnrealClient.h"`, `#include "Misc/Paths.h"`, `#include "Engine/Engine.h"`.

주의: 스크린샷 프로브는 정지 플레이어가 죽기 전(첫 Spiral 5s 페이즈 내)에 찍혀야 나선이 보인다. KeepFiring 없이도 3s/5s 시점은 첫 Spiral 페이즈 안(0~5s). DEFEAT로 StopFiring돼도 이미 뜬 탄은 유지되므로 5s 스샷도 유효.

- [ ] **Step 2: 후보값 A로 빌드 + 스샷**

`REBossCharacter.h`: `static constexpr float SpiralRotationStepDeg = 15.f;` → `= 137.5f;` (황금각).

Build (게이트 명령 동일) → Run (실RHI 창모드):
```bash
export MSYS_NO_PATHCONV=1 MSYS2_ARG_CONV_EXCL="*"
rm -f /e/UnrealProjects/Project_RE/Saved/SpiralTune3s.png /e/UnrealProjects/Project_RE/Saved/SpiralTune5s.png
timeout 300 "/e/UE_5.8/Engine/Binaries/Win64/UnrealEditor.exe" "E:\UnrealProjects\Project_RE\Project_RE.uproject" "/Game/Level/Main" -game -windowed -ResX=1280 -ResY=720 -nosplash -nosound -stdout -abslog="E:\UnrealProjects\Project_RE\Saved\Logs\spiral-tuneA.log" >/dev/null 2>&1
sleep 3; ls -la /e/UnrealProjects/Project_RE/Saved/SpiralTune5s.png
```
Read 툴로 `SpiralTune5s.png` 확인. 후보 A 결과를 `SpiralTuneA5s.png`로 복사 보관.

- [ ] **Step 3: 후보값 B로 빌드 + 스샷**

`SpiralRotationStepDeg = 9.7f;` (소각). Build → 위와 동일 실행(로그/파일명 `-tuneB`, PNG는 `SpiralTuneB5s.png`로 보관). Read 툴로 비교.

- [ ] **Step 4: 확정값 선택**

판단 기준: ① 직선 방사(모든 각도 살) 소멸, ② 나선 팔이 휘어 보임, ③ 균등 밀도. 두 후보 중 나은 쪽 확정. (둘 다 부족하면 3번째 후보 예: 5.3° / 23.1° 추가 반복.) `REBossCharacter.h`에 확정값 세팅 + 주석 갱신:

```cpp
	/** Spiral 호출당 BaseAngle 증가량(deg). 링 간격(22.5°)과 비정합 → 나선 팔이 휜다(#64). */
	static constexpr float SpiralRotationStepDeg = <확정값>f;
```

- [ ] **Step 5: 스펙 문서에 확정값 기록**

`docs/superpowers/specs/2026-07-18-boss-pattern-rotation-design.md`의 설계 결정 5번 "Spiral 회전 스텝 조정" 항목에 확정값 + 근거(어느 후보가 왜 나았는지 1줄) 추가.

- [ ] **Step 6: 임시 스크린샷 프로브 제거 + 빌드**

Step 1에서 넣은 `TEMP-PROBE` 블록 + 3개 임시 include 제거. Build 게이트 → `Result: Succeeded`.

- [ ] **Step 7: Commit**

```bash
git add Source/Project_RE/Core/REBossCharacter.h docs/superpowers/specs/2026-07-18-boss-pattern-rotation-design.md
git commit -m "feat(M5): Spiral 회전스텝 비정합값으로 나선 개선 (#64)"
```

---

## Self-Review

**Spec coverage:**
- 발사 주체 Boss 이관 → Task 1 ✓
- 랜덤 페이즈 로테이션 + 대기 → Task 1 ✓
- 시드 결정성 → Task 1 (PhaseRng.Initialize) ✓
- KeepFiring 우회 + 첫 페이즈 Spiral → Task 1 ✓
- Fan 플레이어 조준 → Task 2 ✓
- Spiral 회전스텝 조정 → Task 3 ✓
- 측정 하네스 회귀 없음 → Task 1 Step 7 ✓
- Homing/Settings 노출 → 스코프 밖(스펙 명시) ✓

**Placeholder scan:** 확정값 `<확정값>`은 Task 3에서 스크린샷 비교로 런타임 결정되는 값 — 플레이스홀더가 아니라 측정 산출물(후보 A/B 명시됨). 나머지 코드 블록 전부 완전.

**Type consistency:** `StartFiring(int32)`/`StopFiring()` Task 1 정의, GameMode에서 동일 시그니처 호출 ✓. `CurrentPhasePattern`/`bFirstPhase`/`PhaseRng`/`FireTimer`/`PhaseTimer` 헤더 선언과 cpp 사용 일치 ✓. `FireCurrentPattern`/`BeginPhase`/`EndPhase` 콜백 시그니처(void, 인자없음) `SetTimer(this, &..., ...)`와 일치 ✓.
