param(
 [Parameter(Mandatory)][string]$EndpointFile,
 [Parameter(Mandatory)][string]$OutputDirectory,
 [Parameter(Mandatory)][string]$FixturePath,
 [ValidateSet('fsr','dlss','xess')][string]$Provider='fsr',
 [ValidateSet('native-aa','quality','balanced','performance','ultra-performance')][string]$Quality='quality',
 [string]$CameraTarget='Camera',
 [switch]$KeepRawCaptures
)
$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot '../regression/ArtifactRetention.ps1')
Assert-ArtifactCapacity $OutputDirectory
$out=[IO.Path]::GetFullPath($OutputDirectory)
if(Test-Path -LiteralPath $out){throw 'New output directory required'}
New-Item -ItemType Directory -Path $out|Out-Null
$e=Get-Content -LiteralPath $EndpointFile -Raw|ConvertFrom-Json
$base='http://127.0.0.1:'+$e.port;$headers=@{Authorization='Bearer '+$e.token}
function Command([string]$name,[string[]]$arguments=@()){
 $r=Invoke-RestMethod "$base/command" -DisableKeepAlive -Method Post -Headers $headers -ContentType application/json -Body (@{command=$name;args=@($arguments);mode='async'}|ConvertTo-Json -Compress) -TimeoutSec 300
 if($r.PSObject.Properties['operationId']){
  $poll=[string]$r.poll;if(!$poll.StartsWith('/operations/')){throw 'Invalid operation poll'}
  $limit=[DateTime]::UtcNow.AddSeconds(300)
  do{Start-Sleep -Milliseconds 100;$r=Invoke-RestMethod "$base$poll" -DisableKeepAlive -Headers $headers -TimeoutSec 300;if([DateTime]::UtcNow -ge $limit){throw 'Operation timeout'}}until($r.state -eq 'completed')
 }
 @{command=$name;arguments=@($arguments);result=$r}|ConvertTo-Json -Depth 20 -Compress|Add-Content "$out\commands.jsonl" -Encoding utf8
 if($r.status -ne 'succeeded'){throw "$name failed: $($r.message)"};return $r.data
}
@{schema='temporal.sdk.reset-quality.v1';provider=$Provider;quality=$Quality;fixturePath=[IO.Path]::GetFullPath($FixturePath);fixtureSha256=(Get-FileHash -LiteralPath $FixturePath).Hash.ToLowerInvariant()}|ConvertTo-Json|Set-Content "$out\inputs.json" -Encoding utf8
& (Join-Path $PSScriptRoot 'Invoke-TemporalStage.ps1') -EndpointFile $EndpointFile -Stage disable -OutputDirectory "$out\disable"|Out-Null
& (Join-Path $PSScriptRoot 'Invoke-TemporalStage.ps1') -EndpointFile $EndpointFile -Stage upscale -Provider $Provider -Quality $Quality -OutputDirectory "$out\upscale"|Out-Null
$records=[Collections.Generic.List[object]]::new()
$passed=$false
$cancelled=$false
try{
 foreach($name in @('reset-first','reset-repeat')){
  $before=Command 'temporal.status'
  Command 'render.temporal.history.capture' @("$out\$name",'1','fixed-sdk-clock')|Out-Null
  $receipt=Command 'camera.cut' @($CameraTarget)
  $limit=[DateTime]::UtcNow.AddSeconds(180)
  do{
   $s=Command 'render.temporal.history.capture' @('status')
   if($s.directory -ne "$out\$name" -or $s.failed){throw "Reset capture failed/changed: $($s.error)"}
   if($s.complete){break};if([DateTime]::UtcNow -ge $limit){throw 'Reset capture timeout'}
   Start-Sleep -Milliseconds 100
  }while($true)
  $after=Command 'temporal.status'
  $records.Add(@{name=$name;ordinal=1;action='camera.cut';before=$before;after=$after;receipt=$receipt;manifestSha256=(Get-FileHash "$out\$name\manifest.json").Hash.ToLowerInvariant()})
  $limit=[DateTime]::UtcNow.AddSeconds(180)
  do{
   $d=Command 'temporal.status'
   if([uint64]$d.lastRealFrameId -gt [uint64]$after.lastRealFrameId+16 -and !$d.reset){break}
   if([DateTime]::UtcNow -ge $limit){throw 'History did not resume'};Start-Sleep -Milliseconds 100
  }while($true)
 }
 $records.ToArray()|ConvertTo-Json -Depth 25|Set-Content "$out\records.json" -Encoding utf8
 $v=Command 'dx12.validation';$v|ConvertTo-Json -Depth 20|Set-Content "$out\validation.json"
 if(!$v.layerEnabled -or $v.mode -ne 'basic' -or $v.messages -ne 0 -or $v.problems -ne 0 -or $v.droppedMessages -ne 0){throw 'DX12 validation failed'}
 Command 'render.temporal.history.capture' @('cancel')|Out-Null
 $cancelled=$true
 python (Join-Path $PSScriptRoot 'Compare-TemporalResetQuality.py') --directory $out --output "$out\oracle.json"
 if($LASTEXITCODE -ne 0){throw 'Reset quality oracle rejected'}
 $passed=$true
}finally{
 $cleanupError=$null
 if(!$cancelled){try{Command 'render.temporal.history.capture' @('cancel')|Out-Null;$cancelled=$true}catch{$cleanupError=$_.Exception.Message;$passed=$false}}
 @{completed=$records.Count -eq 2;passed=$passed;provider=$Provider;quality=$Quality;captureCount=$records.Count;captureReleased=$cancelled;cleanupError=$cleanupError;scope='camera-cut first reset SDK HDR and repeatability'}|ConvertTo-Json|Set-Content "$out\results.json"
}

if($passed){Complete-ArtifactRetention $out "reset-$Provider" -KeepRawCaptures:$KeepRawCaptures}
