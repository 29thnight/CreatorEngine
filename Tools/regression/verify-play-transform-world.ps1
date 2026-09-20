[CmdletBinding()]
param(
    [string]$Exe = (Join-Path $PSScriptRoot '../../Bin/x64-Debug/Editor/CreatorEditor.exe'),
    [string]$Work = '',
    [int]$TimeoutSeconds = 180
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$Exe = [IO.Path]::GetFullPath($Exe)
if (-not (Test-Path -LiteralPath $Exe)) { throw "Editor executable missing: $Exe" }
if (-not $Work) { $Work = Join-Path $env:TEMP ('CE_PlayTransformWorld_' + [guid]::NewGuid().ToString('N')) }
$Work = [IO.Path]::GetFullPath($Work)
New-Item -ItemType Directory -Force -Path $Work | Out-Null

# 실제 Editor 프레임에서 Play/Stop을 세 번 반복한다. 저장된 TRS만 비교하면
# 씬 루트의 파괴 표시 때문에 world cache가 원점에 남는 결함을 놓친다.
$lines = [Collections.Generic.List[string]]::new()
@('wait 30', 'scene.new PlayTransformWorld', 'wait 10', 'scene.sparseresolver 1',
    'object.create TF_Empty Empty', 'object.transform TF_Empty 11 22 33 12 24 36 1.2 1.5 0.75',
    'object.create TF_Camera Camera', 'object.transform TF_Camera 21 12 13 -10 20 30',
    'object.create TF_Light Light', 'object.transform TF_Light 31 32 23 30 -15 5',
    'object.create TF_Parent Empty', 'object.transform TF_Parent 10 20 30',
    'object.create TF_Child Empty', 'object.parent TF_Child TF_Parent',
    'object.transform TF_Child 1 2 3 5 6 7 0.8 0.9 1.1',
    'wait 10', 'scene.transformdigest edit') | ForEach-Object { $lines.Add($_) }
for ($cycle = 1; $cycle -le 3; ++$cycle) {
    @('play', 'wait 10', 'play.state', "object.create TF_Runtime$cycle Empty",
        "object.transform TF_Runtime$cycle 41 42 43", 'wait 10', "scene.transformdigest playing$cycle",
        'stop', 'wait 10', 'play.state', "scene.transformdigest restored$cycle") |
        ForEach-Object { $lines.Add($_) }
}
@('object.transform TF_Parent 50 60 70', 'wait 10', 'scene.transformdigest edited', 'quit') |
    ForEach-Object { $lines.Add($_) }
$commandFile = Join-Path $Work 'commands.txt'
$resultFile = Join-Path $Work 'results.jsonl'
[IO.File]::WriteAllLines($commandFile, $lines, [Text.UTF8Encoding]::new($false))
if (Test-Path -LiteralPath $resultFile) { Remove-Item -LiteralPath $resultFile }

$priorWorkspace = $env:CREATOR_EDITOR_WORKSPACE_DIR
$priorLegacy = $env:CREATOR_EDITOR_LEGACY_INI
$process = $null
try {
    $env:CREATOR_EDITOR_WORKSPACE_DIR = Join-Path $Work 'workspace'
    $env:CREATOR_EDITOR_LEGACY_INI = Join-Path $Work 'workspace/legacy.ini'
    $process = Start-Process -FilePath $Exe -WorkingDirectory (Split-Path $Exe) -WindowStyle Hidden `
        -ArgumentList @('--script', ('"' + $commandFile + '"'), '--result-format', 'jsonl',
            '--result-file', ('"' + $resultFile + '"')) `
        -RedirectStandardOutput (Join-Path $Work 'editor.out') `
        -RedirectStandardError (Join-Path $Work 'editor.err') -PassThru
    $deadline = (Get-Date).AddSeconds($TimeoutSeconds)
    while (-not $process.WaitForExit(1000)) {
        if ((Get-Date) -ge $deadline) { throw "Editor timed out: $Work" }
    }
    $process.WaitForExit()
    $exitCode = $process.ExitCode
}
finally {
    if ($process -and -not $process.HasExited) { $process.Kill(); $process.WaitForExit() }
    $env:CREATOR_EDITOR_WORKSPACE_DIR = $priorWorkspace
    $env:CREATOR_EDITOR_LEGACY_INI = $priorLegacy
}

$rows = @(Get-Content -LiteralPath $resultFile | ForEach-Object { $_ | ConvertFrom-Json })
if ($rows.Count -ne $lines.Count) { throw "Incomplete results: $($rows.Count)/$($lines.Count)" }
for ($i = 0; $i -lt $rows.Count; ++$i) {
    if ($rows[$i].status -ne 'succeeded') { throw "Command failed: $($lines[$i]): $($rows[$i].message)" }
}
if ($exitCode -ne 0) { throw "Editor exited ${exitCode}: $Work" }
$states = @($rows | Where-Object command -eq 'play.state')
if ($states.Count -ne 6) { throw 'Missing Play/Stop state observations' }
for ($i = 0; $i -lt $states.Count; ++$i) {
    $playing = ($i % 2) -eq 0
    if ($states[$i].data.committed -ne $playing -or $states[$i].data.gameStart -ne $playing) {
        throw "Play/Stop did not commit at observation $i"
    }
}

function Assert-Near([object[]]$Actual, [object[]]$Expected, [string]$Label) {
    if ($Actual.Count -ne $Expected.Count) { throw "${Label}: incorrect element count" }
    for ($i = 0; $i -lt $Actual.Count; ++$i) {
        $value = [double]$Actual[$i]
        if ([double]::IsNaN($value) -or [double]::IsInfinity($value) -or
            [math]::Abs($value - [double]$Expected[$i]) -gt 0.0005) {
            throw "${Label}[$i]: actual=$value expected=$($Expected[$i])"
        }
    }
}

$positions = @{
    TF_Empty = @(11,22,33); TF_Camera = @(21,12,13); TF_Light = @(31,32,23)
    TF_Parent = @(10,20,30); TF_Child = @(1,2,3)
}
$snapshots = @($rows | Where-Object command -eq 'scene.transformdigest')
if ($snapshots.Count -ne 8) { throw 'Missing world-transform snapshots' }
$baseline = @{}
foreach ($snapshot in $snapshots) {
    $label = $snapshot.data.label
    $byName = @{}
    foreach ($transform in $snapshot.data.worldTransforms) {
        if ($byName.ContainsKey($transform.name)) { throw "${label}: duplicate object $($transform.name)" }
        $byName[$transform.name] = $transform
    }
    if (-not $byName.ContainsKey('<scene-root>') -or $byName['<scene-root>'].destroyMarked) {
        throw "${label}: scene root is missing or marked for destruction"
    }
    for ($cycle = 1; $cycle -le 3; ++$cycle) {
        if ($byName.ContainsKey("TF_Runtime$cycle") -ne ($label -eq "playing$cycle")) {
            throw "${label}: runtime-only object lifetime is incorrect"
        }
    }
    foreach ($name in $positions.Keys) {
        if (-not $byName.ContainsKey($name)) { throw "${label}: missing $name" }
        $actual = $byName[$name]
        if ($actual.destroyMarked) { throw "${label}: $name is marked for destruction" }
        $localPosition = $positions[$name]
        $worldPosition = if ($name -eq 'TF_Child') { @(11,22,33) } else { $localPosition }
        if ($label -eq 'edited') {
            if ($name -eq 'TF_Parent') { $localPosition = @(50,60,70); $worldPosition = $localPosition }
            if ($name -eq 'TF_Child') { $worldPosition = @(51,62,73) }
        }
        Assert-Near $actual.position $localPosition "$label/$name authored position"
        Assert-Near $actual.worldPosition $worldPosition "$label/$name cached world position"
        if ($actual.worldMatrix.Count -ne 16) { throw "$label/${name}: missing world matrix" }
        Assert-Near $actual.worldMatrix[12..14] $worldPosition "$label/$name gizmo matrix translation"
        if ($label -eq 'edit') { $baseline[$name] = @($actual.worldMatrix) }
        $expectedMatrix = @($baseline[$name])
        for ($axis = 0; $axis -lt 3; ++$axis) { $expectedMatrix[12 + $axis] = $worldPosition[$axis] }
        Assert-Near $actual.worldMatrix $expectedMatrix "$label/$name world matrix"
    }
}
Write-Output "PLAY_TRANSFORM_WORLD_OK cycles=3 snapshots=8 objects=5 runtimeLifetime=ok parentPropagation=ok"
Write-Output "Evidence: $Work"
