[CmdletBinding()]
param()
$ErrorActionPreference='Stop'
$repo=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
if(Get-Process CreatorEditor -ErrorAction SilentlyContinue){throw 'F# authoring gate owns its Editor session'}
$out=Join-Path $repo ('Build/Verification/FSharp/Editor-'+[guid]::NewGuid().ToString('N'))
$project=Join-Path $out 'Project'
New-Item -ItemType Directory -Force "$project/Assets/Script","$project/Assets/Scenes"|Out-Null
Copy-Item "$repo/Dynamic_CPP/ProjectSetting" "$project/ProjectSetting" -Recurse
Copy-Item "$repo/Dynamic_CPP/Assets/Shaders" "$project/Assets/Shaders" -Recurse
Set-Content "$project/Assets/Script/compile.order" '' -Encoding utf8
$exe="$repo/Bin/x64-Release/Editor/CreatorEditor.exe"
$managedScriptsDirectory="$repo/Bin/x64-Release/Managed/Scripts"
$gameScriptsBackup=Join-Path $out 'managed-before'
New-Item -ItemType Directory -Force $gameScriptsBackup|Out-Null
Get-ChildItem $managedScriptsDirectory -File -Filter 'GameScripts*' | Copy-Item -Destination $gameScriptsBackup
function RestoreGameScripts {
 Get-ChildItem $managedScriptsDirectory -File -Filter 'GameScripts*' | ForEach-Object {Remove-Item -LiteralPath $_.FullName -Force}
 Get-ChildItem $gameScriptsBackup -File | Copy-Item -Destination $managedScriptsDirectory -Force
}
# Begin with a completed, empty catalog instead of a prior fixture's DLL. This
# makes repeated runs independent of successful or failed earlier authoring.
Set-Content "$project/Assets/Script/AuthoringGateBootstrap.fs" "namespace CreatorEngine.AuthoringGate`nmodule Bootstrap =`n    let value = 0" -Encoding utf8
Set-Content "$project/Assets/Script/compile.order" 'AuthoringGateBootstrap.fs' -Encoding utf8
& dotnet build "$repo/GameScripts/GameScripts.fsproj" -c Release "-p:CreatorScriptSourceRoot=$project/Assets/Script" -v:q *> "$out/initial-build.log"
if($LASTEXITCODE){RestoreGameScripts;throw "Initial F# catalog build failed: $out/initial-build.log"}
$p=$null
try {
 $p=Start-Process $exe -ArgumentList @('--development-project',('"'+$project+'"'),'--command-service','--allow-user-code','--smoke-offscreen') -WorkingDirectory (Split-Path $exe) -WindowStyle Hidden -RedirectStandardOutput "$out/editor.out" -RedirectStandardError "$out/editor.err" -PassThru
 # Acquire the handle while the child is alive so PowerShell 5 retains ExitCode.
 # Startup/handle acquisition failures also pass through managed-output restore.
 $null=$p.Handle
 $deadline=[DateTime]::UtcNow.AddSeconds(120);$e=$null
 do {if($p.HasExited){throw 'Editor exited'};if(Test-Path "$project/Library/CommandService/endpoint.json"){$candidate=Get-Content "$project/Library/CommandService/endpoint.json" -Raw|ConvertFrom-Json;if($candidate.pid -eq $p.Id){$e=$candidate}};if(!$e){Start-Sleep -Milliseconds 150}}until($e -or [DateTime]::UtcNow -ge $deadline)
 if(!$e){throw 'Owned Editor endpoint missing'}
 $base='http://127.0.0.1:'+$e.port;$headers=@{Authorization='Bearer '+$e.token}
 function Command([string]$name,[string[]]$arguments=@()) {
  $v=Invoke-RestMethod "$base/command" -Method Post -Headers $headers -ContentType application/json -Body (@{command=$name;args=@($arguments);mode='async'}|ConvertTo-Json -Compress) -TimeoutSec 30
  if($name -eq 'quit'){return}
  if($v.PSObject.Properties['operationId']){$poll=$v.poll;$limit=[DateTime]::UtcNow.AddSeconds(150);do{Start-Sleep -Milliseconds 100;$v=Invoke-RestMethod "$base$poll" -Headers $headers -TimeoutSec 30}until($v.state -eq 'completed' -or [DateTime]::UtcNow -ge $limit)}
  @{command=$name;result=$v}|ConvertTo-Json -Depth 15 -Compress|Add-Content "$out/commands.jsonl"
  if($v.status -ne 'succeeded'){throw "$name failed: $($v.message)"};return $v.data
 }
 function WaitBuild([bool]$success){$limit=[DateTime]::UtcNow.AddSeconds(150);do{Start-Sleep -Milliseconds 200;$state=Command 'script.creation'}while($state.busy -and [DateTime]::UtcNow -lt $limit);if($state.busy -or $state.succeeded -ne $success){throw "Unexpected F# build state: $($state|ConvertTo-Json -Compress)"};return $state}
 function WaitVersion([string]$expected){$limit=[DateTime]::UtcNow.AddSeconds(150);do{Start-Sleep -Milliseconds 250;try{$value=Command 'script.invoke' @('FSharpAuthoringGate','Version');if([string]$value.returnValue -eq $expected){return}}catch{if([DateTime]::UtcNow -ge $limit){throw}}}while([DateTime]::UtcNow -lt $limit);throw 'F# automatic reload timed out'}
 function WaitPlay([bool]$expected) {
  $limit=[DateTime]::UtcNow.AddSeconds(30)
  do {
   Start-Sleep -Milliseconds 100
   $play=Command 'play.state'
   if(!$play.pending -and $play.committed -eq $expected){return}
  } while([DateTime]::UtcNow -lt $limit)
  throw 'Requested Play/Stop was not committed'
 }
 $null=Command 'scene.new' @('FSharpAuthoring')
 $null=Command 'object.create' @('Owner')
 $null=Command 'script.create' @('Owner','FSharpAuthoringGate')
 $state=WaitBuild $true
 if([IO.Path]::GetExtension($state.source) -ne '.fs'){throw 'Editor did not create F#'}
 $source=$state.source
 $templateText=[IO.File]::ReadAllText($source)
 if($templateText -match '\b(member|override)\b'){throw 'New F# template must not create unused lifecycle members'}
 $templateComponents=@((Command 'script.status').components|Where-Object type -eq 'FSharpAuthoringGate')
 if($templateComponents.Count -ne 1){throw 'Hook-free F# component was not attached'}
 $null=Command 'play.foreground_override' @('on')
 $null=Command 'play'
 WaitPlay $true
 Start-Sleep -Milliseconds 200
 $null=Command 'stop'
 WaitPlay $false
 if((Get-Content "$project/Assets/Script/compile.order") -notcontains 'FSharpAuthoringGate.fs'){throw 'New F# file missing explicit order'}
 $text=@'
namespace CreatorEngine.Scripts
open CreatorEngine

type FSharpAuthoringGate() =
    inherit Component()
    [<DefaultValue; SerializeField>]
    val mutable Value: int
    [<DefaultValue; SerializeField>]
    val mutable Ticks: int
    member this.OnTickSimulate(dt: single) = this.Ticks <- this.Ticks + 1
    [<EngineCallable>]
    static member Version() = 1
'@
 [IO.File]::WriteAllText($source,$text,[Text.UTF8Encoding]::new($false));WaitVersion '1'
 $scripts=@((Command 'script.status').components|Where-Object type -eq 'FSharpAuthoringGate')
 if($scripts.Count -ne 1){throw 'F# creation attachment count mismatch'}
 $id=$scripts[0].instanceId
 $field=@((Command 'script.fields' @([string]$id)).fields|Where-Object name -eq 'Value')[0]
 $null=Command 'script.set' @([string]$id,[string]$field.index,'37')
 [IO.File]::WriteAllText($source,$text.Replace('Version() = 1','Version() = 2'),[Text.UTF8Encoding]::new($false));WaitVersion '2'
 $script2=@((Command 'script.status').components|Where-Object type -eq 'FSharpAuthoringGate')[0]
 $value=@((Command 'script.fields' @([string]$script2.instanceId)).fields|Where-Object name -eq 'Value')[0]
 if([string]$value.value -ne '37'){throw 'F# reload lost serialized field'}
 $ids=[string]$script2.instanceId
 [IO.File]::WriteAllText($source,$text+"`nlet = invalid",[Text.UTF8Encoding]::new($false))
 Start-Sleep -Seconds 1
 $null=WaitBuild $false
 if([string](Command 'script.invoke' @('FSharpAuthoringGate','Version')).returnValue -ne '2'){throw 'Failed F# compile replaced active code'}
 $after=@((Command 'script.status').components|Where-Object type -eq 'FSharpAuthoringGate')[0]
 if([string]$after.instanceId -ne $ids){throw 'Failed F# compile replaced instance'}
 [IO.File]::WriteAllText($source,$text.Replace('Version() = 1','Version() = 3'),[Text.UTF8Encoding]::new($false));WaitVersion '3'
 $null=Command 'play.foreground_override' @('on');$null=Command 'play';WaitPlay $true;Start-Sleep -Milliseconds 400
 $active=@((Command 'script.status').components|Where-Object type -eq 'FSharpAuthoringGate')[0]
 $ticks=@((Command 'script.fields' @([string]$active.instanceId)).fields|Where-Object name -eq 'Ticks')[0]
 if([int]$ticks.value -le 0){throw 'Created F# OnTickSimulate did not run'}
 $null=Command 'stop';WaitPlay $false
 $shutdownWatch=[Diagnostics.Stopwatch]::StartNew()
 $null=Command 'quit'
 $exited=$p.WaitForExit(30000)
 $shutdownWatch.Stop()
 @{exited=$exited;exitCode=$(if($exited){$p.ExitCode}else{$null});pid=$p.Id;elapsedMs=$shutdownWatch.Elapsed.TotalMilliseconds;shell=$PSVersionTable.PSVersion.ToString()}|ConvertTo-Json|Set-Content "$out/shutdown.json" -Encoding utf8
 if(!$exited -or $p.ExitCode -ne 0){throw "Editor shutdown failed: exited=$exited, code=$($p.ExitCode)"}
 if($shutdownWatch.Elapsed.TotalSeconds -ge 5){throw 'Editor shutdown exceeded 5 seconds with an idle command-service connection'}
 if(Test-Path "$project/Library/CommandService/endpoint.json"){throw 'Command service endpoint survived graceful shutdown'}
 @{result='FSHARP_EDITOR_AUTHORING_OK';source=$source;create='hook-free F# component attached and played + explicit order';reload='field retained';failure='previous code and instance retained';ticks=[int]$ticks.value}|ConvertTo-Json|Set-Content "$out/result.json" -Encoding utf8
 "FSHARP_EDITOR_AUTHORING_OK evidence=$out"
} finally {
 if($null -ne $p) {
  if(!$p.HasExited){$p.Kill();$p.WaitForExit()}
  $p.Dispose()
 }
 RestoreGameScripts
}
