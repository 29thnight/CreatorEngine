param(
    [string]$Exe = (Join-Path $PSScriptRoot '..\..\Bin\x64-Debug\Editor\CreatorEditor.exe'),
    [string]$Work = $env:TEMP,
    [int]$ExpectedGraphs = 1,
    [int]$TimeoutSeconds = 180
)

# Source-only implementation added this harness; execute explicitly on a native host.
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'CommandResults.ps1')
$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$inputRoot = Join-Path $repoRoot 'Dynamic_CPP\Assets\InputGraph'
$graphs = @(Get-ChildItem -LiteralPath $inputRoot -Filter '*.inputgraph' -File | Sort-Object Name)
if ($graphs.Count -ne $ExpectedGraphs) { throw "Expected $ExpectedGraphs InputGraphs, found $($graphs.Count)" }
$legacy = @(Get-ChildItem -LiteralPath (Join-Path $repoRoot 'Dynamic_CPP\Assets') -Filter '*.inputmap' -Recurse -File)
if ($legacy.Count -ne 0) { throw 'Legacy InputMaps must be offline fixtures, never packaged Assets' }
if (-not (Test-Path -LiteralPath $Exe -PathType Leaf)) { throw "Editor executable is missing: $Exe" }
$Exe = (Resolve-Path -LiteralPath $Exe).Path
$before = @{}
$run = Join-Path $Work ('CE_InputGraph_' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $run | Out-Null
$commands = [Collections.Generic.List[string]]::new()
foreach ($graph in $graphs) {
    $text = Get-Content -LiteralPath $graph.FullName -Raw
    $header = [regex]::Match($text, '^LXINPUT 1 "([0-9a-f-]{36})"')
    if (-not $header.Success) { throw "InputGraph archive/header is unsupported: $($graph.FullName)" }
    $meta = $graph.FullName + '.meta'
    if (-not (Test-Path -LiteralPath $meta -PathType Leaf)) { throw "InputGraph has no identity sidecar: $meta" }
    $metadata = Get-Content -LiteralPath $meta -Raw
    if ($metadata -notmatch ('(?m)^guid:\s*"?' + [regex]::Escape($header.Groups[1].Value) + '"?\s*$')) {
        throw "InputGraph source and sidecar GUID differ: $($graph.Name)"
    }
    foreach ($path in @($graph.FullName, $meta)) {
        $before[$path] = (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash
    }
    $commands.Add('input.graph.inspect "' + $graph.FullName + '"')
    $commands.Add('input.graph.roundtrip "' + $graph.FullName + '"')
}
$commands.Add('input.graph.selftest')
$scenario = Join-Path $run 'commands.txt'
$resultPath = Join-Path $run 'results.jsonl'
$commands | Set-Content -LiteralPath $scenario -Encoding UTF8
try {
    $process = Start-Process -FilePath $Exe -ArgumentList @('--commandlet-script', ('"' + $scenario + '"'),
        '--result-file', ('"' + $resultPath + '"')) -WorkingDirectory $repoRoot -WindowStyle Hidden `
        -RedirectStandardOutput (Join-Path $run 'stdout.txt') -RedirectStandardError (Join-Path $run 'stderr.txt') -PassThru
    if (-not $process.WaitForExit($TimeoutSeconds * 1000)) {
        $process.Kill(); $process.WaitForExit(); throw "InputGraph corpus probe timed out: $run"
    }
    if ($process.ExitCode -ne 0) { throw "InputGraph probe exited $($process.ExitCode): $run" }
    $results = @(Read-CommandResults $resultPath)
    if ($results.Count -ne (2 * $graphs.Count + 1) -or @($results | Where-Object status -ne succeeded).Count -ne 0) {
        throw 'InputGraph probe did not execute every requested successful check'
    }
    foreach ($graph in $graphs) {
        $checked = @($results | Where-Object { $_.command -eq 'input.graph.roundtrip' -and $_.data.path -eq $graph.FullName })
        if ($checked.Count -ne 1 -or -not $checked[0].data.roundTrip -or $checked[0].data.signals -le 0 -or
            $checked[0].data.bindings -le 0 -or $checked[0].data.layers -le 0) {
            throw "InputGraph round-trip/prepared definition is missing: $($graph.Name)"
        }
        if ($graph.Name -eq 'Gameplay.inputgraph' -and ($checked[0].data.signals -ne 3 -or
            $checked[0].data.bindings -ne 3 -or $checked[0].data.layers -ne 1)) {
            throw 'Gameplay example must retain exactly Move, Jump and Look in one layer'
        }
    }
    $null = Get-SucceededCommand $results 'input.graph.selftest'
    foreach ($path in $before.Keys) {
        if ((Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash -ne $before[$path]) {
            throw "Read-only InputGraph probe mutated source/meta: $path"
        }
    }
    "InputGraph corpus: PASS (graphs=$($graphs.Count), native archive/codec tests, source/meta mutations=0)"
}
finally {
    # Delete only this invocation's explicitly created child directory.
    if (Test-Path -LiteralPath $run) {
        $parent = [IO.Path]::GetFullPath((Split-Path $run -Parent)).TrimEnd('\', '/')
        if ($parent -ne [IO.Path]::GetFullPath($Work).TrimEnd('\', '/') -or
            -not (Split-Path $run -Leaf).StartsWith('CE_InputGraph_')) {
            throw "Refusing to remove an unverified temp directory: $run"
        }
        Remove-Item -LiteralPath $run -Recurse -Force
    }
}
