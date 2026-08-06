[CmdletBinding()]
param(
    [ValidateSet("Preflight", "Build", "SelfTest", "LiveSmoke", "Verify", "PixCapture", "PixAnalyze", "RenderDoc")]
    [string]$Action = "Preflight",

    [ValidateSet("Off", "Basic", "Gpu")]
    [string]$Validation = "Gpu",

    [string]$ScenePath = "",
    [string]$CapturePath = "",
    [string]$OutputRoot = "",
    [int]$WaitFrames = 1800,
    [ValidateRange(10, 3600)]
    [int]$TimeoutSec = 180
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$repoRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$solutionPath = Join-Path $repoRoot "CreatorEngine.sln"
$exePath = Join-Path $repoRoot "x64\Debug\Academy_4Q.exe"
$baselinePath = Join-Path $PSScriptRoot "vcpkg-baseline.txt"
$requiredVcpkgBaseline = (Get-Content -LiteralPath $baselinePath -TotalCount 1).Trim()

if ([string]::IsNullOrWhiteSpace($OutputRoot)) {
    $OutputRoot = Join-Path $repoRoot "artifacts\dx12-validation"
}
$OutputRoot = [IO.Path]::GetFullPath($OutputRoot)

$sceneSwitchCommand = ""
if (-not [string]::IsNullOrWhiteSpace($ScenePath)) {
    $ScenePath = [IO.Path]::GetFullPath($ScenePath)
    if (-not (Test-Path -LiteralPath $ScenePath -PathType Leaf)) {
        throw "검증 씬 파일이 없다: $ScenePath"
    }
    $sceneSwitchCommand = "scene.switch " + $ScenePath.Replace('\', '/')
}

function Test-File([string]$Path) {
    return -not [string]::IsNullOrWhiteSpace($Path) -and
        (Test-Path -LiteralPath $Path -PathType Leaf)
}

function Find-MSBuild {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
    if (Test-File $vswhere) {
        $found = & $vswhere -latest -products * -requires Microsoft.Component.MSBuild `
            -find "MSBuild\**\Bin\MSBuild.exe" | Select-Object -First 1
        if (-not [string]::IsNullOrWhiteSpace($found)) { return $found.Trim() }
    }

    $fallback = Join-Path $env:ProgramFiles "Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe"
    if (Test-File $fallback) { return $fallback }
    return ""
}

function Find-VcpkgRoot {
    $sourceRoot = Split-Path -Parent (Split-Path -Parent $repoRoot)
    $candidates = @(
        $env:VCPKG_ROOT,
        (Join-Path $sourceRoot "vcpkg"),
        (Join-Path ([Environment]::GetFolderPath("UserProfile")) "source\vcpkg")
    ) | Where-Object { -not [string]::IsNullOrWhiteSpace($_) } | Select-Object -Unique

    foreach ($candidate in $candidates) {
        if (Test-File (Join-Path $candidate "vcpkg.exe")) { return $candidate }
    }
    return ""
}

function Find-PixTool {
    $pixRoot = Join-Path $env:ProgramFiles "Microsoft PIX"
    if (-not (Test-Path -LiteralPath $pixRoot -PathType Container)) { return "" }

    $candidate = Get-ChildItem -LiteralPath $pixRoot -Directory |
        Sort-Object Name -Descending |
        ForEach-Object { Join-Path $_.FullName "pixtool.exe" } |
        Where-Object { Test-File $_ } |
        Select-Object -First 1
    return [string]$candidate
}

function Find-RenderDoc {
    $candidate = Join-Path $env:ProgramFiles "RenderDoc\renderdoccmd.exe"
    if (Test-File $candidate) { return $candidate }
    return ""
}

function Find-CudaRoot {
    $candidates = @(
        $env:CUDA_PATH_V12_8,
        (Join-Path $env:ProgramFiles "NVIDIA GPU Computing Toolkit\CUDA\v12.8")
    ) | Where-Object { -not [string]::IsNullOrWhiteSpace($_) } | Select-Object -Unique

    foreach ($candidate in $candidates) {
        if (Test-File (Join-Path $candidate "bin\nvcc.exe")) { return $candidate }
    }
    return ""
}

function Find-DotNetHostPack {
    [xml]$props = Get-Content -LiteralPath (Join-Path $repoRoot "EngineOutput.props")
    $version = [string]$props.Project.PropertyGroup.DotNetHostPackVersion
    if ([string]::IsNullOrWhiteSpace($version)) { return "" }

    $native = Join-Path $env:ProgramFiles `
        "dotnet\packs\Microsoft.NETCore.App.Host.win-x64\$version\runtimes\win-x64\native"
    if ((Test-File (Join-Path $native "nethost.h")) -and
        (Test-File (Join-Path $native "nethost.lib")) -and
        (Test-File (Join-Path $native "nethost.dll"))) { return $native }
    return ""
}

function Test-PixDeveloperMode {
    try {
        $value = Get-ItemPropertyValue `
            -LiteralPath "HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\AppModelUnlock" `
            -Name "AllowDevelopmentWithoutDevLicense" -ErrorAction Stop
        return 1 -eq $value
    }
    catch { return $false }
}

$toolPaths = [ordered]@{
    MSBuild = Find-MSBuild
    VcpkgRoot = Find-VcpkgRoot
    PIX = Find-PixTool
    RenderDoc = Find-RenderDoc
    CUDA = Find-CudaRoot
    DotNetHost = Find-DotNetHostPack
}

function Get-VcpkgHead {
    if ([string]::IsNullOrWhiteSpace($toolPaths.VcpkgRoot)) { return "" }
    $git = Get-Command git -ErrorAction SilentlyContinue
    if ($null -eq $git) { return "" }

    $head = & $git.Source -C $toolPaths.VcpkgRoot rev-parse HEAD 2>$null
    if ($LASTEXITCODE -ne 0 -or [string]::IsNullOrWhiteSpace($head)) { return "" }
    return $head.Trim()
}

function Invoke-Preflight {
    $vcpkgHead = Get-VcpkgHead
    $vcpkgVs18File = Join-Path $toolPaths.VcpkgRoot "scripts\cmake\vcpkg_configure_cmake.cmake"
    $vcpkgVs18Patched = (Test-File $vcpkgVs18File) -and
        [bool](Select-String -LiteralPath $vcpkgVs18File -SimpleMatch "Visual Studio 18 2026" -Quiet)
    $checks = @(
        [pscustomobject]@{ Item = "MSBuild"; OK = (Test-File $toolPaths.MSBuild); Value = $toolPaths.MSBuild },
        [pscustomobject]@{ Item = "vcpkg"; OK = (Test-File (Join-Path $toolPaths.VcpkgRoot "vcpkg.exe")); Value = $toolPaths.VcpkgRoot },
        [pscustomobject]@{ Item = "vcpkg baseline"; OK = ($vcpkgHead -eq $requiredVcpkgBaseline); Value = $vcpkgHead },
        [pscustomobject]@{ Item = "vcpkg VS18 patch"; OK = $vcpkgVs18Patched; Value = $vcpkgVs18File },
        [pscustomobject]@{ Item = "CUDA 12.8"; OK = (Test-File (Join-Path $toolPaths.CUDA "bin\nvcc.exe")); Value = $toolPaths.CUDA },
        [pscustomobject]@{ Item = ".NET host pack"; OK = -not [string]::IsNullOrWhiteSpace($toolPaths.DotNetHost); Value = $toolPaths.DotNetHost },
        [pscustomobject]@{ Item = "PIX"; OK = (Test-File $toolPaths.PIX); Value = $toolPaths.PIX },
        [pscustomobject]@{ Item = "PIX Developer Mode"; OK = (Test-PixDeveloperMode); Value = "PIX replay/export에 필요" },
        [pscustomobject]@{ Item = "RenderDoc"; OK = (Test-File $toolPaths.RenderDoc); Value = $toolPaths.RenderDoc },
        [pscustomobject]@{ Item = "Debug executable"; OK = (Test-File $exePath); Value = $exePath }
    )

    Write-Host "CreatorEngine DX12 validation preflight"
    Write-Host ($checks | Format-Table -AutoSize | Out-String)

    # 실행 파일은 Build가 만드는 산출물이므로 최초 Preflight의 필수 조건은 아니다.
    $required = $checks | Where-Object { $_.Item -notin @("Debug executable", "PIX Developer Mode") }
    return -not ($required | Where-Object { -not $_.OK })
}

function Ensure-EngineVersionHeader {
    $versionPath = Join-Path $repoRoot "version.txt"
    $headerPath = Join-Path $repoRoot "EngineEntry\EngineVersion.h"
    $version = (Get-Content -LiteralPath $versionPath -TotalCount 1).Trim()
    $content = "#pragma once`r`n#define ENGINE_VERSION `"$version`"`r`n"

    $current = if (Test-File $headerPath) { Get-Content -LiteralPath $headerPath -Raw } else { "" }
    if ($current -ne $content) {
        [IO.File]::WriteAllText($headerPath, $content, [Text.UTF8Encoding]::new($false))
    }
}

function Invoke-Build {
    Ensure-EngineVersionHeader
    Write-Host "Debug|x64 전체 솔루션 빌드"
    & $toolPaths.MSBuild $solutionPath /m /t:Build /p:Configuration=Debug /p:Platform=x64 `
        "/p:VcpkgRoot=$($toolPaths.VcpkgRoot)\" /nologo /verbosity:minimal
    if ($LASTEXITCODE -ne 0) { throw "MSBuild failed with exit code $LASTEXITCODE" }
}

function New-RunDirectory([string]$Name) {
    $stamp = Get-Date -Format "yyyyMMdd-HHmmss"
    $path = Join-Path $OutputRoot "$stamp-$Name"
    New-Item -ItemType Directory -Force -Path $path | Out-Null
    return $path
}

function Write-CommandFile([string]$Path, [string[]]$Commands) {
    [IO.File]::WriteAllText($Path, ($Commands -join "`r`n") + "`r`n", [Text.UTF8Encoding]::new($false))
}

function Invoke-EngineScript([string]$CommandFile, [string]$RunDirectory) {
    if (-not (Test-File $exePath)) { throw "실행 파일이 없다. -Action Build를 먼저 실행한다: $exePath" }

    $stdoutPath = Join-Path $RunDirectory "stdout.log"
    $stderrPath = Join-Path $RunDirectory "stderr.log"
    $startInfo = [Diagnostics.ProcessStartInfo]::new()
    $startInfo.FileName = $exePath
    $startInfo.WorkingDirectory = Split-Path -Parent $exePath
    $startInfo.UseShellExecute = $false
    $startInfo.CreateNoWindow = $true
    $startInfo.RedirectStandardOutput = $true
    $startInfo.RedirectStandardError = $true
    $startInfo.StandardOutputEncoding = [Text.Encoding]::UTF8
    $startInfo.StandardErrorEncoding = [Text.Encoding]::UTF8
    [void]$startInfo.ArgumentList.Add("--script")
    [void]$startInfo.ArgumentList.Add($CommandFile)
    $startInfo.Environment["CREATOR_DX12_VALIDATION"] = $Validation.ToLowerInvariant()
    $startInfo.Environment["CREATOR_DX12_DRED"] = "1"
    $startInfo.Environment["CREATOR_DX12_BREAK_ON_ERROR"] = "0"

    $process = [Diagnostics.Process]::new()
    $process.StartInfo = $startInfo
    if (-not $process.Start()) { throw "엔진 프로세스를 시작하지 못했다" }

    $stdoutTask = $process.StandardOutput.ReadToEndAsync()
    $stderrTask = $process.StandardError.ReadToEndAsync()
    if (-not $process.WaitForExit($TimeoutSec * 1000)) {
        try { $process.Kill($true) } catch {}
        throw "엔진 실행이 ${TimeoutSec}초 안에 끝나지 않았다. PID=$($process.Id)"
    }
    $process.WaitForExit()

    $stdout = $stdoutTask.GetAwaiter().GetResult()
    $stderr = $stderrTask.GetAwaiter().GetResult()
    [IO.File]::WriteAllText($stdoutPath, $stdout, [Text.UTF8Encoding]::new($false))
    [IO.File]::WriteAllText($stderrPath, $stderr, [Text.UTF8Encoding]::new($false))

    return [pscustomobject]@{
        ExitCode = $process.ExitCode
        Stdout = $stdout
        Stderr = $stderr
        StdoutPath = $stdoutPath
        StderrPath = $stderrPath
    }
}

function Invoke-SelfTest {
    $runDirectory = New-RunDirectory "selftest-$($Validation.ToLowerInvariant())"
    $pngPath = Join-Path $runDirectory "dx12-selftest.png"
    $commandFile = Join-Path $runDirectory "commands.txt"
    Write-CommandFile $commandFile @(
        "dx12.selftest $($pngPath.Replace('\', '/'))",
        "wait 10",
        "quit"
    )

    $result = Invoke-EngineScript $commandFile $runDirectory
    $combined = $result.Stdout + "`n" + $result.Stderr
    $passed = $result.ExitCode -eq 0 -and $combined -match "dx12\.selftest 통과"
    Write-Host "SelfTest exit=$($result.ExitCode) output=$runDirectory"
    if (-not $passed) { throw "DX12 self-test failed. 로그: $($result.StdoutPath), $($result.StderrPath)" }
    if (-not (Test-File $pngPath)) { throw "자가 검증은 통과했지만 PNG가 없다: $pngPath" }
}

function Invoke-LiveSmoke {
    $runDirectory = New-RunDirectory "live-$($Validation.ToLowerInvariant())"
    $commandFile = Join-Path $runDirectory "commands.txt"
    $commands = @()
    if (-not [string]::IsNullOrWhiteSpace($sceneSwitchCommand)) {
        $commands += $sceneSwitchCommand
    }
    $liveFrames = if ([string]::IsNullOrWhiteSpace($sceneSwitchCommand)) {
        $WaitFrames
    } else {
        [Math]::Max($WaitFrames, 5000)
    }
    $commands += @(
        "render.backend dx12",
        "wait $liveFrames",
        "render.backend status",
        "quit"
    )
    Write-CommandFile $commandFile $commands

    $result = Invoke-EngineScript $commandFile $runDirectory
    $combined = $result.Stdout + "`n" + $result.Stderr
    $validationFailure = $combined -match "\[dx12\.live 검증\]" -or
        $combined -match "\[CORRUPTION\]" -or $combined -match "\[DRED\]"
    $passed = $result.ExitCode -eq 0 -and
        $combined -match "활성: enhanced-dx12 \(단독\)" -and -not $validationFailure
    Write-Host "LiveSmoke exit=$($result.ExitCode) output=$runDirectory"
    if (-not $passed) { throw "DX12 live smoke failed. 로그: $($result.StdoutPath), $($result.StderrPath)" }
}

function Invoke-PixCapture {
    if (-not (Test-File $exePath)) { throw "실행 파일이 없다. -Action Build를 먼저 실행한다" }
    $runDirectory = New-RunDirectory "pix"
    $commandFile = Join-Path $runDirectory "commands.txt"
    $capture = Join-Path $runDirectory "creator-dx12.wpix"
    $warmupFrames = [Math]::Max($WaitFrames, 5000)
    $commands = @()
    if (-not [string]::IsNullOrWhiteSpace($sceneSwitchCommand)) {
        $commands += $sceneSwitchCommand
    }
    $commands += @(
        "render.backend dx12",
        "wait $warmupFrames",
        "pix.capture begin",
        "wait 5",
        "pix.capture end",
        "wait 60",
        "quit"
    )
    Write-CommandFile $commandFile $commands

    # pixtool은 --command-line="..."에서 따옴표가 option 이름 바깥이 아니라
    # '=' 뒤에 있어야 한다. PowerShell native binder는 이 모양을 바꾸므로 raw
    # Arguments를 사용한다. 아래 경로는 모두 이 스크립트가 만든 절대 경로다.
    $pixArguments = 'launch "' + $exePath + '"' +
        ' --command-line="--script ' + $commandFile + '"' +
        ' --working-directory="' + (Split-Path -Parent $exePath) + '"' +
        ' --setenv="CREATOR_DX12_VALIDATION=' + $Validation.ToLowerInvariant() + '"' +
        ' --setenv="CREATOR_DX12_DRED=1"' +
        ' --setenv="CREATOR_DX12_BREAK_ON_ERROR=0"' +
        ' programmatic-capture --open save-capture "' + $capture + '"'

    $startInfo = [Diagnostics.ProcessStartInfo]::new()
    $startInfo.FileName = $toolPaths.PIX
    $startInfo.Arguments = $pixArguments
    $startInfo.WorkingDirectory = $repoRoot
    $startInfo.UseShellExecute = $false
    $startInfo.CreateNoWindow = $true
    $startInfo.RedirectStandardOutput = $true
    $startInfo.RedirectStandardError = $true
    $process = [Diagnostics.Process]::Start($startInfo)
    $stdoutTask = $process.StandardOutput.ReadToEndAsync()
    $stderrTask = $process.StandardError.ReadToEndAsync()
    if (-not $process.WaitForExit($TimeoutSec * 1000)) {
        try { $process.Kill($true) } catch {}
        throw "PIX capture timed out after ${TimeoutSec}s"
    }
    $process.WaitForExit()
    $pixStdout = $stdoutTask.GetAwaiter().GetResult()
    $pixStderr = $stderrTask.GetAwaiter().GetResult()
    [IO.File]::WriteAllText((Join-Path $runDirectory "pixtool.stdout.log"), $pixStdout, [Text.UTF8Encoding]::new($false))
    [IO.File]::WriteAllText((Join-Path $runDirectory "pixtool.stderr.log"), $pixStderr, [Text.UTF8Encoding]::new($false))
    if ($process.ExitCode -ne 0 -or -not (Test-File $capture)) { throw "PIX capture failed: $capture" }
    Write-Host "PIX capture: $capture"
}

function Invoke-PixAnalyze {
    if ([string]::IsNullOrWhiteSpace($CapturePath) -or -not (Test-File $CapturePath)) {
        throw "-CapturePath에 기존 .wpix 파일을 지정한다"
    }
    if (-not (Test-PixDeveloperMode)) {
        throw "PIX replay/export에는 Windows Developer Mode가 필요하다. Windows 설정에서 '개발자 모드'를 검색해 켠 뒤 다시 실행한다"
    }
    $runDirectory = New-RunDirectory "pix-analysis"
    $events = Join-Path $runDirectory "events.csv"
    $screenshot = Join-Path $runDirectory "screenshot.png"
    # 프로그램 캡처 대상이 DX12 오프스크린 RT이고 최종 Present는 DX11인 경우,
    # PIX 캡처에는 gold image가 없어 save-screenshot이 E_POINTER로 실패한다.
    # Debug Layer 재생과 이벤트 목록은 그 경우에도 유효한 핵심 산출물이므로
    # 먼저 필수 단계로 확정하고, 스크린샷은 별도 선택 단계로 처리한다.
    & $toolPaths.PIX open-capture $CapturePath run-debug-layer `
        save-event-list $events "--counter-groups=D3D:*"
    if ($LASTEXITCODE -ne 0) { throw "PIX replay/event export failed: $CapturePath" }

    & $toolPaths.PIX open-capture $CapturePath save-screenshot $screenshot
    if ($LASTEXITCODE -ne 0) {
        Write-Warning "PIX screenshot unavailable (offscreen capture may have no gold image): $CapturePath"
        Remove-Item -LiteralPath $screenshot -Force -ErrorAction SilentlyContinue
    }
    Write-Host "PIX analysis: $runDirectory"
}

function Invoke-RenderDoc {
    if (-not (Test-File $exePath)) { throw "실행 파일이 없다. -Action Build를 먼저 실행한다" }
    $runDirectory = New-RunDirectory "renderdoc"
    $commandFile = Join-Path $runDirectory "commands.txt"
    $capturePrefix = Join-Path $runDirectory "creator-dx12"
    $commands = @()
    if (-not [string]::IsNullOrWhiteSpace($sceneSwitchCommand)) {
        $commands += $sceneSwitchCommand
    }
    $commands += @("render.backend dx12", "wait $WaitFrames", "quit")
    Write-CommandFile $commandFile $commands

    $oldValidation = $env:CREATOR_DX12_VALIDATION
    $oldDred = $env:CREATOR_DX12_DRED
    $oldBreak = $env:CREATOR_DX12_BREAK_ON_ERROR
    try {
        $env:CREATOR_DX12_VALIDATION = $Validation.ToLowerInvariant()
        $env:CREATOR_DX12_DRED = "1"
        $env:CREATOR_DX12_BREAK_ON_ERROR = "0"
        Write-Host "RenderDoc가 실행되면 캡처할 프레임에서 F12를 누른다. 출력: $runDirectory"
        & $toolPaths.RenderDoc capture --wait-for-exit `
            --working-dir (Split-Path -Parent $exePath) `
            --capture-file $capturePrefix `
            --opt-api-validation --opt-capture-callstacks --opt-capture-callstacks-only-actions `
            $exePath --script $commandFile
        if ($LASTEXITCODE -ne 0) { throw "RenderDoc launch failed with exit code $LASTEXITCODE" }
    }
    finally {
        $env:CREATOR_DX12_VALIDATION = $oldValidation
        $env:CREATOR_DX12_DRED = $oldDred
        $env:CREATOR_DX12_BREAK_ON_ERROR = $oldBreak
    }
}

if (-not (Invoke-Preflight)) { throw "Preflight failed. 위 경로와 vcpkg baseline을 확인한다" }

switch ($Action) {
    "Preflight" { break }
    "Build" { Invoke-Build }
    "SelfTest" { Invoke-SelfTest }
    "LiveSmoke" { Invoke-LiveSmoke }
    "Verify" {
        Invoke-Build
        Invoke-SelfTest
        Invoke-LiveSmoke
    }
    "PixCapture" { Invoke-PixCapture }
    "PixAnalyze" { Invoke-PixAnalyze }
    "RenderDoc" { Invoke-RenderDoc }
}

Write-Host "DX12 validation action '$Action' completed."
