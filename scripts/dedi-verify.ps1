<#
.SYNOPSIS
  데디 서버 + 클라 N프로세스 검증 (#82). 기동 → 프로브 완주 대기 → 종료 → 로그 판정까지
  한 커맨드로 수행하고 성공/실패를 종료 코드로 낸다.

.DESCRIPTION
  이미 스테이징된 산출물을 전제한다 — 빌드/쿡은 하지 않는다.
  선행 절차는 docs/guides/dedicated-server.md "빌드" / "쿡 + 스테이징" 참조.

  서버 프로브(REPlayerController::RunHeadlessDashProbe)가 마지막에 RequestExit 하므로
  서버는 클라 접속 후 약 4.4초에 스스로 종료한다 — 이 스크립트는 고정 대기가 아니라
  서버 프로세스 종료를 기다린다.

.EXAMPLE
  scripts/dedi-verify.ps1
  scripts/dedi-verify.ps1 -Clients 2
  scripts/dedi-verify.ps1 -Victory      # BossMaxHealth 임시 하향 + 재쿡 후에만
  scripts/dedi-verify.ps1 -SelfTest     # 판정 로직만 검사(프로세스 미기동)
#>
param(
    # 접속시킬 클라 개수 (M5 N인 협동 대비). 기본 1.
    [int]$Clients = 1,
    [int]$Port = 7777,
    # 서버 리스닝 대기 상한(초). 첫 실행은 pak 마운트로 느리다.
    [int]$ListenTimeoutSec = 90,
    # 클라 접속 후 서버 자체 종료 대기 상한(초). 프로브 완주는 약 4.4초.
    [int]$ProbeTimeoutSec = 60,
    # 승리 경로(Client_ShowResult: VICTORY)까지 판정한다.
    # DefaultGame.ini 의 BossMaxHealth 를 프로브 1히트(10 데미지) 이하로 낮추고 재쿡한 상태에서만 성립.
    [switch]$Victory,
    # 프로세스를 띄우지 않고 판정 로직만 합성 로그로 검사한다.
    [switch]$SelfTest
)

$ErrorActionPreference = 'Stop'

$Root     = Split-Path $PSScriptRoot -Parent
$Server   = Join-Path $Root 'Saved\StagedBuilds\WindowsServer\Project_RE\Binaries\Win64\Project_REServer.exe'
$Editor   = 'E:\UnrealEngine-5.8\UnrealEngine-5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe'
$Uproject = Join-Path $Root 'Project_RE.uproject'

# 접속 종료 후 클라는 자기 스탠드얼론 월드로 폴백한다 — 그 뒤 로그는 별개 게임이므로 판정에서 잘라낸다.
$DisconnectMarker = 'Host closed the connection'

# ---------------------------------------------------------------- 판정 엔진

$script:Failures = @()

function Get-ConnectedSpan {
    <# 클라 로그에서 접속 유지 구간만 남긴다. 마커가 없으면 전체가 접속 구간이다. #>
    param([string[]]$Lines)
    $idx = -1
    for ($i = 0; $i -lt $Lines.Count; $i++) {
        if ($Lines[$i] -match [regex]::Escape($DisconnectMarker)) { $idx = $i; break }
    }
    if ($idx -lt 0) { return $Lines }
    return $Lines[0..([Math]::Max($idx - 1, 0))]
}

function Assert-Log {
    <# Present: 패턴이 있어야 성공 / Absent: 없어야 성공. #>
    param(
        [string]$Scope,
        [string]$Desc,
        [string[]]$Lines,
        [string]$Pattern,
        [ValidateSet('Present', 'Absent')][string]$Expect = 'Present'
    )
    $hits = @($Lines | Select-String -Pattern $Pattern -AllMatches)
    $ok = if ($Expect -eq 'Present') { $hits.Count -gt 0 } else { $hits.Count -eq 0 }

    if ($ok) {
        Write-Host ("  [PASS] {0}: {1}" -f $Scope, $Desc)
    } else {
        $detail = if ($Expect -eq 'Present') { "패턴 없음 /$Pattern/" }
                  else { "있으면 안 되는 패턴 {0}건: {1}" -f $hits.Count, $hits[0].Line.Trim() }
        Write-Host ("  [FAIL] {0}: {1} — {2}" -f $Scope, $Desc, $detail) -ForegroundColor Red
        $script:Failures += ("{0}: {1} — {2}" -f $Scope, $Desc, $detail)
    }
}

function Assert-Range {
    <# 캡처그룹 1의 수치가 범위 안인지. 대쉬 거리처럼 "있기만" 하면 안 되는 항목용. #>
    param(
        [string]$Scope,
        [string]$Desc,
        [string[]]$Lines,
        [string]$Pattern,
        [double]$Min,
        [double]$Max
    )
    $m = @($Lines | Select-String -Pattern $Pattern)
    if ($m.Count -eq 0) {
        Write-Host ("  [FAIL] {0}: {1} — 패턴 없음 /{2}/" -f $Scope, $Desc, $Pattern) -ForegroundColor Red
        $script:Failures += ("{0}: {1} — 패턴 없음" -f $Scope, $Desc)
        return
    }
    $val = [double]$m[-1].Matches[0].Groups[1].Value
    if ($val -ge $Min -and $val -le $Max) {
        Write-Host ("  [PASS] {0}: {1} (={2})" -f $Scope, $Desc, $val)
    } else {
        Write-Host ("  [FAIL] {0}: {1} — {2} 가 [{3}, {4}] 밖" -f $Scope, $Desc, $val, $Min, $Max) -ForegroundColor Red
        $script:Failures += ("{0}: {1} — {2} 가 [{3}, {4}] 밖" -f $Scope, $Desc, $val, $Min, $Max)
    }
}

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

# ---------------------------------------------------------------- SelfTest

if ($SelfTest) {
    # 판정 엔진이 살아있는지 확인하는 최소 검사. 합성 로그라 스테이징 산출물이 필요 없다.
    Write-Host '[dedi-verify] SelfTest — 합성 로그로 판정 로직 검사'

    $goodServer = @(
        'LogNet: IpNetDriver listening on port 7777',
        'LogWorld: Bringing World /Game/Level/Main.Main up for play',
        'LogTemp: [Move] probe start: pawn=X=0 target=X=0',
        'LogTemp: [Dash] dist=602.4 (기대 ~600)',
        'LogTemp: [RE] Boss FireDirect: Pattern=0 Angle=0.0 N=16 Elapsed=0.000 role=ROLE_Authority',
        'LogTemp: [Dash] probe done'
    )
    $goodClient = @(
        'LogTemp: [Attack] fire montage len=1.20',
        'LogTemp: [Dash] anim len=0.97 (role=ROLE_AutonomousProxy)',
        'LogTemp: [RE] Boss FireDirect: Pattern=0 Angle=0.0 N=16 Elapsed=0.084 role=ROLE_SimulatedProxy',
        'LogNet: Host closed the connection',
        'LogTemp: [Dash] anim len=0.97 (role=ROLE_Authority)'   # 폴백 구간 — 잘려야 한다
    )

    Invoke-Verdict -ServerLines $goodServer -ClientLines @{ 'client1' = $goodClient } -CheckVictory $false -Mode Probe -ClientCount 1
    if ($script:Failures.Count -ne 0) { throw "SelfTest: 정상 로그가 통과하지 못했다 ($($script:Failures.Count)건)" }

    # 폴백 구간 절단이 실제로 동작하는지 — 마커 뒤의 ROLE_Authority 줄은 안 보여야 한다.
    $span = Get-ConnectedSpan $goodClient
    if (@($span | Select-String 'ROLE_Authority').Count -ne 0) { throw 'SelfTest: 접속 종료 후 구간이 절단되지 않았다' }

    # 고장 로그는 반드시 잡혀야 한다 — 통과만 확인하면 항상-PASS 버그를 못 잡는다.
    $script:Failures = @()
    $badServer = $goodServer + 'LogTemp: [Attack] fire montage len=1.20'   # 데디 가드 파손
    Invoke-Verdict -ServerLines $badServer -ClientLines @{ 'client1' = @('nothing') } -CheckVictory $false -Mode Probe -ClientCount 1
    if ($script:Failures.Count -eq 0) { throw 'SelfTest: 고장 로그를 잡아내지 못했다' }
    # 볼리 어서션 자체가 (다른 어서션과 무관하게) 고장을 잡는지 — 실패 목록에 그 항목이 실제로 있어야 한다.
    # 'nothing'은 모든 클라 패턴에 안 걸리므로, 다른 어서션이 이미 실패해도 이 어서션이 조용히
    # 빠졌다면(예: 패턴 오타로 무력화) 위 Count 체크만으론 못 잡는다 — 항목 자체를 찾는다.
    if (-not ($script:Failures -match '탄막 수신·스폰')) { throw 'SelfTest: 볼리 수신 어서션이 고장을 못 잡는다' }

    # Assert-Count 가 개수 부족을 잡는지 — 클라 2인데 프로브 완주가 1건뿐인 상황을 합성한다.
    # 이것이 바로 #87 이전의 실제 증상이다(첫 프로브가 서버를 내려 뒤 프로브가 안 돎).
    $script:Failures = @()
    Invoke-Verdict -ServerLines $goodServer -ClientLines @{ 'client1' = $goodClient; 'client2' = $goodClient } `
                   -CheckVictory $false -Mode Probe -ClientCount 2
    if (-not ($script:Failures -match '프로브 완주')) { throw 'SelfTest: 프로브 완주 개수 부족을 잡아내지 못했다' }

    Write-Host "`n[dedi-verify] SelfTest OK" -ForegroundColor Green
    exit 0
}

# ---------------------------------------------------------------- 실행

if (-not (Test-Path $Server))   { throw "서버 exe 없음: $Server`n먼저 쿡+스테이징 — docs/guides/dedicated-server.md" }
if (-not (Test-Path $Editor))   { throw "에디터 없음: $Editor" }
if (-not (Test-Path $Uproject)) { throw "uproject 없음: $Uproject" }

$Stamp  = Get-Date -Format 'yyyyMMdd-HHmmss'
$RunDir = Join-Path $Root "Saved\DediVerify\$Stamp"
New-Item -ItemType Directory -Force -Path $RunDir | Out-Null

$ServerLog = Join-Path $RunDir 'server.log'
$Procs = @()

Write-Host "[dedi-verify] clients=$Clients port=$Port"
Write-Host "[dedi-verify] run dir: $RunDir"

try {
    # -abslog 은 반드시 전개된 경로로 넘긴다. 리터럴이 들어가면 로그가 조용히 사라진다(가이드 함정).
    $srv = Start-Process $Server -PassThru -WindowStyle Hidden -ArgumentList @(
        '-log', "-port=$Port", '-unattended', "-abslog=`"$ServerLog`""
    )
    $Procs += $srv

    # 고정 대기 대신 리스닝 로그를 폴링한다 — 첫 실행(pak 마운트)과 재실행의 편차를 흡수한다.
    $deadline = (Get-Date).AddSeconds($ListenTimeoutSec)
    $listening = $false
    while ((Get-Date) -lt $deadline) {
        if ($srv.HasExited) { throw "서버가 리스닝 전에 종료됨 (exit=$($srv.ExitCode)). 로그: $ServerLog" }
        if ((Test-Path $ServerLog) -and (Select-String -Path $ServerLog -Pattern 'IpNetDriver listening' -Quiet)) {
            $listening = $true; break
        }
        Start-Sleep -Milliseconds 500
    }
    if (-not $listening) { throw "서버 리스닝 타임아웃 ${ListenTimeoutSec}s. 로그: $ServerLog" }
    Write-Host '[dedi-verify] 서버 리스닝 확인'

    $ClientLogs = @{}
    for ($i = 1; $i -le $Clients; $i++) {
        $log = Join-Path $RunDir "client$i.log"
        $ClientLogs["client$i"] = $log
        $Procs += Start-Process $Editor -PassThru -WindowStyle Hidden -ArgumentList @(
            "`"$Uproject`"", "127.0.0.1:$Port", '-game', '-nullrhi', '-unattended', '-log', "-abslog=`"$log`""
        )
        Write-Host "[dedi-verify] client$i 접속 요청"
    }

    # 서버측 프로브가 완주하면 RequestExit 로 스스로 죽는다 — 그 종료가 곧 "프로브 완료" 신호다.
    # ponytail: 첫 클라의 프로브 완주가 서버를 내리므로 -Clients 2 이상이면 뒤 클라의 서버측 프로브는
    #           잘린다. 클라별 완주가 필요해지면 프로브에 클라 인덱스 게이트를 넣어야 한다 (M5 몫).
    if (-not $srv.WaitForExit($ProbeTimeoutSec * 1000)) {
        throw "프로브 타임아웃 ${ProbeTimeoutSec}s — 서버가 스스로 종료하지 않았다. 로그: $ServerLog"
    }
    Write-Host '[dedi-verify] 서버 자체 종료 확인 (프로브 완주)'

    # 클라는 서버 종료를 감지하고 폴백 월드를 띄우므로 스스로 안 죽는다. 로그 플러시만 주고 정리한다.
    Start-Sleep -Seconds 3
}
finally {
    foreach ($p in $Procs) {
        if ($p -and -not $p.HasExited) {
            Stop-Process -Id $p.Id -Force -ErrorAction SilentlyContinue
        }
    }
    Start-Sleep -Seconds 1   # 핸들이 닫혀야 로그를 온전히 읽는다
}

$serverLines = @(Get-Content $ServerLog -ErrorAction SilentlyContinue)
$clientLines = @{}
foreach ($name in $ClientLogs.Keys) {
    $clientLines[$name] = @(Get-Content $ClientLogs[$name] -ErrorAction SilentlyContinue)
    if ($clientLines[$name].Count -eq 0) { throw "클라 로그가 비었다: $($ClientLogs[$name])" }
}

Invoke-Verdict -ServerLines $serverLines -ClientLines $clientLines -CheckVictory $Victory.IsPresent -Mode Probe -ClientCount $Clients

Write-Host "`n[dedi-verify] 로그: $RunDir"
if ($script:Failures.Count -gt 0) {
    Write-Host "[dedi-verify] 실패 $($script:Failures.Count)건" -ForegroundColor Red
    $script:Failures | ForEach-Object { Write-Host "  - $_" -ForegroundColor Red }
    exit 1
}
Write-Host '[dedi-verify] 전 항목 통과' -ForegroundColor Green
exit 0
