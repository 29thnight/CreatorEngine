param(
    [string]$Editor = (Join-Path $PSScriptRoot '../../Bin/x64-Debug/Editor/CreatorEditor.exe'),
    [string]$Work = (Join-Path $PSScriptRoot '../../Artifacts/PR169'),
    [int]$TimeoutSeconds = 300
)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'CommandResults.ps1')
$root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
if (Get-Process CreatorEditor -ErrorAction SilentlyContinue) { throw 'Close the Editor before this isolated gate.' }
$run = Join-Path ([IO.Path]::GetFullPath($Work)) ('temporal-input-demand-' + [guid]::NewGuid().ToString('N'))
[IO.Directory]::CreateDirectory($run) | Out-Null
$commands = @('scene.new TemporalDemand','object.create ProbeCamera','component.add ProbeCamera CameraComponent',
    'object.transform ProbeCamera 0 1 -5 0 0 0','object.create ProbeLight Light','object.transform ProbeLight 0 2 0 45 -25 0',
    ('model.async "' + (Join-Path $root 'Dynamic_CPP/Assets/Models/Prim_Cone.glb') + '"'),
    'model.async wait','camera.editor match','temporal.fg none','temporal.upscale none','render.graph scene')
$observations = @()
foreach ($case in @(@{name='native-gi';intensity=1;provider='none'},
                   @{name='native-bypass';intensity=0;provider='none'},
                   @{name='dlss-no-gi';intensity=0;provider='dlss'},
                   @{name='native-resumed';intensity=1;provider='none'})) {
    $commands += @("render.ssgi.intensity $($case.intensity)","temporal.upscale $($case.provider)",
        'render.live.wait','wait 120','render.graph scene','render.live.wait','wait 60','render.graph scene')
    $observations += @{case=$case;index=$commands.Count-1}
}
$commands += @('dx12.ssgi','dx12.ui','dx12.validation','quit')
$utf8 = [Text.UTF8Encoding]::new($false)
[IO.File]::WriteAllLines((Join-Path $run 'commands.txt'), $commands, $utf8)
$start = [Diagnostics.ProcessStartInfo]::new()
$start.FileName = [IO.Path]::GetFullPath($Editor)
foreach ($arg in @('--commandlet-script',"$run/commands.txt",'--result-file',"$run/results.jsonl")) { $start.ArgumentList.Add($arg) }
$start.WorkingDirectory=$root; $start.UseShellExecute=$false; $start.CreateNoWindow=$true
$start.WindowStyle=[Diagnostics.ProcessWindowStyle]::Hidden
$start.RedirectStandardOutput=$true; $start.RedirectStandardError=$true
$start.Environment['CREATOR_DX12_VALIDATION']='basic'
$process=[Diagnostics.Process]::Start($start)
$stdout=$process.StandardOutput.ReadToEndAsync(); $stderr=$process.StandardError.ReadToEndAsync()
if (!$process.WaitForExit($TimeoutSeconds*1000)) { $process.Kill(); $process.WaitForExit(); throw "Demand gate timeout: $run" }
[IO.File]::WriteAllText("$run/stdout.txt",$stdout.GetAwaiter().GetResult(),$utf8)
[IO.File]::WriteAllText("$run/stderr.txt",$stderr.GetAwaiter().GetResult(),$utf8)
$rows=@(Read-CommandResults "$run/results.jsonl")
if ($process.ExitCode -ne 0 -or $rows.Count -ne $commands.Count -or @($rows | Where-Object status -ne succeeded).Count) {
    throw "Demand commands failed: $run"
}
$results=@()
foreach ($observation in $observations) {
    $case=$observation.case; $graph=$rows[$observation.index].data
    if (!$graph.ready) { throw "Graph not current: $($case.name)" }
    $temporal=@($graph.passes | Where-Object name -Like 'Temporal.CameraMotion*').Count
    $gi=@($graph.passes | Where-Object name -Like 'SSGI.*').Count
    $wantsTemporal=$case.intensity -gt 0 -or $case.provider -ne 'none'
    if (($temporal -gt 0) -ne $wantsTemporal -or ($gi -gt 0) -ne ($case.intensity -gt 0)) {
        throw "Unexpected consumer graph: $($case.name), temporal=$temporal GI=$gi"
    }
    if ($case.provider -eq 'dlss' -and $graph.upscaler -ne 'dlss') { throw 'DLSS request fell back' }
    $results += @{name=$case.name;temporalPasses=$temporal;giPasses=$gi;resources=$graph.resources.Count;frame=$graph.frame}
}
$validation=($rows | Where-Object command -eq 'dx12.validation').data
if (!$validation.layerEnabled -or $validation.messages -ne 0 -or $validation.problems -ne 0 -or $validation.droppedMessages -ne 0) {
    throw "Demand validation failed: $run"
}
$runtime=Join-Path ([IO.Path]::GetDirectoryName([IO.Path]::GetFullPath($Editor))) 'CreatorEditor.runtime.dll'
@{passed=$true;cases=$results;binarySha256=(Get-FileHash $runtime).Hash;validation=$validation} |
    ConvertTo-Json -Depth 8 | Set-Content "$run/summary.json"
"PASS temporal input demand: $run"
