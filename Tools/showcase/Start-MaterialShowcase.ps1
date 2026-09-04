# CLI로 FT_Material 씬을 저작·로드하고, 사람이 화면을 조작하거나 녹화할 수 있는
# 상태로 Editor를 유지한다.
#
# 기본 실행:
#   pwsh Tools/showcase/Start-MaterialShowcase.ps1
#
# 산출물은 git ignore 대상인 Artifacts/material-showcase 아래에 둔다. 저장소의
# canonical scene이나 프로젝트 설정은 수정하지 않는다.
[CmdletBinding()]
param(
    [string]$Exe = "",
    [string]$OutDir = "",
    [int]$BuildTimeoutSec = 180,
    [int]$WindowTimeoutSec = 60,
    [int]$ReadyDelaySec = 22,
    [int]$Width = 1280,
    [int]$Height = 720,
    [int]$WarmupFrames = 240,
    [int]$HoldFrames = 1000000,
    [switch]$ResizeTo,
    [switch]$KeepFog,
    [switch]$VisibleConsole,
    [switch]$SkipSceneBuild,
    [switch]$WaitForExit
)

$ErrorActionPreference = "Stop"
$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot "..\.."))

if ([string]::IsNullOrWhiteSpace($Exe)) {
    $Exe = Join-Path $repoRoot "Bin\x64-Debug\Editor\CreatorEditor.exe"
}
$Exe = [IO.Path]::GetFullPath($Exe)

if ([string]::IsNullOrWhiteSpace($OutDir)) {
    $OutDir = Join-Path $repoRoot "Artifacts\material-showcase"
}
$OutDir = [IO.Path]::GetFullPath($OutDir)

if (-not (Test-Path -LiteralPath $Exe -PathType Leaf)) {
    throw "실행 파일이 없다: $Exe"
}
if ($BuildTimeoutSec -le 0 -or $WindowTimeoutSec -le 0 -or
    $ReadyDelaySec -lt 0 -or $Width -le 0 -or $Height -le 0 -or
    $WarmupFrames -lt 0 -or $HoldFrames -le 0) {
    throw "시간 제한은 양수, 대기 프레임은 0 이상, 유지 프레임은 양수여야 한다."
}

$runningEditors = @(Get-Process -Name "CreatorEditor" -ErrorAction SilentlyContinue)
if ($runningEditors.Count -gt 0) {
    $ids = ($runningEditors | ForEach-Object { $_.Id }) -join ", "
    throw "이미 실행 중인 CreatorEditor가 있다(PID: $ids). 캡처 대상을 모호하게 만들 수 있어 중단한다."
}

$sceneDir = Join-Path $OutDir "Scenes"
$scenePath = Join-Path $sceneDir "FT_Material.creator"
$commandFile = Join-Path $OutDir "material-showcase.cli.txt"
$consoleLauncher = Join-Path $OutDir "material-showcase.cmd"
$stillPath = Join-Path $OutDir "material-showcase.png"
$buildScript = Join-Path $repoRoot "Tools\featuretest\build-scenes.ps1"
$captureScript = Join-Path $repoRoot "Tools\regression\capture-window.ps1"

New-Item -ItemType Directory -Force -Path $sceneDir | Out-Null

# 모델 로더는 로드 시 .glb.meta의 importer timestamp를 현재 소스 시각으로
# 동기화할 수 있다. showcase 실행만으로 추적 파일이 바뀌지 않도록, 실행 전에
# 있던 정확한 바이트를 보존한다. 사용자 변경이 있었다면 그 상태 그대로 복원된다.
$metaSnapshots = @{}
foreach ($metaName in @("Prim_Plane.glb.meta", "Prim_MatGrid.glb.meta")) {
    $metaPath = Join-Path $repoRoot "Dynamic_CPP\Assets\Models\$metaName"
    if (Test-Path -LiteralPath $metaPath -PathType Leaf) {
        $metaSnapshots[$metaPath] = [IO.File]::ReadAllBytes($metaPath)
    }
}
function Restore-ModelMetaSnapshots {
    foreach ($entry in $metaSnapshots.GetEnumerator()) {
        [IO.File]::WriteAllBytes($entry.Key, $entry.Value)
    }
}

if (-not $SkipSceneBuild) {
    Write-Host "[showcase] FT_Material 씬을 CLI로 저작한다."
    $buildExitCode = 1
    try {
        & pwsh -NoProfile -File $buildScript `
            -Exe $Exe `
            -SceneDir $sceneDir `
            -Only FT_Material `
            -TimeoutSec $BuildTimeoutSec
        $buildExitCode = $LASTEXITCODE
    }
    finally {
        Restore-ModelMetaSnapshots
    }
    if ($buildExitCode -ne 0) {
        throw "FT_Material 씬 저작·스키마 검증에 실패했다(exit $buildExitCode)."
    }
}

if (-not (Test-Path -LiteralPath $scenePath -PathType Leaf)) {
    throw "머터리얼 씬이 없다: $scenePath"
}

# ConsoleCommandSystem tokenizer는 큰따옴표 경로를 지원한다. 경로를 항상 절대경로로
# 기록해 Editor의 작업 디렉터리(Bin/x64-Debug/Editor)에 따라 결과가 달라지지 않게 한다.
$sceneCliPath = $scenePath.Replace('\', '/')
$commands = @()
if ($ResizeTo) {
    $commands += @("window.resize $Width $Height", "wait 20")
}
$commands += @("scene.switch `"$sceneCliPath`"", "wait $WarmupFrames")
if (-not $KeepFog) {
    # ★ 포그를 런타임으로 끌 수 없다. `render.post fog off` 는 DX12 전환 이후
    #   아무것도 안 하는 무효 명령이었고 LC6 이 표에서 지웠다. 자세한 사연은
    #   `Tools/featuretest/run-featuretests.ps1` 의 같은 자리에 적었다.
    Write-Warning ("포그를 런타임으로 끌 수 없다(Enhanced PostChain 튜닝 API 부재). " +
                   "그림에 포그가 남는다 — 이전 캡처도 마찬가지였다.")
}
$commands += @(
    "scene.dump MaterialShowcase",
    "wait $HoldFrames",
    "quit"
)
Set-Content -LiteralPath $commandFile -Value ($commands -join "`n") `
    -NoNewline -Encoding UTF8

if (Test-Path -LiteralPath $stillPath) {
    Remove-Item -LiteralPath $stillPath -Force
}

Write-Host "[showcase] Editor를 시작한다."
$editorWorkingDirectory = Split-Path -Parent $Exe
$consoleProcess = $null
if ($VisibleConsole) {
    # GUI 실행 파일이 부모 cmd 콘솔에 붙도록 별도 launcher를 연다. --console은
    # 표준 입력 reader도 켜므로, 긴 wait 동안 로그를 보고 이후 명령을 입력할 수 있다.
    $launcherLines = @(
        "@echo off",
        "chcp 65001 >nul",
        "title CreatorEngine CLI - FT_Material",
        "cd /d `"$editorWorkingDirectory`"",
        "`"$Exe`" --console --script `"$commandFile`"",
        "exit /b %errorlevel%"
    )
    Set-Content -LiteralPath $consoleLauncher -Value ($launcherLines -join "`r`n") `
        -NoNewline -Encoding ASCII

    $consoleArgumentLine = "/d /s /c `"`"$consoleLauncher`"`""
    $consoleProcess = Start-Process -FilePath $env:ComSpec `
        -ArgumentList $consoleArgumentLine `
        -WorkingDirectory $editorWorkingDirectory `
        -PassThru -WindowStyle Normal

    $editorDeadline = (Get-Date).AddSeconds($WindowTimeoutSec)
    $process = $null
    while ((Get-Date) -lt $editorDeadline) {
        $process = Get-Process -Name "CreatorEditor" -ErrorAction SilentlyContinue |
            Select-Object -First 1
        if ($process) { break }
        $consoleProcess.Refresh()
        if ($consoleProcess.HasExited) {
            throw "CLI 콘솔이 Editor를 시작하기 전에 종료됐다(exit $($consoleProcess.ExitCode))."
        }
        Start-Sleep -Milliseconds 250
    }
    if (-not $process) {
        try { $consoleProcess | Stop-Process -Force } catch {}
        throw "$WindowTimeoutSec 초 안에 CLI 콘솔에서 Editor 프로세스를 찾지 못했다."
    }
}
else {
    $argumentLine = "--script `"$commandFile`""
    $process = Start-Process -FilePath $Exe `
        -ArgumentList $argumentLine `
        -WorkingDirectory $editorWorkingDirectory `
        -PassThru -NoNewWindow
}

$deadline = (Get-Date).AddSeconds($WindowTimeoutSec)
$windowReady = $false
while ((Get-Date) -lt $deadline) {
    $process.Refresh()
    if ($process.HasExited) {
        Restore-ModelMetaSnapshots
        throw "Editor가 창을 만들기 전에 종료됐다(exit $($process.ExitCode))."
    }
    if ($process.MainWindowHandle -ne [IntPtr]::Zero) {
        $windowReady = $true
        break
    }
    Start-Sleep -Milliseconds 500
}
if (-not $windowReady) {
    try { $process | Stop-Process -Force } catch {}
    Restore-ModelMetaSnapshots
    throw "$WindowTimeoutSec 초 안에 Editor 창을 찾지 못했다."
}

# 창 생성 직후에는 로딩 화면일 수 있다. 기존 feature-test 캡처와 같은 기본값으로
# 씬 로드와 렌더 파이프라인이 안정될 시간을 준다.
if ($ReadyDelaySec -gt 0) {
    Start-Sleep -Seconds $ReadyDelaySec
}

$process.Refresh()
if ($process.HasExited) {
    Restore-ModelMetaSnapshots
    throw "준비 대기 중 Editor가 종료됐다(exit $($process.ExitCode))."
}

& pwsh -NoProfile -File $captureScript `
    -ProcessName CreatorEditor `
    -OutFile $stillPath `
    -TimeoutSec $WindowTimeoutSec
if ($LASTEXITCODE -ne 0 -or -not (Test-Path -LiteralPath $stillPath -PathType Leaf)) {
    try { $process | Stop-Process -Force } catch {}
    Restore-ModelMetaSnapshots
    throw "준비 상태 스크린샷 검증에 실패했다."
}

$still = Get-Item -LiteralPath $stillPath
Restore-ModelMetaSnapshots
Write-Host ""
Write-Host "[showcase] 녹화 준비 완료" -ForegroundColor Green
Write-Host "SHOWCASE_PID=$($process.Id)"
if ($consoleProcess) {
    Write-Host "SHOWCASE_CONSOLE_PID=$($consoleProcess.Id)"
}
Write-Host "SHOWCASE_SCENE=$scenePath"
Write-Host "SHOWCASE_COMMAND_FILE=$commandFile"
Write-Host "SHOWCASE_STILL=$stillPath"
Write-Host "SHOWCASE_STILL_BYTES=$($still.Length)"
Write-Host "[showcase] $HoldFrames 프레임 뒤 CLI quit이 실행된다."

if ($WaitForExit) {
    $process.WaitForExit()
    exit $process.ExitCode
}

exit 0
