# M6 GPU 병목 진단 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Mass 5000급의 GPU 시간이 어느 기능에 귀속되는지 실측해 `docs/profiling/M6-gpu-breakdown.md` 리포트 1건을 낸다. 최적화는 하지 않는다.

**Architecture:** 게임 코드는 건드리지 않는다. `scripts/profile.ps1` 에 CVar 주입(`-ExtraExec`)과 run 디렉터리 라벨(`-Label`)을 추가하고, `frames.csv` 통계를 뽑는 `scripts/profile-stats.ps1` 을 새로 만든다. 그 둘로 10회 스위프를 돌려 엔진 기능을 하나씩 끈 GPU 차이를 표로 만든다.

**Tech Stack:** PowerShell 5.1, UE 5.8 엔진 CSV 프로파일러, 엔진 내장 CVar(`r.BloomQuality` / `r.ShadowQuality` / `r.ScreenPercentage`)

## Global Constraints

- **게임 C++ 코드 변경 0.** 이 계획은 스크립트와 문서만 건드린다. `Source/` 아래 파일을 수정하면 스코프 위반이다.
- **최적화 금지.** GPU를 빠르게 만드는 변경은 이 계획 밖이다(스펙 §3, §9 "명시적 비목표").
- **PowerShell 파일은 반드시 UTF-8 **BOM 포함**으로 저장한다.** BOM이 없으면 PowerShell 5.1이 ANSI로 읽어 한글 주석이 깨지고 따옴표 짝이 어긋나 파싱이 실패한다(#82 실제 사고).
- **`Project_RE.uproject` 를 절대 스테이징/커밋하지 않는다.** 머신 로컬 `EngineAssociation` GUID를 담고 있어 `git status` 에 항상 수정됨으로 뜨는 것이 정상이다.
- **worktree 금지 — 본체 작업 디렉터리에서 실행한다.** 프로파일 런은 빌드된 에디터 바이너리와 머신 로컬 `uproject` 를 쓴다. 격리 worktree의 `uproject` 는 커밋된 버전이라 `EngineAssociation` 이 달라 에디터가 안 뜰 수 있다.
- `frames.csv` 는 **끝에 헤더 1행 + 메타 1행이 더 붙는다**(`[HasHeaderRowAtEnd]`). 통계 낼 때 반드시 잘라낸다.
- 브랜치: `feature/M6-gpu-breakdown` (이미 생성됨, 스펙 커밋 `5e89a8b` 존재)

---

## File Structure

| 파일 | 책임 | 상태 |
|---|---|---|
| `scripts/profile.ps1` | 프로파일 런 1회 실행 | 수정 (파라미터 2개 + 트레이스 채널) |
| `scripts/profile-stats.ps1` | run 디렉터리들 → 마크다운 통계 표 | **신규** |
| `docs/profiling/M6-gpu-breakdown.md` | 진단 리포트 (산출물) | **신규** |
| `docs/guides/profiling.md` | 하네스 정본 문서 | 수정 (새 파라미터·스크립트 반영) |

---

### Task 1: `profile.ps1` 에 `-ExtraExec` / `-Label` / gpu 트레이스 채널 추가

**Files:**
- Modify: `scripts/profile.ps1`

**Interfaces:**
- Consumes: 없음 (첫 태스크)
- Produces: `profile.ps1 -ExtraExec '<cvar cmds>' -Label '<name>'`. `-ExtraExec` 문자열은 기존 `-ExecCmds` 목록 끝에 쉼표로 이어붙는다. `-Label` 은 run 디렉터리 이름에 `_<label>` 로 삽입된다 — `Saved\Profiling\RE_Mass_1600_base_20260814-193000` 형태. Task 3이 이 두 파라미터를 쓴다.

- [ ] **Step 1: 현재 param 블록과 ExecCmd 조립부 확인**

Run:
```
Select-String -Path .\scripts\profile.ps1 -Pattern 'param\(|\[double\]\$Ki|ExecCmd \+=|\$RunDir = |-trace='
```

Expected: `[double]$Ki = 0` 라인, `if ($Ki -gt 0) { $ExecCmd += ",re.Bullets.SpawnKi $Ki" }` 라인, `$RunDir = Join-Path ...` 라인, `'-trace=cpu,frame,counters',` 라인이 각각 1건씩 나온다.

- [ ] **Step 2: param 블록에 두 파라미터 추가**

`[double]$Ki = 0` 바로 뒤에 (같은 param 블록 안, 쉼표 주의) 추가:

```powershell
    # 스위프 구성별 CVar 주입. 기존 -ExecCmds 끝에 쉼표로 이어붙는다 (#50 진단).
    [string]$ExtraExec = '',
    # run 디렉터리 이름에 삽입할 구성 이름. 같은 탄환 수로 여러 번 돌 때 폴더를 구분한다.
    [string]$Label = ''
```

주의: `[double]$Ki = 0` 뒤의 쉼표가 필요하다. `$Label` 은 param 블록의 마지막이므로 뒤에 쉼표를 붙이지 않는다.

- [ ] **Step 3: ExtraExec 을 ExecCmd 에 이어붙이기**

기존 줄:
```powershell
if ($Ki -gt 0) { $ExecCmd += ",re.Bullets.SpawnKi $Ki" }
```

바로 뒤에 추가:
```powershell
if ($ExtraExec) { $ExecCmd += ",$ExtraExec" }
```

- [ ] **Step 4: RunDir 이름에 Label 삽입**

기존 줄:
```powershell
$RunDir = Join-Path $Root "Saved\Profiling\RE_${Tag}_${Bullets}_${Stamp}"
```

이것으로 교체:
```powershell
$Suffix = if ($Label) { "_$Label" } else { '' }
$RunDir = Join-Path $Root "Saved\Profiling\RE_${Tag}_${Bullets}${Suffix}_${Stamp}"
```

- [ ] **Step 5: 트레이스 채널에 gpu 추가**

기존 줄:
```powershell
    '-trace=cpu,frame,counters',
```

이것으로 교체:
```powershell
    '-trace=cpu,frame,counters,gpu',
```

- [ ] **Step 6: 파라미터가 실제로 먹는지 1회 런으로 확인**

Run:
```
.\scripts\profile.ps1 -Bullets 1600 -Label smoke -ExtraExec "r.BloomQuality 0"
```

Expected:
- run 디렉터리 이름이 `RE_Mass_1600_smoke_<stamp>` 형태
- `frames.csv` 가 생성됨 (0바이트 아님)

이어서 주입이 실제로 커맨드라인에 들어갔는지 확인:
```
Select-String -Path .\Saved\Profiling\RE_Mass_1600_smoke_*\run.log -Pattern 'r.BloomQuality 0' | Select-Object -First 1
Select-String -Path .\Saved\Profiling\RE_Mass_1600_smoke_*\run.log -Pattern 'trace=cpu,frame,counters,gpu' | Select-Object -First 1
```

Expected: 두 명령 모두 매치 1건 이상. `LogInit: Command Line:` 줄에 둘 다 보인다.

**주입이 로그에 안 보이면 멈춰라.** 커맨드라인에 안 들어간 CVar는 조용히 무시되고, 그러면 스위프 전체가 base 를 10번 잰 것이 된다 — 가장 나쁜 실패 양식이다.

- [ ] **Step 7: 기존 사용법이 안 깨졌는지 확인 (회귀)**

Run:
```
.\scripts\profile.ps1 -Bullets 1000
```

Expected: run 디렉터리 이름에 라벨 접미사가 **없고**(`RE_Mass_1000_<stamp>`), `frames.csv` 생성됨. 두 파라미터 모두 기본값이 빈 문자열이므로 기존 호출이 그대로 동작해야 한다.

- [ ] **Step 8: smoke 런 삭제**

Step 6이 만든 `RE_Mass_1600_smoke_*` 는 Task 3의 통계 수집 와일드카드(`RE_*_1600_*`)에 걸려 표를 오염시킨다. 지운다:

```
Remove-Item -Recurse -Force .\Saved\Profiling\RE_Mass_1600_smoke_*
```

Expected: 에러 없음. 이후 `Get-ChildItem .\Saved\Profiling\RE_*_1600_* -Directory` 가 아무것도 반환하지 않아야 한다 (Task 3 전이므로 1600 런이 하나도 없는 상태).

- [ ] **Step 9: 커밋**

```bash
git add scripts/profile.ps1
git commit -m "feat(profiling): profile.ps1 에 -ExtraExec / -Label / gpu 트레이스 추가 (#50)"
```

---

### Task 2: `scripts/profile-stats.ps1` — frames.csv 통계 추출

**Files:**
- Create: `scripts/profile-stats.ps1`

**Interfaces:**
- Consumes: Task 1이 만든 run 디렉터리 이름 규칙 (`RE_<Tag>_<Bullets>_<Label>_<Stamp>`)
- Produces: `profile-stats.ps1 -RunDir <dir1>,<dir2>,...` → 마크다운 표를 stdout 으로. Task 3이 이 출력을 리포트에 붙인다.

- [ ] **Step 1: 스크립트 작성**

`scripts/profile-stats.ps1` 을 **UTF-8 BOM 포함**으로 생성:

```powershell
<#
.SYNOPSIS
  프로파일 run 디렉터리들의 frames.csv 에서 컬럼별 mean/p99 를 뽑아 마크다운 표로 낸다.

.EXAMPLE
  scripts/profile-stats.ps1 -RunDir (Get-ChildItem Saved\Profiling\RE_Mass_1600_* -Directory).FullName
#>
param(
    [Parameter(Mandatory=$true)][string[]]$RunDir
)

$ErrorActionPreference = 'Stop'

# 표에 낼 컬럼: 표시이름 = frames.csv 헤더명.
# 앞 4개는 총량, 나머지는 패스별 렌더스레드 시간과 인스턴스/드로우콜 실측.
$Cols = [ordered]@{
    'Frame'     = 'FrameTime'
    'GT'        = 'GameThreadTime'
    'RT'        = 'RenderThreadTime'
    'GPU'       = 'GPUTime'
    'BasePass'  = 'Exclusive/RenderThread/RenderBasePass'
    'Shadows'   = 'Exclusive/RenderThread/RenderShadows'
    'PostFX'    = 'Exclusive/RenderThread/RenderPostProcessing'
    'Translu'   = 'Exclusive/RenderThread/RenderTranslucency'
    'GPUScene'  = 'Exclusive/RenderThread/UpdateGPUScene'
    'InstUpd'   = 'Exclusive/RenderThread/UpdatePrimitiveInstances'
    'Instances' = 'GPUSceneInstanceCount'
    'Draws'     = 'RHI/DrawCalls'
}

function Get-Stats {
    param([object[]]$Rows, [string]$Name)
    $v = @(foreach ($r in $Rows) {
        $x = $r.$Name
        if ($null -ne $x -and $x -ne '') { [double]$x }
    })
    if ($v.Count -eq 0) { return $null }
    $s = @($v | Sort-Object)
    $i = [int][math]::Ceiling(0.99 * $s.Count) - 1
    if ($i -lt 0) { $i = 0 }
    [pscustomobject]@{
        Mean = ($v | Measure-Object -Average).Average
        P99  = $s[$i]
    }
}

# 헤더 행
$Head = '| run | frames | ' + (($Cols.Keys | ForEach-Object { "$_ mean | $_ p99" }) -join ' | ') + ' |'
$Sep  = '|---|---:|' + (($Cols.Keys | ForEach-Object { '---:|---:|' }) -join '')
Write-Output $Head
Write-Output $Sep

foreach ($d in $RunDir) {
    $csv = Join-Path $d 'frames.csv'
    if (-not (Test-Path $csv)) {
        Write-Warning "frames.csv 없음 — 건너뜀: $d"
        continue
    }

    # frames.csv 는 끝에 헤더 1행 + 메타 1행([HasHeaderRowAtEnd])이 더 붙는다.
    # 그대로 ConvertFrom-Csv 하면 두 행이 데이터로 섞여 평균이 깨진다.
    $lines = Get-Content -LiteralPath $csv
    if ($lines.Count -lt 4) { Write-Warning "행이 너무 적음 — 건너뜀: $d"; continue }
    $rows = $lines[0..($lines.Count - 3)] | ConvertFrom-Csv

    $cells = foreach ($k in $Cols.Keys) {
        $st = Get-Stats -Rows $rows -Name $Cols[$k]
        if ($null -eq $st) { '-'; '-' }
        elseif ($k -in @('Instances','Draws')) {
            '{0:N0}' -f $st.Mean
            '{0:N0}' -f $st.P99
        }
        else {
            '{0:N2}' -f $st.Mean
            '{0:N2}' -f $st.P99
        }
    }

    $name = Split-Path $d -Leaf
    Write-Output ('| ' + $name + ' | ' + $rows.Count + ' | ' + ($cells -join ' | ') + ' |')
}
```

- [ ] **Step 2: 기존 run 디렉터리로 동작 확인**

Task 1의 Step 7이 만든 `RE_Mass_1000_<stamp>` 로 돌린다:

```
.\scripts\profile-stats.ps1 -RunDir (Get-ChildItem .\Saved\Profiling\RE_Mass_1000_* -Directory | Select-Object -Last 1).FullName
```

Expected:
- 마크다운 표 3줄(헤더/구분/데이터 1행)이 나온다
- **`frames` 컬럼이 720** — 말미 2행이 제대로 잘렸다는 증거다. 722가 나오면 자르기가 안 된 것이다
- `GPU mean` 이 0보다 큰 실수
- `Instances mean` 이 목표 탄환 수 근처 (1000발 런이면 약 1000)

**`frames` 가 720이 아니면 멈춰라.** 말미 2행이 섞이면 숫자 컬럼에 `[HasHeaderRowAtEnd]` 같은 문자열이 들어가 통계가 조용히 오염된다.

- [ ] **Step 3: 결측 컬럼에서 안 죽는지 확인**

빈 임시 디렉터리를 만들어 넘긴다:

```
New-Item -ItemType Directory -Force .\Saved\Profiling\_empty_probe | Out-Null
.\scripts\profile-stats.ps1 -RunDir (Resolve-Path .\Saved\Profiling\_empty_probe).Path
Remove-Item -Recurse -Force .\Saved\Profiling\_empty_probe
```

Expected: `WARNING: frames.csv 없음 — 건너뜀:` 경고가 뜨고 표 헤더만 출력된 뒤 **에러 없이 종료**(exit 0). 스위프 중 런 하나가 실패해도 나머지 통계는 나와야 한다.

- [ ] **Step 4: 커밋**

```bash
git add scripts/profile-stats.ps1
git commit -m "feat(profiling): frames.csv 통계 추출 스크립트 추가 (#50)"
```

---

### Task 3: 스위프 10회 실행

**Files:**
- 파일 변경 없음. `Saved/Profiling/` 아래 run 디렉터리 10개를 산출한다 (`Saved/` 는 gitignore 대상 — 커밋하지 않는다).

**Interfaces:**
- Consumes: Task 1의 `-ExtraExec` / `-Label`, Task 2의 `profile-stats.ps1`
- Produces: run 디렉터리 10개와, `profile-stats.ps1` 이 낸 마크다운 표 2개(1600 / 5000). Task 4가 이 표를 리포트에 싣는다.

- [ ] **Step 1: 1600발 4개 구성 실행**

한 번에 하나씩, 순서대로 (동시 실행 금지 — 같은 머신의 GPU를 나눠 쓰면 측정이 무의미해진다):

```
.\scripts\profile.ps1 -Bullets 1600 -Label base
.\scripts\profile.ps1 -Bullets 1600 -Label nobloom  -ExtraExec "r.BloomQuality 0"
.\scripts\profile.ps1 -Bullets 1600 -Label noshadow -ExtraExec "r.ShadowQuality 0"
.\scripts\profile.ps1 -Bullets 1600 -Label halfres  -ExtraExec "r.ScreenPercentage 50"
```

Expected: 각 런이 `frames.csv` 를 낸다. 안 나온 런이 있으면 그 런만 다시 돌린다.

- [ ] **Step 2: 5000발 4개 구성 실행**

```
.\scripts\profile.ps1 -Bullets 5000 -Label base
.\scripts\profile.ps1 -Bullets 5000 -Label nobloom  -ExtraExec "r.BloomQuality 0"
.\scripts\profile.ps1 -Bullets 5000 -Label noshadow -ExtraExec "r.ShadowQuality 0"
.\scripts\profile.ps1 -Bullets 5000 -Label halfres  -ExtraExec "r.ScreenPercentage 50"
```

- [ ] **Step 3: Actor 대조군 2개 실행**

```
.\scripts\profile.ps1 -Actor -Bullets 1600 -Label base
.\scripts\profile.ps1 -Actor -Bullets 5000 -Label base
```

- [ ] **Step 4: 각 런이 목표 탄환 수를 실제로 유지했는지 검증**

스펙 §9의 성공 기준이다. 목표 ±5% 를 벗어난 런의 숫자는 비교에 쓸 수 없다.

```
Get-ChildItem .\Saved\Profiling\RE_*_1600_*, .\Saved\Profiling\RE_*_5000_* -Directory |
  Sort-Object Name | ForEach-Object {
    $log = Join-Path $_.FullName 'run.log'
    if (-not (Test-Path $log)) { Write-Host ($_.Name + '  run.log 없음'); return }
    $hit = @(Select-String -LiteralPath $log -Pattern 'Live=(\d+) Rate|ActorBulletProbe: live=(\d+)')
    if ($hit.Count -eq 0) { Write-Host ($_.Name + '  탄환 프로브 로그 없음'); return }
    Write-Host ($_.Name + '  ' + $hit[-1].Line.Trim())
  }
```

Expected: 각 줄의 live 값이 목표(1600 또는 5000)의 ±5% 안. 벗어난 런, `run.log 없음`, `탄환 프로브 로그 없음` 이 뜬 런은 다시 돌린다.

**"프로브 로그 없음"을 그냥 넘기지 마라.** 탄환이 안 채워진 채 잰 런은 빈 씬을 잰 것이고, #88이 고친 바로 그 실패 양식이다.

- [ ] **Step 5: 통계 표 2개 생성**

```
.\scripts\profile-stats.ps1 -RunDir (Get-ChildItem .\Saved\Profiling\RE_*_1600_* -Directory | Sort-Object Name).FullName | Tee-Object -FilePath .\Saved\Profiling\_stats_1600.md
.\scripts\profile-stats.ps1 -RunDir (Get-ChildItem .\Saved\Profiling\RE_*_5000_* -Directory | Sort-Object Name).FullName | Tee-Object -FilePath .\Saved\Profiling\_stats_5000.md
```

Expected: 표 두 개. 각 데이터 행의 `frames` 가 720. 1600 표는 5행(Mass 4 + Actor 1), 5000 표도 5행.

- [ ] **Step 6: 커밋할 것이 없음을 확인**

이 태스크는 `Saved/` 만 만든다. gitignore 대상이므로 커밋하지 않는다.

```
git status --short
```

Expected: `Project_RE.uproject` 의 `M` 만 보이고 `Saved/` 는 안 보인다. `Saved/` 가 보이면 gitignore 를 확인하라 — **커밋하지 마라** (수백 MB 트레이스가 들어간다).

---

### Task 4: 리포트 작성 + 가이드 문서 반영

**Files:**
- Create: `docs/profiling/M6-gpu-breakdown.md`
- Modify: `docs/guides/profiling.md`

**Interfaces:**
- Consumes: Task 3의 표 2개(`Saved/Profiling/_stats_1600.md`, `_stats_5000.md`)와 run 로그
- Produces: 최종 산출물. 후속 이슈가 이 리포트를 근거로 무엇을 고칠지 정한다.

- [ ] **Step 1: 리포트 작성**

`docs/profiling/M6-gpu-breakdown.md` 를 만든다. `docs/profiling/M3-mass-vs-actor.md` 의 형식을 따르되, 스펙 §8이 요구하는 5가지를 전부 담는다:

1. **재측정 베이스라인** — Task 3 Step 5의 표 두 개를 그대로. 상단에 측정일/엔진/빌드/하네스 조건 명시(M3 리포트 머리말 형식)
2. **M3 표와의 차이** — 스펙 §2의 표(수명 3→15, 발사간격, 엔진 교체, #88 하네스 수정)를 인용하고, 실제로 숫자가 얼마나 달라졌는지 한 문단
3. **소거 표** — 구성별 `GPU mean` 과 base 대비 감소분(ms 및 %). 부하별로 나눈다
4. **귀속 결론** — 한 문장. 형식: `GPU <base> ms 중 <N> ms 는 <기능> 이다.` 소거로 설명 안 되는 잔여분이 있으면 그것도 숫자로 밝힌다
5. **패스별 교차 검증** — `BasePass` / `Shadows` / `PostFX` / `Translu` / `GPUScene` / `InstUpd` 컬럼이 소거 결론과 일치하는지. 불일치하면 그 사실을 적는다 (렌더스레드 CPU 시간과 GPU 시간은 다른 축이므로 불일치 자체가 정보다)
6. **Actor 대조군** — M3의 "삼각형 적은 Mass가 6.5배 느림" 역전이 현행에서도 재현되는지. 재현/미재현 어느 쪽이든 명시
7. **다음 단계 권고** — 소거 결과가 가리키는 수정. **특히 Niagara 교체가 이 원인에 유효한지 판단을 적는다** — 원인이 필/bloom이면 Niagara도 같은 픽셀을 그리므로 안 나아진다

**금지:** 측정하지 않은 값을 쓰지 마라. 표의 모든 숫자는 Task 3 산출물에서 와야 한다. 추정치를 넣어야 한다면 추정임을 명시하라.

- [ ] **Step 2: 소거로 설명 안 되는 잔여분 점검**

리포트를 쓰다 보면 나오는 확인이다. base GPU 에서 세 소거의 감소분 합을 뺀 값이 base 의 절반을 넘으면 — 즉 bloom·그림자·필레이트 어느 것도 과반을 설명하지 못하면 — 스펙 §10 경로다.

그 경우 리포트에 다음을 적는다:
- 소거 셋이 각각 몇 ms만 설명하는지
- 잔여분이 인스턴스 처리 쪽을 가리킨다는 근거(`GPUScene` / `InstUpd` / `Instances` 컬럼)
- 후속으로 `ProfileGPU` 훅(스펙 §5.4)이 필요하다는 권고

이것은 실패가 아니라 **유효한 결론**이다. 스펙 §9는 "GPU를 빠르게 만드는 것"이 아니라 "귀속을 숫자로 말할 수 있는 것"을 성공으로 정의한다.

- [ ] **Step 3: `docs/guides/profiling.md` 에 새 파라미터·스크립트 반영**

가이드는 하네스의 현행 정본이다. Task 1·2가 추가한 것이 여기 없으면 다음 사람이 못 찾는다.

추가할 것:
- `-ExtraExec` / `-Label` 파라미터 설명과 사용 예 1줄씩
- `scripts/profile-stats.ps1` 의 용도와 호출 예
- **`frames.csv` 말미 2행 함정** — `[HasHeaderRowAtEnd]` 때문에 헤더 1행 + 메타 1행이 끝에 붙으므로 직접 파싱할 때 잘라내야 한다는 것. 이건 스크립트를 안 쓰고 손으로 읽는 사람에게 필요한 경고다
- 트레이스 채널이 `cpu,frame,counters,gpu` 로 바뀐 것 (`## 고정 조건` 표 또는 인접 위치)

기존 문서의 다른 부분은 건드리지 마라.

- [ ] **Step 4: 리포트의 숫자가 산출물과 일치하는지 대조**

리포트에 쓴 GPU mean 값 몇 개를 골라 Task 3의 표와 직접 대조한다.

```
Get-Content .\Saved\Profiling\_stats_5000.md
```

Expected: 리포트의 5000발 표와 문자 단위로 일치. 옮겨 적다가 틀린 숫자 하나가 결론을 뒤집을 수 있다.

- [ ] **Step 5: 커밋**

```bash
git add docs/profiling/M6-gpu-breakdown.md docs/guides/profiling.md
git commit -m "docs(profiling): M6 GPU 병목 진단 리포트 (#50)"
```

---

## Self-Review

**1. 스펙 커버리지**

| 스펙 절 | 담당 태스크 |
|---|---|
| §4 부하 1600/5000 | Task 3 Step 1·2 |
| §5.1 소거법 3구성 | Task 3 Step 1·2 |
| §5.2 패스별 CSV 컬럼 | Task 2 `$Cols`, Task 4 Step 1-5 |
| §5.3 Insights GPU 트랙 | Task 1 Step 5 (채널 추가). 읽기는 필요시 Task 4 |
| §6.1 `-ExtraExec` | Task 1 Step 2·3 |
| §6.2 `-Label` | Task 1 Step 2·4 |
| §6.3 트레이스 채널 | Task 1 Step 5 |
| §6.4 `profile-stats.ps1` | Task 2 |
| §7 스위프 10회 | Task 3 Step 1·2·3 |
| §8 산출물 5항목 | Task 4 Step 1 |
| §9 성공 기준(탄환 수 ±5%) | Task 3 Step 4 |
| §10 실패 시 경로 | Task 4 Step 2 |

빠진 요구사항 없음.

**2. 플레이스홀더 스캔**

TBD/TODO 없음. 모든 코드 단계에 실제 코드 블록이 있고, 모든 검증 단계에 실제 명령과 기대 출력이 있다.

**3. 이름 일관성**

`-ExtraExec` / `-Label` / `profile-stats.ps1` / `-RunDir` 이 Task 1→2→3→4에서 동일하게 쓰인다. run 디렉터리 규칙 `RE_<Tag>_<Bullets>_<Label>_<Stamp>` 이 Task 1 Step 4에서 정의되고 Task 3 Step 5의 와일드카드(`RE_*_1600_*`)와 맞는다.
