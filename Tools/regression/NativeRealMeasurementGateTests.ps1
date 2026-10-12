param([Parameter(Mandatory)][string]$Results,[Parameter(Mandatory)][string]$Output)
$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot 'CommandResults.ps1')
. (Join-Path $PSScriptRoot 'NativeRealMeasurementGate.ps1')
$before=(Get-FileHash -LiteralPath $Results).Hash
$snapshots=@(Read-CommandResults $Results | Where-Object { $_.command -eq 'dx12.live' -and $_.status -eq 'succeeded' })
if(!$snapshots.Count){throw 'No actual successful dx12.live snapshot'}
$rejections=@()
foreach($row in $snapshots){Assert-NativeRealMeasurement $row.data}
$source=$snapshots[-1].data
foreach($name in @('temporalProvenance','gpuTemporalProvenance')) {
 foreach($field in @('schemaVersion','valid','frameKind','realFrameId','publicationFrameId','viewId','sceneEpoch',
     'generatedOrdinal','upscaler','frameGenerator','spatialMode','deepDvcApplied','resolutionState',
     'renderWidth','renderHeight','displayWidth','displayHeight')) {
  $damaged=$source|ConvertTo-Json -Depth 40|ConvertFrom-Json
  $damaged.$name.PSObject.Properties.Remove($field)
  $failed=$false
  try{Assert-NativeRealMeasurement $damaged}catch{$failed=$true}
  if(!$failed){throw "Missing metadata accepted: $name.$field"}
  $rejections+="$name.missing-$field"
 }
 foreach($mutation in @(
   @{field='frameKind';value='generated'},@{field='generatedOrdinal';value=1},
   @{field='renderWidth';value=0},@{field='displayWidth';value=($source.$name.displayWidth+1)},
   @{field='viewId';value=0},@{field='sceneEpoch';value=-1},@{field='valid';value='true'},
   @{field='deepDvcApplied';value='false'},@{field='deepDvcApplied';value=$true},
   @{field='upscaler';value='dlss'},@{field='frameGenerator';value='fsr'},
   @{field='spatialMode';value='sharpen'},@{field='resolutionState';value='native-fallback'})) {
  $damaged=$source|ConvertTo-Json -Depth 40|ConvertFrom-Json
  $damaged.$name.($mutation.field)=$mutation.value
  $failed=$false
  try{Assert-NativeRealMeasurement $damaged}catch{$failed=$true}
  if(!$failed){throw "Invalid metadata accepted: $name.$($mutation.field)"}
  $rejections+="$name.invalid-$($mutation.field)"
 }
}
foreach($axis in @($null,'display-fps')) {
 $damaged=$source|ConvertTo-Json -Depth 40|ConvertFrom-Json
 $damaged.framesRenderedAxis=$axis
 $failed=$false
 try{Assert-NativeRealMeasurement $damaged}catch{$failed=$true}
 if(!$failed){throw 'Invalid throughput axis accepted'}
 $rejections+='invalid-throughput-axis'
}
if($before -ne (Get-FileHash -LiteralPath $Results).Hash){throw 'Original evidence changed'}
@{passed=$true;sourceSha256=$before;actualSnapshots=$snapshots.Count;rejected=$rejections;
 physicalAcceptance=$false;productBudgetAccepted=$false;scope='actual live preflight and metadata mutations, not recorded or physical performance acceptance'}|
 ConvertTo-Json -Depth 30|Set-Content -LiteralPath $Output -Encoding utf8
Write-Output "PASS native-real consumer: $($snapshots.Count) actual snapshots, $($rejections.Count) rejections"
