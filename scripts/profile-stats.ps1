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
        Write-Warning "frames.csv 없음 - 건너뜀: $d"
        continue
    }

    # frames.csv 는 끝에 헤더 1행 + 메타 1행([HasHeaderRowAtEnd])이 더 붙는다.
    # 그대로 ConvertFrom-Csv 하면 두 행이 데이터로 섞여 평균이 깨진다.
    $lines = Get-Content -LiteralPath $csv
    if ($lines.Count -lt 4) { Write-Warning "행이 너무 적음 - 건너뜀: $d"; continue }
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
