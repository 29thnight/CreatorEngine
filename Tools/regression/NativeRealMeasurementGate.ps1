# Live preflight only. The saved .ceprof remains the recorded-sample authority.
function Assert-NativeRealMeasurement($Snapshot) {
 foreach($name in @('temporalProvenance','gpuTemporalProvenance')) {
  $p=$Snapshot.$name
  if(!$p){throw "Missing native-real provenance: $name"}
  foreach($field in @('schemaVersion','valid','frameKind','realFrameId','publicationFrameId',
      'viewId','sceneEpoch','generatedOrdinal','upscaler','frameGenerator','spatialMode',
      'deepDvcApplied','resolutionState','renderWidth','renderHeight','displayWidth','displayHeight')) {
   if(!$p.PSObject.Properties[$field] -or $null -eq $p.$field){throw "Missing native-real field: $name.$field"}
  }
  if($p.schemaVersion -ne 2 -or $p.valid -isnot [bool] -or !$p.valid -or
     $p.frameKind -ne 'real' -or $p.generatedOrdinal -ne 0 -or
     $p.upscaler -ne 'none' -or $p.frameGenerator -ne 'none' -or
     $p.spatialMode -ne 'off' -or $p.deepDvcApplied -isnot [bool] -or $p.deepDvcApplied -or
     $p.resolutionState -ne 'native') {throw "Ineligible native-real state: $name"}
  foreach($field in @('realFrameId','publicationFrameId','viewId','sceneEpoch',
      'renderWidth','renderHeight','displayWidth','displayHeight')) {
   [uint64]$value=0
   if(![uint64]::TryParse([string]$p.$field,[ref]$value) -or $value -eq 0){throw "Invalid native-real identity/extent: $name.$field"}
  }
  if($p.renderWidth -ne $p.displayWidth -or $p.renderHeight -ne $p.displayHeight) {
   throw "Non-native render/display extents: $name"
  }
 }
 if($Snapshot.framesRenderedAxis -ne 'completed-real-view-renders-not-display-presentations') {
  throw 'Unknown render throughput counter axis'
 }
}
