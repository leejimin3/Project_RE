# 탄막 스케일 노브 (`re.Bullets.Count`) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 콘솔 변수 `re.Bullets.Count N` 하나로 동시 탄환 수를 런타임에 100/1000/5000으로 바꿀 수 있게 한다 (재시작 없이 다음 발사부터 반영).

**Architecture:** 동시 탄환 수 = `발사당_탄수 / 발사주기 × 수명`. 발사주기(0.1s)와 수명(3s)을 상수로 고정하고 **발사당 탄 수만 역산**한다: `Count = round(N × 0.1 / 3)`. 역산은 `REBulletPattern` 네임스페이스의 순수 함수(엔진 의존 없음)에 두고, CVar 읽기는 발사 시점에 `AREBossCharacter::TriggerBulletPattern`이 담당한다. `AngleStepDeg = 360/Count`로 계산해 Count와 무관하게 균등 링을 유지한다.

> **이슈 본문의 `ceil`에서 `round`로 변경했다.** `ceil(100/30)=4` → steady live≈120 = **+20%** 로, 이슈 자신의 완료조건(±10%)을 N=100에서 위반한다. `round`면 4개 수치 전부 ±10% 안에 든다 (근거는 스펙 "결정 요약" 표).

**Tech Stack:** UE 5.8 C++, MassEntity, `TAutoConsoleVariable<int32>`, headless 프로브(`-game -nullrhi -ExecCmds`).

**Spec:** `docs/superpowers/specs/2026-07-15-bullet-scale-knob-design.md`

## Global Constraints

- **파일 소유권 (병렬 작업 — 위반 시 다른 세션과 충돌):** 수정 가능한 파일은 `Source/Project_RE/Core/REGameMode.*`, `Source/Project_RE/Core/REBossCharacter.*`, `Source/Project_RE/Mass/REBulletPatternGenerator.*` **뿐이다**.
  - `Mass/REBulletSimProcessor.cpp`, `Mass/REBulletRenderProcessor.cpp`, `Mass/REBulletHitProcessor.cpp`, `Config/`, `scripts/` → **#44 소유. 읽기만. 절대 수정 금지.**
  - 신규 `Baseline/` 폴더 → **#45 소유. 생성 금지.**
- **자동 테스트 인프라 없음.** 이 프로젝트에 유닛테스트 프레임워크는 없다. 검증 게이트는 **(a) 에디터 빌드 성공, (b) headless 프로브 로그 관측** 두 가지다. 새 테스트 프레임워크를 도입하지 마라.
- **성능 최적화 금지.** `REBulletSpawnSubsystem.cpp:49-55`의 per-entity `CreateEntity` 루프는 느리지만 이번 이슈의 **측정 대상**이지 수정 대상이 아니다 (#46). 손대지 마라.
- **범위 밖:** Fan/Homing 패턴 스케일링, 게임 밸런스, BeginPlay의 기존 M0/M1 프로브 코드 정리(미접촉 유지).
- 커밋 메시지: Conventional Commits, 끝에 `(#43)` 이슈 번호.
- 브랜치: `feature/M3-bullet-scale-knob` (worktree `E:/UnrealProjects/Project_RE-scale-knob`, `dev`에서 분기).

## 빌드 커맨드 (매 태스크에서 사용)

Git Bash에서. `MSYS_NO_PATHCONV=1`이 없으면 경로가 mangling된다:
```bash
MSYS_NO_PATHCONV=1 "/e/UE_5.8/Engine/Build/BatchFiles/Build.bat" \
  Project_REEditor Win64 Development \
  -Project="E:\UnrealProjects\Project_RE-scale-knob\Project_RE.uproject" \
  -WaitMutex -NoHotReload
```
기대 출력: `Result: Succeeded`, 에러 0.

---

### Task 1: 역산 순수 함수 `MakeSpiralForLiveCount`

발사주기·수명 상수를 제너레이터 헤더로 단일 출처화하고, 목표 동시 탄환 수 N → Spiral 파라미터 역산 함수를 추가한다. CVar는 아직 없다 (Task 2). 이 태스크만으로 빌드가 통과해야 한다.

**Files:**
- Modify: `Source/Project_RE/Mass/REBulletPatternGenerator.h`
- Modify: `Source/Project_RE/Mass/REBulletPatternGenerator.cpp`
- Modify: `Source/Project_RE/Core/REGameMode.cpp` (BeginPlay에 임시 검증 로그 — Step 1에서 추가, Step 6에서 제거)

**Interfaces:**
- Consumes: 없음 (기존 `FSpiralParams`, `GenerateSpiral`만 사용)
- Produces:
  - `constexpr float REBulletPattern::FireIntervalSec = 0.1f;`
  - `constexpr float REBulletPattern::BulletLifetimeSec = 3.f;`
  - `REBulletPattern::FSpiralParams REBulletPattern::MakeSpiralForLiveCount(int32 TargetLive, float BaseAngleDeg);`
  - → Task 2의 `AREBossCharacter::TriggerBulletPattern`과 `AREGameMode` 타이머가 이 이름들을 그대로 쓴다.

- [ ] **Step 1: 실패하는 검증 로그를 먼저 심는다 (테스트 프레임워크가 없으므로 headless 로그가 테스트다)**

`Source/Project_RE/Core/REGameMode.cpp`의 `BeginPlay` **맨 끝** (기존 `#16 프로브` 블록 `}` 다음, 함수 닫는 `}` 앞)에 임시 블록 추가. 이 블록은 Task 1 Step 6에서 제거한다:

```cpp
	// [TEMP #43 Task1] 역산 함수 검증 — Task 1 종료 시 제거.
	{
		using namespace REBulletPattern;
		for (const int32 N : { 100, 480, 1000, 5000 })
		{
			const FSpiralParams P = MakeSpiralForLiveCount(N, 0.f);
			UE_LOG(LogTemp, Log, TEXT("[RE] ScaleMath: N=%d -> Count=%d Step=%.3f Ring=%.1f Live=%.0f"),
				N, P.Count, P.AngleStepDeg, P.Count * P.AngleStepDeg,
				P.Count / FireIntervalSec * BulletLifetimeSec);
		}
	}
```

- [ ] **Step 2: 빌드해서 실패를 확인한다**

Run: 위 "빌드 커맨드"
Expected: **FAIL** — `error C2039: 'MakeSpiralForLiveCount': is not a member of 'REBulletPattern'` (및 `FireIntervalSec`/`BulletLifetimeSec` 미정의 에러). 함수가 아직 없으므로 컴파일 에러가 나는 것이 정상이다.

- [ ] **Step 3: 헤더에 상수 + 함수 선언 추가**

`Source/Project_RE/Mass/REBulletPatternGenerator.h` — `namespace REBulletPattern {` 바로 다음 줄에 상수를 넣고, `FSpiralParams::Lifetime` 기본값을 상수 참조로 바꾸고, 파일 하단 `GenerateFan` 선언 다음에 함수를 선언한다.

`namespace REBulletPattern` 여는 중괄호 직후:
```cpp
	/** 발사 주기(s). GameMode 발사 타이머와 역산 공식의 단일 출처. */
	constexpr float FireIntervalSec   = 0.1f;
	/** 탄 수명(s). FSpiralParams::Lifetime 기본값과 역산 공식의 단일 출처. */
	constexpr float BulletLifetimeSec = 3.f;

```

`FSpiralParams`의 Lifetime 줄 교체 (기존: `float Lifetime     = 3.f;    // s`):
```cpp
		float Lifetime     = BulletLifetimeSec;  // s
```

`GenerateFan` 선언 다음(네임스페이스 닫는 `}` 앞):
```cpp

	/**
	 *  목표 동시 탄환 수 N → 균등 링 Spiral 파라미터.
	 *  steady-state 동시 탄환 = 발사당_탄수 / 발사주기 × 수명 이므로
	 *  Count = round(N × FireIntervalSec / BulletLifetimeSec), AngleStep = 360/Count.
	 *  ceil이 아니라 round인 이유: ceil(100/30)=4 → live≈120 (+20%)으로 ±10% 허용치를 넘는다.
	 *  Speed/Lifetime은 FSpiralParams 기본값 유지.
	 */
	FSpiralParams MakeSpiralForLiveCount(int32 TargetLive, float BaseAngleDeg);
```

- [ ] **Step 4: cpp에 구현 추가**

`Source/Project_RE/Mass/REBulletPatternGenerator.cpp` — `GenerateFan` 함수 다음, `namespace REBulletPattern` 닫는 `}` 앞에 추가:

```cpp

	FSpiralParams MakeSpiralForLiveCount(int32 TargetLive, float BaseAngleDeg)
	{
		FSpiralParams P;
		// Max(1,...): TargetLive<=0 (CVar 사용자 입력) 시 360/0 나눗셈 방지.
		P.Count        = FMath::Max(1, FMath::RoundToInt(TargetLive * FireIntervalSec / BulletLifetimeSec));
		P.AngleStepDeg = 360.f / P.Count;   // Count 무관 균등 링
		P.BaseAngleDeg = BaseAngleDeg;
		return P;
	}
```

- [ ] **Step 5: 빌드 + headless 프로브로 역산값 검증**

빌드:
Run: 위 "빌드 커맨드"
Expected: `Result: Succeeded`

프로브 (Git Bash):
```bash
cd /e/UnrealProjects/Project_RE-scale-knob
MSYS_NO_PATHCONV=1 "/e/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" \
  "E:\UnrealProjects\Project_RE-scale-knob\Project_RE.uproject" /Game/Level/Main \
  -game -nullrhi -unattended -nosplash -stdout -NoSound -log=RE_scalemath.log &
sleep 30
grep "ScaleMath" "Saved/Logs/RE_scalemath.log"
"/c/Windows/System32/taskkill.exe" //IM UnrealEditor-Cmd.exe //F
```

Expected: 정확히 아래 4줄 (Ring은 항상 360.0 = 링이 닫힘, Live는 목표 N ±10% 안):
```
[RE] ScaleMath: N=100 -> Count=3 Step=120.000 Ring=360.0 Live=90
[RE] ScaleMath: N=480 -> Count=16 Step=22.500 Ring=360.0 Live=480
[RE] ScaleMath: N=1000 -> Count=33 Step=10.909 Ring=360.0 Live=990
[RE] ScaleMath: N=5000 -> Count=167 Step=2.156 Ring=360.0 Live=5010
```
판정 기준:
- **N=480 → Count=16, Step=22.5°** — 기존 하드코딩과 **정확히 일치**해야 한다. 다르면 회귀다.
- **Ring = 360.0** 4줄 전부 — 링이 닫힌다는 뜻 (완료조건 "균등 링").
- 오차: N=100 −10%(경계), N=1000 −1%, N=5000 +0.2% — 전부 ±10% 안.

Count가 위 표와 다르면 `FMath::RoundToInt` 인자의 정수/부동소수 승격을 확인하라 (`TargetLive * FireIntervalSec`는 `int32 * float` → `float`이 맞다). `CeilToInt`를 쓰면 N=100이 Count=4(Live=120, **+20%**)가 되어 완료조건을 못 지킨다 — `RoundToInt`가 맞다.

- [ ] **Step 6: 임시 검증 블록 제거**

Step 1에서 `REGameMode.cpp` BeginPlay에 넣은 `// [TEMP #43 Task1]` 블록 **전체를 삭제**한다. 역산은 Task 2에서 Boss가 실제로 호출하므로 이 로그는 더 필요 없다.

- [ ] **Step 7: 빌드 재확인 후 커밋**

Run: 위 "빌드 커맨드"
Expected: `Result: Succeeded` (TEMP 블록 제거 후에도 에러 0 — `REGameMode.cpp`는 원본 상태)

```bash
git add Source/Project_RE/Mass/REBulletPatternGenerator.h Source/Project_RE/Mass/REBulletPatternGenerator.cpp
git status --short   # REGameMode.cpp가 목록에 없어야 한다 (TEMP 블록 완전 제거 확인)
git commit -m "feat(M3): derive spiral count from target live bullet count (#43)"
```

---

### Task 2: CVar `re.Bullets.Count` 배선

CVar를 선언하고 발사 시점에 읽어 역산 함수에 넘긴다. GameMode 타이머의 `0.1f` 리터럴을 상수 참조로 바꾼다.

**Files:**
- Modify: `Source/Project_RE/Core/REBossCharacter.cpp` (파일 상단 CVar 선언 + `TriggerBulletPattern`의 Spiral 케이스, 현재 `:43-51`)
- Modify: `Source/Project_RE/Core/REGameMode.cpp:67` (SetTimer 1줄)

**Interfaces:**
- Consumes: Task 1의 `REBulletPattern::MakeSpiralForLiveCount(int32, float)`, `REBulletPattern::FireIntervalSec`
- Produces: 콘솔 변수 `re.Bullets.Count` (int32, 기본 480). 로그 `[RE] Boss Spiral: Target=%d BaseAngle=%.1f -> N=%d` — Task 3이 이 로그를 관측한다.

- [ ] **Step 1: CVar 선언 추가**

`Source/Project_RE/Core/REBossCharacter.cpp` — include 블록 다음, `AREBossCharacter::AREBossCharacter()` 앞에 추가:

```cpp
/**
 *  목표 동시 탄환 수. 발사 시점에 조회하므로 재시작 없이 다음 발사부터 반영된다.
 *  발사당 탄 수 = round(N × 발사주기 / 수명) 로 역산 (REBulletPattern::MakeSpiralForLiveCount).
 *  기본 480 = 기존 하드코딩(16발 / 0.1s × 3s)과 동일 — 회귀 없음.
 */
static TAutoConsoleVariable<int32> CVarBulletCount(
	TEXT("re.Bullets.Count"),
	480,
	TEXT("목표 동시 탄환 수(steady-state). 다음 발사부터 반영."),
	ECVF_Cheat);
```

`TAutoConsoleVariable`은 `CoreMinimal.h` 경유로 이미 가시적이지만, 링크 에러가 나면 include에 `#include "HAL/IConsoleManager.h"`를 추가하라.

- [ ] **Step 2: Spiral 케이스에서 CVar 조회 → 역산 함수 호출**

`Source/Project_RE/Core/REBossCharacter.cpp`의 `TriggerBulletPattern` Spiral 케이스를 교체.

기존 (현재 `:43-51`):
```cpp
	case EBulletPattern::Spiral:
	{
		REBulletPattern::FSpiralParams SP;
		SP.BaseAngleDeg = SpiralBaseAngleDeg;
		Params = REBulletPattern::GenerateSpiral(GetActorLocation(), SP);
		UE_LOG(LogTemp, Log, TEXT("[RE] Boss Spiral: BaseAngle=%.1f -> N=%d"), SpiralBaseAngleDeg, Params.Num());
		SpiralBaseAngleDeg += SpiralRotationStepDeg;  // 다음 호출 시 회전
		break;
	}
```

교체 후:
```cpp
	case EBulletPattern::Spiral:
	{
		// 발사 시점 조회 — CVar 변경이 재시작/타이머 재설정 없이 다음 발사부터 반영된다.
		const int32 TargetLive = CVarBulletCount.GetValueOnGameThread();
		const REBulletPattern::FSpiralParams SP =
			REBulletPattern::MakeSpiralForLiveCount(TargetLive, SpiralBaseAngleDeg);
		Params = REBulletPattern::GenerateSpiral(GetActorLocation(), SP);
		UE_LOG(LogTemp, Log, TEXT("[RE] Boss Spiral: Target=%d BaseAngle=%.1f -> N=%d"),
			TargetLive, SpiralBaseAngleDeg, Params.Num());
		SpiralBaseAngleDeg += SpiralRotationStepDeg;  // 다음 호출 시 회전
		break;
	}
```

Fan / Homing 케이스, 함수 시그니처, `bIsDead` 가드, `SpawnBulletBatch` 호출, 하단 요약 로그는 **건드리지 않는다**.

- [ ] **Step 3: GameMode 타이머를 상수 참조로**

`Source/Project_RE/Core/REGameMode.cpp:67` 한 줄 교체.

기존:
```cpp
			GetWorld()->GetTimerManager().SetTimer(DemoFireTimer, FireDel, 0.1f, /*bLoop=*/true);
```
교체 후:
```cpp
			GetWorld()->GetTimerManager().SetTimer(DemoFireTimer, FireDel, REBulletPattern::FireIntervalSec, /*bLoop=*/true);
```
`REBulletPatternGenerator.h`는 `REGameMode.cpp:12`에서 이미 include 중이다. BeginPlay의 다른 코드(프로브, 초기 트리거 2회, SimProbe)는 **미접촉**.

- [ ] **Step 4: 빌드**

Run: 위 "빌드 커맨드"
Expected: `Result: Succeeded`, 에러 0.

- [ ] **Step 5: 기본값 회귀 확인 (CVar 미지정 = 기존 동작)**

```bash
cd /e/UnrealProjects/Project_RE-scale-knob
MSYS_NO_PATHCONV=1 "/e/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" \
  "E:\UnrealProjects\Project_RE-scale-knob\Project_RE.uproject" /Game/Level/Main \
  -game -nullrhi -unattended -nosplash -stdout -NoSound -log=RE_default.log &
sleep 30
grep "Boss Spiral" "Saved/Logs/RE_default.log" | head -3
"/c/Windows/System32/taskkill.exe" //IM UnrealEditor-Cmd.exe //F
```
Expected: `Target=480 ... -> N=16` — 기본값이 기존 하드코딩 16발과 정확히 같다 (회귀 없음):
```
[RE] Boss Spiral: Target=480 BaseAngle=0.0 -> N=16
[RE] Boss Spiral: Target=480 BaseAngle=15.0 -> N=16
```

- [ ] **Step 6: 커밋**

```bash
git add Source/Project_RE/Core/REBossCharacter.cpp Source/Project_RE/Core/REGameMode.cpp
git commit -m "feat(M3): add re.Bullets.Count CVar to scale bullet volume (#43)"
```

---

### Task 3: headless 프로브로 100/1000/5000 실측

CVar를 `-ExecCmds`로 주입해 3개 수치에서 steady-state `live=`가 목표에 수렴하는지 관측한다. 코드 변경 없음 — 실패 시에만 코드로 되돌아간다.

**Files:**
- 코드 변경 없음. 관측 대상: `Mass/REBulletRenderProcessor.cpp:71-76`의 기존 `RenderProbe` 로그 (**#44 소유 — 읽기만, 수정 금지**).
- Create: `docs/superpowers/goals/2026-07-15-bullet-scale-knob-goal.md` (측정 결과 기록)

**Interfaces:**
- Consumes: Task 2의 CVar `re.Bullets.Count`, 로그 `[RE] Boss Spiral: Target=...`; RenderProcessor의 `[RE] RenderProbe: live=%d ISM.Count=%d` (30틱마다 1회)
- Produces: 없음 (검증 종료 태스크)

- [ ] **Step 1: N=100 프로브**

```bash
cd /e/UnrealProjects/Project_RE-scale-knob
MSYS_NO_PATHCONV=1 "/e/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" \
  "E:\UnrealProjects\Project_RE-scale-knob\Project_RE.uproject" /Game/Level/Main \
  -game -nullrhi -unattended -nosplash -stdout -NoSound \
  -ExecCmds="re.Bullets.Count 100" -log=RE_scale100.log &
sleep 40
grep -E "RenderProbe|Boss Spiral" "Saved/Logs/RE_scale100.log" | tail -12
"/c/Windows/System32/taskkill.exe" //IM UnrealEditor-Cmd.exe //F
```
Expected: `Target=100 ... -> N=3`, 그리고 3초(수명) 경과 후 `live=`가 **~90에서 평평하게 안정** (초기 상승 곡선 후 진동 없이 고정). 목표 100 대비 −10% — 완료조건 경계 안이다.

`live`는 배치 단위로 30~31개 배치가 공존하므로 `Count×30 ~ Count×31` (여기선 90~93) 범위에서 1~2 진동할 수 있다. 이건 이산 스폰의 정상 동작이지 버그가 아니다.

**만약 `RenderProbe` 로그가 한 줄도 없으면:** `-nullrhi`에서 ISM이 없어 `RenderProcessor::Execute`가 early-return한 것이다 (`REBulletRenderProcessor.cpp:39-42`). 그 경우 `-nullrhi`를 빼고 `-windowed -ResX=640 -ResY=480`으로 바꿔 실RHI로 재실행하라. RenderProcessor는 여전히 `Standalone` 넷모드에서 돌므로 로그가 나온다.

- [ ] **Step 2: N=1000 프로브**

```bash
MSYS_NO_PATHCONV=1 "/e/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" \
  "E:\UnrealProjects\Project_RE-scale-knob\Project_RE.uproject" /Game/Level/Main \
  -game -nullrhi -unattended -nosplash -stdout -NoSound \
  -ExecCmds="re.Bullets.Count 1000" -log=RE_scale1000.log &
sleep 40
grep -E "RenderProbe|Boss Spiral" "Saved/Logs/RE_scale1000.log" | tail -12
"/c/Windows/System32/taskkill.exe" //IM UnrealEditor-Cmd.exe //F
```
Expected: `Target=1000 ... -> N=33`, steady-state `live=` ≈ **990 (목표 1000의 −1%, ±10% 이내 ✅)**

- [ ] **Step 3: N=5000 프로브**

```bash
MSYS_NO_PATHCONV=1 "/e/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" \
  "E:\UnrealProjects\Project_RE-scale-knob\Project_RE.uproject" /Game/Level/Main \
  -game -nullrhi -unattended -nosplash -stdout -NoSound \
  -ExecCmds="re.Bullets.Count 5000" -log=RE_scale5000.log &
sleep 60
grep -E "RenderProbe|Boss Spiral" "Saved/Logs/RE_scale5000.log" | tail -12
"/c/Windows/System32/taskkill.exe" //IM UnrealEditor-Cmd.exe //F
```
Expected: `Target=5000 ... -> N=167`, steady-state `live=` ≈ **5010 (+0.2%, ±10% 이내 ✅)**

프레임레이트가 떨어져 steady-state 도달이 늦어질 수 있다 (`sleep 60`으로 여유를 뒀다). **느린 건 정상이다 — 이게 #46이 측정할 대상이다. 여기서 최적화하지 마라.**

- [ ] **Step 4: 실측 오차 판정**

3개 로그의 steady-state `live=` 값을 목표 대비 계산한다:

| N | 기대 Count | 기대 live | 허용 범위 (±10%) |
|---|---|---|---|
| 100 | 3 | ~90 | 90 ~ 110 |
| 1000 | 33 | ~990 | 900 ~ 1100 |
| 5000 | 167 | ~5010 | 4500 ~ 5500 |

**N=100이 90 미만이면 실패다** (허용 하한이 정확히 90). 이 경우 배치 공존 수가 30이 아니라 29라는 뜻 — `SimProcessor`의 수명 판정이 `>=`인지 `>`인지, `Lifetime`이 정확히 3.0s로 전달되는지 로그로 확인하라. `REBulletSimProcessor.cpp`는 **#44 소유라 수정 금지** — 읽어서 원인만 파악하고, 수정이 필요하면 `MakeSpiralForLiveCount` 쪽(내 소유)에서 조정하거나 사용자에게 보고하라.

**N=5000이 크게 미달하면** (예: 3000대) 스폰이 프레임 예산을 못 따라간 것이다 — `SpawnBulletBatch`의 per-entity `CreateEntity` 병목. **이건 #46이 측정할 대상이지 여기서 고칠 게 아니다.** 수치를 goal 문서에 기록하고 사용자에게 보고하라.

- [ ] **Step 5: 측정 결과를 goal 문서로 기록**

`docs/superpowers/goals/2026-07-15-bullet-scale-knob-goal.md` 생성. 기존 goal 문서(`docs/superpowers/goals/` 참고) 형식을 따르되, **실제 로그를 붙여넣어라 — 예상값을 옮겨 적지 마라.** 포함할 것:
- 3개 수치 각각의 `Boss Spiral: Target=... -> N=...` 로그 1줄
- 3개 수치 각각의 steady-state `RenderProbe: live=...` 로그 2~3줄
- 목표 대비 오차율 표 (N / Count / 실측 live / 오차%)
- `-nullrhi`에서 RenderProbe가 안 찍혀 실RHI로 폴백했다면 그 사실
- N=5000에서 관측된 체감 프레임 저하 (있다면) — **수치 측정은 #46 범위, 여기선 정성 기록만**

```bash
git add docs/superpowers/goals/2026-07-15-bullet-scale-knob-goal.md
git commit -m "test(M3): headless probe for 100/1000/5000 live bullet counts (#43)"
```

- [ ] **Step 6: 완료조건 최종 체크 (이슈 #43)**

로그 증거를 근거로 6개 항목을 확인한다. **증거 없이 체크하지 마라.**

- [ ] `re.Bullets.Count 100` → live 100±10% 안정 (Step 1 로그, ~90)
- [ ] `re.Bullets.Count 1000` → live 1000±10% 안정 (Step 2 로그, ~990)
- [ ] `re.Bullets.Count 5000` → live 5000±10% 안정 (Step 3 로그, ~5010)
- [ ] CVar 변경이 재시작 없이 다음 발사부터 반영 — 코드상 `GetValueOnGameThread()`가 발사 시점 호출(Task 2 Step 2). **에디터 PIE 콘솔에서 런타임 변경도 1회 확인:** PIE 실행 중 `` ` `` 콘솔에 `re.Bullets.Count 1000` 입력 → 다음 로그 줄부터 `Target=1000 -> N=33`으로 바뀌는지 관측.
- [ ] 링 패턴이 Count와 무관하게 균등 — `AngleStep = 360/Count` (Task 1 Step 5의 `Ring=360.0` 4줄)
- [ ] 헤드리스 프로브로 3개 수치 로그 관측 (Step 1~3)

---

## 스코프 밖 (하지 마라)

- `REBulletSpawnSubsystem::SpawnBulletBatch`의 per-entity `CreateEntity` 루프 최적화 → **#46.** N=5000에서 느린 건 측정 결과지 버그가 아니다.
- CPU 시간·프로파일링 수치 측정 → **#46.**
- `Mass/*Processor.cpp`, `Config/`, `scripts/` 수정 → **#44 소유.**
- 신규 `Baseline/` 폴더 → **#45 소유.**
- Fan/Homing 패턴 스케일링, 게임 밸런스 조정.
- BeginPlay의 M0/M1 프로브 코드(초기 트리거 2회, SpiralProbe/FanProbe, SimProbe) 정리 — **미접촉 결정.** (Task 1의 TEMP 블록만 예외이며 Task 1 Step 6에서 반드시 제거한다.)
