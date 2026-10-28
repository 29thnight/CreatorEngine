param([ValidateSet('Debug','Release')][string]$Configuration='Debug',[Parameter(Mandatory)][string]$Work)
$ErrorActionPreference='Stop'
$repo=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$exe=Join-Path $repo "Bin/x64-$Configuration/Editor/CreatorEditor.exe"
$runtime=Join-Path (Split-Path $exe) 'CreatorEditor.runtime.dll'
$Work=[IO.Path]::GetFullPath($Work)
if(Test-Path $Work){throw 'Use a new verification directory'}
New-Item -ItemType Directory $Work|Out-Null
. "$PSScriptRoot/CommandResults.ps1"
$commands=@('wait 5','containers.integration.probe','render.live.ready 120','light.proxy','scene.transformdigest edit','play','wait 10','object.create __ContainerIntegrationLive','wait 10','play.state','light.proxy','stop','wait 10','play.state','scene.transformdigest restored','containers.integration.probe','quit')
$script=Join-Path $Work 'commands.txt';$result=Join-Path $Work 'results.jsonl'
Write-CommandScript $script $commands
$oldWorkspace=$env:CREATOR_EDITOR_WORKSPACE_DIR;$oldIni=$env:CREATOR_EDITOR_LEGACY_INI
$process=$null;$started=Get-Date -Format o
try{
 $env:CREATOR_EDITOR_WORKSPACE_DIR=Join-Path $Work 'workspace'
 $env:CREATOR_EDITOR_LEGACY_INI=Join-Path $Work 'workspace/legacy.ini'
 $process=Start-Process -FilePath $exe -ArgumentList @('--smoke-offscreen','--commandlet-script',('"'+$script+'"'),'--result-format','jsonl','--result-file',('"'+$result+'"')) -WorkingDirectory (Split-Path $exe) -WindowStyle Hidden -PassThru -RedirectStandardOutput "$Work/stdout.log" -RedirectStandardError "$Work/stderr.log"
 $deadline=(Get-Date).AddSeconds(240)
 while(!$process.WaitForExit(1000)){if((Get-Date) -gt $deadline){$process.Kill();throw 'Container Editor verification timed out'}}
 $process.WaitForExit()
 if($process.ExitCode -ne 0){throw "Editor exit=$($process.ExitCode); see $Work"}
 $rows=Read-CommandResults $result
 Assert-CommandSequence $rows $commands
 if(@($rows|Where-Object status -ne 'succeeded').Count){throw 'A product command failed; inspect results.jsonl'}
 $ready=Get-SucceededCommand $rows 'render.live.ready'
 if($ready.completionKind -ne 'editor_gpu_frame_completed' -or $ready.completedFrame -le $ready.afterFrame){throw 'A newer product GPU frame did not complete'}
 $states=@($rows|Where-Object command -eq 'play.state')
 if($states.Count -ne 2 -or !$states[0].data.gameStart -or $states[1].data.gameStart -or $states[1].data.state -ne 'Stopped'){throw 'Play/Stop state transitions did not occur'}
 foreach($snapshot in @($rows|Where-Object command -eq 'light.proxy')){if($snapshot.data.queued -le 0 -or $snapshot.data.applied -le 0){throw 'Live render proxy publication was not exercised'}}
 $probes=@($rows|Where-Object command -eq 'containers.integration.probe')
 if($probes.Count -ne 2 -or @($probes|Where-Object {!$_.data.exactlyOnce -or !$_.data.geometryRollback -or !$_.data.physicsStorage -or !$_.data.persistentHeapPolicy -or !$_.data.drawSealStorage -or !$_.data.nodeSetStorage -or !$_.data.orderedSetStorage -or !$_.data.orderedMapStorage -or !$_.data.segmentedStorage -or !$_.data.utf16Strings -or !$_.data.funcOwnership -or !$_.data.ifcEngineMode -or $_.data.consumed -ne 8192}).Count){throw 'Module producer/consumer ownership, IFC registration or renderer/physics storage probe failed'}
 $digests=@($rows|Where-Object command -eq 'scene.transformdigest')
 if($digests.Count -ne 2 -or $digests[0].data.objects -le 0 -or $digests[0].data.objects -ne $digests[1].data.objects){throw 'Scene object count was not restored'}
 $before=@($digests[0].data.worldTransforms|ForEach-Object {$_|ConvertTo-Json -Depth 20 -Compress}|Sort-Object) -join "`n"
 $after=@($digests[1].data.worldTransforms|ForEach-Object {$_|ConvertTo-Json -Depth 20 -Compress}|Sort-Object) -join "`n"
 if($before -cne $after){throw 'Play/Stop changed the original editor scene transforms'}
 $receipt=[ordered]@{pass=$true;configuration=$Configuration;started=$started;ended=(Get-Date -Format o);exitCode=$process.ExitCode;commandCount=$rows.Count;probeCount=2;transfersPerProbe=8192;editorSceneRestored=$true;geometryRollback=$true;physicsStorage=$true;persistentHeapPolicy=$true;drawSealStorage=$true;nodeSetStorage=$true;orderedSetStorage=$true;orderedMapStorage=$true;segmentedStorage=$true;utf16Strings=$true;funcOwnership=$true;ifcEngineMode=$true;results=(Get-FileHash $result|Select-Object Path,Hash);binaries=@(Get-FileHash $exe,$runtime|Select-Object Path,Hash)}
 $receipt|ConvertTo-Json -Depth 5|Set-Content "$Work/acceptance.json" -Encoding utf8
 "CONTAINER_ENGINE_INTEGRATION_PASS: $Configuration; 2 x 8192 transfers, live renderer and Play/Stop, scene restored; normal exit."
}finally{
 $env:CREATOR_EDITOR_WORKSPACE_DIR=$oldWorkspace;$env:CREATOR_EDITOR_LEGACY_INI=$oldIni
 if($process -and !$process.HasExited){$process.Kill()}
}
