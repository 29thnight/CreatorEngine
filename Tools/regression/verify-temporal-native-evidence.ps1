param(
    [ValidateSet('Debug','Release')][string]$Config='Debug',
    [ValidateSet('profile','motion')][string]$Stage='motion',
    [switch]$DisableScheduleCache,
    [switch]$KeepRawCaptures,
    [string]$Work=(Join-Path $PSScriptRoot '../../Artifacts/PR169')
)
$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot 'CommandResults.ps1')
. (Join-Path $PSScriptRoot 'ArtifactRetention.ps1')
Assert-ArtifactCapacity $Work
$root=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
if(Get-Process CreatorEditor -ErrorAction SilentlyContinue){throw 'Close Editor before isolated validation'}
$run=Join-Path ([IO.Path]::GetFullPath($Work)) ('closure-native-'+$Config+'-'+$Stage+'-'+[guid]::NewGuid().ToString('N'))
[IO.Directory]::CreateDirectory($run)|Out-Null
$models=Join-Path $root 'Dynamic_CPP/Assets/Models'
$commands=@('scene.new TemporalClosure','object.create ProbeCamera','component.add ProbeCamera CameraComponent',
 'object.transform ProbeCamera 0 1 -5 0 0 0','object.create ProbeLight Light',
 'object.transform ProbeLight 0 2 0 45 -25 0',
 ('model.async "'+(Join-Path $models 'Aria_Dagger_ChainSwordWeapon.glb')+'"'),'model.async wait',
 ('model.async "'+(Join-Path $models 'Prim_Cone.glb')+'"'),'model.async wait',
 'object.transform Prim_Cone -1.478 0 0.516 0 0 0','camera.editor match','temporal.upscale none','wait 120')
if($Stage -eq 'profile'){
 $commands+=@('profile.record','wait 120','profile.pause','wait 60',('profile.save "'+$run+'/steady.ceprof"'),
 'wait 60','profile.save status','quit')
}else{
 $commands+=@('dx12.temporalhistory','editor.viewport game','wait 60',
 ('render.temporal.motion.fixture "'+$run+'/motion-fixture"'),'dx12.validation','quit')
}
$utf8=[Text.UTF8Encoding]::new($false)
[IO.File]::WriteAllLines("$run/commands.txt",$commands,$utf8)
$start=[Diagnostics.ProcessStartInfo]::new()
$exe=Join-Path $root "Bin/x64-$Config/Editor/CreatorEditor.exe"
$runtime=Join-Path (Split-Path $exe) 'CreatorEditor.runtime.dll'
$hash=(Get-FileHash $runtime).Hash
$start.FileName=$exe
foreach($arg in @('--commandlet-script',"$run/commands.txt",'--result-file',"$run/results.jsonl")){$start.ArgumentList.Add($arg)}
$start.WorkingDirectory=$root; $start.UseShellExecute=$false; $start.CreateNoWindow=$true
$start.WindowStyle=[Diagnostics.ProcessWindowStyle]::Hidden
$start.RedirectStandardOutput=$true; $start.RedirectStandardError=$true
$start.Environment['CREATOR_DX12_VALIDATION']=$(if($Stage -eq 'motion'){'basic'}else{'off'})
$start.Environment['CREATOR_RENDERGRAPH_SCHEDULE_CACHE']=$(if($DisableScheduleCache){'0'}else{'1'})
$p=[Diagnostics.Process]::Start($start)
$stdout=$p.StandardOutput.ReadToEndAsync(); $stderr=$p.StandardError.ReadToEndAsync()
if(!$p.WaitForExit(300000)){$p.Kill();$p.WaitForExit();throw "Validation timeout: $run"}
[IO.File]::WriteAllText("$run/stdout.log",$stdout.GetAwaiter().GetResult(),$utf8)
[IO.File]::WriteAllText("$run/stderr.log",$stderr.GetAwaiter().GetResult(),$utf8)
$rows=@(Read-CommandResults "$run/results.jsonl")
if($p.ExitCode -ne 0 -or $rows.Count -ne $commands.Count -or @($rows|Where-Object status -ne succeeded).Count){throw "Command validation failed: $run"}
if($hash -ne (Get-FileHash $runtime).Hash){throw 'Runtime changed during validation'}
if($Stage -eq 'profile'){
 $save=($rows|Where-Object command -eq 'profile.save'|Select-Object -Last 1).data
 if($save.saveState -ne 'saved' -or !$save.complete){throw "Incomplete profile: $run"}
 & python (Join-Path $PSScriptRoot 'summarize-material-profile.py') --require-native-real "$run/steady.ceprof" *> "$run/oracle.log"
 if($LASTEXITCODE -ne 0){throw "Profile oracle failed: $run"}
 $summary=Get-Content "$run/steady.ceprof.summary.json" -Raw|ConvertFrom-Json
 if(($summary.structuralScheduleReuseEvents -gt 0) -eq [bool]$DisableScheduleCache){throw 'Requested schedule cache mode was not exercised'}
}else{
 $validation=($rows|Where-Object command -eq 'dx12.validation').data
 if(!$validation.layerEnabled -or $validation.problems -ne 0 -or $validation.messages -ne 0 -or $validation.droppedMessages -ne 0){throw "GPU validation failed: $run"}
 & python (Join-Path $root 'Tools/temporal-validation/Compare-TemporalMotion.py') --capture "$run/motion-fixture" --fixture "$run/motion-fixture/fixture.json" --output "$run/motion-oracle.json" *> "$run/oracle.log"
}
if($LASTEXITCODE -ne 0){throw "Oracle validation failed: $run"}
@{passed=$true;stage=$Stage;configuration=$Config;scheduleCacheEnabled=(!$DisableScheduleCache);binarySha256=$hash} |
 ConvertTo-Json | Set-Content "$run/verification.json"
Complete-ArtifactRetention $run "native-$Config-$Stage" -KeepRawCaptures:$KeepRawCaptures
"PASS native evidence $Config $Stage : $run"
