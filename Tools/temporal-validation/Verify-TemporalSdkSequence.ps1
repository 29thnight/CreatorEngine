param([Parameter(Mandatory)][string]$EndpointFile,[Parameter(Mandatory)][string]$OutputDirectory,[ValidateSet('fsr','dlss','xess')][string]$Provider='fsr',[ValidateSet('native-aa','quality','balanced','performance','ultra-performance')][string]$Quality='quality',[switch]$Startup)
$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot '../regression/ArtifactRetention.ps1')
Assert-ArtifactCapacity $OutputDirectory
$OutputDirectory=[IO.Path]::GetFullPath($OutputDirectory)
if(Test-Path $OutputDirectory){throw 'A new output directory is required'}
New-Item -ItemType Directory -Path $OutputDirectory|Out-Null
$e=Get-Content $EndpointFile -Raw|ConvertFrom-Json
$headers=@{Authorization='Bearer '+$e.token};$base='http://127.0.0.1:'+$e.port
 function Command([string]$name,[string[]]$arguments=@()){
  $r=Invoke-RestMethod "$base/command" -DisableKeepAlive -Method Post -Headers $headers -ContentType application/json -Body (@{command=$name;args=@($arguments);mode=$(if($name -eq 'render.temporal.motion.fixture'){'async'}else{'sync'})}|ConvertTo-Json -Compress) -TimeoutSec 360
  if($r.PSObject.Properties['operationId']){
   $poll=[string]$r.poll;if(!$poll.StartsWith('/operations/')){throw 'Unexpected operation poll route'}
   $limit=[DateTime]::UtcNow.AddSeconds(360)
   do{Start-Sleep -Milliseconds 100;$r=Invoke-RestMethod "$base$poll" -Headers $headers -TimeoutSec 360;if([DateTime]::UtcNow -ge $limit){throw 'Fixture operation timeout'}}until($r.state -eq 'completed')
  }
  @{command=$name;result=$r}|ConvertTo-Json -Depth 15 -Compress|Add-Content "$OutputDirectory\commands.jsonl"
  if($r.status -ne 'succeeded'){throw "$name failed: $($r.message)"}
  return $r.data
 }

& (Join-Path $PSScriptRoot 'Invoke-TemporalStage.ps1') -EndpointFile $EndpointFile -Stage disable -OutputDirectory "$OutputDirectory\disable"|Out-Null
& (Join-Path $PSScriptRoot 'Invoke-TemporalStage.ps1') -EndpointFile $EndpointFile -Stage upscale -Provider $Provider -Quality $Quality -OutputDirectory "$OutputDirectory\upscale"|Out-Null
Command 'render.temporal.motion.fixture' @("$OutputDirectory\fixture",$(if($Startup){'sdk-startup'}else{'sdk-sequence'}))|Out-Null
$restored=Command 'temporal.status'
$restored|ConvertTo-Json -Depth 15|Set-Content "$OutputDirectory\restored.json"
if($restored.nativeCaptureExclusionActive){throw 'Fixture retained native exclusion'}
python (Join-Path $PSScriptRoot 'Compare-TemporalSdkSequence.py') --capture "$OutputDirectory\fixture" --output "$OutputDirectory\oracle.json" --quality $Quality --require-exact-warmup $(if($Startup){'--require-startup'}else{'--require-baseline'})
if($LASTEXITCODE -ne 0){throw 'SDK sequence oracle rejected'}
