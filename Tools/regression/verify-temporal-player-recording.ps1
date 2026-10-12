param(
 [ValidateSet('Debug','Release')][string]$Config='Release',
 [Parameter(Mandatory)][string]$Package,
 [string]$Work=(Join-Path $PSScriptRoot '../../Artifacts/PR169')
)
$ErrorActionPreference='Stop'
$root=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
if(Get-Process Player -ErrorAction SilentlyContinue){throw 'Close Player before isolated recording'}
if(!(Test-Path (Join-Path $Package 'GameAssets.pak'))){throw 'Cooked package required'}
$out=Join-Path ([IO.Path]::GetFullPath($Work)) ('tr0-player-'+$Config+'-'+[guid]::NewGuid().ToString('N'))
$stage=Join-Path $out 'package'
[IO.Directory]::CreateDirectory($stage)|Out-Null
Copy-Item -Path (Join-Path $Package '*') -Destination $stage -Recurse -Force
$bin=Join-Path $root "Bin/x64-$Config/Player"
Copy-Item (Join-Path $bin 'Player.exe'),(Join-Path $bin 'Player.runtime.dll') $stage -Force
$common=Join-Path $root "Bin/x64-$Config/Runtime/Common"
Copy-Item -Path (Join-Path $common '*') -Destination (Join-Path $stage 'Runtime/Common') -Recurse -Force
$temporal=Join-Path $root "Bin/x64-$Config/Editor/Temporal"
$runtimeHash=(Get-FileHash (Join-Path $stage 'Player.runtime.dll')).Hash
$start=[Diagnostics.ProcessStartInfo]::new()
$start.FileName=Join-Path $stage 'Player.exe';$start.ArgumentList.Add('--command-service')
$start.WorkingDirectory=$stage;$start.UseShellExecute=$false;$start.CreateNoWindow=$true
$start.WindowStyle=[Diagnostics.ProcessWindowStyle]::Hidden
$start.RedirectStandardOutput=$true;$start.RedirectStandardError=$true
$start.Environment['CREATOR_DX12_VALIDATION']='basic'
$start.Environment['CREATOR_TEMPORAL_RUNTIME_DIRECTORY']=$temporal
$start.Environment['CREATOR_TEMPORAL_UPSCALER']='none'
$start.Environment['CREATOR_TEMPORAL_FRAME_GENERATOR']='none'
$start.Environment['CREATOR_TEMPORAL_ENABLED']='0'
$start.Environment['CREATOR_NIS_MODE']='off';$start.Environment['CREATOR_DEEPDVC']='0'
$p=[Diagnostics.Process]::Start($start)
$stdout=$p.StandardOutput.ReadToEndAsync();$stderr=$p.StandardError.ReadToEndAsync()
$endpoint=Join-Path $env:TEMP "CreatorEngine/Player/$($p.Id)/RuntimeData/Library/CommandService/endpoint.json"
$session=[Microsoft.PowerShell.Commands.WebRequestSession]::new()
$passed=$false
try{
 $deadline=[DateTime]::UtcNow.AddSeconds(180)
 do{
  if($p.HasExited){throw "Player exited during startup: $($p.ExitCode)"}
  if(Test-Path $endpoint){$e=Get-Content $endpoint -Raw|ConvertFrom-Json;if($e.pid -eq $p.Id){break}}
  Start-Sleep -Milliseconds 200
 }while([DateTime]::UtcNow -lt $deadline)
 if(!$e -or $e.pid -ne $p.Id){throw 'No matching Player endpoint'}
 $base="http://127.0.0.1:$($e.port)";$headers=@{Authorization='Bearer '+$e.token}
 function Command([string]$command){
  $receipt=Invoke-RestMethod "$base/command" -Method Post -Headers $headers -WebSession $session -ContentType 'application/json' -Body (@{command=$command;mode='sync'}|ConvertTo-Json -Compress) -TimeoutSec 180
  $limit=[DateTime]::UtcNow.AddSeconds(180)
  while($receipt.operationId){
   if($receipt.poll -notlike '/operations/*'){throw 'Unexpected poll route'}
   $poll=$receipt.poll
   do{Start-Sleep -Milliseconds 50;$operation=Invoke-RestMethod "$base$poll" -Headers $headers -WebSession $session -TimeoutSec 180;if([DateTime]::UtcNow -gt $limit){throw 'Command deadline'}}while($operation.state -ne 'completed')
   $receipt=$(if($operation.PSObject.Properties['result']){$operation.result}else{$operation})
  }
  if($receipt.status -ne 'succeeded'){throw "Command failed: $command ($($receipt.code))"}
  return $receipt.data
 }
 $readyDeadline=[DateTime]::UtcNow.AddSeconds(120)
 do{$status=Command 'temporal.status';if($status.rendererObserved -and $status.playerObserved){break};Start-Sleep -Milliseconds 250}while([DateTime]::UtcNow -lt $readyDeadline)
 if(!$status.rendererObserved -or !$status.playerObserved){throw 'Player did not submit/observe real frames'}
 $status|ConvertTo-Json -Depth 25|Set-Content "$out/initial-state.json"
 & (Join-Path $root 'Tools/temporal-validation/Record-TemporalEvidence.ps1') -EndpointFile $endpoint -OutputDirectory "$out/native-record" -Seconds 3
 & python (Join-Path $PSScriptRoot 'summarize-material-profile.py') --require-native-real "$out/native-record/actual.ceprof" *> "$out/oracle.log"
 if($LASTEXITCODE -ne 0){throw 'Player native-real recording oracle failed'}
 $summary=Get-Content "$out/native-record/actual.ceprof.summary.json" -Raw|ConvertFrom-Json
 if(!$summary.presenterReturns.completeWithinObservedBounds -or $summary.presenterReturns.unknownIdentities -ne 0 -or $summary.presenterReturns.unsuccessfulOutcomes -ne 0){throw 'Incomplete or invalid continuous presenter records'}
 if($summary.presenterReturns.generatedOutputTimestampsAvailable -or $summary.presentationPacingAcceptance -or $summary.latencyAcceptance){throw 'CPU presenter mislabeled as displayed/photon evidence'}
 $status=Command 'temporal.status';$status|ConvertTo-Json -Depth 25|Set-Content "$out/final-state.json"
 # Quit may close the service before its operation can be polled.
 Invoke-RestMethod "$base/command" -Method Post -Headers $headers -WebSession $session -ContentType 'application/json' -Body '{"command":"quit","mode":"sync"}' -TimeoutSec 30|Out-Null
 if(!$p.WaitForExit(30000) -or $p.ExitCode -ne 0){throw 'Player did not shut down normally'}
 $passed=$true
}finally{
 $session.Dispose()
 if(!$p.HasExited){$p.Kill();$p.WaitForExit()}
 [IO.File]::WriteAllText("$out/stdout.log",$stdout.GetAwaiter().GetResult())
 [IO.File]::WriteAllText("$out/stderr.log",$stderr.GetAwaiter().GetResult())
 @{passed=$passed;configuration=$Config;runtimeSha256=$runtimeHash;packageSha256=(Get-FileHash (Join-Path $stage 'GameAssets.pak')).Hash;pid=$p.Id;exitCode=$p.ExitCode;scope='actual native Player CPU presenter observations; no generated/physical presentation proof'}|ConvertTo-Json|Set-Content "$out/verification.json"
}
"PASS native Player recording: $out"
