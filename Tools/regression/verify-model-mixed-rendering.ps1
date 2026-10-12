param(
    [string]$Editor = (Join-Path $PSScriptRoot '../../Bin/x64-Debug/Editor/CreatorEditor.exe'),
    [string]$Work = $env:TEMP,
    [switch]$FullViewport,
    [ValidateSet('none','fsr','dlss','xess')][string]$Upscaler = 'none',
    [switch]$RequireRequestedProvider,
    [int]$TimeoutSeconds = 180
)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'CommandResults.ps1')
$root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
if (Get-Process CreatorEditor -ErrorAction SilentlyContinue) { throw 'Close the Editor before this isolated gate.' }
$run = Join-Path ([IO.Path]::GetFullPath($Work)) ('creator-mixed-rendering-' + [guid]::NewGuid().ToString('N'))
[IO.Directory]::CreateDirectory($run) | Out-Null
$models = Join-Path $root 'Dynamic_CPP/Assets/Models'
$captureMode = if ($Upscaler -eq 'none') { '' } else { ' observed' }
$commands = @('scene.new MixedRendering', 'object.create ProbeCamera', 'component.add ProbeCamera CameraComponent',
    'object.transform ProbeCamera 0 1 -5 0 0 0', 'object.create ProbeLight Light',
    'object.transform ProbeLight 0 2 0 45 -25 0',
    ('model.async "' + (Join-Path $models 'Aria_Dagger_ChainSwordWeapon.glb') + '"'), 'model.async wait',
    ('model.async "' + (Join-Path $models 'Prim_Cone.glb') + '"'), 'model.async wait',
    'object.transform Prim_Cone -1.478 0 0.516 0 0 0', 'camera.editor match', "temporal.upscale $Upscaler quality", 'wait 120',
    ('render.pbr.capture "' + (Join-Path $run 'scene') + '" editor' + $captureMode),
    'editor.viewport game', 'wait 5',
    ('render.pbr.capture "' + (Join-Path $run 'game') + '" game' + $captureMode), 'editor.viewport scene', 'temporal.upscale none', 'wait 5', 'quit')
$utf8 = [Text.UTF8Encoding]::new($false)
[IO.File]::WriteAllLines((Join-Path $run 'commands.txt'), $commands, $utf8)
$start = [Diagnostics.ProcessStartInfo]::new()
$start.FileName = [IO.Path]::GetFullPath($Editor)
if (!$FullViewport) { $start.ArgumentList.Add('--smoke-offscreen') }
$start.ArgumentList.Add('--commandlet-script'); $start.ArgumentList.Add((Join-Path $run 'commands.txt'))
$start.ArgumentList.Add('--result-file'); $start.ArgumentList.Add((Join-Path $run 'results.jsonl'))
$start.WorkingDirectory = $root
$start.UseShellExecute = $false
$start.CreateNoWindow = $true
$start.WindowStyle = [Diagnostics.ProcessWindowStyle]::Hidden
$start.RedirectStandardOutput = $true
$start.RedirectStandardError = $true
$process = [Diagnostics.Process]::Start($start)
$stdoutTask = $process.StandardOutput.ReadToEndAsync()
$stderrTask = $process.StandardError.ReadToEndAsync()
if (!$process.WaitForExit($TimeoutSeconds * 1000)) {
    $process.Kill(); $process.WaitForExit(); throw "Mixed rendering timed out: $run"
}
$stdout = $stdoutTask.GetAwaiter().GetResult()
[IO.File]::WriteAllText((Join-Path $run 'stdout.txt'), $stdout, $utf8)
[IO.File]::WriteAllText((Join-Path $run 'stderr.txt'), $stderrTask.GetAwaiter().GetResult(), $utf8)
$rows = @(Read-CommandResults (Join-Path $run 'results.jsonl'))
if ($process.ExitCode -ne 0 -or $rows.Count -ne $commands.Count -or @($rows | Where-Object status -ne succeeded).Count) {
    throw "Mixed rendering command failed: $run"
}
if ($stdout.Contains('[render.view.capture] failed')) { throw "View capture rejected mixed geometry: $run" }
$records = @()
foreach ($view in 'scene', 'game') {
    $capture = Join-Path $run $view
    $manifest = [IO.File]::ReadAllText((Join-Path $capture 'manifest.json')) | ConvertFrom-Json
    $draws = @($manifest.draws)
    if ($RequireRequestedProvider -and $manifest.upscaler -ne $Upscaler) {
        throw "Requested $Upscaler fell back to $($manifest.upscaler) for ${view}: $run"
    }
    if ($Upscaler -ne 'none' -and ($manifest.temporalNativeGateActive -or $manifest.upscaler -eq 'none' -or
        $manifest.renderWidth -ge $manifest.displayWidth)) { throw "Actual reconstruction not captured for ${view}: $run" }
    $skinned = @($draws | Where-Object { $_.pose.Count -gt 0 }).Count
    $rigid = @($draws | Where-Object { $_.pose.Count -eq 0 }).Count
    # Aria body/hair use skinning; its attached dagger and the cone must not.
    if (!$manifest.finite -or $draws.Count -ne 4 -or $skinned -ne 2 -or $rigid -ne 2) {
        throw "Wrong mixed geometry/pose output for ${view}: $run"
    }
    $depth = [IO.File]::ReadAllBytes((Join-Path $capture 'depth.f32'))
    $covered = 0
    for ($offset = 0; $offset -lt $depth.Length; $offset += 4) {
        $value = [BitConverter]::ToSingle($depth, $offset)
        if ([single]::IsNaN($value) -or [single]::IsInfinity($value)) { throw 'Nonfinite depth' }
        if ($value -lt 0.9999) { ++$covered }
    }
    if (!$covered) { throw "No rendered mesh pixels for ${view}: $run" }
    $records += @{view=$view;draws=$draws.Count;skinned=$skinned;rigid=$rigid;coveredDepthPixels=$covered;
        width=$manifest.width;height=$manifest.height;upscaler=$manifest.upscaler;
        renderWidth=$manifest.renderWidth;renderHeight=$manifest.renderHeight;capture=$capture}
}
@{passed=$true;fullViewport=[bool]$FullViewport;requestedUpscaler=$Upscaler;views=$records} | ConvertTo-Json -Depth 5 |
    Set-Content -LiteralPath (Join-Path $run 'summary.json') -Encoding utf8
"Mixed model rendering PASS: $run"
