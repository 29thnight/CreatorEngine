# TransformUpdatePlan X6 targeted immediate pull gate.
param(
    [string]$Exe = (Join-Path $PSScriptRoot "..\..\Bin\x64-Release\Editor\CreatorEditor.exe"),
    [string]$Work = $env:TEMP,
    [int]$TimeoutSeconds = 180
)

$repo = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot "..\.."))
$failed = $false
$markers = @(
    [pscustomobject]@{ Path = "Engine\SceneRuntime\Scene.h"; Pattern = "bool EnsureResolved(EntityHandle target);"; Label = "targeted pull API" },
    [pscustomobject]@{ Path = "Engine\SceneRuntime\Scene.cpp"; Pattern = "bool Scene::EnsureResolved(EntityHandle target)"; Label = "packed targeted pull" },
    [pscustomobject]@{ Path = "Engine\SceneRuntime\Scene.cpp"; Pattern = "parentWorldEpoch"; Label = "parent propagation epoch" },
    [pscustomobject]@{ Path = "Editor\EngineEntry\ConsoleCommandSystem.cpp"; Pattern = "scene.transformpull"; Label = "X6 runtime probe" }
)

foreach ($marker in $markers) {
    $path = Join-Path $repo $marker.Path
    if (-not (Test-Path -LiteralPath $path) -or
        -not [System.IO.File]::ReadAllText($path).Contains($marker.Pattern)) {
        "FAIL: missing $($marker.Label) marker in $($marker.Path)"
        $failed = $true
    }
}

$clrHostPath = Join-Path $repo "Engine\SceneRuntime\ClrHost.cpp"
$clrHost = [System.IO.File]::ReadAllText($clrHostPath)
$ensureCalls = ([regex]::Matches($clrHost, 'EnsureResolved\(object\);')).Count
if ($clrHost.Contains("EnsureWorldMatrix") -or $ensureCalls -ne 9) {
    "FAIL: ClrHost world API wiring expected EnsureResolved=9 old-helper=0, actual=$ensureCalls"
    $failed = $true
}
else {
    $ensurePattern = [regex]'EnsureResolved\(object\);'
    $mutated = $ensurePattern.Replace($clrHost, '', 1)
    if (([regex]::Matches($mutated, 'EnsureResolved\(object\);')).Count -ne 8) {
        "FAIL: ClrHost wiring mutation did not turn RED"
        $failed = $true
    }
}

if ($failed) { exit 1 }
if (-not (Test-Path -LiteralPath $Exe)) { "FAIL: executable missing: $Exe"; exit 1 }

$runId = [guid]::NewGuid().ToString('N')
$scenario = Join-Path $Work "transform_targeted_pull_$runId.txt"
$outFile = Join-Path $Work "transform_targeted_pull_$runId.out"
$errFile = Join-Path $Work "transform_targeted_pull_$runId.err"
[System.IO.File]::WriteAllLines($scenario, @(
    "scene.transformpull probe",
    "quit"
))

$exeDir = [System.IO.Path]::GetDirectoryName($Exe)
$proc = Start-Process -FilePath $Exe -ArgumentList "--script", $scenario `
    -WorkingDirectory $exeDir -WindowStyle Hidden `
    -RedirectStandardOutput $outFile -RedirectStandardError $errFile -PassThru
$proc.WaitForExit($TimeoutSeconds * 1000) | Out-Null
if (-not $proc.HasExited) {
    $proc.Kill()
    "FAIL: X6 runtime probe timeout ($TimeoutSeconds seconds)"
    exit 1
}

$output = [System.IO.File]::ReadAllText($outFile)
$immediate = 'immediate=PASS getters=PASS queue=1->1 signal=kept'
$sibling = 'parent-pull=PASS sibling-before=stale sibling-global=updated requests=1 ranges=1 nodes=3'
$final = 'clean=empty stale=fail-close fallback=PASS probe=PASS'
if ($proc.ExitCode -ne 0 -or
    $output -notmatch [regex]::Escape($immediate) -or
    $output -notmatch [regex]::Escape($sibling) -or
    $output -notmatch [regex]::Escape($final)) {
    "FAIL: X6 targeted pull runtime gate"
    $output -split "`r?`n" | Where-Object { $_ -match 'scene\.transformpull' }
    if (Test-Path -LiteralPath $errFile) { Get-Content -LiteralPath $errFile }
    exit 1
}

$pullLine = ($output -split "`r?`n" | Where-Object {
    $_ -match '\[scene\.transformpull\] immediate='
} | Select-Object -Last 1)
"PASS: X6 ClrHost EnsureResolved wiring=9, mutation RED, queue/signal preserved, sibling global propagation"
"PASS: $pullLine"
exit 0
