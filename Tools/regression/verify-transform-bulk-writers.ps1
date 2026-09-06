# TransformUpdatePlan X7 Animator/Physics bulk writer gate.
param(
    [string]$Exe = (Join-Path $PSScriptRoot "..\..\Bin\x64-Release\Editor\CreatorEditor.exe"),
    [string]$Work = $env:TEMP,
    [int]$TimeoutSeconds = 180,
    [string]$PoseModel = (Join-Path $PSScriptRoot '../../Dynamic_CPP/Assets/Models/Gunner_F_Mythic.glb'),
    [string]$RebindModel = (Join-Path $PSScriptRoot '../../Dynamic_CPP/Assets/Models/SU_Mythic.glb')
)

. (Join-Path $PSScriptRoot 'CommandResults.ps1')
$repo = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot "..\.."))
$failed = $false
$markers = @(
    [pscustomobject]@{ Path = "Engine\SceneRuntime\Scene.h"; Pattern = "AnimatorPoseUploadMetrics PublishAnimatorPose"; Label = "Animator pose bulk API" },
    [pscustomobject]@{ Path = "Engine\SceneRuntime\Scene.h"; Pattern = "ApplyWorldWriteBatch"; Label = "world write batch API" },
    [pscustomobject]@{ Path = "Engine\SceneRuntime\Scene.cpp"; Pattern = "animatorPoseBindings"; Label = "skeleton binding cache" },
    [pscustomobject]@{ Path = "Engine\SceneRuntime\AnimationJob.cpp"; Pattern = "scene->PublishAnimatorPose(*animator)"; Label = "barrier pose commit" }
)
foreach ($marker in $markers) {
    $path = Join-Path $repo $marker.Path
    if (-not (Test-Path -LiteralPath $path) -or
        -not [System.IO.File]::ReadAllText($path).Contains($marker.Pattern)) {
        "FAIL: missing $($marker.Label) marker in $($marker.Path)"
        $failed = $true
    }
}

$animationJob = [System.IO.File]::ReadAllText(
    (Join-Path $repo "Engine\SceneRuntime\AnimationJob.cpp"))
function Test-BarrierCommitContract([string]$source) {
    $barrierIndex = $source.IndexOf("m_UpdateThreadPool->NotifyAllAndWait();")
    $poseCommitIndex = $source.IndexOf("scene->PublishAnimatorPose(*animator);")
    $socketCommitIndex = $source.IndexOf("socket->Update();")
    return ($barrierIndex -ge 0) -and
        ($poseCommitIndex -gt $barrierIndex) -and
        ($socketCommitIndex -gt $barrierIndex)
}
if (-not (Test-BarrierCommitContract $animationJob)) {
    "FAIL: Animator pose/socket Scene writes are not after the worker barrier"
    $failed = $true
}

$mutated = $animationJob.Replace("scene->PublishAnimatorPose(*animator);", "")
if (Test-BarrierCommitContract $mutated) {
    "FAIL: Animator bulk commit removal mutation stayed GREEN"
    $failed = $true
} else {
    "mutation RED: Animator bulk commit"
}

$animatorSystem = [System.IO.File]::ReadAllText(
    (Join-Path $repo "Engine\SceneRuntime\AnimatorSystem.cpp"))
if ($animatorSystem.Contains("MarkSpatialTransformsDirty")) {
    "FAIL: AnimatorSystem still forces a full spatial resolve before pose evaluation"
    $failed = $true
}

$physicsManager = [System.IO.File]::ReadAllText(
    (Join-Path $repo "Engine\SceneRuntime\PhysicsManager.cpp"))
$physicsBatchCalls = ([regex]::Matches(
    $physicsManager, 'ApplyWorldWriteBatch\(')).Count
if ($physicsBatchCalls -ne 2) {
    "FAIL: Physics writeback expected 2 batch commits, actual=$physicsBatchCalls"
    $failed = $true
}

if ($failed) { exit 1 }
if (-not (Test-Path -LiteralPath $Exe)) { "FAIL: executable missing: $Exe"; exit 1 }

foreach ($modelInput in @($PoseModel, $RebindModel)) {
    if (-not (Test-Path -LiteralPath $modelInput -PathType Leaf)) {
        "FAIL: required skeleton model missing: $modelInput"
        exit 1
    }
}
New-Item -ItemType Directory -Path $Work -Force | Out-Null
$runId = [guid]::NewGuid().ToString('N')
$scenario = Join-Path $Work "transform_bulk_$runId.txt"
$outFile = Join-Path $Work "transform_bulk_$runId.out"
$errFile = Join-Path $Work "transform_bulk_$runId.err"
[System.IO.File]::WriteAllLines($scenario, @(
    "scene.transformbulk probe `"$([IO.Path]::GetFullPath($PoseModel))`" `"$([IO.Path]::GetFullPath($RebindModel))`"",
    "quit"
))

$exeDir = [System.IO.Path]::GetDirectoryName($Exe)
$resultPath = $outFile + ".jsonl"
$proc = Start-Process -FilePath $Exe -ArgumentList @('--commandlet-script', ('"'+$scenario+'"'), '--result-file', ('"'+$resultPath+'"')) `
    -WorkingDirectory $exeDir -WindowStyle Hidden `
    -RedirectStandardOutput $outFile -RedirectStandardError $errFile -PassThru
$proc.WaitForExit($TimeoutSeconds * 1000) | Out-Null
if (-not $proc.HasExited) {
    $proc.Kill()
    "FAIL: X7 runtime probe timeout ($TimeoutSeconds seconds)"
    exit 1
}

$commandResults = @(Read-CommandResults $resultPath)
if (@($commandResults | Where-Object status -ne 'succeeded').Count -ne 0) { throw 'Runtime command failed or was unavailable' }
$output = [System.IO.File]::ReadAllText($outFile)
$animator = 'animator=PASS first-lookups=3 valid=2 invalid=1 steady-lookups=0 off=held on-lookups=0 reload-lookups=3'
$physics = 'physics=PASS requested=2 accepted=2 epoch=1 immediate=PASS global=PASS socket=PASS'
$final = 'barrier=main-thread invalid=held probe=PASS'
if (($proc.ExitCode -ne 0) -or
    ($output -notmatch [regex]::Escape($animator)) -or
    ($output -notmatch [regex]::Escape($physics)) -or
    ($output -notmatch [regex]::Escape($final))) {
    "FAIL: X7 Animator/Physics bulk runtime gate"
    $output -split "`r?`n" | Where-Object { $_ -match 'scene\.transformbulk' }
    if (Test-Path -LiteralPath $errFile) { Get-Content -LiteralPath $errFile }
    exit 1
}

$output -split "`r?`n" | Where-Object { $_ -match 'scene\.transformbulk' }
"PASS: X7 bind-only bone lookup, barrier pose commit, Physics batch, Socket golden"
