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
    # 캡처 프레임 수. 캡처가 탄환 채움 완료 시점에 시작하므로(#88) 전량이 정상상태 측정분이다.
    # docs/guides/profiling.md 참조.
    [int]$Frames  = 720,
    # 하드 타임아웃(초). 5000발 저FPS 여유 포함.
    [int]$TimeoutSec = 300,
    # Actor 베이스라인(#45) 경로 측정. Mass boss(기본 480발)를 0으로 죽이고 액터만 스폰.
    [switch]$Actor,
    # 스폰 적분 루프게인 오버라이드(튜닝용, 무차원 — #88 이후 설정 무관). 0 이하면 빌드 기본값 사용.
    [double]$Ki = 0,
    # 스위프 구성별 CVar 주입. 기존 -ExecCmds 끝에 쉼표로 이어붙는다 (#50 진단).
    [string]$ExtraExec = '',
    # run 디렉터리 이름에 삽입할 구성 이름. 같은 탄환 수로 여러 번 돌 때 폴더를 구분한다.
    [string]$Label = ''
)

$ErrorActionPreference = 'Stop'

$Root     = Split-Path $PSScriptRoot -Parent
$Editor   = 'E:\UnrealEngine-5.8\UnrealEngine-5.8\Engine\Binaries\Win64\UnrealEditor.exe'
$Uproject = Join-Path $Root 'Project_RE.uproject'
# 엔진 CSV 프로파일러가 어디에 쓰는지는 엔진 빌드 종류에 달렸다 (#88).
#   런처 바이너리 빌드 → 엔진 "유저" 디렉터리(%LOCALAPPDATA%)
#   소스 빌드         → 엔진 트리 자신의 Engine\Saved (M4에서 Server 타깃 때문에 소스 빌드로 갈아탔다)
# 셋 다 훑는다. 하나만 보면 CSV 가 정상 기록됐는데도 "안 나옴" 으로 오진한다.
$EngineDir = Split-Path (Split-Path (Split-Path $Editor -Parent) -Parent) -Parent
$CsvDirs  = @(
    (Join-Path $env:LOCALAPPDATA 'UnrealEngine\5.8\Saved\Profiling\CSV'),
    (Join-Path $Root 'Saved\Profiling\CSV'),
    (Join-Path $EngineDir 'Saved\Profiling\CSV')
)

if (-not (Test-Path $Editor))   { throw "에디터 없음: $Editor" }
if (-not (Test-Path $Uproject)) { throw "uproject 없음: $Uproject" }

$Stamp  = Get-Date -Format 'yyyyMMdd-HHmmss'
# re.Profiling.KeepFiring 1: 즉사 DEFEAT가 보스 발사를 끊어 Mass 탄환이 0발로 측정되는 것을 막는다 (#46).
# re.Cheat.PlayerInvincible 1: #86부터 사망한 플레이어는 GatherHitTargets 대상에서 빠져
# 두 히트 프로세서가 청크 순회 전에 조기 반환한다 — 즉 DEFEAT 이후로는 탄이 아예 소멸하지 않는
# "타겟 없음" 원가만 재는 캡처가 된다. 무적으로 죽지 않게 고정해 원래 측정 의미를 되살린다.
# (클라 로컬 CVar라 이 -game standalone 실행에서만 유효, 데디에는 안 먹는다.)
# Mass 경로: re.Bullets.Count N (Actor는 기본 0). Actor 경로: Mass boss(기본 480)를 0으로 죽이고 액터만.
if ($Actor) {
    $Tag     = 'Actor'
    $ExecCmd = "re.Profiling.KeepFiring 1,re.Cheat.PlayerInvincible 1,re.Bullets.Count 0,re.ActorBullets.Count $Bullets"
} else {
    $Tag     = 'Mass'
    $ExecCmd = "re.Profiling.KeepFiring 1,re.Cheat.PlayerInvincible 1,re.Bullets.Count $Bullets"
}
if ($Ki -gt 0) { $ExecCmd += ",re.Bullets.SpawnKi $Ki" }
if ($ExtraExec) { $ExecCmd += ",$ExtraExec" }
$Suffix = if ($Label) { "_$Label" } else { '' }
$RunDir = Join-Path $Root "Saved\Profiling\RE_${Tag}_${Bullets}${Suffix}_${Stamp}"
New-Item -ItemType Directory -Force -Path $RunDir | Out-Null

$TracePath = Join-Path $RunDir 'trace.utrace'
$LogPath   = Join-Path $RunDir 'run.log'   # 실제 유지 탄환 수(RenderProbe/ActorBulletProbe) 대조용 (#46)
$StartTime = Get-Date

# 측정 조건은 docs/guides/profiling.md 에 고정 기록되어 있다. 바꾸면 문서도 바꿔라.
$ArgLine = @(
    "`"$Uproject`"",
    '/Game/Level/Main',
    '-game',
    '-windowed', '-ResX=1280', '-ResY=720',
    # gpu 채널: Insights GPU 트랙으로 패스별 GPU 시간을 본다 (#50 확인용). 트레이스는 이미 뜨므로 추가 비용 사실상 0.
    '-trace=cpu,frame,counters,gpu',
    '-statnamedevents',
    "-tracefile=`"$TracePath`"",
    # 부팅 시점에 캡처를 시작하면(-csvCaptureFrames) 창의 대부분이 빈 씬이다 — 실측 17.9초 창 중
    # 13.3초가 보스 발사 전이었다 (#88). 보스가 목표 탄수를 다 채운 순간 CSV_EVENT 를 쏘고
    # 거기서부터 캡처한다. -csvCaptureFrames 와 병용 불가(이미 녹화 중이면 이벤트가 안 걸린다).
    "-csvStartOnEvent=REBulletsFilled",
    "-csvCaptureOnEventFrameCount=$Frames",
    "-ExecCmds=`"$ExecCmd`"",
    "-abslog=`"$LogPath`"",
    # -unattended 는 절대 넣지 마라: REPlayerController 의 headless 프로브가 켜지고
    # (HasAuthority() && FApp::IsUnattended()), 그 프로브가 ~4초 뒤 RequestExit 로
    # 게임을 스스로 꺼서 캡처 프레임을 못 채운다.
    '-nosplash', '-NoSound'
) -join ' '

Write-Host "[profile] Path=$Tag Bullets=$Bullets Frames=$Frames"
Write-Host "[profile] run dir: $RunDir"

$Proc = Start-Process -FilePath $Editor -ArgumentList $ArgLine -PassThru

# CSV 프로파일러는 $Frames 프레임을 채우면 csv 를 쓰지만 게임은 계속 돈다.
# 내용이 찬 csv 가 나타나는 것이 곧 "캡처 완료" 신호다.
# 주의: 파일 자체는 캡처 시작 시점에 0바이트로 먼저 생긴다 → Length 조건 필수.
$Deadline = $StartTime.AddSeconds($TimeoutSec)
$Csv = $null
while ((Get-Date) -lt $Deadline) {
    if ($Proc.HasExited) { break }
    $Csv = Get-ChildItem -Path $CsvDirs -Filter '*.csv' -ErrorAction SilentlyContinue |
           Where-Object { $_.LastWriteTime -gt $StartTime -and $_.Length -gt 0 } |
           Sort-Object LastWriteTime | Select-Object -Last 1
    if ($Csv) { break }
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
    Start-Sleep -Seconds 2   # 핸들 닫힌 뒤 trace 파일 크기가 디렉터리에 반영될 때까지
}

if ($Csv) {
    Move-Item -Path $Csv.FullName -Destination (Join-Path $RunDir 'frames.csv') -Force
}

Write-Host "[profile] 산출물:"
Get-ChildItem $RunDir | ForEach-Object { Write-Host ("  " + $_.Name + "  " + $_.Length + " bytes") }
Write-Host $RunDir
