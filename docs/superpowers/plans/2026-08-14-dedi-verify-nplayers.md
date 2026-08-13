# dedi-verify N인 판정 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** `scripts/dedi-verify.ps1 -Clients N` 이 N≥2에서 실제로 N인을 판정하게 하고, #85·#86이 손으로 돌리던 2인 게임 검증을 스크립트로 흡수한다 (#87).

**Architecture:** 헤드리스 프로브의 종료 결정을 개별 PlayerController에서 GameMode로 옮겨 "N개 완주 후 종료"로 게이팅한다(스크립트만으로는 불가능 — 첫 `RequestExit` 이 서버를 내려 뒤 프로브가 시작조차 못 한다). 스크립트는 모드를 둘로 나눈다: 프로브 모드(`-unattended` 켬, 빠름)와 결과 모드(`-unattended` 끔, 순수 전투). 판정은 "있나"에서 "몇 개인가"로 바뀐다.

**Tech Stack:** UE 5.8, C++ (GameMode 통지 패턴), PowerShell 5.1.

설계 스펙: `docs/superpowers/specs/2026-08-14-dedi-verify-nplayers-design.md`

## Global Constraints

- 브랜치: `feature/M5-dedi-verify-nplayers` (이미 생성됨, 스펙 커밋 1건 있음)
- ⚠️ **본체 작업트리(`E:\UnrealProjects\Project_RE`)에서 실행 전제.** 아래 명령의 `-Project` 가 절대경로다. 격리 worktree에서 그대로 돌리면 worktree가 아니라 본체를 빌드하고, 캐시 때문에 수 초 만에 `Succeeded` 가 떠서 검증이 통과한 것처럼 보이지만 아무것도 검증하지 않는다.
- 빌드 게이트:
  ```powershell
  $BB = "E:\UnrealEngine-5.8\UnrealEngine-5.8\Engine\Build\BatchFiles\Build.bat"
  $UP = "E:\UnrealProjects\Project_RE\Project_RE.uproject"
  & $BB Project_REEditor Win64 Development -Project="$UP" -WaitMutex -NoHotReload
  & $BB Project_REServer Win64 Development -Project="$UP" -WaitMutex -NoHotReload
  ```
  기대: `Result: Succeeded`. 각 ~90초, 타임아웃 400000ms.
- **에디터가 켜져 있으면 빌드가 실패한다** (`Unable to build while Live Coding is active`). 닫아둔 상태 유지.
- **데디 실행 전에는 재쿡이 필요하다:**
  ```powershell
  & "E:\UnrealEngine-5.8\UnrealEngine-5.8\Engine\Build\BatchFiles\RunUAT.bat" BuildCookRun `
    -project="$UP" -noP4 -platform=Win64 -server -noclient -serverconfig=Development `
    -cook -stage -pak -skipbuild -utf8output
  ```
  `-skipbuild` 를 빼먹지 마라. 약 2분, 타임아웃 500000ms.
- 자동화 테스트 인프라 없음 → 게이트 = 빌드 + 스크립트 `-SelfTest` + 실 데디.
- **`.ps1` 은 UTF-8 BOM으로 저장한다.** Windows PowerShell 5.1은 BOM 없는 파일을 ANSI로 읽어 한글이 깨지고, 깨진 바이트가 따옴표 짝을 무너뜨려 파싱 자체가 실패한다(#82에서 실제로 밟았다). 기존 파일을 편집만 하면 BOM은 유지된다 — 새로 쓰지 마라.
- 로그 접두어 `[RE]` 고정.
- Git Bash에서 UE 실행 시 `MSYS_NO_PATHCONV=1` 필수.
- `Project_RE.uproject` 는 **절대 스테이징하지 않는다** — `EngineAssociation` GUID가 머신 종속이다. `git status` 에 수정으로 뜨는 것이 정상.
- 주석은 한국어. 주변 스타일에 맞춘다.
- 커밋 메시지 끝에 `Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>`.
- YAGNI: 스펙에 없는 기능·추상화·설정값 추가 금지.

## File Structure

| 파일 | 책임 | 변경 |
|---|---|---|
| `Core/REGameMode.h` / `.cpp` | 프로브 완주 카운트 + 전원 완주 시 프로세스 종료 | 수정 |
| `Core/REPlayerController.cpp` | 프로브가 종료 대신 GameMode에 통지 | 수정 |
| `scripts/dedi-verify.ps1` | 판정 엔진 모드 분리 + 개수 판정 + 결과 모드 실행 | 수정 — 이 이슈의 본체 |
| `docs/guides/dedicated-server.md` | 모순된 지시 2개와 수동 페어 절차 제거 | 수정 |

새 파일 없음.

---

### Task 1: 종료 결정을 GameMode로 이관

`REPlayerController.cpp` 의 대쉬 프로브가 컨트롤러마다 무조건 `RequestExit` 을 부른다. 먼저 끝난 하나가 서버를 내려 뒤 클라의 프로브는 시작조차 못 한다.

**Files:**
- Modify: `Source/Project_RE/Core/REGameMode.h`
- Modify: `Source/Project_RE/Core/REGameMode.cpp`
- Modify: `Source/Project_RE/Core/REPlayerController.cpp`

**Interfaces:**
- Produces: `AREGameMode::NotifyProbeComplete()`
- Produces: 서버 로그 `[RE] Probe complete N/M` 과 `[RE] All probes done — exiting` — Task 2의 판정이 `[Dash] probe done` 개수를 세므로 이 두 줄 자체는 판정 대상이 아니지만 실패 진단에 쓰인다.

- [ ] **Step 1: 헤더 — 진입점과 카운터**

`REGameMode.h` 의 `public:` 블록, `NotifyPlayerDied` 선언 **아래**에 추가:

```cpp
	/**
	 *  서버측 헤드리스 프로브 완주 통지 (#87). 전원 완주 시 프로세스를 종료한다.
	 *  종료 결정이 개별 PC가 아니라 여기 있는 이유: PC는 자기 프로브만 알기 때문에,
	 *  먼저 끝난 하나가 서버를 내리면 뒤 클라의 프로브는 시작조차 못 한다.
	 */
	void NotifyProbeComplete();
```

`private:` 블록의 `bFiringStarted` **아래**에 추가:

```cpp
	/**
	 *  완주한 서버측 프로브 수 (#87). ReadyPlayers처럼 TSet이 아니라 카운터인 이유:
	 *  프로브 완주는 서버 자신의 타이머가 컨트롤러당 정확히 1회 발화시키므로 중복 경로가 없다.
	 */
	int32 CompletedProbes = 0;
```

- [ ] **Step 2: cpp — include 추가**

`REGameMode.cpp` include 블록에 추가:

```cpp
#include "HAL/PlatformMisc.h"
```

- [ ] **Step 3: cpp — 구현 추가**

`REGameMode.cpp` 의 `Logout` 함수 **아래**에 추가:

```cpp
void AREGameMode::NotifyProbeComplete()
{
	++CompletedProbes;
	const int32 Expected = FMath::Max(1, CVarExpectedPlayers.GetValueOnGameThread());
	UE_LOG(LogTemp, Log, TEXT("[RE] Probe complete %d/%d"), CompletedProbes, Expected);

	if (CompletedProbes >= Expected)
	{
		UE_LOG(LogTemp, Log, TEXT("[RE] All probes done — exiting"));
		// 헤드리스 프로세스 자체 종료(결정적 실행). 전원 완주 후에만 부른다.
		FPlatformMisc::RequestExit(false);
	}
}
```

`CVarExpectedPlayers` 는 이 파일 상단 익명 네임스페이스에 이미 있는 CVar다(#85). 재선언하지 마라.

- [ ] **Step 4: PlayerController — 종료 대신 통지**

`REPlayerController.cpp` 의 대쉬 프로브 마지막 람다에서 아래 세 줄을

```cpp
			UE_LOG(LogTemp, Log, TEXT("[Dash] probe done — exiting"));
			// headless 프로세스 자체 종료(결정적 실행).
			FPlatformMisc::RequestExit(false);
```

아래로 교체한다:

```cpp
			UE_LOG(LogTemp, Log, TEXT("[Dash] probe done"));
			// 종료 결정은 GameMode가 한다 (#87). 이 PC는 자기 프로브만 알아서,
			// 먼저 끝난 하나가 서버를 내리면 뒤 클라의 프로브가 시작조차 못 한다.
			if (AREGameMode* GM = GetWorld() ? GetWorld()->GetAuthGameMode<AREGameMode>() : nullptr)
			{
				GM->NotifyProbeComplete();
			}
```

로그 문구에서 `— exiting` 을 뺀 것은 이제 이 PC가 종료하지 않기 때문이다. `dedi-verify.ps1` 의 기존 판정 정규식은 `\[Dash\] probe done` 이라 계속 매칭된다.

`REGameMode.h` include 는 이 파일에 이미 있다(`Server_NotifyReady_Implementation` 이 쓴다). 없으면 추가하라.

- [ ] **Step 5: 빌드 게이트 (Editor + Server)**

기대: 양쪽 `Result: Succeeded`.

- [ ] **Step 6: 1인 회귀 프로브 — 여전히 자체 종료하는가**

`ExpectedPlayers` 기본 1이므로 첫 완주가 곧 전원 완주다. 종료 시점이 종전과 같아야 한다.

```bash
MSYS_NO_PATHCONV=1 "E:/UnrealEngine-5.8/UnrealEngine-5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" \
  "E:\UnrealProjects\Project_RE\Project_RE.uproject" /Game/Level/Main \
  -game -nullrhi -unattended -nosplash -stdout -NoSound -abslog="E:\UnrealProjects\Project_RE\Saved\Logs\RE_87_t1.log"
```

**프로세스가 스스로 종료해야 한다.** 30초 안에 안 죽으면 게이팅이 1인에서도 막고 있는 것이다.

```bash
grep -n "Probe complete\|All probes done\|\[Dash\] probe done" Saved/Logs/RE_87_t1.log
```

기대: `[Dash] probe done` → `[RE] Probe complete 1/1` → `[RE] All probes done — exiting`.

- [ ] **Step 7: 커밋**

```bash
git add Source/Project_RE/Core/REGameMode.h Source/Project_RE/Core/REGameMode.cpp Source/Project_RE/Core/REPlayerController.cpp
git commit -m "feat(verify): 헤드리스 프로브 종료를 전원 완주 게이팅으로 전환 (#87)"
```

---

### Task 2: 판정 엔진 — 개수 판정과 모드 분리

판정이 "패턴이 있나"만 본다. N인에서는 **몇 개인가**가 핵심이다. 그리고 결과 모드는 프로브를 끄므로 프로브가 만들어내던 판정들이 반드시 실패한다 — 모드별로 갈라야 한다.

**Files:**
- Modify: `scripts/dedi-verify.ps1`

**Interfaces:**
- Produces: `Assert-Count` 함수
- Produces: `Invoke-Verdict` 가 `-Mode` ('Probe'|'Outcome') 와 `-ClientCount` 를 받는다 — Task 3의 실행부가 호출한다.

- [ ] **Step 1: `Assert-Count` 추가**

`Assert-Range` 함수 **아래**에 추가:

```powershell
function Assert-Count {
    <# 정확히 N건이어야 성공. N인 판정의 핵심 — "있나"가 아니라 "몇 개인가"를 본다. #>
    param(
        [string]$Scope,
        [string]$Desc,
        [string[]]$Lines,
        [string]$Pattern,
        [int]$Expected
    )
    $n = @($Lines | Select-String -Pattern $Pattern).Count
    if ($n -eq $Expected) {
        Write-Host ("  [PASS] {0}: {1} ({2}건)" -f $Scope, $Desc, $n)
    } else {
        # 기대치를 함께 출력해야 실패 원인이 보인다 — "적다"인지 "많다"인지가 원인을 가른다.
        Write-Host ("  [FAIL] {0}: {1} — {2}건, 기대 {3}건" -f $Scope, $Desc, $n, $Expected) -ForegroundColor Red
        $script:Failures += ("{0}: {1} — {2}건, 기대 {3}건" -f $Scope, $Desc, $n, $Expected)
    }
}
```

- [ ] **Step 2: `Invoke-Verdict` 전문 교체**

기존 `Invoke-Verdict` 함수를 아래로 대체한다:

```powershell
function Invoke-Verdict {
    <#
      서버 로그 1개 + 클라 로그 N개를 받아 전체 판정. 프로세스와 무관한 순수 함수라 SelfTest가 가능하다.
      모드에 따라 적용 판정이 다르다 (#87) — 결과 모드는 -unattended 없이 돌아 프로브가 만들어내던
      로그(몽타주·대쉬 거리·이동 프로브)가 아예 없기 때문이다. 그대로 적용하면 항상 빨간불이 된다.
    #>
    param(
        [string[]]$ServerLines,
        [hashtable]$ClientLines,
        [bool]$CheckVictory,
        [ValidateSet('Probe', 'Outcome')][string]$Mode = 'Probe',
        [int]$ClientCount = 1
    )

    Write-Host "`n[dedi-verify] 판정 (mode=$Mode clients=$ClientCount)"

    # --- 공통: 기동과 권위 발사는 두 모드 모두에서 성립해야 한다.
    Assert-Log 'server' '넷드라이버 리스닝'   $ServerLines 'IpNetDriver listening'
    Assert-Log 'server' '월드 기동'          $ServerLines 'Bringing World /Game/Level/Main\.Main'
    # 보스 첫 볼리는 Spiral/Fan(FireDirect) 또는 Artillery(FireArtillery) 중 랜덤이라 둘 다 인정한다 (#84).
    Assert-Log 'server' '탄막 발사(권위)'    $ServerLines '\[RE\] Boss Fire(Direct|Artillery):.*role=ROLE_Authority'
    Assert-Log 'server' '크래시 없음'         $ServerLines 'Assertion failed|Critical error' -Expect Absent

    if ($Mode -eq 'Probe') {
        # 프로브가 클라 수만큼 완주했는가 — 이 이슈의 핵심 판정 (#87).
        Assert-Count 'server' '프로브 완주'      $ServerLines '\[Dash\] probe done'   $ClientCount
        Assert-Count 'server' '이동 프로브 기동'  $ServerLines '\[Move\] probe start'  $ClientCount
        Assert-Count 'server' '대쉬 거리 측정'    $ServerLines '\[Dash\] dist='        $ClientCount
        Assert-Range 'server' '대쉬 이동거리'     $ServerLines '\[Dash\] dist=([0-9.]+)' 500 700

        # 이동 프로브는 오프메시 거부 경로 확인용으로 맵 밖 좌표(100000)를 일부러 1회 요청한다.
        # 그 좌표를 지목한 거부는 정상이고 오히려 거부 경로가 살아있다는 증거 — 판정에서 제외한다.
        # 제외하지 않으면 이 판정은 항상 실패한다.
        $realTargetLines = @($ServerLines | Where-Object { $_ -notmatch '100000' })
        Assert-Log 'server' 'NavMesh 거부 없음(실목표)' $realTargetLines '\[Move\] rejected: off-navmesh' -Expect Absent

        # 코스메틱은 NM_DedicatedServer 가드로 생략되어야 한다 (#74/#75).
        # 결과 모드에서는 재생을 시도할 계기 자체가 없어 부재가 가드 덕인지 구분 불가 → 프로브 모드 전용.
        Assert-Log 'server' '발사 몽타주 생략'    $ServerLines '\[Attack\] fire montage' -Expect Absent
        Assert-Log 'server' '대쉬 몽타주 생략'    $ServerLines '\[Dash\] anim len='      -Expect Absent
    }
    else {
        # --- 결과 모드: 게이트 → 스폰 이격 → 전원 사망 → 승패 확정 (#85/#86이 손으로 보던 것).
        Assert-Log   'server' '발사 시작(전원 준비)' $ServerLines ("\[RE\] Boss firing started \({0}/{0} ready\)" -f $ClientCount)
        Assert-Count 'server' '스폰 로그'           $ServerLines '\[RE\] Spawn player idx=' $ClientCount

        # 오프셋이 서로 달라야 겹치지 않는다 (#54 겹침 즉사 전례).
        $offsets = @($ServerLines |
            Select-String -Pattern '\[RE\] Spawn player idx=\d+ offsetY=(-?[0-9.]+)' |
            ForEach-Object { $_.Matches[0].Groups[1].Value })
        $distinct = @($offsets | Select-Object -Unique).Count
        if ($distinct -eq $ClientCount) {
            Write-Host ("  [PASS] server: 스폰 이격 상이 ({0}종)" -f $distinct)
        } else {
            Write-Host ("  [FAIL] server: 스폰 이격 상이 — 서로 다른 값 {0}종, 기대 {1}종 (값: {2})" -f $distinct, $ClientCount, ($offsets -join ',')) -ForegroundColor Red
            $script:Failures += ("server: 스폰 이격 상이 — {0}종, 기대 {1}종" -f $distinct, $ClientCount)
        }

        Assert-Log 'server' '전원 사망'   $ServerLines ("\[RE\] All {0} players dead" -f $ClientCount)
        Assert-Log 'server' '승패 확정'   $ServerLines '\[RE\] EndGame: DEFEAT'
    }

    # --- 클라: 접속 유지 구간만 본다 (#82 — 접속 종료 후 폴백 월드 로그가 섞인다).
    foreach ($name in ($ClientLines.Keys | Sort-Object)) {
        $span = Get-ConnectedSpan $ClientLines[$name]

        Assert-Log $name '탄막 수신·스폰' $span '\[RE\] Boss Fire(Direct|Artillery):.*role=ROLE_SimulatedProxy'
        Assert-Log $name '크래시 없음'    $span 'Assertion failed|Critical error' -Expect Absent

        if ($Mode -eq 'Probe') {
            # 이 둘은 서버측 프로브가 발사·대쉬를 유발해야 찍힌다 → 결과 모드에는 없다.
            Assert-Log $name '발사 몽타주 재생'      $span '\[Attack\] fire montage len='
            Assert-Log $name '대쉬 몽타주 재생(오너)' $span '\[Dash\] anim len=.*role=ROLE_AutonomousProxy'
        }
        else {
            Assert-Log $name '결과 화면 도달' $span '\[RE\] Client_ShowResult: DEFEAT'
        }

        if ($CheckVictory) {
            Assert-Log $name '결과 화면(VICTORY)' $span '\[RE\] Client_ShowResult: VICTORY'
        }
    }
}
```

- [ ] **Step 3: SelfTest 합성 로그 갱신**

`-SelfTest` 블록의 `$goodServer` 를 아래로 대체한다(프로브 모드 판정이 요구하는 줄을 모두 담는다):

```powershell
    $goodServer = @(
        'LogNet: IpNetDriver listening on port 7777',
        'LogWorld: Bringing World /Game/Level/Main.Main up for play',
        'LogTemp: [Move] probe start: pawn=X=0 target=X=0',
        'LogTemp: [Dash] dist=602.4 (기대 ~600)',
        'LogTemp: [RE] Boss FireDirect: Pattern=0 Angle=0.0 N=16 Elapsed=0.000 role=ROLE_Authority',
        'LogTemp: [Dash] probe done'
    )
```

`$goodClient` 는 그대로 둔다.

`Invoke-Verdict` 호출 두 곳에 모드와 클라 수를 넘기도록 인자를 추가한다 — 기존 호출은 `-Mode Probe -ClientCount 1` 이다.

- [ ] **Step 4: SelfTest에 개수 부족 검출 추가**

기존 "고장 로그" 블록 **아래**에 추가한다. `Assert-Count` 가 실제로 개수 부족을 잡는지 확인하는 것이 목적이다 — 통과만 확인하면 항상-PASS 회귀를 못 잡는다.

```powershell
    # Assert-Count 가 개수 부족을 잡는지 — 클라 2인데 프로브 완주가 1건뿐인 상황을 합성한다.
    # 이것이 바로 #87 이전의 실제 증상이다(첫 프로브가 서버를 내려 뒤 프로브가 안 돎).
    $script:Failures = @()
    Invoke-Verdict -ServerLines $goodServer -ClientLines @{ 'client1' = $goodClient; 'client2' = $goodClient } `
                   -CheckVictory $false -Mode Probe -ClientCount 2
    if (-not ($script:Failures -match '프로브 완주')) { throw 'SelfTest: 프로브 완주 개수 부족을 잡아내지 못했다' }
```

- [ ] **Step 5: SelfTest 실행**

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\dedi-verify.ps1 -SelfTest
```

기대: `[dedi-verify] SelfTest OK`, 종료 코드 0. 정상 로그 통과 + 고장 로그 검출 + **개수 부족 검출**까지 세 가지가 모두 확인돼야 한다.

- [ ] **Step 6: 커밋**

```bash
git add scripts/dedi-verify.ps1
git commit -m "feat(verify): 판정을 개수 기반으로 + 모드별 판정 집합 분리 (#87)"
```

---

### Task 3: 실행부 — 결과 모드와 N인 기동

**Files:**
- Modify: `scripts/dedi-verify.ps1`

**Interfaces:**
- Consumes: `Invoke-Verdict -Mode -ClientCount` (Task 2)
- Consumes: 서버가 N개 프로브 완주 시 자체 종료 (Task 1)

- [ ] **Step 1: 파라미터 추가**

`param(...)` 블록의 `$SelfTest` **위**에 추가:

```powershell
    # 결과 모드: -unattended 없이 순수 전투를 돌려 게이트·스폰이격·전원사망·결과화면을 판정한다.
    # 프로브 모드(기본)와 타이밍이 양립하지 않아 분리했다 — 프로브 완주 ~4.1초, 전멸 ~5.3초 (#87).
    [switch]$Outcome,
    # 결과 모드에서 EndGame 대기 상한(초).
    [int]$OutcomeTimeoutSec = 120,
```

- [ ] **Step 2: 서버 기동 인자 — 모드 반영**

서버 `Start-Process` 의 `-ArgumentList` 를 아래로 대체한다:

```powershell
    # 협동 인원은 CVar로 넘긴다 — ini면 pak 안이라 인원을 바꿀 때마다 재쿡해야 한다 (#85).
    # -unattended 는 프로브 모드에서만: 결과 모드는 프로브가 플레이어를 +X로 대쉬시켜
    # 보스 탄막 레인(X=600)으로 밀어넣어(#56) 측정 대상인 사망 타이밍을 교란한다.
    $srvArgs = @('-log', "-port=$Port", "-ExecCmds=`"re.Coop.ExpectedPlayers $Clients`"", "-abslog=`"$ServerLog`"")
    if (-not $Outcome) { $srvArgs += '-unattended' }
    $srv = Start-Process $Server -PassThru -WindowStyle Hidden -ArgumentList $srvArgs
```

- [ ] **Step 3: 클라 기동 — 결과 모드는 순차 투입**

클라 기동 `for` 루프를 아래로 대체한다:

```powershell
    $ClientLogs = @{}
    for ($i = 1; $i -le $Clients; $i++) {
        $log = Join-Path $RunDir "client$i.log"
        $ClientLogs["client$i"] = $log
        $Procs += Start-Process $Editor -PassThru -WindowStyle Hidden -ArgumentList @(
            "`"$Uproject`"", "127.0.0.1:$Port", '-game', '-nullrhi', '-unattended', '-log', "-abslog=`"$log`""
        )
        Write-Host "[dedi-verify] client$i 접속 요청"

        # 결과 모드는 "클라 N-1명만으로는 발사가 시작되지 않는다"를 판정한다 —
        # 그 상태가 실제로 존재하려면 순차 투입이어야 한다. 고정 대기가 아니라 로그로 동기화한다:
        # 느린 머신에서 Start-Sleep 은 조용히 어긋난다(#82가 리스닝 대기를 폴링으로 바꾼 것과 같은 이유).
        if ($Outcome -and $i -lt $Clients) {
            $readyDeadline = (Get-Date).AddSeconds(60)
            while ((Get-Date) -lt $readyDeadline) {
                if ((Select-String -Path $ServerLog -Pattern ("\[RE\] Player ready {0}/" -f $i) -Quiet)) { break }
                Start-Sleep -Milliseconds 300
            }
            Write-Host "[dedi-verify] client$i ready 확인 — 다음 클라 투입"
        }
    }
```

- [ ] **Step 4: 종료 대기 — 모드별로**

기존 `$srv.WaitForExit(...)` 블록을 아래로 대체한다:

```powershell
    if ($Outcome) {
        # 결과 모드: 서버가 스스로 죽지 않는다(-unattended 없음). EndGame 을 보고 정리한다.
        $endDeadline = (Get-Date).AddSeconds($OutcomeTimeoutSec)
        $ended = $false
        while ((Get-Date) -lt $endDeadline) {
            if ($srv.HasExited) { break }
            if (Select-String -Path $ServerLog -Pattern '\[RE\] EndGame:' -Quiet) { $ended = $true; break }
            Start-Sleep -Milliseconds 500
        }
        if (-not $ended) {
            throw "결과 타임아웃 ${OutcomeTimeoutSec}s — EndGame 미발생. 로그: $ServerLog"
        }
        Write-Host '[dedi-verify] EndGame 확인'
        Start-Sleep -Seconds 3   # 결과 RPC가 클라 로그에 도달할 여유
    }
    else {
        # 프로브 모드: 서버측 프로브가 전원 완주하면 GameMode가 RequestExit 한다 (#87).
        if (-not $srv.WaitForExit($ProbeTimeoutSec * 1000)) {
            throw "프로브 타임아웃 ${ProbeTimeoutSec}s — 서버가 스스로 종료하지 않았다. 로그: $ServerLog"
        }
        Write-Host '[dedi-verify] 서버 자체 종료 확인 (전원 프로브 완주)'
        Start-Sleep -Seconds 3
    }
```

기존 블록에 달려 있던 `ponytail:` 주석(첫 클라 완주가 서버를 내린다는 천장 설명)은 **삭제한다** — Task 1이 그 천장을 없앴다.

- [ ] **Step 5: 판정 호출에 모드 전달**

파일 끝의 `Invoke-Verdict` 호출을 아래로 대체한다:

```powershell
$mode = if ($Outcome) { 'Outcome' } else { 'Probe' }
Invoke-Verdict -ServerLines $serverLines -ClientLines $clientLines -CheckVictory $Victory.IsPresent `
               -Mode $mode -ClientCount $Clients
```

- [ ] **Step 6: 헤더 주석·사용 예시 갱신**

파일 상단 `.DESCRIPTION` 의 "서버 프로브가 마지막에 RequestExit 하므로 서버는 클라 접속 후 약 4.4초에 스스로 종료한다" 문장을 "서버측 프로브가 **전원 완주**하면 GameMode가 종료한다(#87)"로 고친다.

`.EXAMPLE` 에 결과 모드를 추가한다:

```
  scripts/dedi-verify.ps1 -Clients 2            # 프로브 모드 — 이동·발사·대쉬·NavMesh
  scripts/dedi-verify.ps1 -Clients 2 -Outcome   # 결과 모드 — 게이트·스폰이격·전원사망·결과화면
```

`-Clients` 파라미터 주석 `# 접속시킬 클라 개수 (M5 N인 협동 대비). 기본 1.` 에서 "대비"를 지운다 — 이제 실제로 동작한다. (`# 접속시킬 클라 개수. 기본 1.`)

- [ ] **Step 7: SelfTest 회귀**

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\dedi-verify.ps1 -SelfTest
```

기대: `SelfTest OK`, 종료 코드 0. 실행부만 바꿨으므로 판정은 그대로 통과해야 한다.

- [ ] **Step 8: 커밋**

```bash
git add scripts/dedi-verify.ps1
git commit -m "feat(verify): 결과 모드 추가 + N인 순차 투입·모드별 종료 신호 (#87)"
```

---

### Task 4: 실측 + 가이드 정리

**Files:**
- Modify: `docs/guides/dedicated-server.md`

- [ ] **Step 1: 재쿡**

Global Constraints의 `BuildCookRun` 실행. **빼먹으면 옛 산출물을 검증한다.**

- [ ] **Step 2: 1인 회귀 — 프로브 모드**

```powershell
scripts\dedi-verify.ps1
```

기대: 전 항목 PASS, `EXIT=0`. 여기가 깨지면 나머지를 볼 것도 없다.

- [ ] **Step 3: 2인 프로브 모드 — 이 이슈의 핵심**

```powershell
scripts\dedi-verify.ps1 -Clients 2
```

기대: `프로브 완주 (2건)`, `이동 프로브 기동 (2건)`, `대쉬 거리 측정 (2건)` 이 모두 PASS.

**`프로브 완주 — 1건, 기대 2건` 이 나오면 Task 1의 게이팅이 동작하지 않는 것이다.** 그것이 #87 이전의 증상 그대로다.

- [ ] **Step 4: 2인 결과 모드**

```powershell
scripts\dedi-verify.ps1 -Clients 2 -Outcome
```

기대: `발사 시작(전원 준비)`, `스폰 로그 (2건)`, `스폰 이격 상이 (2종)`, `전원 사망`, `승패 확정`, 그리고 **양 클라의 `결과 화면 도달`** 이 모두 PASS.

- [ ] **Step 5: 실패 경로 확인 — 종료 코드가 실제로 갈리는가**

통과만 확인하면 항상-PASS 회귀를 못 잡는다. 실패가 실제로 종료 코드로 갈리는지 본다.

```powershell
scripts\dedi-verify.ps1 -Clients 2 -OutcomeTimeoutSec 5
```

5초는 전투가 끝나기에 턱없이 짧다(#86 실측 TTK 5.3초 + 접속·기동 시간).

기대: `결과 타임아웃 5s — EndGame 미발생` 으로 던지고 **종료 코드가 0이 아니다.** 타임아웃이 조용히 초록불로 끝나면 안 된다.

`$LASTEXITCODE` 를 함께 출력해 확인하라:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\dedi-verify.ps1 -Clients 2 -OutcomeTimeoutSec 5; "EXIT=$LASTEXITCODE"
```

- [ ] **Step 6: 가이드 정리**

`docs/guides/dedicated-server.md` 에서:

1. **"2인 수동 페어 검증 (#85, 자동화는 #87 몫)" 절을 통째로 제거**하고, 두 모드 사용법으로 대체한다. 그 절의 판정 4개는 이제 `-Outcome` 이 자동으로 본다.
2. **NavMesh 재검증 문단의 "이번엔 서버에 `-unattended` 를 주고 …" 지시를 제거**한다. 프로브 모드가 N개 이동 프로브를 모두 돌리므로 특별 절차가 필요 없다.
3. `-Clients 2` 천장 문단(있다면) 제거 — Task 1이 해소했다.
4. 판정 항목 표에 모드 구분을 반영한다.

**보존할 것:** `#86에서 해소됨` 문단의 실측 기록(1580건 중 1건 등), 곱사탄 구조적 미관측 사유, 궤도 일치 측정 절차(#84). 이 이슈가 무효화하지 않는 기록이다.

- [ ] **Step 7: 커밋**

```bash
git add docs/guides/dedicated-server.md
git commit -m "docs(verify): 수동 페어 절차 제거 + 두 모드 사용법으로 대체 (#87)"
```

---

## 완료 후

### PR

```bash
git push -u origin feature/M5-dedi-verify-nplayers
```

PR 규칙: **base=dev**, 이슈 #87 메타 미러링 — label `enhancement`,`networking` / milestone `M5: 협동 멀티 (N명) + 시드 탄막 + 서버권위 피격` / assignee `leejimin3` / project `Project_RE 개발 로드맵`. **Reviewer 생략.** 본문 6개 필드:

1. 요약
2. 변경사항
3. 이슈링크 (`Closes #87` — dev 머지로는 자동 종료가 안 되므로 머지 후 `gh issue close 87` 수동)
4. 검증 — Task 4의 각 모드 출력 발췌. 특히 `프로브 완주 (2건)` 과 양 클라 `결과 화면 도달`
5. 스코프 제외
6. 참고

### 남는 의도된 한계 (후속 몫 — 건드리지 말 것)

- **VICTORY 경로 자동화** — 헤드리스로 보스 1000HP를 깎을 수단이 없다. `-Victory` 는 `BossMaxHealth` 임시 하향 + 재쿡 전제 그대로다.
- **회전 등 로그에 안 남는 항목** — #70이 실증한 대로 여전히 육안.

## 하지 말 것 (스코프 밖)

- **CI 연동** — 로컬 개발 전용이고 빌드가 무겁다(#82에서 확정)
- **빌드·쿡 자동 수행** — 스크립트는 이미 스테이징된 산출물을 전제한다(#82에서 확정)
- **밸런스 조정** — #86이 남긴 TTK 5.3초 신호는 별건이다
- **`profile.ps1` 수정** — 그 스크립트는 `-unattended` 를 쓰지 않으므로(파일 주석에 명시) 이 변경의 영향을 받지 않는다
- **`Build.cs` / `.uproject` 수정** — 필요 없다
