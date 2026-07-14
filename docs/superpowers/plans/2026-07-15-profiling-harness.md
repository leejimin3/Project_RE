# M3 프로파일링 하네스 Implementation Plan (#44)

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 실행 한 번으로 `.utrace` + `.csv` 가 나오는 재현 가능한 Mass 프로세서 측정 하네스를 만든다.

**Architecture:** Mass 프로세서 3개의 `Execute` 첫 줄에 엔진 내장 `TRACE_CPUPROFILER_EVENT_SCOPE` 를 1줄씩 넣어 Insights 에서 이름으로 찾히게 한다. `scripts/profile.ps1` 이 에디터를 `-game -windowed` 실 RHI 로 띄우고 트레이스/CSV 를 수집한 뒤 죽인다. 측정 조건(해상도/워밍업/구간 길이)은 `docs/guides/profiling.md` 에 고정 기록한다.

**Tech Stack:** UE 5.8 (`E:\UE_5.8`), C++ (MassEntity), PowerShell 5.1, Unreal Insights, 엔진 내장 CSV Profiler.

## Global Constraints

- 워크트리 루트: `E:\UnrealProjects\Project_RE-profiling` (브랜치 `feature/M3-profiling-harness`, dev 분기)
- 엔진: `E:\UE_5.8`
- **건드려도 되는 파일은 이것뿐이다:**
  - `Source/Project_RE/Mass/REBulletSimProcessor.cpp`
  - `Source/Project_RE/Mass/REBulletRenderProcessor.cpp`
  - `Source/Project_RE/Mass/REBulletHitProcessor.cpp`
  - `scripts/profile.ps1` (신규)
  - `docs/guides/profiling.md` (신규)
- **절대 건드리지 않는다:** `Source/Project_RE/Core/*` (특히 `REGameMode.*`, `REBossCharacter.*`), `PatternGenerator` (#43 소유), `Baseline/` (#45 소유), `Config/*.ini`
- 커스텀 프로파일러/타이머 클래스를 만들지 않는다. CSV 파싱 코드를 쓰지 않는다. Editor/Packaged 타깃 스위치를 만들지 않는다.
- `re.Bullets.Count` CVar 는 아직 없다 (#43 작업 중). UE 는 모르는 CVar 에 경고만 찍고 계속 실행한다 — **경고는 예상된 것이고, #43 을 기다리지 않는다.**
- PowerShell 5.1 문법: `&&` / `||` / 삼항 연산자 없다. `if ($?) { ... }` 를 쓴다.
- 빌드 명령 (bash 기준, 이 프로젝트의 기존 방식):
  ```bash
  "/e/UE_5.8/Engine/Build/BatchFiles/Build.bat" Project_REEditor Win64 Development \
    -Project="E:\UnrealProjects\Project_RE-profiling\Project_RE.uproject" -WaitMutex -NoHotReload
  ```

## File Structure

| 파일 | 책임 |
|---|---|
| `Mass/REBulletSimProcessor.cpp` | `RE_BulletSim` 스코프 (워커 스레드에 잡힘) |
| `Mass/REBulletRenderProcessor.cpp` | `RE_BulletRender` 스코프 (GT 고정) |
| `Mass/REBulletHitProcessor.cpp` | `RE_BulletHit` 스코프 (GT 고정) |
| `scripts/profile.ps1` | 띄우기 → 수집 → 죽이기. 해석은 안 한다 |
| `docs/guides/profiling.md` | 측정 조건 고정 + Insights 읽는 법 |

---

### Task 1: 프로세서 3개에 CPU 스코프 삽입

**Files:**
- Modify: `Source/Project_RE/Mass/REBulletSimProcessor.cpp` (`Execute` 본문 첫 줄)
- Modify: `Source/Project_RE/Mass/REBulletRenderProcessor.cpp` (`Execute` 본문 첫 줄)
- Modify: `Source/Project_RE/Mass/REBulletHitProcessor.cpp` (`Execute` 본문 첫 줄)

**Interfaces:**
- Consumes: 없음 (첫 태스크)
- Produces: Insights 트레이스에 나타나는 이벤트 이름 3개 — `RE_BulletSim`, `RE_BulletRender`, `RE_BulletHit`. Task 3 문서가 이 이름을 그대로 참조한다.

**테스트 전략 메모:** UE C++ 프로세서에 유닛 테스트 하네스가 이 프로젝트엔 없다. 이 태스크의 검증 게이트는 **컴파일 통과**다 (매크로 include 체인이 실제로 되는지는 빌드만이 안다 — 추측 금지). 런타임 검증은 Task 2 에서 실제 트레이스로 한다.

- [ ] **Step 1: Sim 프로세서에 스코프 추가**

`Source/Project_RE/Mass/REBulletSimProcessor.cpp` 의 `Execute` 본문 첫 줄:

```cpp
void UREBulletSimProcessor::Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(RE_BulletSim);

	EntityQuery.ForEachEntityChunk(Context, [](FMassExecutionContext& Context)
	{
```

(기존 `ForEachEntityChunk` 이하 본문은 그대로 둔다.)

- [ ] **Step 2: Render 프로세서에 스코프 추가**

`Source/Project_RE/Mass/REBulletRenderProcessor.cpp` 의 `Execute` 본문 첫 줄:

```cpp
void UREBulletRenderProcessor::Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(RE_BulletRender);

	UWorld* World = EntityManager.GetWorld();
```

(기존 본문 그대로.)

- [ ] **Step 3: Hit 프로세서에 스코프 추가**

`Source/Project_RE/Mass/REBulletHitProcessor.cpp` 의 `Execute` 본문 첫 줄:

```cpp
void UREBulletHitProcessor::Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(RE_BulletHit);

	UWorld* World = EntityManager.GetWorld();
```

(기존 본문 그대로.)

- [ ] **Step 4: 빌드 게이트 — 컴파일 통과 확인**

Run (bash):
```bash
"/e/UE_5.8/Engine/Build/BatchFiles/Build.bat" Project_REEditor Win64 Development \
  -Project="E:\UnrealProjects\Project_RE-profiling\Project_RE.uproject" -WaitMutex -NoHotReload
```

Expected: 마지막 줄에 `Total execution time:` 이 찍히고 에러 0.

**실패 시 (`TRACE_CPUPROFILER_EVENT_SCOPE` 를 못 찾는다는 컴파일 에러):** 세 `.cpp` 각각의 include 블록 마지막에 아래 1줄을 추가하고 다시 빌드한다.

```cpp
#include "ProfilingDebugging/CpuProfilerTrace.h"
```

에러가 안 나면 include 를 추가하지 않는다 (불필요한 줄을 넣지 않는다).

- [ ] **Step 5: 커밋**

```bash
git add Source/Project_RE/Mass/REBulletSimProcessor.cpp \
        Source/Project_RE/Mass/REBulletRenderProcessor.cpp \
        Source/Project_RE/Mass/REBulletHitProcessor.cpp
git commit -m "feat(M3): add TRACE_CPUPROFILER_EVENT_SCOPE to the three bullet processors (#44)

Named CPU scopes RE_BulletSim / RE_BulletRender / RE_BulletHit so Unreal
Insights can break down per-processor CPU time. Engine macro only, no custom
profiler.

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

### Task 2: 캡처 스크립트 `scripts/profile.ps1`

**Files:**
- Create: `scripts/profile.ps1`

**Interfaces:**
- Consumes: Task 1 의 스코프 3개 (트레이스에 이름이 실려 나온다)
- Produces: 실행 1회당 폴더 1개 — `Saved/Profiling/RE_<Bullets>_<yyyyMMdd-HHmmss>/` 안에 `trace.utrace` + `frames.csv`. 스크립트는 마지막에 이 폴더 경로를 stdout 으로 출력한다. Task 3 문서가 이 경로 규칙을 참조한다.

- [ ] **Step 1: `scripts/profile.ps1` 작성**

```powershell
<#
.SYNOPSIS
  Mass 탄환 프로세서 프로파일 캡처. 에디터를 실 RHI -game 으로 띄우고
  Insights 트레이스 + CSV 프레임 타임을 수집한 뒤 종료한다.

.EXAMPLE
  scripts/profile.ps1 -Bullets 1000
  scripts/profile.ps1 -Bullets 5000 -Frames 720
#>
param(
    [int]$Bullets = 1000,
    # 캡처 총 프레임 = 워밍업 120 + 측정 600. docs/guides/profiling.md 참조.
    [int]$Frames  = 720,
    # 하드 타임아웃(초). 5000발 저FPS 여유 포함.
    [int]$TimeoutSec = 300
)

$ErrorActionPreference = 'Stop'

$Root     = Split-Path $PSScriptRoot -Parent
$Editor   = 'E:\UE_5.8\Engine\Binaries\Win64\UnrealEditor.exe'
$Uproject = Join-Path $Root 'Project_RE.uproject'
$CsvDir   = Join-Path $Root 'Saved\Profiling\CSV'

if (-not (Test-Path $Editor))   { throw "에디터 없음: $Editor" }
if (-not (Test-Path $Uproject)) { throw "uproject 없음: $Uproject" }

$Stamp  = Get-Date -Format 'yyyyMMdd-HHmmss'
$RunDir = Join-Path $Root "Saved\Profiling\RE_${Bullets}_${Stamp}"
New-Item -ItemType Directory -Force -Path $RunDir | Out-Null

$TracePath = Join-Path $RunDir 'trace.utrace'
$StartTime = Get-Date

# 측정 조건은 docs/guides/profiling.md 에 고정 기록되어 있다. 바꾸면 문서도 바꿔라.
$ArgLine = @(
    "`"$Uproject`"",
    '/Game/Level/Main',
    '-game',
    '-windowed', '-ResX=1280', '-ResY=720',
    '-trace=cpu,frame,counters',
    '-statnamedevents',
    "-tracefile=`"$TracePath`"",
    "-csvCaptureFrames=$Frames",
    "-ExecCmds=`"re.Bullets.Count $Bullets`"",
    '-unattended', '-nosplash', '-NoSound'
) -join ' '

Write-Host "[profile] Bullets=$Bullets Frames=$Frames"
Write-Host "[profile] run dir: $RunDir"

$Proc = Start-Process -FilePath $Editor -ArgumentList $ArgLine -PassThru

# CSV 프로파일러는 $Frames 프레임을 채우면 csv 를 쓰지만 게임은 계속 돈다.
# 새 csv 가 나타나는 것이 곧 "캡처 완료" 신호다.
$Deadline = $StartTime.AddSeconds($TimeoutSec)
$Csv = $null
while ((Get-Date) -lt $Deadline) {
    if ($Proc.HasExited) { break }
    if (Test-Path $CsvDir) {
        $Csv = Get-ChildItem -Path $CsvDir -Filter '*.csv' -ErrorAction SilentlyContinue |
               Where-Object { $_.LastWriteTime -gt $StartTime } |
               Sort-Object LastWriteTime | Select-Object -Last 1
        if ($Csv) { break }
    }
    Start-Sleep -Seconds 2
}

if ($Csv) {
    Start-Sleep -Seconds 3   # csv flush + trace 마무리 여유
} else {
    Write-Warning "[profile] 타임아웃 ${TimeoutSec}s — csv 안 나옴. 프레임을 못 채웠거나 부팅 실패."
}

if (-not $Proc.HasExited) {
    Stop-Process -Id $Proc.Id -Force -ErrorAction SilentlyContinue
    $Proc.WaitForExit(10000) | Out-Null
}

if ($Csv) {
    Move-Item -Path $Csv.FullName -Destination (Join-Path $RunDir 'frames.csv') -Force
}

Write-Host "[profile] 산출물:"
Get-ChildItem $RunDir | ForEach-Object { Write-Host ("  " + $_.Name + "  " + $_.Length + " bytes") }
Write-Host $RunDir
```

- [ ] **Step 2: 실행 — 하네스가 실제로 산출물을 뱉는지 확인 (이것이 이 태스크의 테스트다)**

Run (PowerShell):
```powershell
.\scripts\profile.ps1 -Bullets 1000
```

Expected:
- 게임 창이 1280x720 로 뜬다 (실 RHI — 창이 보이는 게 정상이다)
- 로그에 `re.Bullets.Count` 를 모르는 CVar 라는 **경고**가 뜬다 → **예상된 것** (#43 미머지)
- 스크립트가 끝나며 `[profile] 산출물:` 아래 `trace.utrace` 와 `frames.csv` 두 파일이 0 bytes 가 아닌 크기로 나열된다

**csv 가 안 나오면** — `Saved\Profiling\CSV` 경로가 실제 출력 경로가 맞는지 먼저 확인한다:
```powershell
Get-ChildItem -Path .\Saved -Recurse -Filter *.csv | Select-Object FullName, LastWriteTime
```
찾은 실제 경로로 스크립트의 `$CsvDir` 를 고친다. (경로를 추측하지 말고 실제 파일 위치를 보고 정한다.)

- [ ] **Step 3: CSV 헤더에 GameThread / RenderThread ms 컬럼이 있는지 확인**

Run (PowerShell — `<RunDir>` 는 Step 2 가 출력한 경로):
```powershell
(Get-Content "<RunDir>\frames.csv" -TotalCount 1) -split ',' | Where-Object { $_ -match 'Thread|Frame|GPU' }
```

Expected: `GameThreadTime` / `RenderThreadTime` / `FrameTime` 계열 컬럼명이 출력된다.

**출력된 실제 컬럼명을 그대로 적어 둔다** — Task 3 문서에 이 이름을 박아야 한다 (엔진 버전마다 다를 수 있으므로 추측해서 쓰지 않는다).

- [ ] **Step 4: 커밋**

```bash
git add scripts/profile.ps1
git commit -m "feat(M3): add scripts/profile.ps1 trace + CSV capture harness (#44)

Launches the editor as -game with real RHI at a fixed 1280x720, records an
Insights trace and the engine CSV profiler, then kills the process once the
CSV appears (capture done). Both artifacts land in one run folder under
Saved/Profiling/.

No CSV parsing, no custom timers: the harness collects, it does not interpret.

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

### Task 3: 측정 조건 문서 `docs/guides/profiling.md`

**Files:**
- Create: `docs/guides/profiling.md`

**Interfaces:**
- Consumes: Task 1 의 이벤트 이름 3개, Task 2 의 스크립트 인자/산출물 경로 규칙, Task 2 Step 3 에서 **실제로 관측한** CSV 컬럼명
- Produces: 없음 (문서가 최종 산출물)

- [ ] **Step 1: 문서 작성**

아래 내용으로 `docs/guides/profiling.md` 를 만든다. `<Task 2 Step 3 에서 관측한 실제 컬럼명>` 자리에는 **추측이 아니라 실제로 본 이름**을 넣는다.

```markdown
# 프로파일링 측정 절차 (M3)

측정이 매번 달라지면 증거가 안 된다. 아래 조건을 고정한다.

## 실행

```powershell
scripts/profile.ps1 -Bullets 1000          # 기본 720프레임 캡처
scripts/profile.ps1 -Bullets 5000 -Frames 720
```

산출물: `Saved/Profiling/RE_<탄환수>_<타임스탬프>/`
- `trace.utrace` — Unreal Insights 트레이스
- `frames.csv` — 엔진 CSV 프로파일러의 프레임별 시간

## 고정 조건

| 항목 | 값 | 이유 |
|---|---|---|
| 렌더 | **실 RHI**, `-windowed 1280x720` | `-nullrhi` 는 렌더 비용이 사라져 GT 병목을 못 본다. 과거 ISM Bounds=0 오진 전례 있음 |
| 워밍업 | **앞 120프레임 버린다** | 레벨 로드 / 셰이더 컴파일 스파이크 구간 |
| 측정 구간 | **600프레임** (캡처 총 720 = 120 + 600) | steady-state 만 읽는다 |
| 바이너리 | `UnrealEditor.exe -game` | 패키징(cook)은 수십 분 + 코드 수정마다 재쿡. **절대치가 아니라 상대 비교용**이다 — 에디터 오버헤드가 섞여 있음을 알고 읽어라 |

워밍업 컷은 스크립트가 하지 않는다. `frames.csv` 원본을 그대로 남기고, **읽을 때 앞 120행을 버린다.** (하네스는 수집만 하고 해석하지 않는다 — 해석은 #46.)

## Insights 로 읽는 법

1. `E:\UE_5.8\Engine\Binaries\Win64\UnrealInsights.exe` 실행
2. `trace.utrace` 열기 (Open Trace File)
3. Timing Insights 뷰 → 필터에 이벤트 이름 입력:

| 이벤트 | 프로세서 | 잡히는 스레드 |
|---|---|---|
| `RE_BulletSim` | 이동 / 수명 | 워커 스레드 |
| `RE_BulletRender` | ISM 동기화 | **게임 스레드 고정** (`bRequiresGameThreadExecution = true`) |
| `RE_BulletHit` | 피격 판정 | **게임 스레드 고정** |

Render / Hit 가 GT 에 고정돼 있으므로 탄환 수를 올렸을 때 GT 병목의 최우선 용의자는 `RE_BulletRender` 다. Sim 이 워커 스레드로 빠지는 그림이 "왜 Mass 를 썼나"의 증거다.

## CSV 컬럼

`frames.csv` 의 프레임 시간 컬럼 (엔진 CSV 프로파일러가 찍는 실제 이름):

- <Task 2 Step 3 에서 관측한 실제 컬럼명>

## 알려진 것

- `re.Bullets.Count` CVar 는 #43 이 만든다. 머지 전에는 `-ExecCmds` 가 경고만 찍고 무시된다 (하네스 동작 자체엔 문제 없다).
- 실제 수치 수집·리포트는 #46 의 몫이다.
```

- [ ] **Step 2: 문서와 스크립트 값이 실제로 일치하는지 확인**

Run (PowerShell):
```powershell
Select-String -Path .\scripts\profile.ps1 -Pattern 'ResX|ResY|Frames\s*=|csvCaptureFrames'
```

Expected: 스크립트의 해상도(1280x720)와 기본 프레임 수(720)가 문서 표의 값과 같다. 다르면 **문서가 아니라 실제 값에 맞춰 문서를 고친다.**

- [ ] **Step 3: 커밋**

```bash
git add docs/guides/profiling.md
git commit -m "docs(M3): pin the profiling measurement conditions (#44)

Fixes resolution, warmup cut, capture length, and the reason -nullrhi is
banned, plus how to read the three RE_Bullet* scopes in Insights.

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

## 완료 확인 (이슈 #44 완료 조건 매핑)

- [ ] 3개 프로세서에 `TRACE_CPUPROFILER_EVENT_SCOPE` 삽입, 빌드 통과 → Task 1 Step 4
- [ ] `profile.ps1 -Bullets 1000` → `.utrace` + `.csv` 생성 → Task 2 Step 2
- [ ] CSV 에 GameThread/RenderThread ms 컬럼 존재 → Task 2 Step 3
- [ ] 측정 조건이 `docs/` 에 고정 기록 → Task 3
- [ ] Insights 에서 3개 이벤트가 보이고 ms 가 읽힘 (스크린샷) → **사용자가 직접 캡처한다.** Insights 는 GUI 앱이라 에이전트가 열 수 없다. Task 2 가 만든 `trace.utrace` 를 열어 `RE_BulletSim` / `RE_BulletRender` / `RE_BulletHit` 를 확인하고 스크린샷을 이슈에 붙인다.
