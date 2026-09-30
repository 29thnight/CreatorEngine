param(
 [ValidateSet('Debug','Release')][string]$Configuration='Debug',
 [string]$Label='mat9',
 [Parameter(Mandatory=$true)][string]$FixtureProject,
 [int]$WindowWidth=640,
 [int]$WindowHeight=480,
 [double]$UiScale=0,
 [int]$Seconds=8
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
function Measure-Stage([string]$Stage) {
 Cmd 'render.live.fence' @('120')|Out-Null
 Start-Sleep -Seconds 3
 Cmd 'profile.record'|Out-Null
 $a=Cmd 'dx12.live'
 $watch=[Diagnostics.Stopwatch]::StartNew()
 Start-Sleep -Seconds $Seconds
 $b=Cmd 'dx12.live'
 $watch.Stop()
 if(!$b.enabled -or !$b.ready -or $b.framesRendered -le $a.framesRendered) { throw "$Stage renderer stopped" }
 foreach($counter in @('mismatches','queryOverflowPasses','spanViolations','sliceUnderflows')) {
  if($b.gpu.$counter -ne $a.gpu.$counter) { throw "$Stage GPU timing counter increased: $counter" }
 }
 if($Stage -eq 'robot' -and $b.materialGeometry.uploads -ne $a.materialGeometry.uploads) { throw 'Static model geometry was uploaded again during the warm measurement' }
 Cmd 'profile.pause'|Out-Null
 Cmd 'profile.save' @("$case/$Stage.ceprof")|Out-Null
 $fps=($b.framesRendered-$a.framesRendered)/$watch.Elapsed.TotalSeconds
 @{start=$a;end=$b;elapsedSeconds=$watch.Elapsed.TotalSeconds;renderFps=$fps}|ConvertTo-Json -Depth 30|Set-Content "$case/$Stage.json"
 Cmd 'profile.pause'|Out-Null
 $a=Cmd 'dx12.live'
 $watch.Restart()
 Start-Sleep -Seconds $Seconds
 $b=Cmd 'dx12.live'
 $watch.Stop()
 @{start=$a;end=$b;elapsedSeconds=$watch.Elapsed.TotalSeconds;renderFps=($b.framesRendered-$a.framesRendered)/$watch.Elapsed.TotalSeconds}|ConvertTo-Json -Depth 30|Set-Content "$case/$Stage-paused.json"
 Write-Output "MEASURED $Configuration $Label $Stage"
}
 if($WindowWidth -ne 640 -or $WindowHeight -ne 480) { Cmd 'window.resize' @("$WindowWidth","$WindowHeight")|Out-Null }
 Cmd 'play.foreground_override' @('off')|Out-Null
 Cmd 'scene.switch' @("$project/Assets/Scenes/LX_CookFixture.creator")|Out-Null
 Cmd 'object.delete' @('Ground')|Out-Null
 Cmd 'editor.window' @('###Editor.MaterialGraph','close')|Out-Null
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
