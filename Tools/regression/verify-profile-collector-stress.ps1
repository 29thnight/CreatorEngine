#Requires -Version 7.0
[CmdletBinding()]
param(
    [ValidateRange(5, 300)][int]$DurationSec = 20,
    [ValidateSet('Debug', 'Release', 'All')][string]$Configuration = 'All',
    [string]$OutputRoot
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$repo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$core = Join-Path $repo 'Engine/EngineDiagnostics'
$vcvars = 'C:/Program Files/Microsoft Visual Studio/18/Community/VC/Auxiliary/Build/vcvars64.bat'
if (-not (Test-Path -LiteralPath $vcvars)) { throw "VS18 toolchain not found: $vcvars" }
if (-not $OutputRoot) {
    $OutputRoot = Join-Path $repo ('Build/Validation/ProfileCollectorStress/' + (Get-Date -Format 'yyyyMMdd-HHmmss'))
}
$OutputRoot = [IO.Path]::GetFullPath($OutputRoot)
$null = New-Item -ItemType Directory -Force -Path $OutputRoot
$configs = if ($Configuration -eq 'All') { @('Debug', 'Release') } else { @($Configuration) }
$sources = @(
    (Join-Path $core 'ProfileMarker.cpp'),
    (Join-Path $core 'ProfileThreadStream.cpp'),
    (Join-Path $core 'ProfileCapture.cpp'),
    (Join-Path $core 'ProfileCaptureFile.cpp'),
    (Join-Path $core 'ProfileRecording.cpp'),
    (Join-Path $core 'ProfileService.cpp'),
    (Join-Path $PSScriptRoot 'profile_collector_stress.cpp')
)
$runs = @()
foreach ($config in $configs) {
    $out = Join-Path $OutputRoot $config
    $null = New-Item -ItemType Directory -Force -Path $out
    $exe = Join-Path $out 'profile_collector_stress.exe'
    $flags = if ($config -eq 'Debug') { '/MDd /Od /RTC1 /D_DEBUG' } else { '/MD /O2 /DNDEBUG' }
    $quoted = ($sources | ForEach-Object { '"' + $_ + '"' }) -join ' '
    $command = 'call "' + $vcvars + '" >nul && cl /nologo /EHsc /std:c++latest /utf-8 /W4 /WX /DCE_SHIPPING=0 ' +
        $flags + ' /I"' + $core + '" /Fo"' + $out + '/" /Fd"' + (Join-Path $out 'probe.pdb') +
        '" /Fe"' + $exe + '" ' + $quoted
    $buildLog = & $env:ComSpec /d /s /c $command 2>&1
    $buildLog | Set-Content -LiteralPath (Join-Path $out 'build.log') -Encoding UTF8
    if ($LASTEXITCODE -ne 0) { throw "$config collector stress build failed: $out/build.log" }

    $stdout = Join-Path $out 'result.json'
    $stderr = Join-Path $out 'stderr.log'
    $proc = Start-Process -FilePath $exe -ArgumentList @($DurationSec) -WindowStyle Hidden -PassThru `
        -RedirectStandardOutput $stdout -RedirectStandardError $stderr
    if (-not $proc.WaitForExit(($DurationSec + 120) * 1000)) {
        try { $proc.Kill() } catch { }
        throw "$config collector stress timed out: $stderr"
    }
    $proc.WaitForExit()
    $body = Get-Content -LiteralPath $stdout -Raw -Encoding UTF8
    try { $result = $body | ConvertFrom-Json }
    catch { throw "$config collector stress did not emit JSON: $stdout" }
    $runs += [pscustomobject]@{
        configuration = $config
        exitCode = $proc.ExitCode
        result = $result
        stdout = $stdout
        stderr = $stderr
    }
    Write-Host ("[{0}] passed={1} cycles={2} frames={3} dropped={4} activeShutdown={5}" -f `
        $config, $result.passed, $result.cycles, $result.attemptedFrames,
        $result.droppedFrames, $result.abandoned)
    if ($proc.ExitCode -ne 0 -or -not $result.passed) {
        throw "$config collector stress failed: $stdout / $stderr"
    }
}
$manifest = [ordered]@{
    schema = 'creator.profile-collector-stress.v2'
    createdAt = (Get-Date).ToUniversalTime().ToString('o')
    gitHead = (& git -C $repo rev-parse HEAD)
    durationSecPerConfiguration = $DurationSec
    runs = $runs
}
$manifest | ConvertTo-Json -Depth 12 | Set-Content -LiteralPath (Join-Path $OutputRoot 'manifest.json') -Encoding UTF8
Write-Host "PROFILE_COLLECTOR_STRESS_OK=$OutputRoot"
