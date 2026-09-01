# TransformUpdatePlan X4 sparse compiled projection gate.
param(
    [string]$Exe = (Join-Path $PSScriptRoot "..\..\Bin\x64-Release\Editor\CreatorEditor.exe"),
    [string]$Work = $env:TEMP,
    [int]$TimeoutSeconds = 300,
    [int]$BenchNodes = 10000,
    [int]$BenchSamples = 4
)

$repo = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot "..\.."))
$failed = $false
$markers = @(
    [pscustomobject]@{ Path = "Engine\SceneRuntime\Scene.h"; Pattern = "std::unique_ptr<TransformExecutionGraphState> m_executionGraphs;"; Label = "private execution graph state" },
    [pscustomobject]@{ Path = "Engine\SceneRuntime\Scene.cpp"; Pattern = "struct TransformExecutionGraphState"; Label = "private ExecIndex owner" },
    [pscustomobject]@{ Path = "Engine\SceneRuntime\Scene.cpp"; Pattern = "std::vector<ExecIndex> entityToExec;"; Label = "Entity to Exec mapping" },
    [pscustomobject]@{ Path = "Engine\SceneRuntime\Scene.cpp"; Pattern = "std::vector<EntityHandle> execToEntity;"; Label = "Exec to Entity mapping" },
    [pscustomobject]@{ Path = "Engine\SceneRuntime\Scene.cpp"; Pattern = "std::vector<ExecIndex> subtreeEnd;"; Label = "preorder subtree range" },
    [pscustomobject]@{ Path = "Engine\SceneRuntime\Scene.cpp"; Pattern = "EnsureExecutionGraphsCompiled();"; Label = "sync compile gate" },
    [pscustomobject]@{ Path = "Engine\SceneRuntime\Entity.cpp"; Pattern = "RecordExecutionGraphMembershipChanged();"; Label = "component membership publication" },
    [pscustomobject]@{ Path = "Editor\EngineEntry\ConsoleCommandSystem.cpp"; Pattern = "scene.executiongraph"; Label = "X4 runtime probe" }
)

foreach ($marker in $markers) {
    $path = Join-Path $repo $marker.Path
    if (-not (Test-Path -LiteralPath $path) -or
        -not [System.IO.File]::ReadAllText($path).Contains($marker.Pattern)) {
        "FAIL: missing $($marker.Label) marker in $($marker.Path)"
        $failed = $true
    }
}

# ExecIndex is an implementation coordinate, not a public/serialized identity.
$sceneHeader = [System.IO.File]::ReadAllText((Join-Path $repo "Engine\SceneRuntime\Scene.h"))
if ($sceneHeader -match 'using\s+ExecIndex\s*=' -or
    $sceneHeader -match 'std::vector\s*<\s*ExecIndex\s*>') {
    "FAIL: ExecIndex storage leaked into Scene.h"
    $failed = $true
}

if ($failed) { exit 1 }
if (-not (Test-Path -LiteralPath $Exe)) { "FAIL: executable missing: $Exe"; exit 1 }

$runId = [guid]::NewGuid().ToString('N')
$scenario = Join-Path $Work "transform_execution_graph_$runId.txt"
$outFile = Join-Path $Work "transform_execution_graph_$runId.out"
$errFile = Join-Path $Work "transform_execution_graph_$runId.err"
[System.IO.File]::WriteAllLines($scenario, @(
    "scene.executiongraph probe",
    "scene.executiongraph bench $BenchNodes $BenchSamples",
    "quit"
))

$exeDir = [System.IO.Path]::GetDirectoryName($Exe)
$proc = Start-Process -FilePath $Exe -ArgumentList "--script", $scenario `
    -WorkingDirectory $exeDir -WindowStyle Hidden `
    -RedirectStandardOutput $outFile -RedirectStandardError $errFile -PassThru
$proc.WaitForExit($TimeoutSeconds * 1000) | Out-Null
if (-not $proc.HasExited) {
    $proc.Kill()
    "FAIL: X4 runtime probe timeout ($TimeoutSeconds seconds)"
    exit 1
}

$output = [System.IO.File]::ReadAllText($outFile)
$zeroViolations = 'transformless=0 nonlayout=0 mapping=0 parent-order=0 range=0 hierarchy=0 unreachable=0 cycle=0'
$relationLine = 'nearest-spatial=PASS nearest-layout=PASS canvas-both=PASS dynamic-layout=PASS identity=stable values=exact bulk-compile-delta=1 clean-compile-delta=0'
if ($proc.ExitCode -ne 0 -or
    $output -notmatch '\[scene\.executiongraph\] compile=PASS' -or
    $output -notmatch [regex]::Escape($zeroViolations) -or
    $output -notmatch [regex]::Escape($relationLine) -or
    $output -notmatch '\[scene\.executiongraph\] probe=PASS' -or
    $output -notmatch 'budget=PASS' -or
    $output -notmatch '\[scene\.executiongraph\] bench=PASS') {
    "FAIL: X4 sparse compiled projection runtime gate"
    $output -split "`r?`n" | Where-Object { $_ -match 'scene\.executiongraph' }
    if (Test-Path -LiteralPath $errFile) { Get-Content -LiteralPath $errFile }
    exit 1
}

$benchLine = ($output -split "`r?`n" | Where-Object { $_ -match '\[scene\.executiongraph\] bench nodes=' } | Select-Object -Last 1)
"PASS: X4 packed spatial/layout projections, nearest ancestors, identity/value parity, transaction compile gate"
"PASS: $benchLine"
exit 0
