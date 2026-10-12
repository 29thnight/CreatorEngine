param([ValidateSet('Debug','Release')][string]$Configuration='Debug',
 [string]$Work=(Join-Path $PSScriptRoot '../../Artifacts/PR169'))
$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot 'CommandResults.ps1')
. (Join-Path $PSScriptRoot 'ArtifactRetention.ps1')
Assert-ArtifactCapacity $Work
$repo=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
if(Get-Process CreatorEditor -ErrorAction SilentlyContinue){throw 'Existing Editor must be closed for isolated consumer validation'}
$run=Join-Path ([IO.Path]::GetFullPath($Work)) ('tr0-live-consumer-'+$Configuration+'-'+[guid]::NewGuid().ToString('N'))
[IO.Directory]::CreateDirectory($run)|Out-Null
$commands=@('scene.new NativeConsumer','object.create ProbeCamera','component.add ProbeCamera CameraComponent',
 'object.transform ProbeCamera 0 1 -5 0 0 0','editor.viewport scene',
 'temporal.upscale none','temporal.fg none','temporal.nis off','temporal.deepdvc off',
 'wait 180','dx12.live','wait 60','dx12.live','quit')
[IO.File]::WriteAllLines("$run/commands.txt",$commands,[Text.UTF8Encoding]::new($false))
$start=[Diagnostics.ProcessStartInfo]::new("$repo/Bin/x64-$Configuration/Editor/CreatorEditor.exe")
$start.UseShellExecute=$false;$start.CreateNoWindow=$true
$start.WindowStyle=[Diagnostics.ProcessWindowStyle]::Hidden;$start.WorkingDirectory=$repo
$start.RedirectStandardOutput=$true;$start.RedirectStandardError=$true
foreach($arg in @('--commandlet-script',"$run/commands.txt",'--result-file',"$run/results.jsonl")){$start.ArgumentList.Add($arg)}
foreach($key in @('CREATOR_TEMPORAL_UPSCALER','CREATOR_TEMPORAL_FRAME_GENERATOR')){$start.Environment[$key]='none'}
$start.Environment['CREATOR_NIS_MODE']='off';$start.Environment['CREATOR_DEEPDVC']='0'
$start.Environment['CREATOR_DX12_VALIDATION']='off'
$start.Environment['CREATOR_EDITOR_WORKSPACE_DIR']="$run/workspace"
$start.Environment['CREATOR_EDITOR_LEGACY_INI']="$run/legacy.ini"
$proc=[Diagnostics.Process]::Start($start)
$stdout=$proc.StandardOutput.ReadToEndAsync();$stderr=$proc.StandardError.ReadToEndAsync()
try {
 if(!$proc.WaitForExit(180000)){throw 'Native consumer snapshot timed out'}
 if($proc.ExitCode -ne 0){throw "Editor exited $($proc.ExitCode)"}
 $rows=Read-CommandResults "$run/results.jsonl"
 if(@($rows).Count -ne $commands.Count -or @($rows|Where-Object status -ne 'succeeded').Count){throw 'Commandlet command failed or result missing'}
 & (Join-Path $PSScriptRoot 'NativeRealMeasurementGateTests.ps1') -Results "$run/results.jsonl" -Output "$run/gate-tests.json"
 @{passed=$true;configuration=$Configuration;runtimeSha256=(Get-FileHash "$repo/Bin/x64-$Configuration/Editor/CreatorEditor.runtime.dll").Hash;
   normalExit=$true;productBudgetAccepted=$false;physicalAcceptance=$false}|ConvertTo-Json|Set-Content "$run/verification.json"
 Write-Output "NATIVE_CONSUMER_OK $run"
}finally{
 if(!$proc.HasExited){$proc.Kill();$proc.WaitForExit()}
 [IO.File]::WriteAllText("$run/stdout.log",$stdout.GetAwaiter().GetResult())
 [IO.File]::WriteAllText("$run/stderr.log",$stderr.GetAwaiter().GetResult())
}
