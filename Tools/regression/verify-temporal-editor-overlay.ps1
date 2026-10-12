param(
    [string]$Editor = (Join-Path $PSScriptRoot '../../Bin/x64-Debug/Editor/CreatorEditor.exe'),
    [string]$Work = $env:TEMP,
    [switch]$FullViewport = $true,
    [switch]$RequireRequestedProviders,
    [int]$TimeoutSeconds = 180
)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'CommandResults.ps1')
$root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
if (Get-Process CreatorEditor -ErrorAction SilentlyContinue) { throw 'Close the Editor before this isolated gate.' }
$run = Join-Path ([IO.Path]::GetFullPath($Work)) ('creator-temporal-overlay-' + [guid]::NewGuid().ToString('N'))
[IO.Directory]::CreateDirectory($run) | Out-Null
$commands = @('scene.new OverlayProbe','object.create ProbeCamera','component.add ProbeCamera CameraComponent','object.transform ProbeCamera 0 2 -5 15 0 0','object.create ProbeLight Light','object.transform ProbeLight 0 2 0 45 -25 0','camera.editor match')
foreach ($mode in @('none quality','fsr quality','fsr balanced','fsr performance','fsr native-aa','dlss quality','xess quality','none quality')) {
    $index = $commands.Count
    $commands += @("temporal.upscale $mode",'wait 120','temporal.status',('render.pbr.capture "' + (Join-Path $run ("$index-" + $mode.Replace(' ', '-'))) + '" editor observed'))
}
$commands += 'quit'
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
    $process.Kill(); $process.WaitForExit(); throw "Temporal overlay timed out: $run"
}
$stdout = $stdoutTask.GetAwaiter().GetResult()
[IO.File]::WriteAllText((Join-Path $run 'stdout.txt'), $stdout, $utf8)
[IO.File]::WriteAllText((Join-Path $run 'stderr.txt'), $stderrTask.GetAwaiter().GetResult(), $utf8)
$rows = @(Read-CommandResults (Join-Path $run 'results.jsonl'))
if ($process.ExitCode -ne 0 -or $rows.Count -ne $commands.Count -or @($rows | Where-Object status -ne succeeded).Count) {
    throw "Temporal overlay commands failed: $run"
}
if ($stdout.Contains('[render.view.capture] failed')) { throw "View capture rejected: $run" }
if (@(Get-ChildItem -LiteralPath $run -Directory | Where-Object {
    Test-Path -LiteralPath (Join-Path $_.FullName 'manifest.json')
}).Count -ne 8) { throw "Missing transition captures: $run" }
$oracleArguments = @((Join-Path $PSScriptRoot 'TemporalOverlayImageOracle.py'), $run)
if ($RequireRequestedProviders) { $oracleArguments += '--require-requested-providers' }
& python @oracleArguments
if ($LASTEXITCODE) { throw "Temporal overlay pixel oracle failed: $run" }
"Temporal overlay PASS: $run"
