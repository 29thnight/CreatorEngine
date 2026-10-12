param([ValidateSet('Debug','Release')][string]$Config='Debug',[ValidateSet('dlss')][string]$Provider='dlss',[ValidateSet('native-aa','quality','balanced','performance','ultra-performance')][string]$Quality='quality',[int]$Width=1400,[int]$Height=900,[ValidateSet('transitions','lens')][string]$Scenario='transitions',[switch]$VerifyHistoryContract,[switch]$KeepRawCaptures,[string]$Work=(Join-Path $PSScriptRoot '../../Artifacts/PR169'))
$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot 'ArtifactRetention.ps1')
Assert-ArtifactCapacity $Work
$root=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
if(Get-Process CreatorEditor -ErrorAction SilentlyContinue){throw 'Close Editor before the isolated SDK gate'}
Set-Location $root
$out=Join-Path ([IO.Path]::GetFullPath($Work)) ('closure-'+$Scenario+'-'+$Config+'-'+$Provider+'-'+$Quality+'-'+[guid]::NewGuid().ToString('N'))
if(Test-Path $out){throw 'Fresh output required'}
New-Item -ItemType Directory -Path $out|Out-Null
$project=Join-Path $out 'Project'
New-Item -ItemType Directory -Path (Join-Path $project 'Assets') -Force | Out-Null
Copy-Item -LiteralPath (Join-Path $root 'Dynamic_CPP/ProjectSetting') -Destination $project -Recurse
Copy-Item -LiteralPath (Join-Path $root 'Dynamic_CPP/Assets/Shaders') -Destination (Join-Path $project 'Assets/Shaders') -Recurse
$env:CREATOR_TEMPORAL_RUNTIME_DIRECTORY=Join-Path $root "Bin/x64-$Config/Editor/Temporal"
$env:CREATOR_DX12_VALIDATION='basic'
$exe=Join-Path $root "Bin\x64-$Config\Editor\CreatorEditor.exe"
$previousAssertStack=$env:CREATOR_ASSERT_STACK
try {
 if($Config -eq 'Debug'){$env:CREATOR_ASSERT_STACK='1'}
 $p=Start-Process $exe -ArgumentList @('--development-project',$project,'--command-service','--smoke-offscreen') -WorkingDirectory (Split-Path $exe) -WindowStyle Hidden -RedirectStandardOutput "$out\stdout.log" -RedirectStandardError "$out\stderr.log" -PassThru
} finally {$env:CREATOR_ASSERT_STACK=$previousAssertStack}
$passed=$false;$e=$null
try{
 $endpoint=Join-Path $project 'Library\CommandService\endpoint.json'
 for($i=0;$i -lt 160;$i++){Start-Sleep -Milliseconds 250;if(Test-Path $endpoint){$e=Get-Content $endpoint -Raw|ConvertFrom-Json;if($e.pid -eq $p.Id){break}}}
 if($e.pid -ne $p.Id){throw 'Fresh endpoint missing'}
 $headers=@{Authorization='Bearer '+$e.token};$base='http://127.0.0.1:'+$e.port
 function Command([string]$name,[string[]]$arguments=@()){
  $r=Invoke-RestMethod "$base/command" -DisableKeepAlive -Method Post -Headers $headers -ContentType application/json -Body (@{command=$name;args=@($arguments);mode='async'}|ConvertTo-Json -Compress) -TimeoutSec 360
  if($name -eq 'quit'){
   # The command service disappears during normal shutdown. The initial
   # operation receipt is admission; the caller requires process exit 0.
   @{command=$name;receipt=$r}|ConvertTo-Json -Depth 15 -Compress|Add-Content "$out\commands.jsonl"
   if(!$r.PSObject.Properties['operationId'] -and $r.status -ne 'succeeded'){throw 'Quit was not admitted'}
   return $r
  }
  if($r.PSObject.Properties['operationId']){
   $poll=[string]$r.poll;if(!$poll.StartsWith('/operations/')){throw 'Unexpected operation poll route'}
   $limit=[DateTime]::UtcNow.AddSeconds(360)
   do{Start-Sleep -Milliseconds 100;$r=Invoke-RestMethod "$base$poll" -Headers $headers -TimeoutSec 360;if([DateTime]::UtcNow -ge $limit){throw 'Fixture operation timeout'}}until($r.state -eq 'completed')
  }
  @{command=$name;result=$r}|ConvertTo-Json -Depth 15 -Compress|Add-Content "$out\commands.jsonl"
  if($r.status -ne 'succeeded'){throw "$name failed: $($r.message)"}
  return $r.data
 }
 Command 'scene.new' @('SdkSequence')|Out-Null
 Command 'object.create' @('Camera')|Out-Null
 Command 'component.add' @('Camera','CameraComponent')|Out-Null
 Command 'object.create' @('Light','Light')|Out-Null
 Command 'object.transform' @('Light','0','2','0','45','-25','0')|Out-Null
 $fixturePath=Join-Path $out 'TransitionFixture.gltf'
 Copy-Item -LiteralPath (Join-Path $root 'Tools/temporal-validation/fixtures/SSGIRecovery.gltf') -Destination $fixturePath
 Command 'model.load' @($fixturePath)|Out-Null
 Command 'model.place' @('TransitionFixture')|Out-Null
 Command 'editor.viewport' @('game')|Out-Null
 Command 'window.resize' @([string]$Width,[string]$Height)|Out-Null
 
 Start-Sleep -Seconds 3
 $window=Command 'window.info'
 $window|ConvertTo-Json -Depth 15|Set-Content "$out\window.json"
 if($window.hostClientWidth -ne $Width -or $window.hostClientHeight -ne $Height){throw 'Host resize did not match requested dimensions'}
 $qualityError=$null
 try {
  $driver=if($Scenario -eq 'lens'){'Tools/temporal-validation/Verify-TemporalLens.ps1'}else{'Tools/temporal-validation/Verify-TemporalTransitions.ps1'}
  & $driver -EndpointFile $endpoint -OutputDirectory "$out\verification" -Provider $Provider -Quality $Quality -KeepRawCaptures
 } catch {
  # A rejected image is evidence, not an engine crash. Preserve the rejection
  # and still collect validation plus an ordinary shutdown before failing.
  $qualityError=$_
  $_|Out-String|Set-Content "$out\quality-error.txt"
 }
 if($VerifyHistoryContract){
  Command 'dx12.temporalhistory' | ConvertTo-Json -Depth 15 | Set-Content "$out/history-contract.json"
 }
 $validation=Command 'dx12.validation'
 $validation|ConvertTo-Json -Depth 15|Set-Content "$out\validation.json"
 if(!$validation.layerEnabled -or $validation.messages -ne 0 -or $validation.problems -ne 0 -or $validation.droppedMessages -ne 0){throw 'DX12 validation failed'}
 Command 'quit'|Out-Null
 if(!$p.WaitForExit(30000) -or $p.ExitCode -ne 0){throw 'SDK gate did not shut down normally'}
 if($qualityError){throw $qualityError}
 $passed=$true
}finally{
 if($e -and $e.pid -eq $p.Id -and !(Test-Path "$out\validation.json")){
  try{Command 'dx12.validation'|ConvertTo-Json -Depth 15|Set-Content "$out\validation.json"}catch{$_|Out-String|Set-Content "$out\validation-error.txt"}
 }
 if(Get-CimInstance Win32_Process -Filter "ProcessId=$($p.Id)"){Stop-Process -Id $p.Id -Force}
 for($i=0;$i -lt 100;$i++){if(!(Get-CimInstance Win32_Process -Filter "ProcessId=$($p.Id)")){break};Start-Sleep -Milliseconds 100}
 @{passed=$passed;configuration=$Config;provider=$Provider;quality=$Quality;hostWidth=$Width;hostHeight=$Height;pid=$p.Id;processStopped=!(Get-CimInstance Win32_Process -Filter "ProcessId=$($p.Id)");exitCode=$p.ExitCode;runtimeSha256=(Get-FileHash (Join-Path (Split-Path $exe) 'CreatorEditor.runtime.dll')).Hash}|ConvertTo-Json|Set-Content "$out\launch.json"
}

if(!$passed){throw "Temporal transition gate failed: $out"}

Complete-ArtifactRetention (Join-Path $out 'verification') "$Scenario-$Provider-$Quality" -KeepRawCaptures:$KeepRawCaptures
Complete-ArtifactRetention $out "$Scenario-$Config-$Provider-$Quality" -KeepRawCaptures:$KeepRawCaptures
if(!$KeepRawCaptures){
 # The complete sequence has passed its oracle and the process exited normally.
 # Keep the first reset capture; additional raw comparisons must pin explicitly.
 $representative=if($Scenario -eq 'lens'){'verification/fov-45-1'}else{'verification/quality-1'}
 Remove-ArtifactPixels $out @((Join-Path $out $representative))
}
"PASS temporal $Scenario $Config $Provider $Quality : $out"



