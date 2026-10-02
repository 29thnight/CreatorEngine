param([string]$EditorExe='', [switch]$ScriptProbe, [switch]$StepProbe, [switch]$MotionProbe, [switch]$DdolProbe)
$ErrorActionPreference='Stop'
$repo=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
if(!$EditorExe){$EditorExe=Join-Path $repo 'Bin/x64-Debug/Editor/CreatorEditor.exe'}
$exe=Get-Item -LiteralPath $EditorExe
# The launcher stays unchanged when host implementation TUs relink the runtime DLL.
$runtimePath=Join-Path $exe.DirectoryName 'CreatorEditor.runtime.dll'
$implementation=if(Test-Path -LiteralPath $runtimePath){Get-Item -LiteralPath $runtimePath}else{$exe}
$required=@('Editor/EngineEntry/EditorObjectOperations.cpp','Editor/EngineEntry/PhysicsShapeDocument.h','Editor/EngineEntry/Commands/SceneObjectCommands.cpp','Editor/EngineGUIWindow/InspectorWindow.cpp','Engine/SceneRuntime/PhysicsBodyComponent.h','Engine/SceneRuntime/CharacterMovementComponent.h','Engine/SceneRuntime/CharacterMovementComponent.cpp','Engine/SceneRuntime/PhysicsPrimitivePreview.h','Engine/SceneRuntime/EnhancedGizmoSceneBinding.cpp','Engine/SceneRuntime/CharacterMotionPolicy.h','Engine/SceneRuntime/ClrHost.cpp','Engine/SceneRuntime/ScenePhysicsSimulation.cpp','Engine/Physics/PhysicsScene.cpp') | ForEach-Object {Get-Item (Join-Path $repo $_)}
if($required | Where-Object {$_.LastWriteTimeUtc -gt $implementation.LastWriteTimeUtc}){throw 'Fresh Editor build required; stale executable is not product evidence'}
if(Get-Process CreatorEditor -ErrorAction SilentlyContinue){throw 'An Editor is already running; the gate needs its own session'}
$id=[guid]::NewGuid().ToString('N')
$out=Join-Path $repo "Build/Obj/Phase19C0/http-$id"
New-Item -ItemType Directory -Force $out | Out-Null
$valid=Join-Path $out 'shapes.json'
$scene=Join-Path $repo "Dynamic_CPP/Assets/Scenes/PhysicsCharacterGate-$id.creator"
$launchArguments=@('--command-service','--console')
if($ScriptProbe){$launchArguments+='--allow-user-code'}
$process=Start-Process $exe.FullName -ArgumentList $launchArguments -WorkingDirectory $exe.DirectoryName -WindowStyle Hidden -RedirectStandardOutput "$out/editor.out" -RedirectStandardError "$out/editor.err" -PassThru
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
    function Command([string]$name,[string[]]$arguments=@(),[switch]$Reject) {
        $script:sequence++
        $body=@{command=$name;args=@($arguments);mode='sync';correlationId="physics-character-$script:sequence"}|ConvertTo-Json -Compress
        try {
            $result=Invoke-RestMethod "$base/command" -Headers $headers -Method Post -ContentType 'application/json' -Body $body -TimeoutSec 120
        } catch {
            if (!$Reject -or !$_.ErrorDetails.Message) { throw }
            $result=$_.ErrorDetails.Message | ConvertFrom-Json
            if ($result.schemaVersion -ne 1 -or $result.command -ne $name -or
                $result.status -notin @('invalid_arguments','preconditions_failed')) { throw }
        }
        if($result.operationId){
            $poll=$result.poll; $until=(Get-Date).AddSeconds(90)
            do {Start-Sleep -Milliseconds 60; $result=Invoke-RestMethod "$base$poll" -Headers $headers; if((Get-Date) -gt $until){throw 'Operation timeout'}} until($result.state -eq 'completed')
        }
        @{command=$name;args=@($arguments);result=$result}|ConvertTo-Json -Depth 30 -Compress|Add-Content "$out/results.jsonl" -Encoding utf8
        if($Reject){if($result.status -notin @('invalid_arguments','preconditions_failed')){throw "Expected argument/precondition rejection: $name"}}
        elseif($result.status -ne 'succeeded'){throw "$name failed: $($result.code) $($result.message)"}
        return $result
    }

    [IO.File]::WriteAllText($valid,'[{"shapeId":12,"kind":0,"halfExtent":[20,0.5,20]}]')
    if($DdolProbe){
        $destination=Join-Path $repo "Dynamic_CPP/Assets/Scenes/PhysicsCharacterDestination-$id.creator"
        $null=Command 'scene.new' @("PhysicsCharacterDestination-$id")
        $null=Command 'object.create' @('DestinationMarker')
        $null=Command 'scene.save' @($destination)
    }
    $null=Command 'scene.new' @("PhysicsCharacterGate-$id")
    $null=Command 'object.create' @('CharacterGateFloor')
    $null=Command 'component.add' @('CharacterGateFloor','PhysicsBodyComponent')
    $null=Command 'object.property' @('CharacterGateFloor','PhysicsBodyComponent','m_motion','0')
    $null=Command 'object.transform' @('CharacterGateFloor','0','-0.5','0')
    $null=Command 'physics.shapes' @('CharacterGateFloor','PhysicsBodyComponent',$valid)
    if($StepProbe){
        foreach($obstacle in @(@('LowStep','2','0.1','0.1'),@('HighStep','6','0.4','0.4'))){
            $null=Command 'object.create' @($obstacle[0])
            $null=Command 'component.add' @($obstacle[0],'PhysicsBodyComponent')
            $null=Command 'object.property' @($obstacle[0],'PhysicsBodyComponent','m_motion','0')
            $null=Command 'object.transform' @($obstacle[0],$obstacle[1],$obstacle[2],'0')
            [IO.File]::WriteAllText($valid,('[{"shapeId":12,"kind":0,"halfExtent":[0.5,'+$obstacle[3]+',4]}]'))
            $null=Command 'physics.shapes' @($obstacle[0],'PhysicsBodyComponent',$valid)
        }
    }
    $null=Command 'object.create' @('CharacterGateActor')
    $null=Command 'component.add' @('CharacterGateActor','CharacterMovementComponent')
    $null=Command 'object.transform' @('CharacterGateActor','0','3','0')
    if($MotionProbe){
        $null=Command 'object.property' @('CharacterGateActor','CharacterMovementComponent','m_acceleration','6')
        $null=Command 'object.property' @('CharacterGateActor','CharacterMovementComponent','m_initialVelocity','0, 0, 0')
        $depthBefore=(Command 'undo.state').data.editUndo
        $null=Command 'object.property' @('CharacterGateActor','CharacterMovementComponent','m_acceleration','-1') -Reject
        if((Command 'undo.state').data.editUndo -ne $depthBefore){throw 'Rejected motion policy changed Undo history'}
        foreach($invalid in @(@('m_radius','-1'),@('m_contactOffset','0'),@('m_stepOffset','99'),@('m_slopeLimitCosine','2'),@('m_minimumDistance','-1'))){
            $null=Command 'object.property' @('CharacterGateActor','CharacterMovementComponent',$invalid[0],$invalid[1]) -Reject
        }
        if((Command 'undo.state').data.editUndo -ne $depthBefore){throw 'Rejected capsule authoring changed Undo history'}

    }
    $depth=(Command 'undo.state').data.editUndo
    $null=Command 'component.add' @('CharacterGateActor','PhysicsBodyComponent') -Reject
    $null=Command 'component.add' @('CharacterGateFloor','CharacterMovementComponent') -Reject
    if((Command 'undo.state').data.editUndo -ne $depth){throw 'Rejected physics ownership changed Undo history'}
    $null=Command 'character.velocity' @('CharacterGateActor','1','0','0') -Reject
    $null=Command 'character.teleport' @('CharacterGateActor','0','3','0') -Reject
    if($ScriptProbe){$null=Command 'script.add' @('CharacterGateActor','CharacterMovementProbe')}
    $null=Command 'scene.save' @($scene)
    if([IO.File]::ReadAllText($scene) -notmatch 'm_characterSchema:\s*1'){throw 'Character schema was not serialized'}
    $null=Command 'scene.load' @($scene)
    $initial=(Command 'object.describe' @('CharacterGateActor')).data.position
    $null=Command 'play.foreground_override' @('on')
    $null=Command 'play'
    $null=Command 'character.velocity' @('CharacterGateActor','1','0','0')
    $null=Command 'character.velocity' @('CharacterGateActor','nan','0','0') -Reject
    $null=Command 'object.property' @('CharacterGateActor','CharacterMovementComponent','m_radius','2') -Reject
    $until=(Get-Date).AddSeconds(15)
    do {
        Start-Sleep -Milliseconds 80
        $moved=(Command 'character.state' @('CharacterGateActor')).data
    } while((!$moved.below -or $moved.position[0] -lt 0.1) -and (Get-Date) -lt $until)
    if(!$moved.simulating -or !$moved.below -or $moved.position[0] -lt 0.1 -or [Math]::Abs($moved.footPosition[1]) -gt 0.08 -or [long]$moved.tick -le 0){throw 'Independent character did not move and ground'}
    if($StepProbe){
        $null=Command 'character.velocity' @('CharacterGateActor','2','0','0')
        $until=(Get-Date).AddSeconds(10)
        $onStep=$false; $passedStep=$false; $blocked=$false
        do {
            Start-Sleep -Milliseconds 60
            $stepState=(Command 'character.state' @('CharacterGateActor')).data
            if($stepState.position[0] -gt 1.5 -and $stepState.position[0] -lt 2.5 -and $stepState.footPosition[1] -gt 0.15){$onStep=$true}
            if($stepState.position[0] -gt 3.2){$passedStep=$true}
            if($stepState.position[0] -gt 4.7 -and $stepState.sides -and [Math]::Abs($stepState.actualDisplacement[0]) -lt 0.005){$blocked=$true}
        } while(!$blocked -and (Get-Date) -lt $until)
        if(!$onStep -or !$passedStep -or !$blocked -or $stepState.position[0] -gt 5.5){throw 'Authored low/high step product regression failed'}
        $stepEvidence=@{onStep=$onStep;passedLowStep=$passedStep;blockedHighStep=$blocked;state=$stepState}
    }
    if($ScriptProbe){
        $probe=(Command 'script.invoke' @('CharacterMovementProbe','Results')).data.returnValue | ConvertFrom-Json
        if($probe.completed -ne 1 -or $probe.failed -ne 0 -or $probe.passed -ne 18){throw "Character CLR probe incomplete or failed: $($probe|ConvertTo-Json -Compress)"}
    }
    if($MotionProbe){
        if($ScriptProbe){
            if((Command 'script.invoke' @('CharacterMovementProbe','JumpGrounded')).data.returnValue -ne 'None'){throw 'C# grounded jump failed'}
        } else {$null=Command 'character.jump' @('CharacterGateActor')}
        Start-Sleep -Milliseconds 70
        $airborne=(Command 'character.state' @('CharacterGateActor')).data
        if($airborne.below -or $airborne.fallVelocity -le 0){throw 'Queued jump did not rise'}
        $null=Command 'character.jump' @('CharacterGateActor') -Reject
        $until=(Get-Date).AddSeconds(10)
        do {Start-Sleep -Milliseconds 70; $landed=(Command 'character.state' @('CharacterGateActor')).data} while(!$landed.below -and (Get-Date) -lt $until)
        if(!$landed.below){throw 'Jump did not return to ground'}
        $null=Command 'character.force' @('CharacterGateActor','4','0','0','0.1')
        $null=Command 'character.force' @('CharacterGateActor','0','0','0','nan') -Reject
        $cancelled=(Command 'character.cancel' @('CharacterGateActor')).data
        if($cancelled.forced -or $cancelled.forcedRemaining -ne 0){throw 'Explicit force cancellation failed'}
        $null=Command 'character.force' @('CharacterGateActor','4','0','0','0.1')
        $until=(Get-Date).AddSeconds(5)
        do {Start-Sleep -Milliseconds 60; $expired=(Command 'character.state' @('CharacterGateActor')).data} while($expired.forced -and (Get-Date) -lt $until)
        if($expired.forced -or $expired.forcedRemaining -ne 0){throw 'Timed force did not expire'}
        $motionEvidence=@{airborne=$airborne;landed=$landed;expired=$expired}
    }
    if($DdolProbe){
        $beforeOwner=(Command 'object.describe' @('CharacterGateActor')).data
        $null=Command 'character.velocity' @('CharacterGateActor','1.25','0','0')
        $null=Command 'character.force' @('CharacterGateActor','3','0','0','10')
        $beforeTransfer=(Command 'character.state' @('CharacterGateActor')).data
        $null=Command 'scene.ddol' @('CharacterGateActor')
        $null=Command 'scene.switch' @($destination)
        $null=Command 'object.describe' @('DestinationMarker')
        $afterOwner=(Command 'object.describe' @('CharacterGateActor')).data
        $afterTransfer=(Command 'character.state' @('CharacterGateActor')).data
        if($beforeOwner.sceneId -eq $afterOwner.sceneId -or $beforeOwner.id -eq $afterOwner.id){throw 'DDOL did not change Scene owner handle'}
        $null=Command 'character.state' @($beforeOwner.id) -Reject
        if($ScriptProbe -and (Command 'script.invoke' @('CharacterMovementProbe','Transferred')).data.returnValue -ne 'transferred'){
            throw 'Captured CLR wrapper lost the persistent Entity after DDOL'
        }

        if(!$afterTransfer.simulating -or $afterTransfer.componentId -ne $beforeTransfer.componentId -or
            $afterTransfer.desiredVelocity[0] -ne 1.25 -or !$afterTransfer.forced -or
            $afterTransfer.forcedRemaining -le 0 -or $afterTransfer.forcedRemaining -gt $beforeTransfer.forcedRemaining){
            throw 'DDOL lost character identity, input or forced motion memory'
        }
        $elapsedForce=$beforeTransfer.forcedRemaining-$afterTransfer.forcedRemaining
        if($beforeOwner.layerId -ne $afterOwner.layerId -or
            [Math]::Abs(($afterTransfer.position[0]-$beforeTransfer.position[0])-3*$elapsedForce) -gt 0.1 -or
            [Math]::Abs($afterTransfer.movementVelocity[0]-1.25) -gt 0.15){throw 'DDOL reset pose, layer or accumulated motion'}
        Start-Sleep -Milliseconds 150
        $continued=(Command 'character.state' @('CharacterGateActor')).data
        if([long]$continued.tick -le [long]$afterTransfer.tick -or $continued.position[0] -le $afterTransfer.position[0] -or
            $continued.forcedRemaining -ge $afterTransfer.forcedRemaining -or $continued.below -or
            $continued.position[1] -ge $afterTransfer.position[1]){throw 'Transferred character did not resume fixed movement and gravity'}
        $null=Command 'character.cancel' @('CharacterGateActor')
        $null=Command 'object.enable' @('CharacterGateActor','off')
        $disabledTransfer=(Command 'character.state' @('CharacterGateActor')).data
        $null=Command 'scene.switch' @($destination)
        $disabledArrival=(Command 'character.state' @('CharacterGateActor')).data
        if($disabledArrival.simulating -or ($disabledArrival.position|ConvertTo-Json -Compress) -ne
            ($disabledTransfer.position|ConvertTo-Json -Compress)){throw 'Disabled DDOL character activated or moved'}
        $null=Command 'object.enable' @('CharacterGateActor','on')
        if(!(Command 'character.state' @('CharacterGateActor')).data.simulating){throw 'Disabled DDOL character could not activate in new Scene'}
        $ddolEvidence=@{destination=$destination;beforeOwner=$beforeOwner;afterOwner=$afterOwner;before=$beforeTransfer;after=$afterTransfer;continued=$continued;disabledBefore=$disabledTransfer;disabledAfter=$disabledArrival}
    }
    $teleport=(Command 'character.teleport' @('CharacterGateActor','0','5','0')).data
    if($teleport.position[1] -ne 5 -or $teleport.fallVelocity -ne 0 -or $teleport.below){throw 'Teleport did not clear motion result'}
    $null=Command 'object.enable' @('CharacterGateActor','off')
    $disabled=(Command 'character.state' @('CharacterGateActor')).data
    if($disabled.simulating){throw 'Disable retained an active SDK character'}
    $null=Command 'character.velocity' @('CharacterGateActor','0','0','0') -Reject
    Start-Sleep -Milliseconds 200
    $held=(Command 'character.state' @('CharacterGateActor')).data
    if(($held.position|ConvertTo-Json -Compress) -ne ($disabled.position|ConvertTo-Json -Compress)){throw 'Disabled character moved'}
    $null=Command 'object.enable' @('CharacterGateActor','on')
    if(!(Command 'character.state' @('CharacterGateActor')).data.simulating){throw 'Enable did not recreate SDK character'}
    $null=Command 'stop'
    $restored=(Command 'object.describe' @('CharacterGateActor')).data.position
    if(($restored|ConvertTo-Json -Compress) -ne ($initial|ConvertTo-Json -Compress)){throw 'Editor Stop did not restore character Transform'}
    $stopped=(Command 'character.state' @('CharacterGateActor')).data
    if($stopped.simulating -or [long]$stopped.tick -ne 0 -or $stopped.fallVelocity -ne 0 -or $stopped.desiredVelocity[0] -ne 0){throw 'Editor Stop did not reset character runtime state'}
    if($ScriptProbe){
        $retired=(Command 'script.invoke' @('CharacterMovementProbe','Retired')).data.returnValue
        if($retired -ne 'retired'){throw 'Captured CLR wrapper silently retargeted after Stop'}
    }
    $null=Command 'component.remove' @('CharacterGateActor','CharacterMovementComponent')
    $null=Command 'character.state' @('CharacterGateActor') -Reject
    @{result='PHYSICS_CHARACTER_HTTP_OK';scene=$scene;commands=$script:sequence;scriptProbe=[bool]$ScriptProbe;probe=$probe;stepProbe=[bool]$StepProbe;stepEvidence=$stepEvidence;motionProbe=[bool]$MotionProbe;motionEvidence=$motionEvidence;ddolProbe=[bool]$DdolProbe;ddolEvidence=$ddolEvidence;initial=$initial;moved=$moved;restored=$restored}|ConvertTo-Json -Depth 30|Set-Content "$out/result.json" -Encoding utf8
    Write-Output "PHYSICS_CHARACTER_HTTP_OK evidence=$out"
} finally {
    if(!$process.HasExited){$process.Kill();$process.WaitForExit()}
}
