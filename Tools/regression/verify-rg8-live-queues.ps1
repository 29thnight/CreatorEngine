#Requires -Version 7.0
param(
    [ValidateSet('Debug','Release')][string]$Configuration = 'Debug',
    [switch]$AsyncCompute,
    [string]$ReuseOffEvidence = '',
    [Parameter(Mandatory)][string]$FixtureProject,
    [Parameter(Mandatory)][string]$NativeEvidence,
    [Parameter(Mandatory)][string]$OutputDirectory,
    [string]$Python = 'python'
)
$ErrorActionPreference = 'Stop'
$repo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$out = [IO.Path]::GetFullPath($OutputDirectory)
if (Test-Path -LiteralPath $out)
{
    throw 'Use a fresh evidence directory.'
}
$native = Get-Content (Join-Path $NativeEvidence 'execution-result.json') -Raw | ConvertFrom-Json
$exe = Join-Path $repo "Bin/x64-$Configuration/Editor/CreatorEditor.exe"
$runtime = Join-Path (Split-Path $exe) 'CreatorEditor.runtime.dll'
if (!$native.passed -or !$native.fullRegression -or
    $native.binary.exe -ne (Get-FileHash $exe).Hash -or $native.binary.runtime -ne (Get-FileHash $runtime).Hash)
{
    throw 'Native evidence must include the current binary full regression.'
}
New-Item -ItemType Directory -Path "$out/CompilerEvidence" | Out-Null
$commands = @(Get-Content (Join-Path $NativeEvidence 'results.jsonl') | ConvertFrom-Json)
$full = @($commands | Where-Object { $_.data.log -match 'RGV_READER_OK[^\r\n]*stale-scene' })
$quit = @($commands | Where-Object command -eq 'quit')
if ($full.Count -ne 1 -or $quit.Count -ne 1 -or $full[0].status -ne 'succeeded')
{
    throw 'Missing successful compiler/reader regression.'
}
@($full[0],$quit[0]) | ForEach-Object { $_ | ConvertTo-Json -Depth 50 -Compress } |
    Set-Content "$out/CompilerEvidence/compiler-fixtures.jsonl" -Encoding utf8
@{exe=$native.binary.exe; runtime=$native.binary.runtime; source=[IO.Path]::GetFullPath($NativeEvidence)} |
    ConvertTo-Json | Set-Content "$out/CompilerEvidence/compiler-binary-hashes.json" -Encoding utf8
$saved = @{}
foreach ($name in @('CREATOR_RENDERGRAPH_QUEUE_EXECUTION','CREATOR_RENDERGRAPH_ALIASING',
    'CREATOR_RENDERGRAPH_EXTEND_LIFETIMES','CREATOR_EDITOR_WORKSPACE_DIR','CREATOR_EDITOR_LEGACY_INI'))
{
    $saved[$name] = [Environment]::GetEnvironmentVariable($name)
}
$rows = @()
$runPaths = @{}
try
{
    foreach ($mode in @('Off','On'))
    {
        $env:CREATOR_RENDERGRAPH_QUEUE_EXECUTION = if ($mode -eq 'On') { if ($AsyncCompute) { '2' } else { '1' } } else { '0' }
        $env:CREATOR_RENDERGRAPH_ALIASING = '0'
        $env:CREATOR_RENDERGRAPH_EXTEND_LIFETIMES = '0'
        $env:CREATOR_EDITOR_WORKSPACE_DIR = Join-Path $out "workspace-$mode"
        $env:CREATOR_EDITOR_LEGACY_INI = Join-Path $env:CREATOR_EDITOR_WORKSPACE_DIR 'legacy.ini'
        New-Item -ItemType Directory -Path $env:CREATOR_EDITOR_WORKSPACE_DIR | Out-Null
        $path = Join-Path $out $mode
        if ($mode -eq 'Off' -and $ReuseOffEvidence)
        {
            $path = [IO.Path]::GetFullPath($ReuseOffEvidence)
            $previousBinary = Get-Content "$path/compiler-binary-hashes.json" -Raw | ConvertFrom-Json
            if ($previousBinary.exe -ne $native.binary.exe -or $previousBinary.runtime -ne $native.binary.runtime)
            {
                throw 'Reused OFF evidence must be from the exact current binary.'
            }
        }
        else
        {
            & (Join-Path $PSScriptRoot 'verify-graph-recovery-rgv.ps1') -Configuration $Configuration `
                -FixtureProject $FixtureProject -OutputDirectory $path -CompilerFixtureDirectory "$out/CompilerEvidence"
        }
        $runPaths[$mode] = $path
        $result = Get-Content "$path/result.json" -Raw | ConvertFrom-Json
        if (!$result.complete -or $result.failures.Count -or $result.exitCode -ne 0 -or
            !$result.validation.layerEnabled -or $result.validation.mode -ne 'gpu' -or
            $result.validation.problems -ne 0 -or $result.validation.droppedMessages -ne 0)
        {
            throw "Incomplete product run: $mode"
        }
        $capture = Get-Content (Join-Path $result.captures[0].path 'manifest.json') -Raw | ConvertFrom-Json
        if ($capture.measurement.gpuStatus -ne 'measured' -or $capture.measurement.sliceCount -le 0 -or
            $capture.measurement.queryOverflow -ne 0 -or $capture.measurement.droppedSlices -ne 0 -or
            $capture.measurement.gpuQueueSpanMs -le 0)
        {
            throw "Missing valid live profiler queries: $mode"
        }
        $log = [IO.File]::ReadAllText("$path/editor.stdout.log")
        $submitted = [regex]::Matches($log, '\[rg8.live\] submissions=(\d+) batches=(\d+) profilerQueue=graphics')
        $count = 0L
        foreach ($match in $submitted)
        {
            $count += [long]$match.Groups[1].Value
        }
        if (($mode -eq 'On' -and ($count -eq 0 -or $log -notmatch '\[rg8.live\] enabled=true')) -or
            ($mode -eq 'Off' -and $submitted.Count -ne 0))
        {
            throw "Live queue consumer did not match selected mode: $mode"
        }
        $computeCount = 0L
        foreach ($match in [regex]::Matches($log, '\[rg8.compute\] submissions=(\d+)'))
        {
            $computeCount += [long]$match.Groups[1].Value
        }
        if ($AsyncCompute -and $mode -eq 'On' -and
            ($computeCount -le 0 -or $capture.measurement.computeSliceCount -ne 2 -or
             $capture.measurement.gpuBusyMs -gt ($capture.measurement.gpuQueueSpanMs + 0.000001)))
        {
            throw 'Missing actual compute submissions or queue-local timing slices.'
        }
        $views = @()
        foreach ($target in @('scene','game','preview'))
        {
            $graph = Get-Content "$path/graph-$target.json" -Raw | ConvertFrom-Json
            if (!$graph.ready -or @($graph.resources | Where-Object { $_.used -and $_.aliasGroup -ge 0 }).Count)
            {
                throw "Unready or aliased owned-queue view: $mode/$target"
            }
            $views += @{target=$target; view=$graph.view; width=$graph.width; height=$graph.height}
        }
        if (@($views.view | Select-Object -Unique).Count -ne 3 -or
            $result.sceneChange.before -eq $result.sceneChange.after -or
            $result.resize.before[0] -eq $result.resize.after[0])
        {
            throw 'Missing distinct views/scene change/resize.'
        }
        $rows += @{mode=$mode; evidence=$path; submissions=$count; views=$views; validationErrors=0; exitCode=0;
            computeSubmissions=$computeCount; computeSlices=$capture.measurement.computeSliceCount; profilerSlices=$capture.measurement.sliceCount; profilerStatus=$capture.measurement.gpuStatus}
    }
    $off = Get-Content (Join-Path $runPaths['Off'] 'result.json') -Raw | ConvertFrom-Json
    $on = Get-Content (Join-Path $runPaths['On'] 'result.json') -Raw | ConvertFrom-Json
    & $Python (Join-Path $PSScriptRoot 'compare-material-captures.py') $off.captures[0].path $on.captures[0].path `
        "$out/comparison.json" > "$out/comparison.log"
    if ($LASTEXITCODE -ne 0)
    {
        throw 'Live queue OFF/ON output mismatch.'
    }
    @{passed=$true; phaseComplete=$false; configuration=$Configuration; runs=$rows;
        scope=$(if ($AsyncCompute) { 'Live SSAO compute pilot with queue-local clocks; performance adoption pending' } else { 'Live owned graphics queue; performance adoption pending' });
        binary=$native.binary} | ConvertTo-Json -Depth 30 | Set-Content "$out/live-result.json" -Encoding utf8
    "RG8_LIVE_QUEUE_OK configuration=$Configuration"
}
finally
{
    foreach ($entry in $saved.GetEnumerator())
    {
        [Environment]::SetEnvironmentVariable($entry.Key, $entry.Value)
    }
}
