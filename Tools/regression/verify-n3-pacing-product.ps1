[CmdletBinding()]
param([ValidateSet('Debug','Release')][string]$Configuration='Release',[int[]]$Rates=@(30,60,144),[switch]$RecordOnly,[switch]$MovingQuality,[switch]$CSharpControl,[switch]$Profile)
$ErrorActionPreference='Stop'
$repo=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
if(Get-Process CreatorEditor -ErrorAction SilentlyContinue){throw 'The pacing gate owns its Editor session'}
$out=Join-Path $repo ('Build/Verification/N3Pacing/Editor-'+[guid]::NewGuid().ToString('N'))
$project=Join-Path $out 'Project'
New-Item -ItemType Directory -Force "$project/Assets/Script"|Out-Null
Copy-Item "$repo/Dynamic_CPP/ProjectSetting" $project -Recurse
Copy-Item "$repo/Dynamic_CPP/Assets/Shaders" "$project/Assets/Shaders" -Recurse
if($CSharpControl){
 # Isolated benchmark control only; Editor/SDK authoring remains F# only.
 Copy-Item "$repo/GameScripts/N3PacingProbe.cs" "$project/Assets/Script"
 $control=@"
<Project Sdk="Microsoft.NET.Sdk"><PropertyGroup><TargetFramework>net10.0</TargetFramework><AssemblyName>GameScripts</AssemblyName><EnableDefaultCompileItems>false</EnableDefaultCompileItems><ImplicitUsings>enable</ImplicitUsings><OutputPath>$repo/Bin/x64-$Configuration/Managed/Scripts/</OutputPath><AppendTargetFrameworkToOutputPath>false</AppendTargetFrameworkToOutputPath></PropertyGroup><ItemGroup><Compile Include="Assets/Script/N3PacingProbe.cs" /><Reference Include="ScriptCore"><HintPath>$repo/Bin/x64-$Configuration/Managed/ScriptCore.dll</HintPath><Private>false</Private></Reference><Analyzer Include="$repo/Build/Tools/Managed/ScriptCore.Generators/$Configuration/netstandard2.0/ScriptCore.Generators.dll" /></ItemGroup></Project>
"@
 $control|Set-Content "$project/Control.csproj" -Encoding utf8
 & dotnet build "$project/Control.csproj" -c $Configuration -v:q *> "$out/managed-build.log"
 if($LASTEXITCODE){throw 'C# diagnostic control build failed'}
}else{
Copy-Item "$repo/GameScripts/N3PacingProbe.fs" "$project/Assets/Script"
Set-Content "$project/Assets/Script/compile.order" "N3PacingProbe.fs" -Encoding utf8
& dotnet build "$repo/GameScripts/GameScripts.fsproj" -c $Configuration "-p:CreatorScriptSourceRoot=$project/Assets/Script" -v:q *> "$out/managed-build.log"
if($LASTEXITCODE){throw "F# pacing probe build failed: $out/managed-build.log"}
}
& python "$PSScriptRoot/new-n3-pacing-fixture.py" "$project/Assets/Models"
if($LASTEXITCODE){throw 'Fixture generation failed'}
$floor=Join-Path $out 'floor.json'
'[ {"shapeId":12,"kind":0,"halfExtent":[20,0.5,20]} ]'|Set-Content $floor -Encoding utf8
$exe="$repo/Bin/x64-$Configuration/Editor/CreatorEditor.exe"
$runtime=Join-Path (Split-Path $exe) 'CreatorEditor.runtime.dll'
foreach($source in @('Editor/EngineGUIWindow/ViewportHostWindow.cpp','Editor/EngineEntry/App.cpp','Editor/EngineEntry/EditorPreferences.h','Editor/EngineEntry/Commands/N3PacingCapture.h','Editor/EngineEntry/Commands/RenderTestCommands.cpp','Editor/EngineEntry/Commands/RenderDebugCommands.cpp','Engine/SceneRuntime/CharacterMovementComponent.cpp','Engine/SceneRuntime/CameraComponent.h','Engine/SceneRuntime/Scene.cpp','Engine/SceneRuntime/Scene.h','Engine/SceneRuntime/ScenePhysicsSimulation.cpp','Engine/SceneRuntime/ScenePhysicsSimulation.h','Engine/RenderEngine/Render/Scene/EnhancedPbrCapture.h')){
 if((Get-Item "$repo/$source").LastWriteTimeUtc -gt (Get-Item $runtime).LastWriteTimeUtc){throw "Fresh N3 runtime build required: $source"}
}
$managedDll="$repo/Bin/x64-$Configuration/Managed/Scripts/GameScripts.dll"
if((Get-Item "$repo/GameScripts/N3PacingProbe.fs").LastWriteTimeUtc -gt (Get-Item $managedDll).LastWriteTimeUtc){throw 'Fresh N3 managed probe build required'}
$hash=(Get-FileHash $runtime).Hash
$prior=$env:CREATOR_DX12_VALIDATION;$env:CREATOR_DX12_VALIDATION='basic'
try{$p=Start-Process $exe -ArgumentList @('--development-project',('"'+$project+'"'),'--command-service','--allow-user-code','--smoke-offscreen') -WorkingDirectory (Split-Path $exe) -WindowStyle Hidden -RedirectStandardOutput "$out/stdout.log" -RedirectStandardError "$out/stderr.log" -PassThru}finally{$env:CREATOR_DX12_VALIDATION=$prior}
try {
 $endpoint="$project/Library/CommandService/endpoint.json";$e=$null
 for($i=0;$i -lt 240;$i++){Start-Sleep -Milliseconds 250;if($p.HasExited){throw "Editor exited: $($p.ExitCode)"};if(Test-Path $endpoint){$e=Get-Content $endpoint -Raw|ConvertFrom-Json;if($e.pid -eq $p.Id){break}}}
 if(!$e -or $e.pid -ne $p.Id){throw 'Owned endpoint unavailable'}
 $headers=@{Authorization='Bearer '+$e.token};$base='http://127.0.0.1:'+$e.port
 function Command([string]$name,[string[]]$arguments=@()) {
  $v=Invoke-RestMethod "$base/command" -DisableKeepAlive -Method Post -Headers $headers -ContentType application/json -Body (@{command=$name;args=@($arguments);mode='async'}|ConvertTo-Json -Compress) -TimeoutSec 180
  if($name -ne 'quit' -and $v.PSObject.Properties['operationId']){
   $poll=[string]$v.poll;if(!$poll.StartsWith('/operations/')){throw 'Unexpected poll route'}
   $limit=[DateTime]::UtcNow.AddSeconds(180)
   do{Start-Sleep -Milliseconds 60;$v=Invoke-RestMethod "$base$poll" -Headers $headers -TimeoutSec 180;if([DateTime]::UtcNow -gt $limit){throw 'Pacing command timeout'}}until($v.state -eq 'completed')
  }
  @{command=$name;args=$arguments;result=$v}|ConvertTo-Json -Depth 22 -Compress|Add-Content "$out/commands.jsonl"
  if($name -ne 'quit' -and $v.status -ne 'succeeded'){throw "$name failed: $($v.code) $($v.message)"}
  return $v.data
 }
 Command 'window.resize' @('640','480')|Out-Null
 Command 'temporal.upscale' @('none')|Out-Null
 Command 'play.foreground_override' @('on')|Out-Null
 foreach($fps in $Rates){
  Command 'scene.new' @("N3Pacing$fps")|Out-Null
  Command 'object.create' @('Floor')|Out-Null
  Command 'component.add' @('Floor','PhysicsBodyComponent')|Out-Null
  Command 'object.property' @('Floor','PhysicsBodyComponent','m_motion','0')|Out-Null
  Command 'object.transform' @('Floor','0','-0.5','0')|Out-Null
  Command 'physics.shapes' @('Floor','PhysicsBodyComponent',$floor)|Out-Null
  Command 'object.create' @('N3Character')|Out-Null
  Command 'object.transform' @('N3Character','0','1.05','0')|Out-Null
  Command 'component.add' @('N3Character','CharacterMovementComponent')|Out-Null
  Command 'script.add' @('N3Character','N3PacingProbe')|Out-Null
  Command 'object.create' @('N3Body')|Out-Null
  Command 'object.transform' @('N3Body','-2','3','3')|Out-Null
  Command 'component.add' @('N3Body','PhysicsBodyComponent')|Out-Null
  Command 'object.property' @('N3Body','PhysicsBodyComponent','m_gravityEnabled','false')|Out-Null
  foreach($item in @(@('N3Green','N3Child','N3Character'),@('N3Red','N3BodyVisual','N3Body'))){
   Command 'model.load' @("$project/Assets/Models/$($item[0]).glb")|Out-Null
   $placed=Command 'model.place' @($item[0])
   $placed|ConvertTo-Json -Depth 10|Set-Content "$out/placed-$($item[0])-$fps.json"
   Command 'object.rename' @($placed.name,$item[1])|Out-Null
   Command 'object.parent' @($item[1],$item[2])|Out-Null
   Command 'object.transform' @($item[1],'0','0','0')|Out-Null
  }
  Command 'object.create' @('Camera')|Out-Null
  Command 'component.add' @('Camera','CameraComponent')|Out-Null
  Command 'object.parent' @('Camera','N3Character')|Out-Null
  Command 'object.transform' @('Camera','0','1.95','-10','0','0','0')|Out-Null
  Command 'object.property' @('Camera','CameraComponent','m_isPrimary','true')|Out-Null
  Command 'editor.viewport' @('game')|Out-Null
 $deadline=[DateTime]::UtcNow.AddSeconds(15)
 do{Start-Sleep -Milliseconds 100;$demand=Command 'editor.viewport'}until(($demand.mode -eq 'game' -and $demand.gameTarget -and !$demand.editorTarget) -or [DateTime]::UtcNow -ge $deadline)
 if($demand.mode -ne 'game' -or !$demand.gameTarget -or $demand.editorTarget){throw 'Single visible Game view demand did not settle'}
  $settings=Join-Path $project 'ProjectSetting/EngineSettings.asset';$settingsHash=(Get-FileHash $settings).Hash
  Command 'render.pacing' @('play',"$fps")|Out-Null
  Command 'render.live.capture' @((Join-Path $out "warm-$fps"),'game','controlled')|Out-Null
  Command 'play'|Out-Null
  Start-Sleep -Seconds 2
  Command 'play.pause'|Out-Null
  $run=Join-Path $out "fps-$fps"
  $captureArgs=@($run);if($MovingQuality){$captureArgs+='moving'}
  if($Profile){Command 'profile.record'|Out-Null}
  Command 'simulation.pacing.capture' $captureArgs|ConvertTo-Json -Depth 10|Set-Content "$out/capture-$fps.json"
  if($Profile){Command 'profile.frame'|Out-Null;Start-Sleep -Milliseconds 250;Command 'profile.frame'|ConvertTo-Json -Depth 40|Set-Content "$run/profile.json";Command 'profile.stats'|ConvertTo-Json -Depth 25|Set-Content "$run/profile-stats.json";Command 'profile.pause'|Out-Null}
  Command 'script.invoke' @('N3PacingProbe','State')|ConvertTo-Json -Depth 15|Set-Content "$run/managed.json"
  Command 'render.pacing' @('status')|ConvertTo-Json -Depth 10|Set-Content "$run/pacing.json"
  Command 'render.live.capture' @("$run/pbr",'game','controlled')|Out-Null
  if($fps -eq 144){
   # Repeatedly resume each retained display target with real GPU readback.
   foreach($mode in @('scene','game','scene','game')){
    Command 'editor.viewport' @($mode)|Out-Null
    $deadline=[DateTime]::UtcNow.AddSeconds(15)
    do{Start-Sleep -Milliseconds 100;$demand=Command 'editor.viewport'}until(($demand.mode -eq $mode) -or [DateTime]::UtcNow -ge $deadline)
    if($demand.mode -ne $mode -or $demand.editorTarget -ne ($mode -eq 'scene') -or $demand.gameTarget -ne ($mode -eq 'game')){throw 'Viewport target transition lost demand'}
    $folder=Join-Path $run ('viewport-'+$mode+'-'+[guid]::NewGuid().ToString('N'))
    Command 'render.live.capture' @($folder, $(if($mode -eq 'scene'){'editor'}else{'game'}),'controlled')|Out-Null
    $demand|ConvertTo-Json -Depth 10|Set-Content "$folder/demand.json"
   }
   $transitions=if($MovingQuality){@('prepare','disable','enable','warp')}else{@('prepare','remove')}
   foreach($transition in $transitions){
    $folder=Join-Path $run "transitions/$transition";New-Item -ItemType Directory -Force $folder|Out-Null
    Command 'simulation.pacing.transition' @($transition)|ConvertTo-Json -Depth 8|Set-Content "$folder/expected.json"
    Command 'render.live.capture' @("$folder/pbr",'game','controlled')|Out-Null
   }
  }
  Command 'dx12.validation'|ConvertTo-Json -Depth 12|Set-Content "$run/validation.json"
  if((Get-FileHash $settings).Hash -ne $settingsHash){throw 'Session-only Play pacing changed persisted preferences'}
  Command 'stop'|Out-Null
  Start-Sleep -Milliseconds 400
 }
 Command 'render.pacing' @('play','off')|Out-Null
 Command 'quit'|Out-Null
 if(!$p.WaitForExit(30000) -or $p.ExitCode -ne 0){throw 'Editor shutdown failed'}
 if((Get-FileHash $runtime).Hash -ne $hash){throw 'Runtime changed during pacing acceptance'}
 @{result='N3_PACING_RECORDED';rates=$Rates;runtimeHash=$hash;configuration=$Configuration;movingQuality=[bool]$MovingQuality;csharpControl=[bool]$CSharpControl;managedHash=(Get-FileHash $managedDll).Hash}|ConvertTo-Json|Set-Content "$out/recorded.json"
 if(!$RecordOnly){
  & python "$PSScriptRoot/verify-n3-pacing.py" --work $out
  if($LASTEXITCODE){throw "Independent N3 pacing oracle failed: $out"}
  if(!$MovingQuality -and 144 -in $Rates){
   & python "$PSScriptRoot/n3_pacing_oracle_tests.py" --work $out
   if($LASTEXITCODE){throw 'N3 oracle canaries failed'}
  }
 }
 if($RecordOnly){"N3_PACING_RECORDED evidence=$out"}else{"N3_PACING_PRODUCT_OK evidence=$out"}
}finally{if(!$p.HasExited){$p.Kill();$p.WaitForExit()}}
