[CmdletBinding()]
param([ValidateSet('Debug','Release')][string]$Configuration='Release',[string]$HistoricalEditorExe='',[switch]$MotionFixture)
$ErrorActionPreference='Stop'
$repo=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
if(Get-Process CreatorEditor -ErrorAction SilentlyContinue){throw 'An owned Editor session is required'}
$out=Join-Path $repo ('Build/Verification/N1/Diagnostic-'+[guid]::NewGuid().ToString('N'))
$project=Join-Path $out 'Project'
New-Item -ItemType Directory -Force "$project/Assets"|Out-Null
Copy-Item "$repo/Dynamic_CPP/ProjectSetting" $project -Recurse
Copy-Item "$repo/Dynamic_CPP/Assets/Shaders" "$project/Assets/Shaders" -Recurse
$exe=Join-Path $repo "Bin/x64-$Configuration/Editor/CreatorEditor.exe"
if($HistoricalEditorExe){$exe=[IO.Path]::GetFullPath($HistoricalEditorExe)}
$runtime=Join-Path (Split-Path $exe) 'CreatorEditor.runtime.dll'
foreach($source in @('Engine/Utility_Framework/DiagnosticDocument.cpp','Engine/Utility_Framework/DiagnosticDocument.h','Engine/SceneRuntime/TemporalMotionFixture.cpp','Engine/RenderEngine/Render/Scene/EnhancedPbrCapture.h','Engine/RenderEngine/Render/Scene/EnhancedSceneRenderer.cpp')){
 if(!$HistoricalEditorExe -and (Get-Item "$repo/$source").LastWriteTimeUtc -gt (Get-Item $runtime).LastWriteTimeUtc){throw "Fresh diagnostic producer build required: $source"}
}
$hash=(Get-FileHash $runtime).Hash
$prior=$env:CREATOR_DX12_VALIDATION;$env:CREATOR_DX12_VALIDATION='basic'
try{$p=Start-Process $exe -ArgumentList @('--development-project',('"'+$project+'"'),'--command-service','--smoke-offscreen') -WorkingDirectory (Split-Path $exe) -WindowStyle Hidden -RedirectStandardOutput "$out/stdout.log" -RedirectStandardError "$out/stderr.log" -PassThru}finally{$env:CREATOR_DX12_VALIDATION=$prior}
try {
 $endpoint="$project/Library/CommandService/endpoint.json";$e=$null
 for($i=0;$i -lt 160;$i++){Start-Sleep -Milliseconds 250;if(Test-Path $endpoint){$e=Get-Content $endpoint -Raw|ConvertFrom-Json;if($e.pid -eq $p.Id){break}}}
 if(!$e -or $e.pid -ne $p.Id){throw 'Fresh owned endpoint missing'}
 $headers=@{Authorization='Bearer '+$e.token};$base='http://127.0.0.1:'+$e.port
 function Command([string]$name,[string[]]$arguments=@()) {
  $r=Invoke-RestMethod "$base/command" -DisableKeepAlive -Method Post -Headers $headers -ContentType application/json -Body (@{command=$name;args=@($arguments);mode='async'}|ConvertTo-Json -Compress) -TimeoutSec 360
  if($name -ne 'quit' -and $r.PSObject.Properties['operationId']){
   $poll=[string]$r.poll;if(!$poll.StartsWith('/operations/')){throw 'Unexpected operation poll route'}
   $limit=[DateTime]::UtcNow.AddSeconds(360)
   do{Start-Sleep -Milliseconds 100;$r=Invoke-RestMethod "$base$poll" -Headers $headers -TimeoutSec 360;if([DateTime]::UtcNow -ge $limit){throw 'N1 diagnostic operation timeout'}}until($r.state -eq 'completed')
  }
  @{command=$name;result=$r}|ConvertTo-Json -Depth 20 -Compress|Add-Content "$out/commands.jsonl"
  if($name -ne 'quit' -and $r.status -ne 'succeeded'){throw "$name failed: $($r.message)"}
  return $r.data
 }
 Command 'scene.new' @('N1Diagnostic')|Out-Null
 Command 'object.create' @('Camera')|Out-Null
 Command 'component.add' @('Camera','CameraComponent')|Out-Null
 Command 'editor.viewport' @('game')|Out-Null
 Command 'window.resize' @('640','360')|Out-Null
 Command 'temporal.upscale' @('none')|Out-Null
 Start-Sleep -Seconds 3
 Command 'window.info'|ConvertTo-Json -Depth 10|Set-Content "$out/window.json"
 $capture=Join-Path $out $(if($MotionFixture){'motion-fixture'}else{'pbr-capture'})
 if($MotionFixture){
  Command 'render.temporal.motion.fixture' @($capture.Replace('\','/'))|ConvertTo-Json -Depth 10|Set-Content "$out/fixture-result.json"
  & python "$repo/Tools/temporal-validation/Compare-TemporalMotion.py" --capture $capture --fixture "$capture/fixture.json" --output "$out/motion-oracle.json" *> "$out/oracle.log"
  if($LASTEXITCODE){throw "Motion fixture consumer failed: $out/oracle.log"}
 }else{
  Command 'render.live.capture' @($capture.Replace('\','/'),'game','controlled')|ConvertTo-Json -Depth 10|Set-Content "$out/capture-result.json"
  $manifest=Get-Content "$capture/manifest.json" -Raw|ConvertFrom-Json
  if($manifest.schemaVersion -ne 1 -or $manifest.source -ne 'product-live' -or $manifest.backend -ne 'dx12' -or @($manifest.attachments).Count -lt 6){throw 'PBR diagnostic schema/capture missing'}
  foreach($attachment in $manifest.attachments){
   if($attachment.deepDvcApplied -isnot [bool] -or $attachment.nonfinite -ne 0 -or !$attachment.width -or !$attachment.height){throw 'PBR diagnostic type/finite contract failed'}
   $length=(Get-Item (Join-Path $capture $attachment.file)).Length
   if($length -ne 4L*$attachment.width*$attachment.height*$attachment.channels){throw 'PBR diagnostic attachment byte count mismatch'}
  }
  @{result='N1_PBR_MANIFEST_CONSUMER_OK';attachments=@($manifest.attachments).Count;frameId=$manifest.frameId;manifestHash=(Get-FileHash "$capture/manifest.json").Hash}|ConvertTo-Json|Set-Content "$out/consumer.json"
 }
 $validation=Command 'dx12.validation'
 $validation|ConvertTo-Json -Depth 12|Set-Content "$out/validation.json"
 if(!$validation.layerEnabled -or $validation.problems -ne 0 -or $validation.messages -ne 0 -or $validation.droppedMessages -ne 0){throw 'N1 diagnostic capture failed GPU validation'}
 Command 'quit'|Out-Null
 if(!$p.WaitForExit(30000) -or $p.ExitCode -ne 0){throw 'Diagnostic producer did not shut down normally'}
 if((Get-FileHash $runtime).Hash -ne $hash){throw 'Diagnostic runtime changed during capture'}
 @{result=$(if($HistoricalEditorExe){'N1_DIAGNOSTIC_HISTORICAL_OK'}else{'N1_DIAGNOSTIC_PRODUCT_OK'});configuration=$Configuration;runtimeHash=$hash;oracle=$(if($MotionFixture){"$out/motion-oracle.json"}else{"$out/consumer.json"});motionFixture=[bool]$MotionFixture;scope='Actual selected diagnostic producer and manifest/attachment consumer. Full motion/SDK quality/performance are separate'}|ConvertTo-Json -Depth 8|Set-Content "$out/result.json" -Encoding utf8
 if($HistoricalEditorExe){"N1_DIAGNOSTIC_HISTORICAL_OK evidence=$out"}else{"N1_DIAGNOSTIC_PRODUCT_OK evidence=$out"}
}finally{if(!$p.HasExited){$p.Kill();$p.WaitForExit()}}
