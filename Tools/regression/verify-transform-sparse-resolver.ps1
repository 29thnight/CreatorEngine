# TransformUpdatePlan X5 dirty-root sparse resolver gate.
param(
    [string]$Exe = (Join-Path $PSScriptRoot "..\..\Bin\x64-Release\Editor\CreatorEditor.exe"),
    [string]$Work = $env:TEMP,
    [int]$TimeoutSeconds = 300,
    [int]$BenchNodes = 10000,
    [int]$BenchFrames = 4
)

$repo = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot "..\.."))
$failed = $false
$markers = @(
    [pscustomobject]@{ Path = "Engine\SceneRuntime\Scene.h"; Pattern = "struct SpatialResolveMetrics"; Label = "resolve metrics" },
    [pscustomobject]@{ Path = "Engine\SceneRuntime\Scene.h"; Pattern = "SetSparseSpatialResolverEnabled"; Label = "A/B toggle" },
    [pscustomobject]@{ Path = "Engine\SceneRuntime\Scene.cpp"; Pattern = "dirtyRoots"; Label = "dirty-root queue" },
    [pscustomobject]@{ Path = "Engine\SceneRuntime\Scene.cpp"; Pattern = "entityQueuedEpoch"; Label = "per-node dedupe epoch" },
    [pscustomobject]@{ Path = "Engine\SceneRuntime\Scene.cpp"; Pattern = "std::vector<MeshRenderer*> meshRenderers;"; Label = "compiled culling pointer" },
    [pscustomobject]@{ Path = "Engine\SceneRuntime\Scene.cpp"; Pattern = "ResolveSpatialTransformsSparse"; Label = "packed sparse resolver" },
    [pscustomobject]@{ Path = "Engine\SceneRuntime\Scene.cpp"; Pattern = "ResolveSpatialTransformsLegacy"; Label = "recursive fallback" },
    [pscustomobject]@{ Path = "Editor\EngineEntry\ConsoleCommandSystem.cpp"; Pattern = "scene.sparseresolver"; Label = "X5 runtime probe" }
)

foreach ($marker in $markers) {
    $path = Join-Path $repo $marker.Path
    if (-not (Test-Path -LiteralPath $path) -or
        -not [System.IO.File]::ReadAllText($path).Contains($marker.Pattern)) {
        "FAIL: missing $($marker.Label) marker in $($marker.Path)"
        $failed = $true
    }
}

$sceneSource = [System.IO.File]::ReadAllText((Join-Path $repo "Engine\SceneRuntime\Scene.cpp"))
$sparseBegin = $sceneSource.IndexOf("bool Scene::ResolveSpatialTransformsSparse")
$sparseEnd = $sceneSource.IndexOf("bool Scene::ResolveSpatialTransforms(", $sparseBegin + 1)
if ($sparseBegin -lt 0 -or $sparseEnd -le $sparseBegin) {
    "FAIL: sparse resolver source range missing"
    $failed = $true
}
else {
    $sparseBody = $sceneSource.Substring($sparseBegin, $sparseEnd - $sparseBegin)
    if ($sparseBody -match 'entity\.GetComponent<') {
        "FAIL: Entity component lookup returned to sparse inner loop"
        $failed = $true
    }
}

if ($failed) { exit 1 }
if (-not (Test-Path -LiteralPath $Exe)) { "FAIL: executable missing: $Exe"; exit 1 }

$runId = [guid]::NewGuid().ToString('N')
$scenario = Join-Path $Work "transform_sparse_resolver_$runId.txt"
$outFile = Join-Path $Work "transform_sparse_resolver_$runId.out"
$errFile = Join-Path $Work "transform_sparse_resolver_$runId.err"
[System.IO.File]::WriteAllLines($scenario, @(
    "scene.sparseresolver probe",
    "scene.sparseresolver bench $BenchNodes $BenchFrames",
    "quit"
))

$exeDir = [System.IO.Path]::GetDirectoryName($Exe)
$proc = Start-Process -FilePath $Exe -ArgumentList "--script", $scenario `
    -WorkingDirectory $exeDir -WindowStyle Hidden `
    -RedirectStandardOutput $outFile -RedirectStandardError $errFile -PassThru
$proc.WaitForExit($TimeoutSeconds * 1000) | Out-Null
if (-not $proc.HasExited) {
    $proc.Kill()
    "FAIL: X5 runtime probe timeout ($TimeoutSeconds seconds)"
    exit 1
}

$output = [System.IO.File]::ReadAllText($outFile)
$probeLine = 'idle=empty leaf=requests:1/ranges:1/nodes:1 merged=requests:2/ranges:1/merged:1/nodes:3'
$abLine = 'legacy=recursive return-sparse=packed/full:yes ab=exact'
if ($proc.ExitCode -ne 0 -or
    $output -notmatch [regex]::Escape($probeLine) -or
    $output -notmatch [regex]::Escape($abLine) -or
    $output -notmatch '\[scene\.sparseresolver\] probe=PASS' -or
    $output -notmatch "\[scene\.sparseresolver\] bench n=$BenchNodes result=PASS") {
    "FAIL: X5 dirty-root sparse resolver runtime gate"
    $output -split "`r?`n" | Where-Object { $_ -match 'scene\.sparseresolver' }
    if (Test-Path -LiteralPath $errFile) { Get-Content -LiteralPath $errFile }
    exit 1
}

$leafLine = ($output -split "`r?`n" | Where-Object {
    $_ -match "bench n=$BenchNodes mode=sparse scenario=leaf "
} | Select-Object -Last 1)
$fullLine = ($output -split "`r?`n" | Where-Object {
    $_ -match "bench n=$BenchNodes result=PASS"
} | Select-Object -Last 1)
"PASS: X5 duplicate/descendant range merge, sparse packed resolve, recursive A/B exact parity"
"PASS: $leafLine"
"PASS: $fullLine"
exit 0
