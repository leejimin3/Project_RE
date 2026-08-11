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

function Invoke-Verdict {
    <# 서버 로그 1개 + 클라 로그 N개를 받아 전체 판정. 프로세스와 무관하게 순수 함수라 SelfTest가 가능하다. #>
    param([string[]]$ServerLines, [hashtable]$ClientLines, [bool]$CheckVictory)

    Write-Host "`n[dedi-verify] 판정"

    # --- 서버: 기동 + 권위 판정이 여기에만 찍혀야 한다.
    Assert-Log 'server' '넷드라이버 리스닝'   $ServerLines 'IpNetDriver listening'
    Assert-Log 'server' '월드 기동'          $ServerLines 'Bringing World /Game/Level/Main\.Main'
    Assert-Log 'server' '프로브 완주'         $ServerLines '\[Dash\] probe done'
    Assert-Range 'server' '대쉬 이동거리'      $ServerLines '\[Dash\] dist=([0-9.]+)' 500 700
    # 코스메틱은 NM_DedicatedServer 가드로 생략되어야 한다 (#74/#75).
    Assert-Log 'server' '발사 몽타주 생략'     $ServerLines '\[Attack\] fire montage' -Expect Absent
    Assert-Log 'server' '대쉬 몽타주 생략'     $ServerLines '\[Dash\] anim len=' -Expect Absent
    Assert-Log 'server' '크래시 없음'         $ServerLines 'Assertion failed|Critical error' -Expect Absent

    # --- 클라: 코스메틱은 여기에만. 접속 유지 구간만 본다.
    foreach ($name in ($ClientLines.Keys | Sort-Object)) {
        $span = Get-ConnectedSpan $ClientLines[$name]
        Assert-Log $name '발사 몽타주 재생'    $span '\[Attack\] fire montage len='
        Assert-Log $name '대쉬 몽타주 재생(오너)' $span '\[Dash\] anim len=.*role=ROLE_AutonomousProxy'
        Assert-Log $name '크래시 없음'        $span 'Assertion failed|Critical error'  -Expect Absent
        if ($CheckVictory) {
            Assert-Log $name '결과 화면 도달'  $span '\[RE\] Client_ShowResult: VICTORY'
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
        'LogTemp: [Dash] dist=602.4 (기대 ~600)',
        'LogTemp: [Dash] probe done — exiting'
    )
    $goodClient = @(
        'LogTemp: [Attack] fire montage len=1.20',
        'LogTemp: [Dash] anim len=0.97 (role=ROLE_AutonomousProxy)',
        'LogNet: Host closed the connection',
        'LogTemp: [Dash] anim len=0.97 (role=ROLE_Authority)'   # 폴백 구간 — 잘려야 한다
    )

    Invoke-Verdict -ServerLines $goodServer -ClientLines @{ 'client1' = $goodClient } -CheckVictory $false
    if ($script:Failures.Count -ne 0) { throw "SelfTest: 정상 로그가 통과하지 못했다 ($($script:Failures.Count)건)" }

    # 폴백 구간 절단이 실제로 동작하는지 — 마커 뒤의 ROLE_Authority 줄은 안 보여야 한다.
    $span = Get-ConnectedSpan $goodClient
    if (@($span | Select-String 'ROLE_Authority').Count -ne 0) { throw 'SelfTest: 접속 종료 후 구간이 절단되지 않았다' }

    # 고장 로그는 반드시 잡혀야 한다 — 통과만 확인하면 항상-PASS 버그를 못 잡는다.
    $script:Failures = @()
    $badServer = $goodServer + 'LogTemp: [Attack] fire montage len=1.20'   # 데디 가드 파손
    Invoke-Verdict -ServerLines $badServer -ClientLines @{ 'client1' = @('nothing') } -CheckVictory $false
    if ($script:Failures.Count -eq 0) { throw 'SelfTest: 고장 로그를 잡아내지 못했다' }

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

Invoke-Verdict -ServerLines $serverLines -ClientLines $clientLines -CheckVictory $Victory.IsPresent

Write-Host "`n[dedi-verify] 로그: $RunDir"
if ($script:Failures.Count -gt 0) {
    Write-Host "[dedi-verify] 실패 $($script:Failures.Count)건" -ForegroundColor Red
    $script:Failures | ForEach-Object { Write-Host "  - $_" -ForegroundColor Red }
    exit 1
}
Write-Host '[dedi-verify] 전 항목 통과' -ForegroundColor Green
exit 0
