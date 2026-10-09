param(
 [ValidateSet('Debug','Release')][string]$Configuration='Debug',
 [string]$Label='mat9',
 [Parameter(Mandatory=$true)][string]$FixtureProject,
 [int]$WindowWidth=640,
 [int]$WindowHeight=480,
 [double]$UiScale=0,
 [int]$Seconds=8,
 [string]$Python='python'
)
$ErrorActionPreference='Stop'
$repo=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
if($Label -notmatch '^[A-Za-z0-9_-]+$' -or $Seconds -lt 3 -or $Seconds -gt 120) { throw 'Invalid measurement label or duration' }
if(!(Test-Path "$FixtureProject/Assets/Models/CreatorRobot.glb") -or !(Test-Path "$FixtureProject/Assets/Scenes/LX_CookFixture.creator")) { throw 'Fixture project requires CreatorRobot.glb and LX_CookFixture.creator' }
$case="$repo/Build/Obj/MaterialProductProbe/Mat9Perf-$Configuration-$Label"
$project="$case/Project"
if(Test-Path $case) { throw "Measurement output already exists: $case" }
New-Item -ItemType Directory -Force $case | Out-Null
Copy-Item $FixtureProject $project -Recurse
if($UiScale -eq 0) {
 & (Join-Path $PSScriptRoot 'sync-material-editor-scale.ps1') -Project $project
}
if($UiScale -ne 0) {
 if($UiScale -lt 0.5 -or $UiScale -gt 2) { throw 'UI scale must be between 0.5 and 2' }
 $settings="$project/ProjectSetting/EngineSettings.asset"
 $yaml=Get-Content $settings -Raw
 $yaml=[regex]::Replace($yaml,'(?m)^imguiScale: .+$',"imguiScale: $($UiScale.ToString([Globalization.CultureInfo]::InvariantCulture))")
 [IO.File]::WriteAllText($settings,$yaml,[Text.UTF8Encoding]::new($false))
}
Copy-Item "$repo/Dynamic_CPP/Assets/Shaders/DefaultPassShader" "$project/Assets/Shaders" -Recurse -Force
$exe="$repo/Bin/x64-$Configuration/Editor/CreatorEditor.exe"
$previousValidation=$env:CREATOR_DX12_VALIDATION
$env:CREATOR_DX12_VALIDATION='off'
try {
$proc=Start-Process $exe -ArgumentList @('--development-project',$project,'--command-service','--smoke-offscreen') -WorkingDirectory (Split-Path $exe) -WindowStyle Hidden -PassThru -RedirectStandardOutput "$case/editor.stdout.log" -RedirectStandardError "$case/editor.stderr.log"
$deadline=[DateTime]::UtcNow.AddSeconds(180)
do { Start-Sleep -Milliseconds 100; if($proc.HasExited){throw "Startup exit $($proc.ExitCode)"}; if(Test-Path "$project/Library/CommandService/endpoint.json"){$ep=Get-Content "$project/Library/CommandService/endpoint.json" -Raw|ConvertFrom-Json}; if([DateTime]::UtcNow -gt $deadline){throw 'Startup timeout'} } until($ep -and $ep.pid -eq $proc.Id)
$base="http://127.0.0.1:$($ep.port)"; $headers=@{Authorization="Bearer $($ep.token)"}
function Cmd([string]$Name,[string[]]$Arguments=@()) {
 $r=Invoke-RestMethod "$base/command" -Method Post -Headers $headers -ContentType 'application/json' -Body (@{command=$Name;args=@($Arguments);mode='async'}|ConvertTo-Json -Compress)
 if($Name -eq 'quit'){return}
 $limit=[DateTime]::UtcNow.AddSeconds(180)
 if($r.operationId){do {Start-Sleep -Milliseconds 80;$s=Invoke-RestMethod "$base$($r.poll)" -Headers $headers;if([DateTime]::UtcNow -gt $limit){throw "$Name timeout"}} until($s.state -eq 'completed');$r=$s}
 $r|ConvertTo-Json -Depth 30 -Compress|Add-Content "$case/responses.jsonl"
 if($r.status -ne 'succeeded'){throw "$Name $($r.code) $($r.message)"};$r.data
}
function Require-NativeReal($Snapshot) {
 foreach($name in @('temporalProvenance','gpuTemporalProvenance')) {
  $p=$Snapshot.$name
  if(!$p -or $p.schemaVersion -ne 2 -or !$p.valid -or $p.frameKind -ne 'real' -or
     [uint64]$p.realFrameId -eq 0 -or [uint64]$p.publicationFrameId -eq 0 -or
     [uint64]$p.viewId -eq 0 -or [uint64]$p.sceneEpoch -eq 0 -or $p.generatedOrdinal -ne 0 -or
     $p.upscaler -ne 'none' -or $p.frameGenerator -ne 'none' -or
     $p.spatialMode -ne 'off' -or $p.deepDvcApplied -or $p.resolutionState -ne 'native' -or
     $p.renderWidth -le 0 -or $p.renderHeight -le 0 -or
     $p.renderWidth -ne $p.displayWidth -or $p.renderHeight -ne $p.displayHeight) {
   throw "Native real-frame performance provenance is missing or ineligible: $name"
  }
 }
 if($Snapshot.framesRenderedAxis -ne 'completed-real-view-renders-not-display-presentations') {
  throw 'Unknown render throughput counter axis'
 }
}
function Set-NativeMeasurement {
 foreach($command in @('temporal.fg','temporal.nis','temporal.deepdvc','temporal.upscale')) {
  $argument=if($command -in @('temporal.nis','temporal.deepdvc')){'off'}else{'none'}
  $receipt=Cmd $command @($argument)
  $generation=[string]$receipt.receiptGeneration
  $limit=[DateTime]::UtcNow.AddSeconds(180)
  do {
   $status=Cmd 'temporal.status' @($generation)
   if($status.requestState -eq 'superseded'){throw 'Native measurement request was superseded'}
   if([DateTime]::UtcNow -gt $limit){throw 'Native measurement request was not acknowledged'}
   if(!$status.acknowledged){Start-Sleep -Milliseconds 80}
  } until($status.acknowledged)
 }
 return [string]$status.requestedGeneration
}
function Wait-Recording([string]$Expected) {
 $limit=[DateTime]::UtcNow.AddSeconds(180)
 do {
  $status=Cmd 'profile.stats'
  if($status.recording.state -eq 'failed'){throw "Recording failed: $($status.recording.error)"}
  if([DateTime]::UtcNow -gt $limit){throw "Recording did not reach $Expected"}
  if($status.recording.state -ne $Expected){Start-Sleep -Milliseconds 80}
 } until($status.recording.state -eq $Expected -and ($Expected -ne 'recording' -or $status.state -eq 'recording'))
 return $status
}
function Save-Recording([string]$Path) {
 Cmd 'profile.pause'|Out-Null
 Wait-Recording 'finalized'|Out-Null
 $save=Cmd 'profile.save' @($Path)
 $limit=[DateTime]::UtcNow.AddSeconds(180)
 while($save.saveState -ne 'saved') {
  if($save.saveState -eq 'failed' -or [DateTime]::UtcNow -gt $limit){throw 'Profile save did not complete'}
  Start-Sleep -Milliseconds 80
  $save=Cmd 'profile.save' @('status')
 }
 if(!$save.complete -or $save.frames -le 0 -or !(Test-Path -LiteralPath $Path)){throw 'Saved recording is empty or incomplete'}
 # The file itself is the provenance authority. Live endpoints above/below are
 # preflight only and cannot annotate old, generated or mixed-setting samples.
 & $Python (Join-Path $PSScriptRoot 'summarize-material-profile.py') --require-native-real $Path
 if($LASTEXITCODE -ne 0){throw 'Recorded per-frame native-real provenance gate failed'}
}
function Measure-Stage([string]$Stage) {
 Cmd 'render.live.fence' @('120')|Out-Null
 Start-Sleep -Seconds 3
 Require-NativeReal (Cmd 'dx12.live')
 Cmd 'profile.record'|Out-Null
 Wait-Recording 'recording'|Out-Null
 $a=Cmd 'dx12.live'
 Require-NativeReal $a
 $watch=[Diagnostics.Stopwatch]::StartNew()
 Start-Sleep -Seconds $Seconds
 $b=Cmd 'dx12.live'
 $watch.Stop()
 Require-NativeReal $b
 $settings=Cmd 'temporal.status' @($script:nativeGeneration)
 if(!$settings.acknowledged -or $settings.requestState -eq 'superseded'){throw 'Measurement settings changed during capture'}
 if(!$b.enabled -or !$b.ready -or $b.framesRendered -le $a.framesRendered) { throw "$Stage renderer stopped" }
 foreach($counter in @('mismatches','queryOverflowPasses','spanViolations','sliceUnderflows')) {
  if($b.gpu.$counter -ne $a.gpu.$counter) { throw "$Stage GPU timing counter increased: $counter" }
 }
 if($Stage -eq 'robot' -and $b.materialGeometry.uploads -ne $a.materialGeometry.uploads) { throw 'Static model geometry was uploaded again during the warm measurement' }
 Save-Recording "$case/$Stage.ceprof"
 $throughput=($b.framesRendered-$a.framesRendered)/$watch.Elapsed.TotalSeconds
 @{start=$a;end=$b;elapsedSeconds=$watch.Elapsed.TotalSeconds;completedRealViewRendersPerSecond=$throughput;
   axis='completed-real-view-renders-not-display-fps';profile="$Stage.ceprof";nativeRealProvenanceVerified=$true;
   nativeQualityAcceptance=$false}|ConvertTo-Json -Depth 30|Set-Content "$case/$Stage.json"
 $a=Cmd 'dx12.live'
 Require-NativeReal $a
 $watch.Restart()
 Start-Sleep -Seconds $Seconds
 $b=Cmd 'dx12.live'
 $watch.Stop()
 Require-NativeReal $b
 @{start=$a;end=$b;elapsedSeconds=$watch.Elapsed.TotalSeconds;completedRealViewRendersPerSecond=($b.framesRendered-$a.framesRendered)/$watch.Elapsed.TotalSeconds;
   axis='completed-real-view-renders-not-display-fps';nativeRealProvenanceVerified=$false;
   scope='unrecorded-overhead-diagnostic-only-not-acceptance'}|ConvertTo-Json -Depth 30|Set-Content "$case/$Stage-paused.json"
 Write-Output "MEASURED $Configuration $Label $Stage"
}
 if($WindowWidth -ne 640 -or $WindowHeight -ne 480) { Cmd 'window.resize' @("$WindowWidth","$WindowHeight")|Out-Null }
 Cmd 'play.foreground_override' @('off')|Out-Null
 Cmd 'scene.switch' @("$project/Assets/Scenes/LX_CookFixture.creator")|Out-Null
 Cmd 'object.delete' @('Ground')|Out-Null
 Cmd 'editor.window' @('###Editor.MaterialGraph','close')|Out-Null
 $script:nativeGeneration=Set-NativeMeasurement
 Cmd 'window.info'|ConvertTo-Json -Depth 10|Set-Content "$case/window.json"
 Cmd 'render.rtinfo'|ConvertTo-Json -Depth 10|Set-Content "$case/viewport.json"
 @{configuration=$Configuration;validation='off';windowWidth=$WindowWidth;windowHeight=$WindowHeight;uiScale=$UiScale;seconds=$Seconds;runtimeSha256=(Get-FileHash (Join-Path (Split-Path $exe) 'CreatorEditor.runtime.dll')).Hash;startedUtc=[DateTime]::UtcNow.ToString('o')}|ConvertTo-Json|Set-Content "$case/measurement.json"
 Measure-Stage 'empty'
 Cmd 'model.loadcached' @("$project/Assets/Models/CreatorRobot.glb")|Out-Null
 Cmd 'model.place' @('CreatorRobot')|Out-Null
 Measure-Stage 'robot'
 Cmd 'render.live.capture' @("$case/RobotCapture",'editor','controlled')|Out-Null
 Cmd 'scene.save' @("$project/Assets/Scenes/Mat9Robot.creator")|Out-Null
 Cmd 'quit'
 if(!$proc.WaitForExit(30000)){throw 'Shutdown timeout'}
 if($proc.ExitCode -ne 0){throw "Exit $($proc.ExitCode)"}
 Write-Output "MAT9_MEASURE_OK $case"
} finally {
 if($proc -and !$proc.HasExited){Stop-Process -Id $proc.Id}
 $env:CREATOR_DX12_VALIDATION=$previousValidation
}
