[CmdletBinding()]
param([ValidateSet('Debug','Release')][string]$Configuration='Release', [string]$EditorExe='')
$ErrorActionPreference='Stop'
$repo=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
if(Get-Process CreatorEditor -ErrorAction SilentlyContinue){throw 'Close the existing Editor; this gate owns its session.'}
$out=Join-Path $repo ('Build/Verification/FSharpFixedUpdate/Editor-'+[guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Force $out|Out-Null
$exe=if($EditorExe){(Resolve-Path -LiteralPath $EditorExe).Path}else{Join-Path $repo "Bin/x64-$Configuration/Editor/CreatorEditor.exe"}
$project=Join-Path $out 'Project'
New-Item -ItemType Directory -Force "$project/Assets/Script","$project/Assets/Scenes"|Out-Null
Copy-Item "$repo/Dynamic_CPP/ProjectSetting" $project -Recurse
Copy-Item "$repo/Dynamic_CPP/Assets/Shaders" "$project/Assets/Shaders" -Recurse
$scripts=@('FixedUpdateContractProbe','ExternalAwaitProbe','WorkerCallProbe','FailCleanupProbe','FailInitializedProbe','FailBeginProbe','DisableInBeginProbe')
foreach($name in $scripts){Copy-Item "$repo/GameScripts/$name.fs" "$project/Assets/Script"}
Set-Content "$project/Assets/Script/compile.order" @($scripts|ForEach-Object {"$_.fs"}) -Encoding utf8
& dotnet build "$repo/Engine/Scripting/GameScripts/ScriptNative.GameScripts.fsproj" -c $Configuration "-p:CreatorScriptSourceRoot=$project/Assets/Script" -v:q *> "$out/managed-build.log"
if($LASTEXITCODE){throw "F# fixed fixture build failed: $out/managed-build.log"}
$started=Get-Date
$process=Start-Process $exe -ArgumentList @('--development-project',('"'+$project+'"'),'--command-service','--allow-user-code','--smoke-offscreen') -WorkingDirectory (Split-Path $exe) -WindowStyle Hidden -RedirectStandardOutput "$out/editor.out" -RedirectStandardError "$out/editor.err" -PassThru
$script:sequence=0
try {
    $until=(Get-Date).AddSeconds(180)
    $endpoint=Join-Path $project 'Library/CommandService/endpoint.json'
    do {
        if($process.HasExited){throw "Editor exited $($process.ExitCode)"}
        if(Test-Path $endpoint){$candidate=Get-Content $endpoint -Raw|ConvertFrom-Json;if($candidate.pid -eq $process.Id){$info=$candidate;break}}
        Start-Sleep -Milliseconds 200
    }while((Get-Date) -lt $until)
    if(!$info){throw 'Owned endpoint unavailable'}
    $headers=@{Authorization="Bearer $($info.token)"};$base="http://127.0.0.1:$($info.port)"
    function Command([string]$name,[string[]]$arguments=@()) {
        $script:sequence++
        $body=@{command=$name;args=@($arguments);mode='sync';correlationId="fixed-update-$script:sequence"}|ConvertTo-Json -Compress
        $result=Invoke-RestMethod "$base/command" -Headers $headers -Method Post -ContentType 'application/json' -Body $body -TimeoutSec 120
        if($result.operationId){$poll=$result.poll;$deadline=(Get-Date).AddSeconds(120);do {Start-Sleep -Milliseconds 100;$result=Invoke-RestMethod "$base$poll" -Headers $headers;if((Get-Date)-gt $deadline){throw 'Command timeout'}}until($result.state -eq 'completed')}
        @{command=$name;args=$arguments;result=$result}|ConvertTo-Json -Depth 25 -Compress|Add-Content "$out/results.jsonl" -Encoding utf8
        if($result.status -ne 'succeeded'){throw "$name failed: $($result.code) $($result.message)"}
        return $result
    }
    $null=Command 'scene.new' @('CSharpFixedUpdate')
    foreach($name in @('FixedDriver','FixedTarget','FixedFault','FixedRemove')) {
        $null=Command 'object.create' @($name)
        $null=Command 'script.add' @($name,'FixedUpdateContractProbe')
    }
    foreach($probe in @('ExternalAwaitProbe','WorkerCallProbe','FailCleanupProbe','FailInitializedProbe','FailBeginProbe','DisableInBeginProbe')) {
        $null=Command 'object.create' @($probe)
        $null=Command 'script.add' @($probe,$probe)
    }
    $null=Command 'object.create' @('FixedCamera')
    $null=Command 'component.add' @('FixedCamera','CameraComponent')
    $null=Command 'object.property' @('FixedCamera','CameraComponent','m_isPrimary','true')
    $scene=Join-Path $project ('Assets/Scenes/FixedUpdate-'+(Split-Path $out -Leaf)+'.creator')
    $null=Command 'scene.save' @($scene)
    $sceneHash=(Get-FileHash $scene).Hash
    $null=Command 'play.foreground_override' @('on')
    $cycles=@()
    foreach($cycle in 1..2) {
        $null=Command 'play'
        $until=(Get-Date).AddSeconds(90)
        do {
            Start-Sleep -Milliseconds 200
            $state=(Command 'script.invoke' @('FixedUpdateContractProbe','State')).data.returnValue|ConvertFrom-Json
            if($state.failures -gt 0){throw 'Fixed update product assertions failed'}
            if($process.HasExited -or (Get-Date)-gt $until){throw 'Fixed update product timeout'}
        }until($state.completed -eq $cycle)
        $null=Command 'play.pause'
        $paused=(Command 'script.invoke' @('FixedUpdateContractProbe','State')).data.returnValue|ConvertFrom-Json
        Start-Sleep -Milliseconds 300
        $still=(Command 'script.invoke' @('FixedUpdateContractProbe','State')).data.returnValue|ConvertFrom-Json
        if($paused.driverUpdates -ne $still.driverUpdates){throw 'OnTickSimulate advanced while paused'}
        $null=Command 'play.resume'
        $until=(Get-Date).AddSeconds(10)
        do{Start-Sleep -Milliseconds 50;$resumed=(Command 'script.invoke' @('FixedUpdateContractProbe','State')).data.returnValue|ConvertFrom-Json;if((Get-Date)-gt $until){throw 'OnTickSimulate did not resume'}}until($resumed.driverUpdates -gt $still.driverUpdates)
        $null=Command 'stop'
        $until=(Get-Date).AddSeconds(30)
        do{Start-Sleep -Milliseconds 100;$play=(Command 'play.state').data;if((Get-Date)-gt $until){throw 'Stop timeout'}}while($play.pending -or $play.committed)
        $state=(Command 'script.invoke' @('FixedUpdateContractProbe','State')).data.returnValue|ConvertFrom-Json
        if($state.ended -ne $cycle*4 -or $state.removedEnded -ne $cycle -or $state.faultDisabled -ne $cycle){throw 'Teardown/generation assertions failed'}
        $cycles+=@{cycle=$cycle;state=$state}
    }
    $null=Command 'log.flush'
    $null=Command 'quit'
    if(!$process.WaitForExit(30000)){throw 'Editor did not close its log'}
    $log=Get-ChildItem (Join-Path $project 'Saved/Editor/Log') -File -Filter 'Editor_*.html' |
        Where-Object LastWriteTime -ge $started | Sort-Object LastWriteTime -Descending | Select-Object -First 1
    if(!$log){throw 'Fresh managed HTML log missing'}
    Copy-Item $log.FullName "$out/editor.html"
    $stdout=(Get-Content "$out/editor.html" -Raw) -replace '<[^>]+>',''

    foreach($kind in @('failinit','failbegin','disablebegin')) {
        if($stdout -match ("kind="+$kind+" hook=OnTickSimulate")){throw 'Failed startup received OnTickSimulate'}
    }
    foreach($mark in @('cleanup1','cleanup2','cleanup3')) {
        if([regex]::Matches($stdout,"kind=failcleanup hook=$mark").Count -ne 2){throw 'Standard C# cleanup callback missing'}
    }
    $game=[regex]::Matches($stdout,'\[LC5b\] point=begin tid=(\d+)')
    $external=[regex]::Matches($stdout,'\[LC5b\] point=external tid=(\d+)')
    $worker=[regex]::Matches($stdout,'\[LC5b\] point=worker tid=(\d+)')
    if($game.Count -ne 2 -or $external.Count -ne 2 -or $worker.Count -ne 2){throw 'Ordinary C# await evidence incomplete'}
    foreach($i in 0..1) {
        if($game[$i].Groups[1].Value -ne $external[$i].Groups[1].Value -or $game[$i].Groups[1].Value -eq $worker[$i].Groups[1].Value){throw 'C# await did not return to the game thread'}
    }
    if([regex]::Matches($stdout,"\[LC5c\] where=worker tid=\d+ alive=False name=''").Count -ne 2){throw 'Worker engine access guard failed'}
    if((Get-FileHash $scene).Hash -ne $sceneHash){throw 'Play changed authored scene'}
    @{result='FSHARP_FIXED_UPDATE_EDITOR_OK';scene=$scene;sceneSha256=$sceneHash;cycles=$cycles;commands=$script:sequence;hostHash=(Get-FileHash (Join-Path (Split-Path $exe) 'CreatorEditor.runtime.dll')).Hash;nativeBindingHash=(Get-FileHash (Join-Path (Split-Path (Split-Path $exe)) 'Managed/CreatorEngine.NativeBindings.dll')).Hash}|ConvertTo-Json -Depth 20|Set-Content "$out/result.json" -Encoding utf8
    "FSHARP_FIXED_UPDATE_EDITOR_OK evidence=$out"
} finally {if(!$process.WaitForExit(30000)){$process.Kill();$process.WaitForExit()}}
