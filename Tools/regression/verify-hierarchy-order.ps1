param([string]$Configuration = 'Debug')
$ErrorActionPreference = 'Stop'
$repo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
. (Join-Path $PSScriptRoot 'CommandResults.ps1')
$out = Join-Path $repo "Build/Verification/HierarchyOrder/$Configuration"
New-Item -ItemType Directory -Force -Path $out | Out-Null
$commands = @(
    'scene.new HierarchyOrderProbe', 'wait 5',
    'object.create OrderA', 'object.create OrderB', 'object.create OrderC', 'object.create OrderP',
    'scene.order -', 'object.order OrderC OrderA before', 'scene.order -',
    'undo', 'scene.order -', 'redo', 'scene.order -',
    'object.order OrderA OrderB after', 'scene.order -',
    'object.parent OrderA OrderP', 'object.order OrderB OrderA before',
    'scene.order -', 'scene.order OrderP', 'undo', 'scene.order -', 'scene.order OrderP',
    'redo', 'scene.order -', 'scene.order OrderP', 'scene.hierarchycheck',
    'play', 'wait 5', 'play.state', 'object.create RuntimeOnly',
    'object.order OrderA OrderB before', 'stop', 'wait 5', 'play.state',
    'scene.order -', 'scene.order OrderP', 'scene.hierarchycheck',
    'play', 'wait 5', 'stop', 'wait 5', 'scene.order -', 'scene.order OrderP',
    'object.order OrderB OrderC after', 'scene.order -', 'scene.order OrderP',
    'undo', 'scene.order -', 'scene.order OrderP', 'redo', 'scene.order -', 'scene.order OrderP',
    'object.order OrderP OrderA after', 'object.order OrderC OrderC before', 'quit'
)
$commandFile = Join-Path $out 'commands.txt'
$resultFile = Join-Path $out 'results.jsonl'
$commands | Set-Content $commandFile -Encoding utf8
$env:CREATOR_EDITOR_WORKSPACE_DIR = Join-Path $out 'workspace'
$env:CREATOR_EDITOR_LEGACY_INI = Join-Path $out 'workspace/legacy.ini'
New-Item -ItemType Directory -Force -Path $env:CREATOR_EDITOR_WORKSPACE_DIR | Out-Null
$exe = Join-Path $repo "Bin/x64-$Configuration/Editor/CreatorEditor.exe"
$p = Start-Process -FilePath $exe -WindowStyle Hidden -WorkingDirectory (Split-Path $exe) -PassThru `
    -ArgumentList @('--commandlet-script', ('"'+$commandFile+'"'), '--result-format', 'jsonl', '--result-file', ('"'+$resultFile+'"')) `
    -RedirectStandardOutput (Join-Path $out 'stdout.log') -RedirectStandardError (Join-Path $out 'stderr.log')
$deadline = (Get-Date).AddSeconds(240)
while (-not $p.WaitForExit(1000)) {
    if ((Get-Date) -ge $deadline) { $p.Kill(); throw 'Hierarchy order probe timed out' }
}
$p.WaitForExit()
$results = @(Read-CommandResults $resultFile)
$failures = @($results | Where-Object status -ne 'succeeded')
if ($p.ExitCode -ne 2 -or $failures.Count -ne 2 -or @($failures | Where-Object command -ne 'object.order').Count) {
    throw "Unexpected command failure or exit: $($p.ExitCode) $($failures | ConvertTo-Json -Compress)"
}
$expected = @(
    'OrderA,OrderB,OrderC,OrderP', 'OrderC,OrderA,OrderB,OrderP',
    'OrderA,OrderB,OrderC,OrderP', 'OrderC,OrderA,OrderB,OrderP', 'OrderC,OrderB,OrderA,OrderP',
    'OrderC,OrderP', 'OrderB,OrderA', 'OrderC,OrderB,OrderP', 'OrderA',
    'OrderC,OrderP', 'OrderB,OrderA', 'OrderC,OrderP', 'OrderB,OrderA',
    'OrderC,OrderP', 'OrderB,OrderA', 'OrderC,OrderB,OrderP', 'OrderA', 'OrderC,OrderP', 'OrderB,OrderA',
    'OrderC,OrderB,OrderP', 'OrderA'
)
$orders = @($results | Where-Object command -eq 'scene.order')
if ($orders.Count -ne $expected.Count) { throw "Missing order snapshots: $($orders.Count)" }
for ($i = 0; $i -lt $orders.Count; ++$i) {
    $actual = @($orders[$i].data.children) -join ','
    if ($actual -ne $expected[$i]) { throw "Order snapshot $i expected=$($expected[$i]) actual=$actual" }
    if ($expected[$i].Contains('OrderC') -and (@($orders[$i].data.visibleRoots) -join ',') -ne $expected[$i]) {
        throw "Hierarchy display differs from authored roots at snapshot $i"
    }
}
$states = @($results | Where-Object command -eq 'play.state')
if ($states.Count -ne 2 -or -not $states[0].data.gameStart -or $states[1].data.gameStart) {
    throw 'Play transition did not occur'
}
"HIERARCHY_ORDER_OK configuration=$Configuration snapshots=$($orders.Count) playCycles=2 rejected=2 undoRedo=passed visibleRoots=passed"
