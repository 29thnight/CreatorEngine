[CmdletBinding()]
param(
    [ValidateSet('Debug', 'Release')]
    [string]$Configuration = 'Debug',
    [string]$VisualStudioInstallation = '',
    [ValidateRange(5, 300)]
    [int]$ShutdownTimeoutSeconds = 60,
    [ValidateRange(0, 20)]
    [int]$PerformanceRuns = 0
)

# Phase 22 exact-source gate. No engine unity libraries or legacy vendor runtime.
# Both the explicit no-device PCM renderer and the miniaudio software device are
# exercised. This is NOT a physical output, WASAPI, Editor/Player, or package gate.
# Fixtures are deterministically generated under ignored Build/Validation only.
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$build = Join-Path $repoRoot "Build\Validation\Phase22Portable\windows-$($Configuration.ToLowerInvariant())"
[IO.Directory]::CreateDirectory($build) | Out-Null
if ([string]::IsNullOrWhiteSpace($VisualStudioInstallation)) {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (-not (Test-Path -LiteralPath $vswhere -PathType Leaf)) {
        throw 'Visual Studio C++ compiler locator was not found'
    }
    $installations = @(& $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath)
    if ($LASTEXITCODE -ne 0 -or $installations.Count -eq 0) {
        throw 'Visual Studio x64 C++ toolchain was not found'
    }
    $VisualStudioInstallation = $installations[0]
}
$vcvars = Join-Path $VisualStudioInstallation 'VC\Auxiliary\Build\vcvars64.bat'
if (-not (Test-Path -LiteralPath $vcvars -PathType Leaf)) {
    throw "Compiler environment script is missing: $vcvars"
}
$sources = @('Tools\regression\audio_voice_contract_probe.cpp')
foreach ($name in @('VoiceTable', 'NullAudioBackend', 'AudioRuntime', 'AudioHost', 'ClipDirectory', 'MiniaudioBackend', 'SoundGraph', 'PlaybackService')) {
    $sources += "Engine\SceneRuntime\Audio\$name.cpp"
}
$includes = @('Engine\SceneRuntime', 'Engine\SceneRuntime\Audio', 'Engine\RenderEngine', 'Engine\Utility_Framework', 'ThirdParty\Mathematics\include', 'Tools\regression')
$ledger = [ordered]@{}
foreach ($source in $sources) {
    $full = Join-Path $repoRoot $source
    if (-not (Test-Path -LiteralPath $full -PathType Leaf)) {
        throw "Exact production source is missing: $full"
    }
    $ledger[$source] = (Get-FileHash -LiteralPath $full -Algorithm SHA256).Hash.ToLowerInvariant()
}
$ledger | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $build 'source-sha256.json') -Encoding UTF8
$flags = '/nologo /std:c++latest /EHsc /utf-8 /W4 /DNOMINMAX /DWAVE_AUDIO_PROBE /DMA_ENABLE_ONLY_SPECIFIC_BACKENDS /DMA_ENABLE_NULL'
if ($Configuration -eq 'Debug') {
    $flags += ' /Od /Z7 /MTd'
} else {
    $flags += ' /O2 /DNDEBUG /MT'
}
$includeArgs = ($includes | ForEach-Object { '/I"' + (Join-Path $repoRoot $_) + '"' }) -join ' '
$sourceArgs = ($sources | ForEach-Object { '"' + (Join-Path $repoRoot $_) + '"' }) -join ' '
$exe = Join-Path $build 'audio_voice_contract_probe.exe'
$batch = Join-Path $build 'build.cmd'
@"
@echo off
call "$vcvars" >nul
if errorlevel 1 exit /b %errorlevel%
cd /d "$build"
cl.exe $flags $includeArgs $sourceArgs /Fe"$exe" /link ole32.lib winmm.lib
exit /b %errorlevel%
"@ | Set-Content -LiteralPath $batch -Encoding ASCII
& cmd.exe /d /c $batch 2>&1 | Tee-Object -FilePath (Join-Path $build 'build.log')
if ($LASTEXITCODE -ne 0) {
    throw "Exact-source audio probe compilation failed ($LASTEXITCODE)"
}
$records = @()
$failed = $false
foreach ($suite in @('core', 'decode', 'render', 'software-device')) {
    $log = Join-Path $build "$suite.log"
    $errorLog = Join-Path $build "$suite.stderr.log"
    $work = Join-Path $build "$suite-work"
    $watch = [Diagnostics.Stopwatch]::StartNew()
    $process = Start-Process -FilePath $exe -ArgumentList @($suite, ('"' + $work + '"')) -PassThru -NoNewWindow -RedirectStandardOutput $log -RedirectStandardError $errorLog
    if (-not $process.WaitForExit($ShutdownTimeoutSeconds * 1000)) {
        $process.Kill()
        throw "$suite exceeded the bounded $ShutdownTimeoutSeconds second deadline"
    }
    $process.WaitForExit()
    $watch.Stop()
    $code = $process.ExitCode
    $text = Get-Content -Raw -LiteralPath $log
    $records += [ordered]@{ suite = $suite; exit_code = $code; seconds = $watch.Elapsed.TotalSeconds; log = $log }
    Write-Host $text
    if ($code -ne 0) {
        $failed = $true
    }
}
[ordered]@{ configuration = $Configuration; physical_device = 'not tested'; source_mode = 'exact production translation units'; suites = $records } |
    ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Join-Path $build 'results.json') -Encoding UTF8
if ($PerformanceRuns -gt 0) {
    Write-Warning 'PerformanceRuns is retained for command compatibility; this contract gate does not certify latency or performance'
}
if ($failed) {
    throw 'One or more Phase 22 contract suites failed; see results.json and logs'
}
Write-Host '[PHASE22 AUDIO] All exact-source contracts passed; physical device and packaged application remain separate gates'
