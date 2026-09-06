# 구현 목표: [M3 / #44] 프로파일링 하네스 — Insights CPU 스코프 + 캡처 스크립트

## 컨텍스트

Unreal Engine 5.8 탄막 게임(`Project_RE`). Mass Entity 로 탄환 수천 발을 시뮬한다.

M3 산출물은 "프로파일 수치 스크린샷 — 면접 '왜 Mass 썼나' 질문의 증거"다. 지금 측정 인프라는 **0**이다. `stat unit`은 프레임 총합만 주고, Mass 프로세서 3개가 각각 몇 ms 쓰는지 분해할 방법이 없다.

이 goal 이 하는 것: **"실행 한 번 → 수치 파일 하나"가 나오는 재현 가능한 측정 하네스.** 프로세서 3개에 이름 있는 CPU 스코프를 넣고, 실 RHI 로 게임을 띄워 트레이스+CSV 를 수집하는 스크립트를 만들고, 측정 조건을 문서에 못 박는다.

- 이슈: **#44** / 마일스톤: **M3: 스케일 업 + 프로파일링**
- 병렬 작업 중: **#43**(`Core/` + `PatternGenerator` + `re.Bullets.Count` CVar), **#45**(신규 `Baseline/`). **둘의 파일은 절대 건드리지 않는다.**
- 실제 수치 수집·리포트는 **#46**. 이 goal 은 하네스만 만든다.

설계 스펙: `docs/superpowers/specs/2026-07-15-profiling-harness-design.md`
상세 플랜: `docs/superpowers/plans/2026-07-15-profiling-harness.md`
(참고 가능. 단 **아래 코드가 최종 정본**이다.)

## 브랜치

`dev` 에서 분기: **`feature/M3-profiling-harness`**
(워크트리 이미 존재: `E:\UnrealProjects\Project_RE-profiling`)

## 전역 제약

- 워크트리 루트: `E:\UnrealProjects\Project_RE-profiling` / 엔진: `E:\UE_5.8`
- **건드려도 되는 파일은 이것뿐이다:**
  - `Source/Project_RE/Mass/REBulletSimProcessor.cpp`
  - `Source/Project_RE/Mass/REBulletRenderProcessor.cpp`
  - `Source/Project_RE/Mass/REBulletHitProcessor.cpp`
  - `scripts/profile.ps1` (신규)
  - `docs/guides/profiling.md` (신규)
- **절대 건드리지 않는다:** `Source/Project_RE/Core/*` (특히 `REGameMode.*`, `REBossCharacter.*`), `REBulletPatternGenerator.*` (#43 소유), `Baseline/` (#45 소유), `Config/*.ini`, `Project_RE.Build.cs`
- 커스텀 프로파일러/타이머 클래스를 만들지 않는다. **CSV 파싱 코드를 쓰지 않는다.** Editor/Packaged 타깃 스위치를 만들지 않는다.
- `re.Bullets.Count` CVar 는 **아직 없다** (#43 이 만드는 중). UE 는 모르는 CVar 에 **경고만 찍고 계속 실행**한다 — 경고는 **예상된 것**이고 **#43 을 기다리지 않는다.**
- PowerShell 5.1: `&&` / `||` / 삼항 연산자 **없다**. `if ($?) { ... }` 를 쓴다.
- 빌드 커맨드 (bash):
  ```bash
  "/e/UE_5.8/Engine/Build/BatchFiles/Build.bat" Project_REEditor Win64 Development \
    -Project="E:\UnrealProjects\Project_RE-profiling\Project_RE.uproject" -WaitMutex -NoHotReload
  ```
  기대: `Result: Succeeded` / `Total execution time:` + 에러 0.

## 검증된 API (실물 확인됨)

| 항목 | 값 | 출처 |
|---|---|---|
| CPU 스코프 매크로 | `TRACE_CPUPROFILER_EVENT_SCOPE(<이름>)` — 엔진 내장 | UE 5.8 `ProfilingDebugging/CpuProfilerTrace.h` |
| 매크로 include | 기존 include 체인으로 들어오는지 **빌드로 확인한다**. 컴파일 에러 나면 그때만 `#include "ProfilingDebugging/CpuProfilerTrace.h"` 를 각 `.cpp` include 블록 끝에 추가 | 추측 금지 — 빌드가 답 |
| 프로세서 시그니처 | `void U<X>Processor::Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context)` | 소스 실물 |
| 에디터 실행 파일 | `E:\UE_5.8\Engine\Binaries\Win64\UnrealEditor.exe` | — |
| Insights 실행 파일 | `E:\UE_5.8\Engine\Binaries\Win64\UnrealInsights.exe` | — |
| 트레이스 인자 | `-trace=cpu,frame,counters` `-statnamedevents` `-tracefile=<경로>` | 엔진 내장 |
| CSV 인자 | `-csvCaptureFrames=<N>` — N프레임 채우면 csv 를 쓰지만 **게임은 계속 돈다** (자동 종료 없음) | 엔진 내장 |
| CSV 출력 경로 | `Saved\Profiling\CSV\` **로 추정** — TASK 2 게이트에서 **실제 경로를 확인하고 안 맞으면 스크립트를 고친다** | 미검증. 확인 대상 |
| CSV 컬럼명 | `GameThreadTime` / `RenderThreadTime` / `FrameTime` 계열 **로 추정** — TASK 2 게이트에서 **헤더를 직접 읽어 실제 이름을 확인**하고 그 이름을 TASK 3 문서에 적는다 | 미검증. 확인 대상 |

## 기존 파일 현황 (변경 대상)

세 프로세서 모두 계측 스코프가 **하나도 없다**.

| 프로세서 | 파일 | `Execute` 위치 | 실행 스레드 |
|---|---|---|---|
| Sim (이동/수명) | `Mass/REBulletSimProcessor.cpp` | L21 | 워커 (GT 고정 아님) |
| Render (ISM 동기화) | `Mass/REBulletRenderProcessor.cpp` | L34 | **게임 스레드 고정** (`bRequiresGameThreadExecution = true`, L25) |
| Hit (피격 판정) | `Mass/REBulletHitProcessor.cpp` | L42 | **게임 스레드 고정** (L30) |

Render 가 GT 고정이라 5000발에서 GT 병목의 최우선 용의자다. 계측 없이는 추측일 뿐이다.

`scripts/` 디렉터리는 **없다** (TASK 2 에서 새로 만든다). `docs/guides/` 는 있다 (`M0-정리.md`, `orca.md`).

================================================================
## TASK 1: 프로세서 3개에 CPU 스코프 삽입
================================================================

### 1-1. `Source/Project_RE/Mass/REBulletSimProcessor.cpp` (수정)

`Execute` 함수를 아래로 교체한다 (본문 첫 줄에 스코프 1줄 추가, 나머지 동일):

```cpp
void UREBulletSimProcessor::Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(RE_BulletSim);

	EntityQuery.ForEachEntityChunk(Context, [](FMassExecutionContext& Context)
	{
		const float Dt = Context.GetDeltaTimeSeconds();
		const int32 Num = Context.GetNumEntities();
		const TArrayView<FTransformFragment> Transforms = Context.GetMutableFragmentView<FTransformFragment>();
		const TArrayView<FBulletSimFragment> Sims       = Context.GetMutableFragmentView<FBulletSimFragment>();

		for (int32 i = 0; i < Num; ++i)
		{
			FBulletSimFragment& Sim = Sims[i];
			Transforms[i].GetMutableTransform().AddToTranslation(Sim.Velocity * Dt);
			Sim.Lifetime -= Dt;
			if (Sim.Lifetime <= 0.f)
			{
				Context.Defer().DestroyEntity(Context.GetEntity(i));
			}
		}
	});
}
```

### 1-2. `Source/Project_RE/Mass/REBulletRenderProcessor.cpp` (수정)

`Execute` 함수를 아래로 교체한다:

```cpp
void UREBulletRenderProcessor::Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(RE_BulletRender);

	UWorld* World = EntityManager.GetWorld();
	UREBulletRenderSubsystem* RS = World ? World->GetSubsystem<UREBulletRenderSubsystem>() : nullptr;
	UInstancedStaticMeshComponent* ISM = RS ? RS->GetISM() : nullptr;
	if (!ISM)
	{
		return;  // 데디서버 등 ISM 없으면 no-op
	}

	// 1) live 탄환 트랜스폼 수집 (청크를 가로질러 누적 → 전역 인스턴스 인덱스 연속).
	TArray<FTransform> Xf;
	EntityQuery.ForEachEntityChunk(Context, [&Xf](FMassExecutionContext& Ctx)
	{
		const int32 Num = Ctx.GetNumEntities();
		const TConstArrayView<FTransformFragment> T = Ctx.GetFragmentView<FTransformFragment>();
		for (int32 i = 0; i < Num; ++i)
		{
			FTransform B = T[i].GetTransform();
			B.SetScale3D(FVector(BulletScale));  // 탄환 크기 통일
			Xf.Add(B);
		}
	});

	// 2) 인스턴스 수를 M에 맞춤 (꼬리에서 add/remove → 타 인덱스 불변, swap 없음).
	const int32 M = Xf.Num();
	int32 Count = ISM->GetInstanceCount();
	while (Count < M) { ISM->AddInstance(FTransform::Identity, /*bWorldSpace=*/true); ++Count; }
	while (Count > M) { ISM->RemoveInstance(Count - 1);                               --Count; }

	// 3) i번째 인스턴스 = i번째 live 탄환. dirty flush는 마지막 1회만.
	for (int32 i = 0; i < M; ++i)
	{
		ISM->UpdateInstanceTransform(i, Xf[i], /*bWorldSpace=*/true,
			/*bMarkRenderStateDirty=*/(i == M - 1), /*bTeleport=*/true);
	}

	// 프로브: 인스턴스 수 == live 탄환 수 추종 확인 (매 30틱 1회, 로그 과다 방지).
	static int32 ProbeTick = 0;
	if (((ProbeTick++) % 30) == 0)
	{
		UE_LOG(LogTemp, Log, TEXT("[RE] RenderProbe: live=%d ISM.Count=%d"), M, ISM->GetInstanceCount());
	}
}
```

### 1-3. `Source/Project_RE/Mass/REBulletHitProcessor.cpp` (수정)

`Execute` 함수를 아래로 교체한다:

```cpp
void UREBulletHitProcessor::Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(RE_BulletHit);

	UWorld* World = EntityManager.GetWorld();
	APawn* Pawn = World ? UGameplayStatics::GetPlayerPawn(World, 0) : nullptr;
	ARECharacterBase* Player = Cast<ARECharacterBase>(Pawn);
	if (!Player)
	{
		return;  // 플레이어 없으면 no-op (레벨 전환 등)
	}

	// 대쉬 중 무적 — #25가 부여하는 State.Dashing 태그를 여기서 소비.
	// 판정 자체를 스킵(탄환 미파괴) — 대쉬는 탄막을 "통과"하지 "지우지" 않는다.
	const UAbilitySystemComponent* ASC = Player->GetAbilitySystemComponent();
	if (ASC && ASC->HasMatchingGameplayTag(RETag_State_Dashing))
	{
		// 대쉬 중엔 매 프레임 찍히므로 Verbose — 검증 시 -LogCmds="LogTemp Verbose"로 관측.
		UE_LOG(LogTemp, Verbose, TEXT("[RE] BulletHit: skipped (State.Dashing)"));
		return;
	}

	const FVector PlayerLoc = Player->GetActorLocation();

	EntityQuery.ForEachEntityChunk(Context, [&](FMassExecutionContext& Ctx)
	{
		const int32 Num = Ctx.GetNumEntities();
		const TConstArrayView<FTransformFragment> Transforms = Ctx.GetFragmentView<FTransformFragment>();

		for (int32 i = 0; i < Num; ++i)
		{
			// 탑다운 — XY 평면 거리만 비교 (탄환 Z와 캡슐 중심 Z 불일치 함정 회피)
			if (FVector::DistSquaredXY(Transforms[i].GetTransform().GetLocation(), PlayerLoc)
				<= HitRadius * HitRadius)
			{
				const float Applied = Player->TakeDamage(BulletDamage, FDamageEvent(), nullptr, nullptr);
				Ctx.Defer().DestroyEntity(Ctx.GetEntity(i));
				UE_LOG(LogTemp, Log, TEXT("[RE] BulletHit: Applied=%.0f"), Applied);
			}
		}
	});
}
```

### 1-4. 빌드 게이트

```bash
"/e/UE_5.8/Engine/Build/BatchFiles/Build.bat" Project_REEditor Win64 Development \
  -Project="E:\UnrealProjects\Project_RE-profiling\Project_RE.uproject" -WaitMutex -NoHotReload
```

기대 출력: `Result: Succeeded` + `Total execution time:` (에러 0).

**컴파일 에러 `TRACE_CPUPROFILER_EVENT_SCOPE: identifier not found` 가 나면**, 세 `.cpp` **각각**의 include 블록 마지막 줄 뒤에 아래 1줄을 추가하고 다시 빌드한다:

```cpp
#include "ProfilingDebugging/CpuProfilerTrace.h"
```

에러가 안 나면 include 를 **추가하지 않는다** (불필요한 줄 금지).

### 1-5. 커밋

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

================================================================
## TASK 2: 캡처 스크립트 `scripts/profile.ps1`
================================================================

### 2-1. `scripts/profile.ps1` (신규)

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

### 2-2. 실행 게이트 — 하네스가 실제로 산출물을 뱉는가

PowerShell:
```powershell
.\scripts\profile.ps1 -Bullets 1000
```

기대:
- 게임 창이 1280x720 으로 **보인다** (실 RHI — 창이 뜨는 게 정상)
- 로그에 `re.Bullets.Count` 를 모르는 CVar 라는 **경고** → **예상된 것** (#43 미머지). 무시한다.
- 마지막에:
  ```
  [profile] 산출물:
    frames.csv    <0 아닌 bytes>
    trace.utrace  <0 아닌 bytes>
  E:\UnrealProjects\Project_RE-profiling\Saved\Profiling\RE_1000_<타임스탬프>
  ```

**csv 가 안 나오면** (경로 추정이 틀린 경우) — 추측하지 말고 실제 위치를 찾는다:
```powershell
Get-ChildItem -Path .\Saved -Recurse -Filter *.csv | Select-Object FullName, LastWriteTime
```
찾은 실제 경로로 스크립트의 `$CsvDir` 를 고치고 재실행한다.

### 2-3. CSV 컬럼 게이트 — GameThread / RenderThread ms 컬럼 존재 확인

PowerShell (`<RunDir>` = 2-2 가 출력한 경로):
```powershell
(Get-Content "<RunDir>\frames.csv" -TotalCount 1) -split ',' | Where-Object { $_ -match 'Thread|Frame|GPU' }
```

기대: `GameThreadTime` / `RenderThreadTime` / `FrameTime` 계열 컬럼명 출력.

**출력된 실제 컬럼명을 그대로 받아 적는다** — TASK 3 문서의 "CSV 컬럼" 절에 이 이름을 박는다. **엔진 버전마다 다를 수 있으므로 추측해서 쓰지 않는다.**

### 2-4. 커밋

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

================================================================
## TASK 3: 측정 조건 문서 `docs/guides/profiling.md`
================================================================

### 3-1. `docs/guides/profiling.md` (신규)

아래 전문을 그대로 쓴다. **단 "CSV 컬럼" 절의 이름은 TASK 2-3 에서 실제로 관측한 것으로 채운다** (아래 값은 미검증 추정).

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

`frames.csv` 의 프레임 시간 컬럼 (엔진 CSV 프로파일러가 찍는 **실제** 이름):

- (TASK 2-3 에서 관측한 이름을 여기에 적는다)

## 알려진 것

- `re.Bullets.Count` CVar 는 #43 이 만든다. 머지 전에는 `-ExecCmds` 가 경고만 찍고 무시된다 (하네스 동작 자체엔 문제 없다).
- 실제 수치 수집·리포트는 #46 의 몫이다.
```

### 3-2. 정합성 게이트 — 문서 값 == 스크립트 값

```powershell
Select-String -Path .\scripts\profile.ps1 -Pattern 'ResX|ResY|Frames\s*=|csvCaptureFrames'
```

기대: 해상도 `1280x720`, 기본 프레임 `720` 이 문서 표의 값과 일치. 다르면 **실제 값에 맞춰 문서를 고친다** (반대 아님).

### 3-3. 커밋

```bash
git add docs/guides/profiling.md
git commit -m "docs(M3): pin the profiling measurement conditions (#44)

Fixes resolution, warmup cut, capture length, and the reason -nullrhi is
banned, plus how to read the three RE_Bullet* scopes in Insights.

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

## 완료 후

### Insights 스크린샷 (사용자 몫)

Insights 는 GUI 앱이라 에이전트가 열 수 없다. **사용자가 직접** `trace.utrace` 를 열어 `RE_BulletSim` / `RE_BulletRender` / `RE_BulletHit` 3개 이벤트와 각 ms 가 보이는 스크린샷을 찍어 이슈 #44 에 붙인다.

### PR

- **base: `dev`** (main 아님)
- 이슈 #44 의 메타를 그대로 미러링: **label**(`architecture`, `C++`), **milestone**(`M3: 스케일 업 + 프로파일링`), **assignee**(`leejimin3`), **project**(`Project_RE 개발 로드맵`). Reviewer 는 생략.
- 본문 구조 (이 저장소 관행):
  ```
  ## 요약
  ## 변경사항
  ## 이슈링크
  Closes #44
  ## 검증
  ```
- 검증 섹션에는 **실제 관측한 것만** 적는다: 빌드 `Result: Succeeded`, run 폴더에 나온 `trace.utrace` / `frames.csv` 크기, CSV 헤더에서 본 실제 컬럼명. `re.Bullets.Count` 경고는 #43 미머지 때문임을 명시.

## 하지 말 것 (스코프 밖)

- **실제 수치 수집·리포트** — #46
- **최적화** — 측정 전 최적화 금지
- **`re.Bullets.Count` CVar 를 직접 만들기** — #43 소유. 없다고 만들지 마라. 경고 뜨는 게 정상이다.
- **`Core/*`, `REBulletPatternGenerator.*`, `Baseline/`, `Config/*.ini`, `Build.cs` 수정** — 타 이슈 소유 / 불필요
- **CSV 파싱·트리밍 코드** — 하네스는 수집만 한다
- **커스텀 프로파일러/타이머 클래스, Insights 대체 UI** — 엔진 내장으로 충분
- **자동 회귀 감지 / CI 성능 게이트** — 1인 프로젝트에 과함
- **Editor/Packaged 타깃 스위치** — YAGNI
