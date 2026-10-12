param(
 [ValidateSet('Debug','Release')][string]$Config='Debug',
 [switch]$RequireActiveDlss,
 [switch]$KeepRawCaptures,
 [string]$Work=(Join-Path $PSScriptRoot '../../Artifacts/PR169')
)
$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot 'CommandResults.ps1')
. (Join-Path $PSScriptRoot 'ArtifactRetention.ps1')
Assert-ArtifactCapacity $Work
$root=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
if(Get-Process CreatorEditor -ErrorAction SilentlyContinue){throw 'Close Editor before isolated validation'}
$run=Join-Path ([IO.Path]::GetFullPath($Work)) ('tr0-golden-'+$Config+'-'+[guid]::NewGuid().ToString('N'))
[IO.Directory]::CreateDirectory($run)|Out-Null
$commands=@('scene.new GoldenExclusion','object.create ProbeCamera','component.add ProbeCamera CameraComponent',
 'object.transform ProbeCamera 0 1 -5 0 0 0','object.create ProbeLight Light',
 'object.transform ProbeLight 0 2 0 45 -25 0',
 ('model.async "'+(Join-Path $root 'Dynamic_CPP/Assets/Models/Prim_Cone.glb')+'"'),'model.async wait',
 'editor.viewport game','wait 120','temporal.upscale',
 ('render.pbr.capture "'+$run+'/first" game controlled'),'temporal.upscale','wait 120',
 ('render.pbr.capture "'+$run+'/second" game controlled'),'temporal.upscale','dx12.validation','quit')
$utf8=[Text.UTF8Encoding]::new($false)
[IO.File]::WriteAllLines("$run/commands.txt",$commands,$utf8)
$exe=Join-Path $root "Bin/x64-$Config/Editor/CreatorEditor.exe"
$runtime=Join-Path (Split-Path $exe) 'CreatorEditor.runtime.dll'
$hash=(Get-FileHash $runtime).Hash
$start=[Diagnostics.ProcessStartInfo]::new()
$start.FileName=$exe
foreach($arg in @('--commandlet-script',"$run/commands.txt",'--result-file',"$run/results.jsonl")){$start.ArgumentList.Add($arg)}
$start.WorkingDirectory=$root; $start.UseShellExecute=$false; $start.CreateNoWindow=$true
$start.WindowStyle=[Diagnostics.ProcessWindowStyle]::Hidden
$start.RedirectStandardOutput=$true; $start.RedirectStandardError=$true
$start.Environment['CREATOR_DX12_VALIDATION']='basic'
$start.Environment['CREATOR_TEMPORAL_ENABLED']='1'
$start.Environment['CREATOR_TEMPORAL_UPSCALER']='dlss'
$start.Environment['CREATOR_TEMPORAL_FRAME_GENERATOR']='fsr'
$start.Environment['CREATOR_TEMPORAL_RUNTIME_DIRECTORY']=(Join-Path (Split-Path $exe) 'Temporal')
$start.Environment['CREATOR_NIS_MODE']='sharpen'
$start.Environment['CREATOR_DEEPDVC']='1'
$p=[Diagnostics.Process]::Start($start)
$stdout=$p.StandardOutput.ReadToEndAsync(); $stderr=$p.StandardError.ReadToEndAsync()
if(!$p.WaitForExit(300000)){$p.Kill();$p.WaitForExit();throw "Golden exclusion timeout: $run"}
[IO.File]::WriteAllText("$run/stdout.log",$stdout.GetAwaiter().GetResult(),$utf8)
[IO.File]::WriteAllText("$run/stderr.log",$stderr.GetAwaiter().GetResult(),$utf8)
$rows=@(Read-CommandResults "$run/results.jsonl")
if($p.ExitCode -ne 0 -or $rows.Count -ne $commands.Count -or @($rows|Where-Object status -ne succeeded).Count){throw "Golden command failed: $run"}
if($hash -ne (Get-FileHash $runtime).Hash){throw 'Runtime changed during golden gate'}
$states=@($rows|Where-Object command -eq 'temporal.upscale')
if($states.Count -ne 3){throw 'Missing before/after temporal snapshots'}
if($RequireActiveDlss){
 $initial=$states[0].data
 if($initial.activeUpscaler -ne 'dlss' -or !$initial.temporalAaApplied -or
    $initial.upscaleResult.status -ne 'success' -or $initial.requestedUpscaleResult.status -ne 'success') {
  throw 'Golden exclusion requires actual DLSS SDK reconstruction before native capture'
 }
}
foreach($state in $states){
 $d=$state.data
 if($d.requestedUpscaler -ne 'dlss' -or $d.requestedFrameGenerator -ne 'fsr' -or
    $d.spatialPost.requestedNisMode -ne 'sharpen' -or !$d.spatialPost.deepDvcRequested -or
    $d.nativeCaptureExclusionActive){throw 'Startup intent lost or native exclusion not released'}
}
$validation=($rows|Where-Object command -eq 'dx12.validation').data
if(!$validation.layerEnabled -or $validation.messages -ne 0 -or $validation.problems -ne 0 -or $validation.droppedMessages -ne 0){throw 'Golden validation failed'}
& python (Join-Path $PSScriptRoot 'TemporalGoldenExclusionOracle.py') $run *> "$run/exclusion-oracle.log"
if($LASTEXITCODE -ne 0){throw "Golden foreground/intent oracle failed: $run"}
& python (Join-Path $PSScriptRoot 'base0_artifacts.py') compare "$run/first" "$run/second" "$run/comparison" *> "$run/compare.log"
if($LASTEXITCODE -ne 0){throw "Golden comparison failed: $run"}
& python (Join-Path $PSScriptRoot 'base0_artifacts.py') mutations "$run/first" "$run/mutations.json" *> "$run/mutations.log"
if($LASTEXITCODE -ne 0){throw "Golden mutations failed: $run"}
@{passed=$true;configuration=$Config;binarySha256=$hash;activeDlssRequired=[bool]$RequireActiveDlss;
 preCaptureActiveUpscaler=$states[0].data.activeUpscaler;requested=@{upscaler='dlss';frameGenerator='fsr';nis='sharpen';deepDvc=$true};
 scope='forced native controlled golden under non-native startup intent; Editor FG prohibited, no generated physical output'}|
 ConvertTo-Json -Depth 6|Set-Content "$run/verification.json"
Complete-ArtifactRetention $run "golden-$Config" -KeepRawCaptures:$KeepRawCaptures
"PASS TR0 golden exclusion $Config : $run"
