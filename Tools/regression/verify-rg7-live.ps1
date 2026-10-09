param(
    [ValidateSet('Debug','Release')][string]$Configuration = 'Release',
    [Parameter(Mandatory)][string]$OutputDirectory,
    [string]$FixtureProject = 'Build/Verification/RG5Final20261007/fixture/Project',
    [string]$Python = 'python',
    [ValidateRange(0,500)][int]$PerformanceSamples = 25
)
$ErrorActionPreference = 'Stop'
$repo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$output = [IO.Path]::GetFullPath($OutputDirectory)
if (Test-Path -LiteralPath $output)
{
    throw 'Use a new evidence directory'
}
New-Item -ItemType Directory -Path $output | Out-Null
$previousAlias = $env:CREATOR_RENDERGRAPH_ALIASING
$previousExtend = $env:CREATOR_RENDERGRAPH_EXTEND_LIFETIMES
$previousWorkspace = $env:CREATOR_EDITOR_WORKSPACE_DIR
$previousIni = $env:CREATOR_EDITOR_LEGACY_INI
try
{
    foreach ($mode in @('Off','On'))
    {
        $env:CREATOR_RENDERGRAPH_ALIASING = if ($mode -eq 'On') { '1' } else { '0' }
        $env:CREATOR_RENDERGRAPH_EXTEND_LIFETIMES = '0'
        $env:CREATOR_EDITOR_WORKSPACE_DIR = Join-Path $output "workspace-$mode"
        $env:CREATOR_EDITOR_LEGACY_INI = Join-Path $env:CREATOR_EDITOR_WORKSPACE_DIR 'legacy.ini'
        New-Item -ItemType Directory -Path $env:CREATOR_EDITOR_WORKSPACE_DIR | Out-Null
        & (Join-Path $PSScriptRoot 'verify-render-base0.ps1') -Configuration $Configuration `
            -FixtureProject $FixtureProject -OutputDirectory (Join-Path $output $mode) `
            -Python $Python -GraphSamples 2 -PerformanceSamples $PerformanceSamples -WarmupFrames 8
    }
    $offCapture = Join-Path $output 'Off/dx12-0/capture-0'
    $onCapture = Join-Path $output 'On/dx12-0/capture-0'
    & $Python (Join-Path $PSScriptRoot 'base0_artifacts.py') compare $offCapture $onCapture `
        (Join-Path $output 'off-on-comparison') > (Join-Path $output 'comparison.stdout.json')
    if ($LASTEXITCODE -ne 0)
    {
        throw 'RG7 live OFF/ON comparison failed'
    }
    $manifest = Get-Content (Join-Path $onCapture 'manifest.json') -Raw | ConvertFrom-Json
    $activations = @($manifest.compiledGraph.passes | ForEach-Object barriers | Where-Object aliasing)
    $transients = @($manifest.compiledGraph.resources | Where-Object { $_.used -and !$_.imported })
    $shared = @($transients | Where-Object { $_.aliasGroup -ne [uint32]::MaxValue })
    if (!$activations.Count -or $shared.Count -ne $activations.Count)
    {
        throw 'Live capture did not exercise actual shared heap activation'
    }
    $cacheSamples = @()
    foreach ($captureFile in @(Get-ChildItem (Join-Path $output 'On') -Filter manifest.json -Recurse |
        Where-Object { $_.Directory.Name -like 'capture-*' }))
    {
        $captured = Get-Content $captureFile.FullName -Raw | ConvertFrom-Json
        $members = @($captured.compiledGraph.resources |
            Where-Object { $_.used -and !$_.imported -and $_.aliasGroup -ne [uint32]::MaxValue })
        $groups = @($members | Group-Object aliasGroup)
        if ($captured.graph.aliasHeapCreates -ne 0 -or $captured.graph.transientAllocationQueries -ne 0 -or
            $captured.graph.aliasHeapReuses -ne $groups.Count -or
            $captured.graph.aliasResourceReuses -ne $members.Count)
        {
            throw "Warm live capture repeated native preparation: $($captureFile.FullName)"
        }
        $cacheSamples += [ordered]@{capture=$captureFile.FullName; counters=$captured.graph}
    }
    if ($cacheSamples.Count -ne 4)
    {
        throw 'Expected four independently audited warm cache captures'
    }
    $unaliasedBytes = [uint64]0
    $allocatedBytes = [uint64]0
    foreach ($resource in $transients)
    {
        $unaliasedBytes += [uint64]$resource.allocationBytes
        if ($resource.aliasGroup -eq [uint32]::MaxValue)
        {
            $allocatedBytes += [uint64]$resource.allocationBytes
        }
    }
    foreach ($group in @($shared | Group-Object aliasGroup))
    {
        $allocatedBytes += [uint64](($group.Group | Measure-Object allocationBytes -Maximum).Maximum)
    }
    if ($allocatedBytes -ge $unaliasedBytes)
    {
        throw 'Live sharing did not reduce equivalent graph transient allocation bytes'
    }
    $timings = @()
    if ($PerformanceSamples -gt 0)
    {
        foreach ($mode in @('Off','On'))
        {
            $samples = @()
            foreach ($run in 0,1)
            {
                $samples += @(Get-Content (Join-Path $output "$mode/dx12-$run/live-performance.json") -Raw |
                    ConvertFrom-Json)
            }
            if ($samples.Count -ne 2 * $PerformanceSamples)
            {
                throw "Incorrect timing sample count for $mode"
            }
            $cpu = @($samples.cpuRecordMs | Sort-Object)
            $gpu = @($samples.gpuMs | Sort-Object)
            $lower = [int][Math]::Floor(($samples.Count - 1) / 2)
            $upper = [int][Math]::Floor($samples.Count / 2)
            $p95 = [int][Math]::Ceiling($samples.Count * 0.95) - 1
            $timings += [ordered]@{mode=$mode; samples=$samples.Count;
                cpuMedian=($cpu[$lower]+$cpu[$upper])/2; cpuP95=$cpu[$p95];
                gpuMedian=($gpu[$lower]+$gpu[$upper])/2; gpuP95=$gpu[$p95]}
        }
        $timings | ConvertTo-Json | Set-Content (Join-Path $output 'timing-summary.json') -Encoding utf8
    }
    [ordered]@{configuration=$Configuration; passed=$true; phaseComplete=$false;
        activations=$activations.Count; transientUnaliasedBytes=$unaliasedBytes;
        transientAllocatedBytes=$allocatedBytes; performanceSamples=$PerformanceSamples;
        cacheSamples=$cacheSamples;
        timings=$timings;
        scope='static LX Editor OFF/ON capture; graph allocation bytes, not peak resident VRAM';
        comparison=(Join-Path $output 'off-on-comparison/comparison.json')} |
        ConvertTo-Json -Depth 5 | Set-Content (Join-Path $output 'result.json') -Encoding utf8
    "RG7_LIVE_OK configuration=$Configuration output=$output"
}
finally
{
    $env:CREATOR_RENDERGRAPH_ALIASING = $previousAlias
    $env:CREATOR_RENDERGRAPH_EXTEND_LIFETIMES = $previousExtend
    $env:CREATOR_EDITOR_WORKSPACE_DIR = $previousWorkspace
    $env:CREATOR_EDITOR_LEGACY_INI = $previousIni
}
