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
    [int]$TimeoutSec = 300,
    # Actor 베이스라인(#45) 경로 측정. Mass boss(기본 480발)를 0으로 죽이고 액터만 스폰.
    [switch]$Actor
)

$ErrorActionPreference = 'Stop'

$Root     = Split-Path $PSScriptRoot -Parent
$Editor   = 'E:\UE_5.8\Engine\Binaries\Win64\UnrealEditor.exe'
$Uproject = Join-Path $Root 'Project_RE.uproject'
# 엔진 CSV 프로파일러는 프로젝트 Saved 가 아니라 엔진 유저 디렉터리에 쓴다 (로그로 관측).
# 프로젝트 쪽도 같이 훑는다 — 엔진 설정이 바뀌면 그쪽으로 떨어질 수 있다.
$CsvDirs  = @(
    (Join-Path $env:LOCALAPPDATA 'UnrealEngine\5.8\Saved\Profiling\CSV'),
    (Join-Path $Root 'Saved\Profiling\CSV')
)

if (-not (Test-Path $Editor))   { throw "에디터 없음: $Editor" }
if (-not (Test-Path $Uproject)) { throw "uproject 없음: $Uproject" }

$Stamp  = Get-Date -Format 'yyyyMMdd-HHmmss'
# re.Profiling.KeepFiring 1: 즉사 DEFEAT가 보스 발사를 끊어 Mass 탄환이 0발로 측정되는 것을 막는다 (#46).
# Mass 경로: re.Bullets.Count N (Actor는 기본 0). Actor 경로: Mass boss(기본 480)를 0으로 죽이고 액터만.
if ($Actor) {
    $Tag     = 'Actor'
    $ExecCmd = "re.Profiling.KeepFiring 1,re.Bullets.Count 0,re.ActorBullets.Count $Bullets"
} else {
    $Tag     = 'Mass'
    $ExecCmd = "re.Profiling.KeepFiring 1,re.Bullets.Count $Bullets"
}
$RunDir = Join-Path $Root "Saved\Profiling\RE_${Tag}_${Bullets}_${Stamp}"
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
    '-trace=cpu,frame,counters',
    '-statnamedevents',
    "-tracefile=`"$TracePath`"",
    "-csvCaptureFrames=$Frames",
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
