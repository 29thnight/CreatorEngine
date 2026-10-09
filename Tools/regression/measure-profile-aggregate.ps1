[CmdletBinding()]
param(
    [string]$BaselineRef = '3afe1daaee7b75f644ac10b12d96fb684a0e74c8',
    [string]$OutputDirectory = 'Artifacts/cpu-profile-aggregate'
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$repo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$out = [IO.Path]::GetFullPath((Join-Path $repo $OutputDirectory))
$core = Join-Path $repo 'Engine/EngineDiagnostics'
$vcvars = 'C:/Program Files/Microsoft Visual Studio/18/Community/VC/Auxiliary/Build/vcvars64.bat'
New-Item -ItemType Directory -Force -Path $out | Out-Null
$baselineText = & git -C $repo show "${BaselineRef}:Engine/EngineDiagnostics/ProfileAggregate.cpp"
if ($LASTEXITCODE -ne 0) { throw 'Unable to read baseline revision' }
$baselineSource = Join-Path $out 'BaselineAggregate.cpp'
[IO.File]::WriteAllText($baselineSource, ($baselineText -join "`n"), [Text.UTF8Encoding]::new($false))
# Build two distinct aggregate types/functions into one executable. The baseline
# header is generated so both variants retain their own valid friend declaration.
$baselineHeader = [IO.File]::ReadAllText((Join-Path $core 'ProfileAggregate.h'))
$classBegin = $baselineHeader.IndexOf('class frame_aggregate')
if ($classBegin -lt 0) { throw 'Aggregate class anchor missing' }
$baselineHeader = "#pragma once`n#include `"ProfileAggregate.h`"`nnamespace ce`n{`n" + $baselineHeader.Substring($classBegin)
$baselineHeader = $baselineHeader -creplace '\bframe_aggregate\b', 'frame_aggregate_baseline'
$baselineHeader = $baselineHeader -creplace '\baggregate_frames\b', 'aggregate_frames_baseline'
[IO.File]::WriteAllText((Join-Path $out 'ProfileAggregateBaseline.h'), $baselineHeader, [Text.UTF8Encoding]::new($false))
$baselineBody = ($baselineText -join "`n").Replace('"ProfileAggregate.h"', '"ProfileAggregateBaseline.h"')
$baselineBody = $baselineBody -creplace '\bframe_aggregate\b', 'frame_aggregate_baseline'
$baselineBody = $baselineBody -creplace '\baggregate_frames\b', 'aggregate_frames_baseline'
[IO.File]::WriteAllText($baselineSource, $baselineBody, [Text.UTF8Encoding]::new($false))
$sources = @('ProfileMarker.cpp', 'ProfileThreadStream.cpp', 'ProfileCapture.cpp', 'ProfileAggregate.cpp') |
    ForEach-Object { Join-Path $core $_ }
$sources += @($baselineSource, (Join-Path $PSScriptRoot 'profile_aggregate_benchmark.cpp'))
$quoted = ($sources | ForEach-Object { '"' + $_ + '"' }) -join ' '
$exe = Join-Path $out 'benchmark.exe'
$command = 'call "' + $vcvars + '" >nul && cl /nologo /Bv /EHsc /std:c++latest /utf-8 /W4 /WX /MD /O2 /DNDEBUG /I"' +
    $core + '" /I"' + $out + '" /Fo"' + $out + '/" /Fe"' + $exe + '" ' + $quoted
& $env:ComSpec /d /s /c $command *> (Join-Path $out 'build.log')
if ($LASTEXITCODE -ne 0) {
    Get-Content (Join-Path $out 'build.log') -Tail 30
    throw 'Benchmark compile failed'
}
$compilerLogs = @{ paired = Get-Content (Join-Path $out 'build.log') -Raw }
$buildCpu = @{}
Get-Process MSBuild -ErrorAction SilentlyContinue | ForEach-Object { $buildCpu[$_.Id] = $_.CPU }
Start-Sleep -Milliseconds 1000
$activeBuild = @(Get-Process MSBuild -ErrorAction SilentlyContinue | Where-Object {
    -not $buildCpu.ContainsKey($_.Id) -or ($_.CPU - $buildCpu[$_.Id]) -gt 0.02
})
if ((Get-Process cl -ErrorAction SilentlyContinue) -or $activeBuild.Count -gt 0) {
    throw 'Finish competing compilation before timing the benchmark'
}
$lines = & $exe
if ($LASTEXITCODE -ne 0) { throw 'Paired benchmark failed' }
$cases = @($lines | ForEach-Object { $_ | ConvertFrom-Json })
$runs = @()
foreach ($side in @('baseline', 'candidate')) {
    $runs += @{ side = $side; cases = @($cases | Where-Object { $_.side -eq $side }) }
}

$comparisons = @()
foreach ($reference in $runs[0].cases) {
    $samplesBySide = @{}
    foreach ($side in @('baseline', 'candidate')) {
        $matching = @($runs | Where-Object { $_.side -eq $side } | ForEach-Object {
            $_.cases | Where-Object { $_.lanes -eq $reference.lanes -and $_.scope -eq $reference.scope }
        })
        if (@($matching | Where-Object { $_.digest -ne $reference.digest }).Count -ne 0) {
            throw "Aggregate output mismatch: $($reference.lanes)/$($reference.scope)"
        }
        $sorted = @($matching.samples_ms | Sort-Object)
        $samplesBySide[$side] = @{
            median_ms = ($sorted[6] + $sorted[7]) / 2
            p95_ms = $sorted[13]
        }
    }
    $comparisons += @{
        lanes = $reference.lanes; scope = $reference.scope; digest = $reference.digest
        baseline = $samplesBySide.baseline; candidate = $samplesBySide.candidate
        median_improvement_percent = 100 * (1 - $samplesBySide.candidate.median_ms / $samplesBySide.baseline.median_ms)
    }
}
$cpu = Get-CimInstance Win32_Processor | Select-Object Name, NumberOfCores, NumberOfLogicalProcessors
$report = @{
    measured_at_utc = [DateTime]::UtcNow.ToString('o')
    baseline_ref = $BaselineRef; current_head = (& git -C $repo rev-parse HEAD)
    baseline_sha256 = (Get-FileHash $baselineSource -Algorithm SHA256).Hash
    candidate_sha256 = (Get-FileHash (Join-Path $core 'ProfileAggregate.cpp') -Algorithm SHA256).Hash
    fixture_sha256 = (Get-FileHash (Join-Path $PSScriptRoot 'profile_aggregate_benchmark.cpp') -Algorithm SHA256).Hash
    configuration = 'MSVC /O2 /MD /DNDEBUG /std:c++latest'; cpu = $cpu
    benchmark_threads = 1; microarchitecture = 'Unverified; record from vendor documentation before acceptance'
    frequency_and_turbo = 'Uncontrolled DVFS/turbo; effective clock not sampled'
    smt = 'Core/logical-processor topology recorded; firmware SMT state not verified'
    evidence_status = 'Observation only; hardware clock/isolation and real-capture acceptance remain open'
    os = [Environment]::OSVersion.VersionString; power_plan = (& powercfg /getactivescheme)
    compiler_logs = $compilerLogs; raw_runs = $runs; comparisons = $comparisons
    build_command = $command
    source_hashes = @($sources | ForEach-Object {
        @{ path = $_; sha256 = (Get-FileHash -LiteralPath $_ -Algorithm SHA256).Hash }
    })
    output_equivalent = $true
    limitations = 'Synthetic 120-frame capture; no affinity or clock isolation; no engine FPS claim. 14 samples per case: p95 is the maximum nearest-rank sample.'
}
$report | ConvertTo-Json -Depth 12 | Set-Content -LiteralPath (Join-Path $out 'report.json') -Encoding utf8
$comparisons | ForEach-Object {
    '{0,3} lanes {1,5}: {2:N3} -> {3:N3} ms ({4:N1}%)' -f $_.lanes, $_.scope,
        $_.baseline.median_ms, $_.candidate.median_ms, $_.median_improvement_percent
}
'PROFILE_AGGREGATE_EQUIVALENT=true'
