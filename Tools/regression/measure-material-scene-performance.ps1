param(
 [ValidateSet('Debug','Release')][string]$Configuration='Debug',
 [string]$Label='mat9',
 [Parameter(Mandatory=$true)][string]$FixtureProject,
 [ValidatePattern('^[A-Za-z0-9_-]+$')][string]$ModelName='CreatorRobot',
 [switch]$CreateEmptyScene,
 [switch]$KeepRawCaptures,
 [switch]$UnlimitedPacing,
 [switch]$PlayMode,
 [int]$WindowWidth=640,
 [int]$WindowHeight=480,
 [double]$UiScale=0,
 [int]$Seconds=8,
 [ValidateRange(30,600)][int]$ReadyTimeoutSeconds=600,
 [string]$Python='python'
)
$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot 'NativeRealMeasurementGate.ps1')
. (Join-Path $PSScriptRoot 'ArtifactRetention.ps1')
. (Join-Path $PSScriptRoot 'PerformanceEnvironment.ps1')
Assert-ArtifactCapacity $PSScriptRoot
$repo=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
if(Get-Process CreatorEditor -ErrorAction SilentlyContinue){throw 'Existing Editor must be closed before isolated measurement'}
if($Label -notmatch '^[A-Za-z0-9_-]+$' -or $Seconds -lt 3 -or $Seconds -gt 120) { throw 'Invalid measurement label or duration' }
if(!(Test-Path "$FixtureProject/Assets/Models/$ModelName.glb") -or
   (!$CreateEmptyScene -and !(Test-Path "$FixtureProject/Assets/Scenes/LX_CookFixture.creator"))) {
 throw 'Fixture project requires the selected model and, unless CreateEmptyScene is set, LX_CookFixture.creator'
}
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
New-Item -ItemType Directory -Force "$project/Assets/Shaders"|Out-Null
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
 if($r.operationId){do {Start-Sleep -Milliseconds 80;$s=Invoke-RestMethod "$base$($r.poll)" -Headers $headers;if([DateTime]::UtcNow -gt $limit){throw "$Name timeout"}} until($s.state -eq 'completed');$r=if($s.result){$s.result}else{$s}}
 @{command=$Name;arguments=@($Arguments);result=$r}|ConvertTo-Json -Depth 30 -Compress|Add-Content "$case/responses.jsonl"
 if($r.status -ne 'succeeded'){throw "$Name $($r.code) $($r.message)"};$r.data
}
function Require-NativeReal($Snapshot) {
 Assert-NativeRealMeasurement $Snapshot
}
function Read-LiveMeasurement {
 $limit=[DateTime]::UtcNow.AddSeconds(30)
 do {
  $live=Cmd 'dx12.live'
  # GetLiveStatus takes a separate lock from GetLiveDebugSnapshot. Its busy
  # string does not invalidate an already complete immutable snapshot.
  if($live.gpu -and $live.materialGeometry) {
   Require-NativeReal $live
   return $live
  }
  if([DateTime]::UtcNow -gt $limit){throw 'Complete live measurement snapshot unavailable'}
  Start-Sleep -Milliseconds 80
 }while($true)
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
 & $Python (Join-Path $PSScriptRoot 'summarize-material-profile.py') --require-native-real $Path *> "$Path.oracle.log"
 if($LASTEXITCODE -ne 0){throw 'Recorded per-frame native-real provenance gate failed'}
}
function Measure-Stage([string]$Stage) {
 if($PlayMode) {
  Cmd 'scene.save' @("$project/Assets/Scenes/Performance-$Stage.creator")|Out-Null
  Cmd 'play'|Out-Null
  $playLimit=[DateTime]::UtcNow.AddSeconds(60)
  do {
   $playState=Cmd 'play.state'
   if([DateTime]::UtcNow -gt $playLimit){throw 'Play mode entry timeout'}
   if(!$playState.gameStart){Start-Sleep -Milliseconds 100}
  } until($playState.gameStart)
  Cmd 'render.pacing' @('status')|ConvertTo-Json -Depth 10|Set-Content "$case/$Stage-play-pacing.json"
 }
 Cmd 'render.live.fence' @('120')|Out-Null
 Start-Sleep -Seconds 3
 if($Stage -ne 'empty') {
  # Scene placement can finish before material/mesh preparation. Do not start
  # the warm recording with an empty geometry cache and count initial uploads
  # as recurring warm-frame uploads.
  $limit=[DateTime]::UtcNow.AddSeconds($ReadyTimeoutSeconds)
  $previous=$null;$stableSince=$null
  do {
   $warm=Read-LiveMeasurement
   if($warm.materialGeometry.entries -gt 0 -and $warm.materialGeometry.residentBytes -gt 0 -and
      $previous -and $warm.framesRendered -gt $previous.framesRendered -and
      $warm.materialGeometry.uploads -eq $previous.materialGeometry.uploads) {
    if(!$stableSince){$stableSince=[DateTime]::UtcNow}
    if(([DateTime]::UtcNow-$stableSince).TotalSeconds -ge 2){break}
   }else{$stableSince=$null}
   if([DateTime]::UtcNow -gt $limit){throw 'Model geometry did not become warm and stable'}
   $previous=$warm
   Start-Sleep -Milliseconds 250
  }while($true)
  $warm|ConvertTo-Json -Depth 30|Set-Content "$case/$Stage-warmup.json"
 }
 # 프로세스 열거 비용이 native 기록에 섞이지 않도록 시작 전에 경계를 관측한다.
 $environmentBefore = Get-PerformanceEnvironmentSnapshot
 Read-LiveMeasurement|Out-Null
 Cmd 'profile.record'|Out-Null
 Wait-Recording 'recording'|Out-Null
 $a=Read-LiveMeasurement
 Require-NativeReal $a
 $measurementBeginQpc = [Diagnostics.Stopwatch]::GetTimestamp()
 $watch=[Diagnostics.Stopwatch]::StartNew()
 Start-Sleep -Seconds $Seconds
 $b=Read-LiveMeasurement
 $watch.Stop()
 $measurementEndQpc = [Diagnostics.Stopwatch]::GetTimestamp()
 Require-NativeReal $b
 $settings=Cmd 'temporal.status' @($script:nativeGeneration)
 if(!$settings.acknowledged -or $settings.requestState -eq 'superseded'){throw 'Measurement settings changed during capture'}
 if(!$b.enabled -or !$b.ready -or $b.framesRendered -le $a.framesRendered) { throw "$Stage renderer stopped" }
 foreach($counter in @('mismatches','queryOverflowPasses','spanViolations','sliceUnderflows')) {
  if($b.gpu.$counter -ne $a.gpu.$counter) { throw "$Stage GPU timing counter increased: $counter" }
 }
 if($Stage -ne 'empty' -and $b.materialGeometry.uploads -ne $a.materialGeometry.uploads) { throw 'Static model geometry was uploaded again during the warm measurement' }
 if($Stage -ne 'empty' -and ($b.materialGeometry.entries -le 0 -or $b.materialGeometry.residentBytes -le 0)) {
  throw 'Selected model produced no resident geometry'
 }
 Save-Recording "$case/$Stage.ceprof"
 # 저장까지의 간격은 별도로 남긴다. 이 CPU 차분은 정확한 측정 구간의 ETW를 대신하지 않는다.
 $environmentAfter = Get-PerformanceEnvironmentSnapshot
 $environmentBefore | ConvertTo-Json -Depth 10 | Set-Content "$case/$Stage-environment-before.json"
 $environmentAfter | ConvertTo-Json -Depth 10 | Set-Content "$case/$Stage-environment-after.json"
 Compare-PerformanceEnvironment -Before $environmentBefore -After $environmentAfter -TargetProcessId $proc.Id `
     -MeasurementBeginQpc $measurementBeginQpc -MeasurementEndQpc $measurementEndQpc |
     ConvertTo-Json -Depth 10 | Set-Content "$case/$Stage-environment.json"
 $throughput=($b.framesRendered-$a.framesRendered)/$watch.Elapsed.TotalSeconds
 @{start=$a;end=$b;elapsedSeconds=$watch.Elapsed.TotalSeconds;completedRealViewRendersPerSecond=$throughput;
   axis='completed-real-view-renders-not-display-fps';profile="$Stage.ceprof";nativeRealProvenanceVerified=$true;
   nativeQualityAcceptance=$false}|ConvertTo-Json -Depth 30|Set-Content "$case/$Stage.json"
 $environmentBefore = Get-PerformanceEnvironmentSnapshot
 $a=Read-LiveMeasurement
 Require-NativeReal $a
 $measurementBeginQpc = [Diagnostics.Stopwatch]::GetTimestamp()
 $watch.Restart()
 Start-Sleep -Seconds $Seconds
 $b=Read-LiveMeasurement
 $watch.Stop()
 $measurementEndQpc = [Diagnostics.Stopwatch]::GetTimestamp()
 $environmentAfter = Get-PerformanceEnvironmentSnapshot
 $environmentBefore | ConvertTo-Json -Depth 10 | Set-Content "$case/$Stage-paused-environment-before.json"
 $environmentAfter | ConvertTo-Json -Depth 10 | Set-Content "$case/$Stage-paused-environment-after.json"
 Compare-PerformanceEnvironment -Before $environmentBefore -After $environmentAfter -TargetProcessId $proc.Id `
     -MeasurementBeginQpc $measurementBeginQpc -MeasurementEndQpc $measurementEndQpc |
     ConvertTo-Json -Depth 10 | Set-Content "$case/$Stage-paused-environment.json"
 Require-NativeReal $b
 @{start=$a;end=$b;elapsedSeconds=$watch.Elapsed.TotalSeconds;completedRealViewRendersPerSecond=($b.framesRendered-$a.framesRendered)/$watch.Elapsed.TotalSeconds;
   axis='completed-real-view-renders-not-display-fps';nativeRealProvenanceVerified=$false;
   scope='unrecorded-overhead-diagnostic-only-not-acceptance'}|ConvertTo-Json -Depth 30|Set-Content "$case/$Stage-paused.json"
 Write-Output "MEASURED $Configuration $Label $Stage"
 if($PlayMode -and $Stage -eq 'empty') {
  Cmd 'stop'|Out-Null
  $playLimit=[DateTime]::UtcNow.AddSeconds(60)
  do {
   $playState=Cmd 'play.state'
   if([DateTime]::UtcNow -gt $playLimit){throw 'Play mode exit timeout'}
   if($playState.gameStart){Start-Sleep -Milliseconds 100}
  } until(!$playState.gameStart)
 }
}
 if($WindowWidth -ne 640 -or $WindowHeight -ne 480) { Cmd 'window.resize' @("$WindowWidth","$WindowHeight")|Out-Null }
 Cmd 'play.foreground_override' @('off')|Out-Null
 if($UnlimitedPacing) {
  Cmd 'render.pacing' @('background','0')|Out-Null
  Cmd 'render.pacing' @('unlimited')|Out-Null
 }
 Cmd 'render.pacing' @('status')|ConvertTo-Json -Depth 10|Set-Content "$case/pacing.json"
 if($CreateEmptyScene) {
  Cmd 'scene.new' @('MaterialMeasurement')|Out-Null
  Cmd 'object.create' @('ProbeCamera')|Out-Null
  Cmd 'component.add' @('ProbeCamera','CameraComponent')|Out-Null
  Cmd 'object.transform' @('ProbeCamera','0','1','-5','0','0','0')|Out-Null
  Cmd 'object.create' @('ProbeLight','Light')|Out-Null
  Cmd 'object.transform' @('ProbeLight','0','2','0','45','-25','0')|Out-Null
  Cmd 'camera.editor' @('match')|Out-Null
 } else {
  Cmd 'scene.switch' @("$project/Assets/Scenes/LX_CookFixture.creator")|Out-Null
  Cmd 'object.delete' @('Ground')|Out-Null
 }
 Cmd 'editor.window' @('###Editor.MaterialGraph','close')|Out-Null
 $script:nativeGeneration=Set-NativeMeasurement
 Cmd 'window.info'|ConvertTo-Json -Depth 10|Set-Content "$case/window.json"
 Cmd 'render.rtinfo'|ConvertTo-Json -Depth 10|Set-Content "$case/viewport.json"
 @{configuration=$Configuration;validation='off';playMode=[bool]$PlayMode;windowWidth=$WindowWidth;windowHeight=$WindowHeight;uiScale=$UiScale;seconds=$Seconds;readyTimeoutSeconds=$ReadyTimeoutSeconds;runtimeSha256=(Get-FileHash (Join-Path (Split-Path $exe) 'CreatorEditor.runtime.dll')).Hash;startedUtc=[DateTime]::UtcNow.ToString('o')}|ConvertTo-Json|Set-Content "$case/measurement.json"
 Measure-Stage 'empty'
 Cmd 'model.loadcached' @("$project/Assets/Models/$ModelName.glb")|Out-Null
 $placement=Cmd 'model.place' @($ModelName)
 $rootMesh=@($placement.components | Where-Object { $_.type -eq 'MeshRenderer' }).Count -gt 0
 if(!$placement.changed -or (!$placement.children.Count -and !$rootMesh)){throw 'Selected model produced no scene mesh root or children'}
 $modelStage=if($ModelName -eq 'CreatorRobot'){'robot'}else{'model'}
 Measure-Stage $modelStage
 $captureTarget=if($PlayMode){'game'}else{'editor'}
 Cmd 'render.live.capture' @("$case/ModelCapture",$captureTarget,'controlled')|Out-Null
 & $Python (Join-Path $PSScriptRoot 'audit-material-measurement.py') $case --output "$case/measurement-audit.json"
 if($LASTEXITCODE -ne 0){throw 'Material recorded-population/foreground audit failed'}
 if($PlayMode){Cmd 'stop'|Out-Null}
 Cmd 'scene.save' @("$project/Assets/Scenes/Mat9Robot.creator")|Out-Null
 Cmd 'quit'
 if(!$proc.WaitForExit(30000)){throw 'Shutdown timeout'}
 if($proc.ExitCode -ne 0){throw "Exit $($proc.ExitCode)"}
 @{passed=$true;normalExit=$true;configuration=$Configuration;model=$ModelName;
   runtimeSha256=(Get-FileHash (Join-Path (Split-Path $exe) 'CreatorEditor.runtime.dll')).Hash;
   modelSourceSha256=(Get-FileHash "$project/Assets/Models/$ModelName.glb").Hash;
   stages=@('empty',$modelStage);physicalAcceptance=$false;productBudgetAccepted=$false}|
  ConvertTo-Json -Depth 10|Set-Content "$case/verification.json"
 Complete-ArtifactRetention $case "material-$Configuration" -KeepRawCaptures:$KeepRawCaptures
 Write-Output "MAT9_MEASURE_OK $case"
} finally {
 if($proc -and !$proc.HasExited){Stop-Process -Id $proc.Id}
 $env:CREATOR_DX12_VALIDATION=$previousValidation
}
