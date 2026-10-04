param([string]$EditorExe='', [string]$ScenePath='')
$ErrorActionPreference='Stop'
$repo=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
if(!$EditorExe){$EditorExe=Join-Path $repo 'Bin/x64-Debug/Editor/CreatorEditor.exe'}
$exe=Get-Item -LiteralPath $EditorExe
# The launcher stays unchanged when host implementation TUs relink the runtime DLL.
$runtimePath=Join-Path $exe.DirectoryName 'CreatorEditor.runtime.dll'
$implementation=if(Test-Path -LiteralPath $runtimePath){Get-Item -LiteralPath $runtimePath}else{$exe}
$required=@('Editor/EngineEntry/EditorObjectOperations.cpp','Editor/EngineEntry/PhysicsShapeDocument.h','Editor/EngineEntry/Commands/SceneObjectCommands.cpp','Editor/EngineGUIWindow/InspectorWindow.cpp','Engine/SceneRuntime/PhysicsBodyComponent.h','Engine/SceneRuntime/Scene.cpp','Engine/SceneRuntime/PhysicsTransformPolicy.h','Engine/SceneRuntime/SceneManager.cpp','Engine/SceneRuntime/ScenePhysicsSimulation.cpp','Engine/SceneRuntime/Transform.cpp') | ForEach-Object {Get-Item (Join-Path $repo $_)}
if($required | Where-Object {$_.LastWriteTimeUtc -gt $implementation.LastWriteTimeUtc}){throw 'Fresh Editor build required; stale executable is not product evidence'}
if(Get-Process CreatorEditor -ErrorAction SilentlyContinue){throw 'An Editor is already running; the gate needs its own session'}
$id=[guid]::NewGuid().ToString('N')
$out=Join-Path $repo "Build/Obj/Phase19B2Editor/run-$id"
New-Item -ItemType Directory -Force $out | Out-Null
$process=Start-Process $exe.FullName -ArgumentList '--command-service','--console','--allow-user-code' -WorkingDirectory $exe.DirectoryName -WindowStyle Hidden -Environment @{CE_PHYSICS_B2_SHEAR='1'} -RedirectStandardOutput "$out/editor.out" -RedirectStandardError "$out/editor.err" -PassThru
$script:sequence=0
try {
    $endpoint=Join-Path $repo 'Dynamic_CPP/Library/CommandService/endpoint.json'
    $deadline=(Get-Date).AddSeconds(180)
    $info=$null
    do {
        if($process.HasExited){throw "Editor exited: $($process.ExitCode)"}
        if(Test-Path $endpoint){$candidate=Get-Content $endpoint -Raw|ConvertFrom-Json; if($candidate.pid -eq $process.Id){$info=$candidate;break}}
        Start-Sleep -Milliseconds 200
    } while((Get-Date) -lt $deadline)
    if(!$info){throw 'Owned HTTP endpoint unavailable'}
    $base="http://127.0.0.1:$($info.port)"
    $headers=@{Authorization="Bearer $($info.token)"}
    function Command([string]$name,[string[]]$arguments=@(),[switch]$Reject,[switch]$AllowPending) {
        $script:sequence++
        $mode=if($name -eq 'render.live.fence'){'async'}else{'sync'}
        $body=@{command=$name;args=@($arguments);mode=$mode;correlationId="physics-b2-$script:sequence"}|ConvertTo-Json -Compress
        try {
            $result=Invoke-RestMethod "$base/command" -Headers $headers -Method Post -ContentType 'application/json' -Body $body -TimeoutSec 120
        } catch {
            if ((!$Reject -and !$AllowPending) -or !$_.ErrorDetails.Message) { throw }
            $result=$_.ErrorDetails.Message | ConvertFrom-Json
            if ($result.schemaVersion -ne 1 -or $result.command -ne $name -or
                $result.status -notin @('invalid_arguments','preconditions_failed')) { throw }
        }
        if($result.operationId){
            $poll=$result.poll; $until=(Get-Date).AddSeconds(150)
            do {Start-Sleep -Milliseconds 60; $result=Invoke-RestMethod "$base$poll" -Headers $headers; if((Get-Date) -gt $until){throw 'Operation timeout'}} until($result.state -eq 'completed')
        }
        @{command=$name;args=@($arguments);result=$result}|ConvertTo-Json -Depth 30 -Compress|Add-Content "$out/results.jsonl" -Encoding utf8
        if($AllowPending){return $result}
        if($Reject){if($result.status -notin @('invalid_arguments','preconditions_failed')){throw "Expected argument/precondition rejection: $name"}}
        elseif($result.status -ne 'succeeded'){throw "$name failed: $($result.code) $($result.message)"}
        return $result
    }
    function CompletedDisplay([string]$target) {
        $fence=(Command 'render.live.fence' @('120')).data
        if([long]$fence.completedFrame -le [long]$fence.afterFrame){throw 'RenderThread completion did not advance'}

        $deadline=(Get-Date).AddSeconds(30)
        do {
            $state=(Command 'dx12.live').data
            $display=$state.display.$target
            if($state.enabled -and $state.ready -and $display.active -and $display.ready -and
               [long]$display.completedFrame -gt [long]$fence.afterFrame){
                return @{target=$target;fence=$fence;display=$display}
            }

            if((Get-Date) -gt $deadline){throw "Completed $target display did not advance after render fence"}
            Start-Sleep -Milliseconds 100
        } while($true)
    }
    $renderEvidence=@()
    $scene=if($ScenePath){[IO.Path]::GetFullPath($ScenePath)}else{"$repo/Dynamic_CPP/Assets/Scenes/PhysicsB2Player.creator"}
    $sceneHash=(Get-FileHash -LiteralPath $scene).Hash
    $null=Command 'scene.switch' @($scene)
    $loadedDeadline=(Get-Date).AddSeconds(90)
    do {
        Start-Sleep -Milliseconds 100
        $loaded=Command 'object.describe' @('B2Dynamic') -AllowPending
        if((Get-Date) -gt $loadedDeadline){throw 'B2 Scene activation timed out'}
    } until($loaded.status -eq 'succeeded')
    $initial=@{}
    foreach($name in @('B2Dynamic','B2Static','B2Kinematic','B2DynamicParent','B2StaticParent','B2KinematicParent')) {
        $initial[$name]=(Command 'object.describe' @($name)).data
    }
    $null=Command 'play.foreground_override' @('on')
    $null=Command 'play'
    $until=(Get-Date).AddSeconds(30)
    do {
        Start-Sleep -Milliseconds 100
        $stdout=[string](Get-Content "$out/editor.out" -Raw)
        $probes=@([regex]::Matches($stdout,'\[physics.player.b2\] (\{[^\r\n]+\})')|ForEach-Object {$_.Groups[1].Value|ConvertFrom-Json})
        if($probes|Where-Object {$_.failed -ne 0}){throw 'B2 Editor assertions failed'}
        if((Get-Date) -gt $until){throw 'B2 Editor probe timed out'}
    } until($probes.Count -eq 3)
    if($probes|Where-Object {$_.passed -ne 9 -or !$_.complete}){throw 'Incomplete Editor B2 assertions'}
    $renderEvidence+=CompletedDisplay 'game'
    $null=Command 'stop'
    $renderEvidence+=CompletedDisplay 'scene'
    foreach($name in $initial.Keys) {
        $restored=(Command 'object.describe' @($name)).data
        foreach($property in @('position','rotation','scale')) {
            if(($initial[$name].$property|ConvertTo-Json -Compress) -ne ($restored.$property|ConvertTo-Json -Compress)){throw "B2 Stop failed restore $name $property"}
        }
    }
    $firstProbes=$probes
    $null=Command 'play'
    $until=(Get-Date).AddSeconds(30)
    do {
        Start-Sleep -Milliseconds 100
        $stdout=[string](Get-Content "$out/editor.out" -Raw)
        $probes=@([regex]::Matches($stdout,'\[physics.player.b2\] (\{[^\r\n]+\})')|Select-Object -Skip 3|ForEach-Object {$_.Groups[1].Value|ConvertFrom-Json})
        if($probes|Where-Object {$_.failed -ne 0}){throw 'B2 Editor replay assertions failed'}
        if((Get-Date) -gt $until){throw 'B2 Editor replay timed out'}
    } until($probes.Count -eq 3)
    if($probes|Where-Object {$_.passed -ne 9 -or !$_.complete}){throw 'Incomplete replay assertions'}
    $renderEvidence+=CompletedDisplay 'game'
    $null=Command 'stop'
    $renderEvidence+=CompletedDisplay 'scene'
    foreach($name in $initial.Keys) {
        $restored=(Command 'object.describe' @($name)).data
        foreach($property in @('position','rotation','scale')) {
            if(($initial[$name].$property|ConvertTo-Json -Compress) -ne ($restored.$property|ConvertTo-Json -Compress)){throw "B2 second Stop failed restore $name $property"}
        }
    }
    $replayProbes=$probes
    $null=Command 'script.add' @('B2Dynamic','PhysicsB2ShearProbe')
    $null=Command 'play'
    $until=(Get-Date).AddSeconds(30)
    do {
        Start-Sleep -Milliseconds 100
        $failure=(Command 'play.state').data
        if((Get-Date) -gt $until){throw 'Shear failure did not return Editor to idle'}
    } until(!$failure.gameStart -and !$failure.committed -and !$failure.pending -and $failure.failureCount -ge 1)
    $stdout=[string](Get-Content "$out/editor.out" -Raw)
    if($stdout -notmatch '\[physics.player.b2.shear\] requested=true'){throw 'Editor shear request not executed'}
    if($failure.failureCount -lt 1 -or $failure.lastFailure -notmatch 'unsupported shear'){throw 'Missing Editor shear failure reason'}
    foreach($name in $initial.Keys) {
        $restored=(Command 'object.describe' @($name)).data
        foreach($property in @('position','rotation','scale')) {
            if(($initial[$name].$property|ConvertTo-Json -Compress) -ne ($restored.$property|ConvertTo-Json -Compress)){throw "B2 shear failure failed restore $name $property"}
        }
    }
    if((Get-FileHash -LiteralPath $scene).Hash -ne $sceneHash){throw 'Editor gate mutated the authored Scene'}

    @{result='PHYSICS_B2_EDITOR_OK';sceneImmutable=$true;renderEvidence=$renderEvidence;probes=$firstProbes;replay=$replayProbes;shearFailure=$failure;restoredEntities=6;playCycles=3;commands=$script:sequence}|ConvertTo-Json -Depth 10|Set-Content "$out/result.json" -Encoding utf8
    "PHYSICS_B2_EDITOR_OK evidence=$out"
    $null=Command 'quit'
} finally {
    if(!$process.WaitForExit(30000)){$process.Kill();$process.WaitForExit()}
}
