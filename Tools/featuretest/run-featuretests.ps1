# 기능별 테스트 씬을 하나씩 불러와 화면을 PNG로 남긴다.
#
#   pwsh Tools/featuretest/run-featuretests.ps1
#
# 저작(build-scenes.ps1)과 나눈 이유: 씬이 저장되는 것과 그 씬이 제대로 보이는
# 것은 다른 문제다. 저작은 CLI 로그로 판정되지만 렌더 결과는 눈으로 봐야 하고,
# 그래서 이쪽은 그림을 남기는 것이 목적이다.
#
# 캡처는 밖에서 창을 찍는다(capture-window.ps1). 엔진 내부 캡처는 게임 스레드에서
# 죽는 문제로 보류돼 있다 — Tools/regression/README.md에 남아 있는 그 건이다.
param(
    [string]$Exe = "",
    [string]$OutDir = "",
    [int]$TimeoutSec = 180,
    [int]$Width = 1600,
    [int]$Height = 900,
    [string[]]$Only = @()
)

$ErrorActionPreference = "Stop"
$repoRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)

if ([string]::IsNullOrWhiteSpace($Exe)) {
    $Exe = Join-Path $repoRoot "x64\Debug\Academy_4Q.exe"
}
if ([string]::IsNullOrWhiteSpace($OutDir)) {
    $OutDir = Join-Path $PSScriptRoot "screenshots"
}

if (-not (Test-Path $Exe)) {
    Write-Host "실행 파일이 없다: $Exe" -ForegroundColor Red
    exit 1
}

$sceneDir = Join-Path $repoRoot "Dynamic_CPP\Assets\Scenes"
$capture = Join-Path $repoRoot "Tools\regression\capture-window.ps1"
$workDir = Join-Path ([System.IO.Path]::GetTempPath()) "CreatorEngine_FeatureTest"
New-Item -ItemType Directory -Force -Path $workDir | Out-Null
New-Item -ItemType Directory -Force -Path $OutDir | Out-Null

$scenes = @("FT_Primitives", "FT_Shadow", "FT_Material", "FT_Lights")
if ($Only.Count -gt 0) { $scenes = $scenes | Where-Object { $Only -contains $_ } }

$captured = @()
$failed = @()

foreach ($name in $scenes) {
    $scenePath = Join-Path $sceneDir "$name.creator"
    if (-not (Test-Path $scenePath)) {
        Write-Host "[$name] 씬이 없다 — build-scenes.ps1을 먼저 돌릴 것" -ForegroundColor Red
        $failed += $name
        continue
    }

    $png = Join-Path $OutDir "$name.png"
    if (Test-Path $png) { Remove-Item $png -Force }

    # 씬을 열고 오래 머문다. 캡처는 밖에서 하므로 엔진은 그동안 그리기만 하면
    # 되고, 캡처가 끝난 뒤 quit이 돌아 프로세스가 닫힌다.
    $cmdFile = Join-Path $workDir "run_$name.txt"
    $commands = @(
        "window.resize $Width $Height",
        "wait 20",
        "scene.switch $($scenePath -replace '\\', '/')",
        "wait 180",
        "scene.dump $name",
        "wait 600",
        "quit"
    )
    Set-Content -Path $cmdFile -Value ($commands -join "`n") -NoNewline -Encoding UTF8

    Write-Host ""
    Write-Host "[$name] 실행 중..."

    $proc = Start-Process -FilePath $Exe -ArgumentList "--script `"$cmdFile`"" `
        -WorkingDirectory (Split-Path -Parent $Exe) -PassThru -NoNewWindow

    # 씬 로드와 첫 프레임이 안정될 때까지 기다린 뒤 찍는다. 너무 일찍 찍으면
    # 로딩 중 화면이 남고, 그건 '렌더가 됐다'의 증거가 아니다.
    Start-Sleep -Seconds 12

    $captureOutput = & pwsh $capture -OutFile $png -TimeoutSec 30 2>&1
    $captureOk = ($LASTEXITCODE -eq 0)

    try { if (-not $proc.HasExited) { $proc | Stop-Process -Force } } catch {}

    if ($captureOk -and (Test-Path $png)) {
        $bytes = (Get-Item $png).Length
        Write-Host ("  캡처: {0} ({1:N0} 바이트)" -f (Split-Path -Leaf $png), $bytes) -ForegroundColor Green
        $captured += $name
    } else {
        Write-Host "  캡처 실패: $captureOutput" -ForegroundColor Red
        $failed += $name
    }
}

Write-Host ""
Write-Host "캡처 $($captured.Count)개 · 실패 $($failed.Count)개 · 출력 $OutDir"
if ($failed.Count -gt 0) {
    Write-Host "실패: $($failed -join ', ')" -ForegroundColor Red
    exit 1
}
exit 0
