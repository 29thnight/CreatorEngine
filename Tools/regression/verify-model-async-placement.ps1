param(
    [string]$Editor = (Join-Path $PSScriptRoot '..\..\Bin\x64-Debug\Editor\CreatorEditor.exe'),
    [string]$Work = $env:TEMP,
    [int]$TimeoutSeconds = 240
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$root = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$run = Join-Path $Work ('creator-model-async-' + [guid]::NewGuid().ToString('N'))
[IO.Directory]::CreateDirectory($run) | Out-Null
$gunner = (Join-Path $root 'Dynamic_CPP\Assets\Models\Gunner_F_Mythic.glb').Replace('\', '/')
$su = (Join-Path $root 'Dynamic_CPP\Assets\Models\SU_Mythic.glb').Replace('\', '/')
$large = (Join-Path $root 'Dynamic_CPP\Assets\Models\scene.glb').Replace('\', '/')
$loaded = (Join-Path $run 'loaded.scene').Replace('\', '/')
$undone = (Join-Path $run 'undone.scene').Replace('\', '/')
$redone = (Join-Path $run 'redone.scene').Replace('\', '/')
$switched = (Join-Path $run 'switched.scene').Replace('\', '/')
$stopped = (Join-Path $run 'stopped.scene').Replace('\', '/')
$guarded = (Join-Path $run 'guarded.scene').Replace('\', '/')
[IO.File]::WriteAllText($guarded, 'preserve-existing-scene')
$commands = @(
    'scene.new AsyncPlacement', 'wait 10',
    "model.async $gunner", 'model.async status', 'model.async wait', 'wait 10',
    'model.async status', 'assets.scenemodel', 'scene.hierarchycheck', "scene.save $loaded",
    "model.async probe $guarded",
    'undo', 'wait 2', "scene.save $undone",
    'redo', 'model.async wait', 'wait 10', 'assets.scenemodel', "scene.save $redone",
    'model.async missing-async-model.glb', 'model.async wait',
    "model.async $su", 'undo', 'wait 10',
    "model.async $large", 'scene.new AsyncSwitched', 'wait 30',
    "scene.save $switched",
    "model.async $gunner", 'play', 'wait 10', 'stop', 'wait 20',
    "scene.save $stopped", 'model.async status', 'quit'
)
$scenario = Join-Path $run 'commands.txt'
[IO.File]::WriteAllText($scenario, ($commands -join "`n") + "`n", [Text.UTF8Encoding]::new($false))
$start = [Diagnostics.ProcessStartInfo]::new()
$start.FileName = (Resolve-Path $Editor).Path
$start.Arguments = '--script "' + $scenario + '"'
$start.WorkingDirectory = $root
$start.UseShellExecute = $false
$start.CreateNoWindow = $true
$start.RedirectStandardOutput = $true
$start.RedirectStandardError = $true
$process = [Diagnostics.Process]::new()
$process.StartInfo = $start
if (-not $process.Start()) { throw 'Editor start failed' }
$stdoutTask = $process.StandardOutput.ReadToEndAsync()
$stderrTask = $process.StandardError.ReadToEndAsync()
if (-not $process.WaitForExit($TimeoutSeconds * 1000)) {
    $process.Kill()
    throw "Editor timeout; artifacts: $run"
}
$process.WaitForExit()
$stdout = $stdoutTask.GetAwaiter().GetResult()
$stderr = $stderrTask.GetAwaiter().GetResult()
[IO.File]::WriteAllText((Join-Path $run 'stdout.txt'), $stdout, [Text.UTF8Encoding]::new($false))
[IO.File]::WriteAllText((Join-Path $run 'stderr.txt'), $stderr, [Text.UTF8Encoding]::new($false))
$failures = [Collections.Generic.List[string]]::new()
if ($process.ExitCode -ne 0) { $failures.Add("Exit code: $($process.ExitCode)") }
if (-not [string]::IsNullOrWhiteSpace($stderr)) { $failures.Add('Nonempty stderr') }
if ($stdout.Contains('[model.async] wait timed out')) { $failures.Add('Async wait timed out') }
if ($stdout -notmatch '\[model\.async\] cancellation-probe pass') { $failures.Add('Cancellation/slot reuse probe failed') }
if ([IO.File]::ReadAllText($guarded) -ne 'preserve-existing-scene') { $failures.Add('A blocked save changed the existing scene file') }
$ready = [regex]::Matches($stdout, '\[model\.async\] ready path=.*? steps=(\d+) frames=(\d+) loadingFrames=(\d+) prepareMs=([\d.]+) maxApplyUs=(\d+)')
if ($ready.Count -ne 2) { $failures.Add("Expected two successful placements, got $($ready.Count)") }
if ($ready.Count -gt 0) {
    if ([int]$ready[0].Groups[2].Value -le 1) { $failures.Add('Scene application did not span frames') }
    if ([int]$ready[0].Groups[3].Value -le 0) { $failures.Add('No engine frames observed while cold preparation ran') }
}
if ([regex]::Matches($stdout, '\[CLI\] assets.scenemodel pass').Count -ne 2) {
    $failures.Add('Typed meshes/material/embedded texture closure did not pass twice')
}
$status = [regex]::Matches($stdout, '\[model\.async\] pending=(\d+) completed=(\d+) failed=(\d+) cancelled=(\d+)')
if ($status.Count -eq 0) { $failures.Add('Missing status') }
else {
    $last = $status[$status.Count - 1]
    if ($last.Groups[1].Value -ne '0' -or $last.Groups[2].Value -ne '2' -or
        $last.Groups[3].Value -ne '1' -or [int]$last.Groups[4].Value -lt 3) {
        $failures.Add("Unexpected final status: $($last.Value)")
    }
}
foreach ($path in @($loaded, $redone)) {
    if (-not (Test-Path -LiteralPath $path)) { $failures.Add("Missing scene: $path"); continue }
    if ([regex]::Matches([IO.File]::ReadAllText($path), 'm_meshAssetId:').Count -ne 2) {
        $failures.Add("Expected two persisted mesh IDs: $path")
    }
}
foreach ($path in @($undone, $switched, $stopped)) {
    if (-not (Test-Path -LiteralPath $path)) { $failures.Add("Missing scene: $path"); continue }
    if ([IO.File]::ReadAllText($path) -match 'm_meshAssetId:|Gunner_F_Mythic|SU_Mythic') {
        $failures.Add("Cancelled model leaked into scene: $path")
    }
}
$ready | ForEach-Object { $_.Value }
"Artifacts: $run"
if ($failures.Count) { $failures | ForEach-Object { Write-Error $_ -ErrorAction Continue }; exit 1 }
'PASS: cold async preparation, incremental placement, undo/redo, failure, scene/play cancellation'
