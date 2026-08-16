<#
.SYNOPSIS
  데디 서버 + 클라 N프로세스 검증 (#82). 기동 → 프로브 완주 대기 → 종료 → 로그 판정까지
  한 커맨드로 수행하고 성공/실패를 종료 코드로 낸다.

.DESCRIPTION
  이미 스테이징된 산출물을 전제한다 — 빌드/쿡은 하지 않는다.
  선행 절차는 docs/guides/dedicated-server.md "빌드" / "쿡 + 스테이징" 참조.

  서버측 프로브가 전원 완주하면 GameMode가 종료한다(#87). 이 스크립트는 고정 대기가 아니라
  모드에 따라 다르게 신호를 감지한다 — 프로브 모드는 서버 프로세스 종료를, 결과 모드는
  로그의 EndGame 라인을 기다린다.

.EXAMPLE
  scripts/dedi-verify.ps1 -Clients 2            # 프로브 모드 — 이동·발사·대쉬·NavMesh
  scripts/dedi-verify.ps1 -Clients 2 -Outcome   # 결과 모드 — 게이트·스폰이격·전원사망·결과화면
  scripts/dedi-verify.ps1 -Victory      # BossMaxHealth 임시 하향 + 재쿡 후에만
  scripts/dedi-verify.ps1 -SelfTest     # 판정 로직만 검사(프로세스 미기동)
#>
param(
    # 접속시킬 클라 개수. 기본 1.
    [int]$Clients = 1,
    [int]$Port = 7777,
    # 서버 리스닝 대기 상한(초). 첫 실행은 pak 마운트로 느리다.
    [int]$ListenTimeoutSec = 90,
    # 클라 접속 후 서버 자체 종료 대기 상한(초). 프로브 완주는 약 4.4초.
    [int]$ProbeTimeoutSec = 60,
    # 결과 모드: -unattended 없이 순수 전투를 돌려 게이트·스폰이격·전원사망·결과화면을 판정한다.
    # 프로브 모드(기본)와 타이밍이 양립하지 않아 분리했다 — 프로브 완주 ~4.1초, 전멸 ~5.3초 (#87).
    [switch]$Outcome,
    # 결과 모드에서 EndGame 대기 상한(초).
    [int]$OutcomeTimeoutSec = 120,
    # 승리 경로(Client_ShowResult: VICTORY)까지 판정한다.
    # DefaultGame.ini 의 BossMaxHealth 를 프로브 1히트(10 데미지) 이하로 낮추고 재쿡한 상태에서만 성립.
    [switch]$Victory,
    # 프로세스를 띄우지 않고 판정 로직만 합성 로그로 검사한다.
    [switch]$SelfTest,
    # 스테이징 서버가 소스보다 낡아도 진행한다. 렌더/클라 전용 변경처럼 서버가 무관할 때만 써라 (#103).
    [switch]$SkipStaleCheck
)

$ErrorActionPreference = 'Stop'

$Root     = Split-Path $PSScriptRoot -Parent
$Server   = Join-Path $Root 'Saved\StagedBuilds\WindowsServer\Project_RE\Binaries\Win64\Project_REServer.exe'
$Editor   = 'E:\UnrealEngine-5.8\UnrealEngine-5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe'
$Uproject = Join-Path $Root 'Project_RE.uproject'

# 접속 종료 후 클라는 자기 스탠드얼론 월드로 폴백한다 — 그 뒤 로그는 별개 게임이므로 판정에서 잘라낸다.
$DisconnectMarker = 'Host closed the connection'

<#
  스테이징 신선도 검사 (#103).

  서버는 쿡·스테이징 산출물에서 뜬다. 이 스크립트에 재스테이징 단계가 없으므로,
  소스를 고치고 Build.bat 으로 컴파일해도 그 코드는 서버 exe 에 반영되지 않는다.
  클라는 방금 빌드한 에디터 바이너리로 뜨기 때문에 **서버 쪽만 조용히 낡는다.**

  실제로 #98 에서 서버 판정 로직을 바꾸고 전 항목 PASS 를 받았는데, 서버 exe 가
  하루 반 낡아 그 변경이 한 번도 실행되지 않았다. 거짓 통과를 여기서 막는다.

  매번 재쿡하지는 않는다 — 쿡은 수 분~수십 분이라 검증 리듬이 깨진다
  (docs/guides/dedicated-server.md: "코드 고칠 때마다 재쿡하지 마라").
#>
function Assert-StagedServerFresh {
    param([string]$ServerExe, [string]$SourceRoot)

    if (-not (Test-Path $ServerExe)) {
        throw "스테이징 서버가 없다: $ServerExe`n  재스테이징이 필요하다 — docs/guides/dedicated-server.md '쿡 + 스테이징' 참조."
    }

    $exeTime = (Get-Item $ServerExe).LastWriteTime
    $newest  = Get-ChildItem $SourceRoot -Recurse -Include *.cpp,*.h,*.cs -File -ErrorAction SilentlyContinue |
               Sort-Object LastWriteTime -Descending | Select-Object -First 1
    if (-not $newest) { return }   # 소스를 못 찾으면 판단 근거가 없다 — 조용히 통과시킨다

    if ($exeTime -lt $newest.LastWriteTime) {
        throw @"
스테이징 서버가 소스보다 낡았다 — 서버 코드 변경이 검증되지 않는다 (#103).
  staged: $($exeTime.ToString('yyyy-MM-dd HH:mm'))   ($ServerExe)
  source: $($newest.LastWriteTime.ToString('yyyy-MM-dd HH:mm'))   ($($newest.Name))

  재스테이징:
    & "E:\UnrealEngine-5.8\UnrealEngine-5.8\Engine\Build\BatchFiles\RunUAT.bat" BuildCookRun ``
      -project="$(Split-Path $SourceRoot -Parent)\Project_RE.uproject" ``
      -noP4 -platform=Win64 -server -noclient -serverconfig=Development ``
      -cook -stage -pak -build -utf8output

  렌더/클라 전용 변경이라 서버가 무관하면: -SkipStaleCheck
"@
    }
}

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

function Assert-Progress {
    <# 이동 프로브가 실제로 전진했는가 (#112).

       "로그가 있는가"(Assert-Count)만으로는 못 잡는다 — 데디에서 서버가 폰을 거의 못 움직여
       0.5초에 14uu 만 가던 회귀가 전 항목 PASS 로 통과했다. 남은거리의 **최솟값**을 본다:
       가장 많이 접근한 시점이 기준선 안이면 전진한 것이다.

       마지막 값이 아니라 최솟값을 쓰는 이유는 발사 프로브가 이동을 끊기 때문이다
       (REPlayerController: 발사 성공 시 StopMovement — 의도된 동작). 끊긴 뒤 로그는 계속
       같은 값으로 나오므로 마지막 값을 보면 무엇을 재는지 흐려진다. #>
    param(
        [string]$Scope,
        [string]$Desc,
        [string[]]$Lines,
        [string]$Pattern,
        [double]$MaxRemaining
    )
    $m = @($Lines | Select-String -Pattern $Pattern)
    if ($m.Count -eq 0) {
        Write-Host ("  [FAIL] {0}: {1} — 패턴 없음 /{2}/" -f $Scope, $Desc, $Pattern) -ForegroundColor Red
        $script:Failures += ("{0}: {1} — 패턴 없음" -f $Scope, $Desc)
        return
    }
    $vals = @($m | ForEach-Object { [double]$_.Matches[0].Groups[1].Value })
    $best = ($vals | Measure-Object -Minimum).Minimum
    if ($best -gt $MaxRemaining) {
        Write-Host ("  [FAIL] {0}: {1} — 최소 잔여거리 {2:N1} > 기준 {3:N1} (거의 전진 못함)" -f $Scope, $Desc, $best, $MaxRemaining) -ForegroundColor Red
        $script:Failures += ("{0}: {1} — 최소 잔여거리 {2:N1}" -f $Scope, $Desc, $best)
        return
    }
    Write-Host ("  [PASS] {0}: {1} (최소 잔여거리 {2:N1})" -f $Scope, $Desc, $best) -ForegroundColor Green
}

function Assert-Range {
    <# 캡처그룹 1의 수치가 범위 안인지. 대쉬 거리처럼 "있기만" 하면 안 되는 항목용.
       매치가 N건이면(N인) 전부 검사한다 — 마지막 것만 보면 앞쪽 클라의 이상값을 놓친다. #>
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
    $vals = @($m | ForEach-Object { [double]$_.Matches[0].Groups[1].Value })
    # 어느 occurrence가 고장인지 짚어야 클라 여럿일 때 원인을 가른다.
    $bad = @()
    for ($i = 0; $i -lt $vals.Count; $i++) {
        if ($vals[$i] -lt $Min -or $vals[$i] -gt $Max) { $bad += ("#{0}={1}" -f ($i + 1), $vals[$i]) }
    }
    if ($bad.Count -eq 0) {
        Write-Host ("  [PASS] {0}: {1} ({2}건, ={3})" -f $Scope, $Desc, $vals.Count, ($vals -join ', '))
    } else {
        Write-Host ("  [FAIL] {0}: {1} — [{2}, {3}] 밖: {4}" -f $Scope, $Desc, $Min, $Max, ($bad -join ', ')) -ForegroundColor Red
        $script:Failures += ("{0}: {1} — [{2}, {3}] 밖: {4}" -f $Scope, $Desc, $Min, $Max, ($bad -join ', '))
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
        # 이동이 실제로 일어났는가 (#112). 목표는 500uu, 발사 프로브가 ~0.5초에 끊으므로
        # 그때까지 200uu 안팎 전진한다. 기준 350 = 최소 150uu 전진.
        #   정상   : 최소 잔여 265 (데디) / 288 (스탠드얼론)
        #   회귀   : 최소 잔여 485 (서버가 폰을 못 움직임)
        Assert-Progress 'server' '이동 전진' $ServerLines '\[Move\] probe dist=([0-9.]+)' 350
        Assert-Count 'server' '대쉬 거리 측정'    $ServerLines '\[Dash\] dist='        $ClientCount
        Assert-Range 'server' '대쉬 이동거리'     $ServerLines '\[Dash\] dist=([0-9.]+)' 500 700

        # 이동 프로브는 오프메시 거부 경로 확인용으로 맵 밖 좌표(100000)를 일부러 1회 요청한다.
        # 그 좌표를 지목한 거부는 정상이고 오히려 거부 경로가 살아있다는 증거 — 판정에서 제외한다.
        # 제외하지 않으면 이 판정은 항상 실패한다.
        $realTargetLines = @($ServerLines | Where-Object { $_ -notmatch '100000' })
        Assert-Log 'server' 'NavMesh 거부 없음(실목표)' $realTargetLines '\[Move\] rejected: off-navmesh' -Expect Absent

        # 위 판정은 거부 로그가 통째로 없어도(=투사가 항상 성공하는 회귀로 거부 경로 자체가 죽어도)
        # 통과해버린다 — 맵 밖 좌표가 실제로 거부됐는지 별도로 확인해야 이 침묵 통과를 못 잡는다.
        $offMapLines = @($ServerLines | Where-Object { $_ -match '100000' })
        Assert-Log 'server' 'NavMesh 거부 확인(맵밖 좌표)' $offMapLines '\[Move\] rejected: off-navmesh'

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
        'LogTemp: [Move] probe dist=485.1 loc=X=0.000 Y=14.873 Z=130.150',
        'LogTemp: [Move] probe dist=265.7 loc=X=0.000 Y=234.256 Z=130.150',
        'LogTemp: [Move] rejected: off-navmesh X=100000.000 Y=100000.000 Z=0.000',
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

    # Assert-Range 가 마지막 occurrence만 보던 버그 재현 — 1번 클라 대쉬거리가 범위 밖, 2번은 정상.
    # 구버전은 $m[-1](마지막=정상값)만 봐서 이 상황을 통과시켰다 — 그 회귀를 다시 잡는지 확인한다.
    $script:Failures = @()
    $twoDashServer = @(
        'LogNet: IpNetDriver listening on port 7777',
        'LogWorld: Bringing World /Game/Level/Main.Main up for play',
        'LogTemp: [RE] Boss FireDirect: Pattern=0 Angle=0.0 N=16 Elapsed=0.000 role=ROLE_Authority',
        'LogTemp: [Move] probe start: pawn=X=0 target=X=0',
        'LogTemp: [Dash] dist=50.0 (기대 ~600)',
        'LogTemp: [Dash] probe done',
        'LogTemp: [Move] probe start: pawn=X=0 target=X=0',
        'LogTemp: [Dash] dist=602.4 (기대 ~600)',
        'LogTemp: [Dash] probe done'
    )
    Invoke-Verdict -ServerLines $twoDashServer -ClientLines @{ 'client1' = $goodClient; 'client2' = $goodClient } `
                   -CheckVictory $false -Mode Probe -ClientCount 2
    if (-not ($script:Failures -match '대쉬 이동거리.*#1')) { throw 'SelfTest: 대쉬 거리 범위 검사가 1번 클라(앞쪽 occurrence)의 이상값을 놓쳤다' }

    # 데디에서 서버가 폰을 거의 못 움직이던 회귀 (#112) — 전진 없이 잔여거리가 계속 큰 상황.
    # Assert-Count('이동 프로브 기동')는 로그 존재만 보므로 이걸 못 잡는다. 실제로 못 잡아서
    # 16항목 전부 PASS 를 내주고 있었다.
    $script:Failures = @()
    $noProgressServer = @($goodServer | ForEach-Object { $_ -replace 'probe dist=265', 'probe dist=487' })
    Invoke-Verdict -ServerLines $noProgressServer -ClientLines @{ 'client1' = $goodClient } -CheckVictory $false -Mode Probe -ClientCount 1
    if (-not ($script:Failures -match '이동 전진')) { throw 'SelfTest: 데디 이동 정체(#112 회귀)를 잡아내지 못했다' }

    # 이동 프로브 거리 로그가 통째로 사라진 경우도 실패해야 한다 — 침묵 통과 방지.
    $script:Failures = @()
    $noMoveServer = @($goodServer | Where-Object { $_ -notmatch 'probe dist=' })
    Invoke-Verdict -ServerLines $noMoveServer -ClientLines @{ 'client1' = $goodClient } -CheckVictory $false -Mode Probe -ClientCount 1
    if (-not ($script:Failures -match '이동 전진')) { throw 'SelfTest: 이동 프로브 로그 부재를 잡아내지 못했다' }

    # NavMesh 거부 경로 자체가 죽은 회귀(=투사가 항상 성공) — 맵 밖 좌표 거부 로그가 아예 없는 상황을 합성한다.
    # 기존 "실목표 거부 없음" 판정은 거부 로그가 0건이어도 통과해버려 이 회귀를 못 잡는다.
    $script:Failures = @()
    $noRejectServer = @($goodServer | Where-Object { $_ -notmatch '100000' })
    Invoke-Verdict -ServerLines $noRejectServer -ClientLines @{ 'client1' = $goodClient } -CheckVictory $false -Mode Probe -ClientCount 1
    if (-not ($script:Failures -match 'NavMesh 거부 확인')) { throw 'SelfTest: NavMesh 거부 경로 소실(맵 밖 좌표 거부 부재)을 잡아내지 못했다' }

    # --- 결과 모드(-Outcome) 자체 검사. 여태까지는 전부 Probe 모드였다 — 결과 모드의 스폰이격
    # 계산·포맷 정규식은 이 브랜치에서 새로 짠 손코드라 별도로 검사해야 한다.
    $script:Failures = @()
    $goodServerOutcome = @(
        'LogNet: IpNetDriver listening on port 7777',
        'LogWorld: Bringing World /Game/Level/Main.Main up for play',
        'LogTemp: [RE] Boss FireDirect: Pattern=0 Angle=0.0 N=16 Elapsed=0.000 role=ROLE_Authority',
        'LogTemp: [RE] Spawn player idx=0 offsetY=-125 loc=X=0.000 Y=-125.000 Z=0.000',
        'LogTemp: [RE] Spawn player idx=1 offsetY=125 loc=X=0.000 Y=125.000 Z=0.000',
        'LogTemp: [RE] Boss firing started (2/2 ready)',
        'LogTemp: [RE] Player died 1/2',
        'LogTemp: [RE] Player died 2/2',
        'LogTemp: [RE] All 2 players dead',
        'LogTemp: [RE] EndGame: DEFEAT'
    )
    $goodClientOutcome = @(
        'LogTemp: [RE] Boss FireDirect: Pattern=0 Angle=0.0 N=16 Elapsed=0.084 role=ROLE_SimulatedProxy',
        'LogTemp: [RE] Client_ShowResult: DEFEAT'
    )
    Invoke-Verdict -ServerLines $goodServerOutcome `
                   -ClientLines @{ 'client1' = $goodClientOutcome; 'client2' = $goodClientOutcome } `
                   -CheckVictory $false -Mode Outcome -ClientCount 2
    if ($script:Failures.Count -ne 0) { throw "SelfTest: 결과 모드 정상 로그가 통과하지 못했다 ($($script:Failures.Count)건)" }

    # 결과 모드의 손코드(스폰 이격 상이 계산)를 겨냥한 고장 — 두 스폰 offsetY가 우연히 같은 값으로 겹친 상황.
    # Assert-Count(스폰 로그 2건)는 여전히 통과하므로 공용 어서션으로는 못 잡고, distinctness 계산만 잡아야 한다.
    $script:Failures = @()
    $dupOffsetServerOutcome = $goodServerOutcome -replace 'idx=1 offsetY=125', 'idx=1 offsetY=-125'
    Invoke-Verdict -ServerLines $dupOffsetServerOutcome `
                   -ClientLines @{ 'client1' = $goodClientOutcome; 'client2' = $goodClientOutcome } `
                   -CheckVictory $false -Mode Outcome -ClientCount 2
    if (-not ($script:Failures -match '스폰 이격 상이')) { throw 'SelfTest: 결과 모드 스폰 이격 중복(#54 겹침 회귀)을 잡아내지 못했다' }

    # --- 신선도 판정 자기검사 (#103) ---
    # 이 판정 자체가 고장나면 다시 조용한 거짓 통과로 돌아간다. 양·음성 둘 다 본다.
    $fresh = Join-Path ([IO.Path]::GetTempPath()) ("dedi-fresh-" + [Guid]::NewGuid().ToString('N'))
    New-Item -ItemType Directory -Path (Join-Path $fresh 'Source') -Force | Out-Null
    $fakeSrc = Join-Path $fresh 'Source\Fake.cpp'
    $fakeExe = Join-Path $fresh 'Project_REServer.exe'
    Set-Content -LiteralPath $fakeSrc -Value '// test' -Encoding utf8
    Set-Content -LiteralPath $fakeExe -Value 'MZ' -Encoding utf8
    try {
        # 1) 양성 — exe 가 소스보다 새로우면 통과해야 한다
        (Get-Item $fakeSrc).LastWriteTime = (Get-Date).AddHours(-2)
        (Get-Item $fakeExe).LastWriteTime = (Get-Date).AddHours(-1)
        Assert-StagedServerFresh -ServerExe $fakeExe -SourceRoot (Join-Path $fresh 'Source')

        # 2) 음성 — exe 가 낡으면 실패하고 재스테이징 명령을 알려야 한다 (#98 에서 실제로 겪은 상황)
        (Get-Item $fakeExe).LastWriteTime = (Get-Date).AddHours(-3)
        $caught = $null
        try { Assert-StagedServerFresh -ServerExe $fakeExe -SourceRoot (Join-Path $fresh 'Source') }
        catch { $caught = $_.Exception.Message }
        if (-not $caught) { throw 'SelfTest: 낡은 스테이징 서버를 통과시켰다 (#103 회귀)' }
        if ($caught -notmatch 'BuildCookRun') { throw 'SelfTest: 낡음 메시지에 재스테이징 명령이 없다' }
        if ($caught -notmatch 'SkipStaleCheck') { throw 'SelfTest: 낡음 메시지에 탈출구 안내가 없다' }

        # 3) exe 자체가 없으면 실패해야 한다 — 조용히 넘어가면 다음 단계가 엉뚱한 데서 죽는다
        $caught = $null
        try { Assert-StagedServerFresh -ServerExe (Join-Path $fresh 'nope.exe') -SourceRoot (Join-Path $fresh 'Source') }
        catch { $caught = $_.Exception.Message }
        if (-not $caught) { throw 'SelfTest: 스테이징 서버 부재를 잡아내지 못했다' }

        # 4) 소스를 못 찾으면 판단 근거가 없다 — 통과시킨다(오탐으로 검증을 막지 않는다)
        New-Item -ItemType Directory -Path (Join-Path $fresh 'Empty') -Force | Out-Null
        Assert-StagedServerFresh -ServerExe $fakeExe -SourceRoot (Join-Path $fresh 'Empty')
    }
    finally { Remove-Item -LiteralPath $fresh -Recurse -Force -ErrorAction SilentlyContinue }
    Write-Host '[dedi-verify] 신선도 판정 자기검사 4/4 통과 (#103)' -ForegroundColor DarkGray

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

# 서버를 띄우기 전에 스테이징 신선도부터 본다 — 낡은 exe 로 초록불이 나오면
# 그 검증은 아무것도 보증하지 않는다 (#103).
if (-not $SkipStaleCheck) {
    Assert-StagedServerFresh -ServerExe $Server -SourceRoot (Join-Path $Root 'Source')
}

try {
    # -abslog 은 반드시 전개된 경로로 넘긴다. 리터럴이 들어가면 로그가 조용히 사라진다(가이드 함정).
    # 협동 인원은 CVar로 넘긴다 — ini면 pak 안이라 인원을 바꿀 때마다 재쿡해야 한다 (#85).
    # -unattended 는 프로브 모드에서만: 결과 모드는 프로브가 플레이어를 +X로 대쉬시켜
    # 보스 탄막 레인(X=600)으로 밀어넣어(#56) 측정 대상인 사망 타이밍을 교란한다.
    $srvArgs = @('-log', "-port=$Port", "-ExecCmds=`"re.Coop.ExpectedPlayers $Clients`"", "-abslog=`"$ServerLog`"")
    if (-not $Outcome) { $srvArgs += '-unattended' }
    $srv = Start-Process $Server -PassThru -WindowStyle Hidden -ArgumentList $srvArgs
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

        # 결과 모드는 "클라 N-1명만으로는 발사가 시작되지 않는다"를 판정한다 —
        # 그 상태가 실제로 존재하려면 순차 투입이어야 한다. 고정 대기가 아니라 로그로 동기화한다:
        # 느린 머신에서 Start-Sleep 은 조용히 어긋난다(#82가 리스닝 대기를 폴링으로 바꾼 것과 같은 이유).
        if ($Outcome -and $i -lt $Clients) {
            $readyDeadline = (Get-Date).AddSeconds(60)
            $found = $false
            while ((Get-Date) -lt $readyDeadline) {
                if ((Select-String -Path $ServerLog -Pattern ("\[RE\] Player ready {0}/" -f $i) -Quiet)) {
                    $found = $true
                    break
                }
                Start-Sleep -Milliseconds 300
            }
            if (-not $found) {
                throw "클라 ${i} ready 타임아웃 60s — 패턴 '[RE] Player ready ${i}/' 미발생. 로그: $ServerLog"
            }
            Write-Host "[dedi-verify] client$i ready 확인 — 다음 클라 투입"
        }
    }

    if ($Outcome) {
        # 결과 모드: 서버가 스스로 죽지 않는다(-unattended 없음). EndGame 을 보고 정리한다.
        $endDeadline = (Get-Date).AddSeconds($OutcomeTimeoutSec)
        $ended = $false
        while ((Get-Date) -lt $endDeadline) {
            if ($srv.HasExited) { break }
            if (Select-String -Path $ServerLog -Pattern '\[RE\] EndGame:' -Quiet) { $ended = $true; break }
            Start-Sleep -Milliseconds 500
        }
        if ($srv.HasExited -and -not $ended) {
            throw "서버 예기치 않게 종료 (exit=$($srv.ExitCode)). EndGame 미발생. 로그: $ServerLog"
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

$mode = if ($Outcome) { 'Outcome' } else { 'Probe' }
Invoke-Verdict -ServerLines $serverLines -ClientLines $clientLines -CheckVictory $Victory.IsPresent `
               -Mode $mode -ClientCount $Clients

Write-Host "`n[dedi-verify] 로그: $RunDir"
if ($script:Failures.Count -gt 0) {
    Write-Host "[dedi-verify] 실패 $($script:Failures.Count)건" -ForegroundColor Red
    $script:Failures | ForEach-Object { Write-Host "  - $_" -ForegroundColor Red }
    exit 1
}
Write-Host '[dedi-verify] 전 항목 통과' -ForegroundColor Green
exit 0
