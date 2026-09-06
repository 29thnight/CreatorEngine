# TransformUpdatePlan X2 UI/Spatial domain gate.
param(
    [string]$Exe = (Join-Path $PSScriptRoot "..\..\Bin\x64-Release\Editor\CreatorEditor.exe"),
    [string]$Work = $env:TEMP,
    [int]$TimeoutSeconds = 120
)

$repo = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot "..\.."))
$failed = $false

$markers = @(
    [pscustomobject]@{ Path = "Engine\SceneRuntime\Scene.h"; Pattern = "void SyncDerivedState("; Label = "ordered sync entry" },
    [pscustomobject]@{ Path = "Engine\SceneRuntime\Scene.h"; Pattern = "bool ResolveSpatialTransforms("; Label = "spatial entry" },
    [pscustomobject]@{ Path = "Engine\SceneRuntime\Scene.h"; Pattern = "std::atomic<uint64_t> m_uiDirtyEpoch"; Label = "UI epoch" },
    [pscustomobject]@{ Path = "Engine\SceneRuntime\Scene.h"; Pattern = "std::atomic<uint64_t> m_spatialDirtyEpoch"; Label = "spatial epoch" },
    [pscustomobject]@{ Path = "Engine\SceneRuntime\RectTransformComponent.cpp"; Pattern = "scene->MarkUILayoutDirty()"; Label = "Rect writer publication" },
    [pscustomobject]@{ Path = "Engine\SceneRuntime\AnimationJob.cpp"; Pattern = "scene->PublishAnimatorPose(*animator)"; Label = "Animator bulk publication" }
)

foreach ($marker in $markers) {
    $path = Join-Path $repo $marker.Path
    if (-not (Test-Path $path) -or
        -not [System.IO.File]::ReadAllText($path).Contains($marker.Pattern)) {
        "FAIL: missing $($marker.Label) marker in $($marker.Path)"
        $failed = $true
    }
}

$sceneSource = [System.IO.File]::ReadAllText((Join-Path $repo "Engine\SceneRuntime\Scene.cpp"))
$ordered = $sceneSource.IndexOf("UpdateUILayout();", $sceneSource.IndexOf("void Scene::SyncDerivedState"))
$spatial = $sceneSource.IndexOf("ResolveSpatialTransforms();", $sceneSource.IndexOf("void Scene::SyncDerivedState"))
if ($ordered -lt 0 -or $spatial -lt 0 -or $ordered -ge $spatial) {
    "FAIL: SyncDerivedState is not UI -> Spatial"
    $failed = $true
}

if ($failed) { exit 1 }
if (-not (Test-Path $Exe)) { "FAIL: executable missing: $Exe"; exit 1 }

$runId = [guid]::NewGuid().ToString('N')
$scenario = Join-Path $Work "transform_domain_gate_$runId.txt"
$outFile = Join-Path $Work "transform_domain_gate_$runId.out"
$errFile = Join-Path $Work "transform_domain_gate_$runId.err"
[System.IO.File]::WriteAllLines($scenario, @(
    "scene.transformdomains probe",
    "quit"
))

$exeDir = [System.IO.Path]::GetDirectoryName($Exe)
$proc = Start-Process -FilePath $Exe -ArgumentList "--commandlet-script", $scenario `
    -WorkingDirectory $exeDir -WindowStyle Hidden `
    -RedirectStandardOutput $outFile -RedirectStandardError $errFile -PassThru
$proc.WaitForExit($TimeoutSeconds * 1000) | Out-Null
if (-not $proc.HasExited) {
    $proc.Kill()
    "FAIL: runtime probe timeout ($TimeoutSeconds seconds)"
    exit 1
}

$output = [System.IO.File]::ReadAllText($outFile)
$expected = 'clean=empty/empty ui-write=run/empty spatial-write=empty/run subtree=immediate\+run/empty paused=ui-consumed\+empty/run final=empty/empty'
if ($proc.ExitCode -ne 0 -or $output -notmatch $expected -or
    $output -notmatch '\[scene\.transformdomains\] probe=PASS order=UI->Spatial') {
    "FAIL: X2 runtime domain probe"
    $output -split "`r?`n" | Where-Object { $_ -match 'scene\.transformdomains' }
    if (Test-Path $errFile) { Get-Content $errFile }
    exit 1
}

"PASS: X2 clean=empty/empty, UI-only=run/empty, Spatial-only=empty/run, subtree=immediate, paused=UI-only"
exit 0
