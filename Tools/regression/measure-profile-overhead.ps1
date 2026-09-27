#Requires -Version 7.0
[CmdletBinding()]
param(
    [ValidateRange(100, 100000)][int]$Frames = 2000,
    [ValidateRange(1, 20)][int]$Repeats = 5,
    [string]$OutputRoot
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$repo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$core = Join-Path $repo 'Engine/EngineDiagnostics'
$vcvars = 'C:/Program Files/Microsoft Visual Studio/18/Community/VC/Auxiliary/Build/vcvars64.bat'
if (-not (Test-Path -LiteralPath $vcvars)) { throw "VS18 toolchain not found: $vcvars" }
if (-not $OutputRoot) {
    $OutputRoot = Join-Path $repo ('Build/Validation/ProfileOverhead/' + (Get-Date -Format 'yyyyMMdd-HHmmss'))
}
$OutputRoot = [IO.Path]::GetFullPath($OutputRoot)
$null = New-Item -ItemType Directory -Force -Path $OutputRoot
$sources = @(
    (Join-Path $core 'ProfileMarker.cpp'),
    (Join-Path $core 'ProfileThreadStream.cpp'),
    (Join-Path $core 'ProfileCapture.cpp'),
    (Join-Path $core 'ProfileService.cpp'),
    (Join-Path $PSScriptRoot 'profile_overhead_probe.cpp')
)
$binaries = @{}
foreach ($build in @('shipping', 'development')) {
    $out = Join-Path $OutputRoot $build
    $null = New-Item -ItemType Directory -Force -Path $out
    $exe = Join-Path $out 'profile_overhead_probe.exe'
    $shipping = if ($build -eq 'shipping') { 1 } else { 0 }
    $quoted = ($sources | ForEach-Object { '"' + $_ + '"' }) -join ' '
    $command = 'call "' + $vcvars + '" >nul && cl /nologo /EHsc /std:c++latest /utf-8 /W4 /WX ' +
        '/MD /O2 /DNDEBUG /DCE_SHIPPING=' + $shipping + ' /I"' + $core +
        '" /Fo"' + $out + '/" /Fd"' + (Join-Path $out 'probe.pdb') +
        '" /Fe"' + $exe + '" ' + $quoted
    $buildLog = & $env:ComSpec /d /s /c $command 2>&1
    $buildLog | Set-Content -LiteralPath (Join-Path $out 'build.log') -Encoding UTF8
    if ($LASTEXITCODE -ne 0) { throw "$build overhead probe build failed: $out/build.log" }
    $binaries[$build] = $exe
}

$modes = @('shipping', 'stopped', 'cpu', 'cpu-gpu')
$runs = @()
for ($repeat = 0; $repeat -lt $Repeats; ++$repeat) {
    # Rotate order so one mode does not always inherit the cold or hot CPU.
    for ($position = 0; $position -lt $modes.Count; ++$position) {
        $mode = $modes[($repeat + $position) % $modes.Count]
        $build = if ($mode -eq 'shipping') { 'shipping' } else { 'development' }
        $label = '{0:D2}-{1}' -f ($repeat + 1), $mode
        $stdout = Join-Path $OutputRoot "$label.json"
        $stderr = Join-Path $OutputRoot "$label.stderr.log"
        $proc = Start-Process -FilePath $binaries[$build] -ArgumentList @($mode, $Frames) `
            -WindowStyle Hidden -PassThru -RedirectStandardOutput $stdout `
            -RedirectStandardError $stderr
        if (-not $proc.WaitForExit(($Frames * 5 + 60000))) {
            try { $proc.Kill() } catch { }
            throw "$mode overhead probe timed out: $stderr"
        }
        $proc.WaitForExit()
        try { $result = Get-Content -LiteralPath $stdout -Raw -Encoding UTF8 | ConvertFrom-Json }
        catch { throw "$mode overhead probe did not emit JSON: $stdout" }
        $runs += [pscustomobject]@{
            repeat = $repeat + 1
            mode = $mode
            exitCode = $proc.ExitCode
            result = $result
            stdout = $stdout
            stderr = $stderr
        }
        Write-Host ("[{0}] p50={1:N4} p95={2:N4} p99={3:N4} ms collector={4:N3} ms drop={5}" -f `
            $label, $result.activeP50Ms, $result.activeP95Ms,
            $result.activeP99Ms, $result.collectorCpuMs, $result.droppedEvents)
        if ($proc.ExitCode -ne 0 -or -not $result.valid) {
            throw "$mode overhead probe invalid: $stdout / $stderr"
        }
    }
}
$summary = @()
foreach ($mode in $modes) {
    $samples = @($runs | Where-Object mode -eq $mode | ForEach-Object { $_.result })
    $p50 = @($samples | ForEach-Object { [double]$_.activeP50Ms } | Sort-Object)
    $p95 = @($samples | ForEach-Object { [double]$_.activeP95Ms } | Sort-Object)
    $p99 = @($samples | ForEach-Object { [double]$_.activeP99Ms } | Sort-Object)
    $collector = @($samples | ForEach-Object { [double]$_.collectorCpuMs } | Sort-Object)
    $events = @($samples | ForEach-Object { [double]$_.emittedEventsPerSec } | Sort-Object)
    $bytes = @($samples | ForEach-Object { [double]$_.payloadBytesPerSec } | Sort-Object)
    $peak = @($samples | ForEach-Object { [long]$_.peakCaptureBytes } | Sort-Object)
    $droppedEvents = @($samples | ForEach-Object { [long]$_.droppedEvents } | Sort-Object)
    $droppedFrames = @($samples | ForEach-Object { [long]$_.droppedFrames } | Sort-Object)
    $middle = [int][Math]::Floor(($samples.Count - 1) / 2)
    $summary += [pscustomobject]@{
        mode = $mode
        runCount = $samples.Count
        medianRunP50Ms = $p50[$middle]
        medianRunP95Ms = $p95[$middle]
        medianRunP99Ms = $p99[$middle]
        medianCollectorCpuMs = $collector[$middle]
        medianEmittedEventsPerSec = $events[$middle]
        medianPayloadBytesPerSec = $bytes[$middle]
        maxPeakCaptureBytes = $peak[-1]
        maxDroppedEvents = $droppedEvents[-1]
        maxDroppedFrames = $droppedFrames[-1]
    }
}
$manifest = [ordered]@{
    schema = 'creator.profile-overhead-core.v1'
    createdAt = (Get-Date).ToUniversalTime().ToString('o')
    gitHead = (& git -C $repo rev-parse HEAD)
    framesPerRun = $Frames
    repeats = $Repeats
    scopeCallsPerFrame = 64
    gpuSpansPerFrameInCpuGpuMode = 8
    pacingYieldUsOutsideActiveSample = 500
    limitation = 'Synthetic core workload. CPU/GPU counters and product GPU frame timing are not measured; P5 counters do not exist yet.'
    summary = $summary
    runs = $runs
}
$manifest | ConvertTo-Json -Depth 12 | Set-Content -LiteralPath (Join-Path $OutputRoot 'manifest.json') -Encoding UTF8
Write-Host "PROFILE_OVERHEAD_CORE_OK=$OutputRoot"
