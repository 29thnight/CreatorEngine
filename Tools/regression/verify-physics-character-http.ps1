param([string]$EditorExe='', [switch]$ScriptProbe, [switch]$StepProbe, [switch]$MotionProbe, [switch]$DdolProbe, [switch]$HierarchyProbe, [switch]$TransitionFailureProbe, [switch]$TerrainProbe)
$ErrorActionPreference='Stop'
if($TransitionFailureProbe){$HierarchyProbe=$true;$ScriptProbe=$true}
if($HierarchyProbe){$DdolProbe=$true}
if($StepProbe -and $HierarchyProbe){throw 'StepProbe requires the unscaled fixture; run hierarchy transition independently'}
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

    function SwitchScene([string]$path) {
        $request=Command 'scene.switch' @($path)
        if(!$request.data.activationRequested){throw 'Scene activation was not requested'}

        $deadline=(Get-Date).AddSeconds(120)
        do {
            Start-Sleep -Milliseconds 100
            $state=Command 'scene.load.status' @("$($request.data.requestId)")
            if((Get-Date) -gt $deadline){throw 'Scene activation timeout'}
        }until($state.data.complete)

        if($state.data.state -ne 'Ready' -or !$state.data.activationRequested){throw 'Scene activation did not reach Ready'}
        Start-Sleep -Milliseconds 100
    }
    function SamePosition($left,$right){
        if($left.Count -ne $right.Count){return $false}
        for($axis=0;$axis -lt $left.Count;$axis++){
            if([Math]::Abs($left[$axis]-$right[$axis]) -gt 0.0001){return $false}
        }
        return $true
    }

    [IO.File]::WriteAllText($valid,'[{"shapeId":12,"kind":0,"halfExtent":[20,0.5,20]}]')
    if($DdolProbe){
        $destination=Join-Path $repo "Dynamic_CPP/Assets/Scenes/PhysicsCharacterDestination-$id.creator"
        $null=Command 'scene.new' @("PhysicsCharacterDestination-$id")
        $null=Command 'object.create' @('DestinationMarker')
        $null=Command 'scene.save' @($destination)
    }
    if($TransitionFailureProbe){
        $failureDestination=Join-Path $repo "Dynamic_CPP/Assets/Scenes/PhysicsRejectedDestination-$id.creator"
        $null=Command 'scene.new' @("PhysicsRejectedDestination-$id")
        $null=Command 'object.create' @('RejectedParent')
        $null=Command 'object.create' @('RejectedBody')
        $null=Command 'component.add' @('RejectedBody','PhysicsBodyComponent')
        $null=Command 'object.property' @('RejectedBody','PhysicsBodyComponent','m_motion','0')
        $null=Command 'object.parent' @('RejectedBody','RejectedParent')
        $null=Command 'object.transform' @('RejectedBody','0','0','0','0','45','0')
        $null=Command 'object.transform' @('RejectedParent','0','0','0','0','0','0','2','1','1')
        $null=Command 'scene.save' @($failureDestination)
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
    if($HierarchyProbe){
        $null=Command 'object.create' @('PersistentParent')
        $null=Command 'object.transform' @('PersistentParent','4','0','2','0','30','0','2','2','2')
        $null=Command 'object.parent' @('CharacterGateActor','PersistentParent')
        $null=Command 'object.create' @('PersistentBranch')
        $null=Command 'object.transform' @('PersistentBranch','7','0','2')
        $null=Command 'object.parent' @('PersistentBranch','PersistentParent')
        $null=Command 'object.create' @('PersistentDisabledCharacter')
        $null=Command 'object.transform' @('PersistentDisabledCharacter','10','5','2')
        $null=Command 'component.add' @('PersistentDisabledCharacter','CharacterMovementComponent')
        $null=Command 'object.property' @('PersistentDisabledCharacter','CharacterMovementComponent','m_initialVelocity','2, 0, 0')
        $null=Command 'entity.layer' @('PersistentDisabledCharacter','Enemy')
        $null=Command 'object.parent' @('PersistentDisabledCharacter','PersistentBranch')
        $null=Command 'object.enable' @('PersistentDisabledCharacter','off')
        $null=Command 'object.create' @('PersistentDisabledBody')
        $null=Command 'object.transform' @('PersistentDisabledBody','13','5','2')
        $null=Command 'component.add' @('PersistentDisabledBody','PhysicsBodyComponent')
        $null=Command 'entity.layer' @('PersistentDisabledBody','Ground')
        $null=Command 'object.parent' @('PersistentDisabledBody','PersistentBranch')
        $null=Command 'object.enable' @('PersistentDisabledBody','off')
    }
    $null=Command 'scene.save' @($scene)
    if([IO.File]::ReadAllText($scene) -notmatch 'm_characterSchema:\s*1'){throw 'Character schema was not serialized'}
    $null=Command 'scene.new' @('CharacterReloadSentinel')
    SwitchScene $scene
    $initial=(Command 'object.describe' @('CharacterGateActor')).data.position
    if($HierarchyProbe){
        $hierarchyNames=@('PersistentParent','PersistentBranch','PersistentDisabledCharacter','PersistentDisabledBody')
        $hierarchyInitial=@{}
        foreach($name in $hierarchyNames){$hierarchyInitial[$name]=(Command 'object.describe' @($name)).data}
    }

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
        # Step rejection can leave the capsule briefly airborne against the wall.
        # Start the independent jump scenario on the flat floor with no input.
        $null=Command 'character.velocity' @('CharacterGateActor','0','0','0')
        $null=Command 'character.teleport' @('CharacterGateActor','0','3','0')
        $until=(Get-Date).AddSeconds(10)
        do {
            Start-Sleep -Milliseconds 70
            $grounded=(Command 'character.state' @('CharacterGateActor')).data
        }while(!$grounded.below -and (Get-Date) -lt $until)

        if(!$grounded.below){throw 'Jump scenario did not settle on the flat floor'}

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
        if($HierarchyProbe){
            $hierarchyBefore=@{}
            foreach($name in $hierarchyNames){$hierarchyBefore[$name]=(Command 'object.describe' @($name)).data}
            $disabledChildBefore=(Command 'character.state' @('PersistentDisabledCharacter')).data
        }
        $null=Command 'scene.ddol' @('CharacterGateActor')
        SwitchScene $destination
        $null=Command 'object.describe' @('DestinationMarker')
        $afterOwner=(Command 'object.describe' @('CharacterGateActor')).data
        $afterTransfer=(Command 'character.state' @('CharacterGateActor')).data
        if($HierarchyProbe){
            $hierarchyAfter=@{}
            foreach($name in $hierarchyNames){
                $before=$hierarchyBefore[$name]
                $after=(Command 'object.describe' @($name)).data
                $hierarchyAfter[$name]=$after
                if($after.sceneId -ne $afterOwner.sceneId -or $after.id -eq $before.id -or
                    $after.layerId -ne $before.layerId -or $after.enabled -ne $before.enabled){throw "Hierarchy membership/layer/activation lost: $name"}
                foreach($field in @('position','scale','rotation')){
                    for($axis=0;$axis -lt $before.$field.Count;$axis++){
                        if([Math]::Abs($before.$field[$axis]-$after.$field[$axis]) -gt 0.0001){throw "Hierarchy world $field changed: $name"}
                    }
                }
                if(($before.components.id|ConvertTo-Json -Compress) -ne ($after.components.id|ConvertTo-Json -Compress)){throw "Hierarchy component identity changed: $name"}
                $null=Command 'character.state' @($before.id) -Reject
            }
            if($afterOwner.parent -ne $hierarchyAfter.PersistentParent.id -or
                $hierarchyAfter.PersistentBranch.parent -ne $hierarchyAfter.PersistentParent.id -or
                $hierarchyAfter.PersistentDisabledCharacter.parent -ne $hierarchyAfter.PersistentBranch.id -or
                $hierarchyAfter.PersistentDisabledBody.parent -ne $hierarchyAfter.PersistentBranch.id){throw 'DDOL hierarchy parent links were not remapped'}
            if(@($hierarchyAfter.PersistentParent.children).Count -ne 2 -or
                @($hierarchyAfter.PersistentBranch.children).Count -ne 2 -or
                $afterOwner.id -notin $hierarchyAfter.PersistentParent.children -or
                $hierarchyAfter.PersistentBranch.id -notin $hierarchyAfter.PersistentParent.children -or
                $hierarchyAfter.PersistentDisabledCharacter.id -notin $hierarchyAfter.PersistentBranch.children -or
                $hierarchyAfter.PersistentDisabledBody.id -notin $hierarchyAfter.PersistentBranch.children){throw 'DDOL hierarchy children were lost or duplicated'}
            $child=(Command 'character.state' @('PersistentDisabledCharacter')).data
            if($child.simulating -or $child.componentId -ne $disabledChildBefore.componentId -or
                $child.desiredVelocity[0] -ne 2 -or !(SamePosition $child.position $disabledChildBefore.position)){throw 'Disabled grandchild runtime state lost'}
            $null=Command 'object.enable' @('PersistentDisabledCharacter','on')
            if(!(Command 'character.state' @('PersistentDisabledCharacter')).data.simulating){throw 'Grandchild SDK controller not recreated'}
            $null=Command 'object.enable' @('PersistentDisabledCharacter','off')
            $hierarchyEvidence=@{before=$hierarchyBefore;after=$hierarchyAfter;disabledCharacterBefore=$disabledChildBefore;disabledCharacterAfter=$child}
        }

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
        SwitchScene $destination
        $disabledArrival=(Command 'character.state' @('CharacterGateActor')).data
        if($disabledArrival.simulating -or !(SamePosition $disabledArrival.position $disabledTransfer.position)){throw 'Disabled DDOL character activated or moved'}
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
    if($HierarchyProbe){
        $hierarchyRestored=@{}
        foreach($name in $hierarchyNames){
            $before=$hierarchyInitial[$name]
            $after=(Command 'object.describe' @($name)).data
            $hierarchyRestored[$name]=$after
            if($after.enabled -ne $before.enabled -or $after.layerId -ne $before.layerId){throw "Stop hierarchy activation/layer differs: $name"}
            foreach($field in @('position','scale','rotation')){
                for($axis=0;$axis -lt $before.$field.Count;$axis++){
                    if([Math]::Abs($before.$field[$axis]-$after.$field[$axis]) -gt 0.0001){throw "Stop hierarchy world $field differs: $name"}
                }
            }
        }
        $actorRestored=(Command 'object.describe' @('CharacterGateActor')).data
        if($actorRestored.parent -ne $hierarchyRestored.PersistentParent.id -or
            $hierarchyRestored.PersistentBranch.parent -ne $hierarchyRestored.PersistentParent.id -or
            $hierarchyRestored.PersistentDisabledCharacter.parent -ne $hierarchyRestored.PersistentBranch.id -or
            $hierarchyRestored.PersistentDisabledBody.parent -ne $hierarchyRestored.PersistentBranch.id){throw 'Stop did not restore parent relationships'}
        $null=Command 'play'
        $childStopped=(Command 'character.state' @('PersistentDisabledCharacter')).data
        if($childStopped.simulating -or [long]$childStopped.tick -ne 0 -or $childStopped.desiredVelocity[0] -ne 2){throw 'RePlay grandchild runtime definition differs'}
        $null=Command 'object.enable' @('PersistentDisabledCharacter','on')
        if(!(Command 'character.state' @('PersistentDisabledCharacter')).data.simulating){throw 'Restored grandchild could not activate in RePlay'}
        $null=Command 'stop'
        if((Command 'object.describe' @('PersistentDisabledCharacter')).data.enabled){throw 'Second Stop lost authored disabled state'}
        $hierarchyEvidence.restored=$hierarchyRestored
    }
    $stopped=(Command 'character.state' @('CharacterGateActor')).data
    if($stopped.simulating -or [long]$stopped.tick -ne 0 -or $stopped.fallVelocity -ne 0 -or $stopped.desiredVelocity[0] -ne 0){throw 'Editor Stop did not reset character runtime state'}
    if($ScriptProbe){
        $retired=(Command 'script.invoke' @('CharacterMovementProbe','Retired')).data.returnValue
        if($retired -ne 'retired'){throw 'Captured CLR wrapper silently retargeted after Stop'}
    }
    if($TransitionFailureProbe){
        $names=@('CharacterGateActor','PersistentParent','PersistentBranch','PersistentDisabledCharacter','PersistentDisabledBody','CharacterGateFloor')
        $authored=@{}

        foreach($name in $names){$authored[$name]=(Command 'object.describe' @($name)).data}

        $sourceHash=(Get-FileHash -LiteralPath $scene).Hash
        $destinationHash=(Get-FileHash -LiteralPath $failureDestination).Hash
        $failuresBefore=(Command 'play.state').data.failureCount
        $null=Command 'play'
        Start-Sleep -Milliseconds 300
        $null=Command 'character.velocity' @('CharacterGateActor','1.25','0','0')
        $null=Command 'character.force' @('CharacterGateActor','3','0','0','10')
        $null=Command 'scene.ddol' @('CharacterGateActor')
        $liveOwner=(Command 'object.describe' @('CharacterGateActor')).data
        $liveState=(Command 'character.state' @('CharacterGateActor')).data
        if(!$liveState.simulating -or !$liveState.forced -or $liveState.tick -le 0){throw 'Failure source did not simulate before transfer'}

        $lifecycleBefore=(Command 'script.invoke' @('CharacterMovementProbe','Lifecycle')).data.returnValue|ConvertFrom-Json
        $null=Command 'scene.switch' @($failureDestination)
        $until=(Get-Date).AddSeconds(30)

        do{
            Start-Sleep -Milliseconds 100
            $failure=(Command 'play.state').data
            if((Get-Date) -gt $until){throw 'Rejected destination did not recover Editor'}
        }until(!$failure.gameStart -and !$failure.committed -and !$failure.pending -and $failure.failureCount -gt $failuresBefore)

        if($failure.lastFailure -notmatch 'unsupported shear'){throw 'Wrong destination failure cause'}
        $lifecycleAfter=(Command 'script.invoke' @('CharacterMovementProbe','Lifecycle')).data.returnValue|ConvertFrom-Json
        if($lifecycleAfter.added -lt $lifecycleBefore.added+1 -or $lifecycleAfter.removed -lt $lifecycleBefore.removed+2){throw 'DDOL failure detach/destroy and restored scene addition evidence missing'}
        $failureOutput=[string](Get-Content "$out/editor.out" -Raw)
        $ownership=[regex]::Match($failureOutput,'\[physics.scene.activation.failed\] transferredPersistent=(\d+)')
        if(!$ownership.Success -or [int]$ownership.Groups[1].Value -lt 1){throw 'No live DDOL owner in failed destination'}
        if((Command 'script.invoke' @('CharacterMovementProbe','Retired')).data.returnValue -ne 'retired'){throw 'Failed transition retained or retargeted captured wrapper'}

        $failureRestored=@{}

        foreach($name in $names){
            $before=$authored[$name]
            $after=(Command 'object.describe' @($name)).data
            $failureRestored[$name]=$after
            if($before.enabled -ne $after.enabled -or $before.layerId -ne $after.layerId){throw "Failure restore activation/layer mismatch: $name"}

            foreach($field in @('position','rotation','scale')){
                if(!(SamePosition $before.$field $after.$field)){throw "Failure restore $field mismatch: $name"}
            }

            if(($before.components.id|ConvertTo-Json -Compress) -ne ($after.components.id|ConvertTo-Json -Compress)){throw "Failure restore component identity mismatch: $name"}
        }

        if($failureRestored.CharacterGateActor.parent -ne $failureRestored.PersistentParent.id -or
            $failureRestored.PersistentBranch.parent -ne $failureRestored.PersistentParent.id -or
            $failureRestored.PersistentDisabledCharacter.parent -ne $failureRestored.PersistentBranch.id -or
            $failureRestored.PersistentDisabledBody.parent -ne $failureRestored.PersistentBranch.id){throw 'Failure did not restore parent relationships'}

        $null=Command 'character.state' @($liveOwner.id) -Reject
        $null=Command 'object.describe' @('RejectedBody') -Reject
        $null=Command 'scene.hierarchycheck'
        $idle=(Command 'character.state' @('CharacterGateActor')).data
        if($idle.simulating -or $idle.tick -ne 0 -or $idle.forced -or $idle.fallVelocity -ne 0 -or $idle.desiredVelocity[0] -ne 0){throw 'Failure restore retained runtime character state'}
        $null=Command 'play'
        Start-Sleep -Milliseconds 300
        $recovered=(Command 'character.state' @('CharacterGateActor')).data
        if(!$recovered.simulating -or $recovered.tick -le 0){throw 'Failure recovery could not replay authored source'}
        $null=Command 'stop'

        $until=(Get-Date).AddSeconds(30)
        do{
            Start-Sleep -Milliseconds 100
            $stopped=(Command 'play.state').data
            if((Get-Date) -gt $until){throw 'Recovery replay Stop did not complete'}
        }until(!$stopped.gameStart -and !$stopped.committed -and !$stopped.pending)

        foreach($name in $names){
            $after=(Command 'object.describe' @($name)).data
            foreach($field in @('position','rotation','scale')){
                if(!(SamePosition $authored[$name].$field $after.$field)){throw "Recovery replay Stop mismatch: $name"}
            }
        }

        if((Get-FileHash -LiteralPath $scene).Hash -ne $sourceHash -or (Get-FileHash -LiteralPath $failureDestination).Hash -ne $destinationHash){throw 'Runtime transition changed authored files'}
        $transitionFailureEvidence=@{destination=$failureDestination;sourceState=$liveState;failure=$failure;transferredPersistent=[int]$ownership.Groups[1].Value;lifecycleBefore=$lifecycleBefore;lifecycleAfter=$lifecycleAfter;restored=$failureRestored;replay=$recovered;authoredFilesImmutable=$true}
    }

    $null=Command 'component.remove' @('CharacterGateActor','CharacterMovementComponent')
    $null=Command 'character.state' @('CharacterGateActor') -Reject
    $terrainEvidence=@()
    if($TerrainProbe){
        foreach($case in @(@{name='gentle';angle=20;limit=0.70710678},@{name='steep';angle=60;limit=0.70710678},@{name='unrestricted';angle=60;limit=0})){
            $null=Command 'scene.new' @("CharacterTerrain-$($case.name)-$id")
            $height=(10*[Math]::Tan($case.angle*[Math]::PI/180)).ToString('R',[Globalization.CultureInfo]::InvariantCulture)
            $input=Join-Path $out "$($case.name)-mesh.txt"
            [IO.File]::WriteAllText($input,"4 0 0 -4 0 0 4 10 $height -4 10 $height 4 2 0 1 2 1 3 2")
            $relative="PhysicsVerification/CharacterTerrain-$($case.name)-$id.cegeometry"
            New-Item -ItemType Directory -Force (Join-Path $repo 'Dynamic_CPP/Assets/PhysicsVerification')|Out-Null
            $geometry=(Command 'geometry.create' @($relative,'mesh',$input)).data

            foreach($name in @('TerrainFloor','TerrainRamp')){
                $null=Command 'object.create' @($name)
                $null=Command 'component.add' @($name,'PhysicsBodyComponent')
                $null=Command 'object.property' @($name,'PhysicsBodyComponent','m_motion','0')
            }

            $null=Command 'object.transform' @('TerrainFloor','0','-0.5','0')
            [IO.File]::WriteAllText($valid,'[{"shapeId":1,"kind":0,"halfExtent":[30,0.5,30]}]')
            $null=Command 'physics.shapes' @('TerrainFloor','PhysicsBodyComponent',$valid)
            $shape=@(@{shapeId=1;kind=4;geometryAsset=$geometry.uuid;geometryRevision=1})
            [IO.File]::WriteAllText($valid,(ConvertTo-Json -InputObject $shape -Compress))
            $null=Command 'physics.shapes' @('TerrainRamp','PhysicsBodyComponent',$valid)
            $null=Command 'object.create' @('TerrainCharacter')
            $null=Command 'component.add' @('TerrainCharacter','CharacterMovementComponent')
            $null=Command 'object.transform' @('TerrainCharacter','-2','1.05','0')
            $limit=([double]$case.limit).ToString('R',[Globalization.CultureInfo]::InvariantCulture)
            $null=Command 'object.property' @('TerrainCharacter','CharacterMovementComponent','m_slopeLimitCosine',$limit)
            $terrainScene=Join-Path $out "$($case.name).creator"
            $null=Command 'scene.save' @($terrainScene)
            $null=Command 'scene.new' @('TerrainReloadSentinel')
            SwitchScene $terrainScene
            $authored=(Command 'object.describe' @('TerrainCharacter')).data.position
            $null=Command 'play'
            $deadline=(Get-Date).AddSeconds(15)
            do {
                Start-Sleep -Milliseconds 50
                $state=(Command 'character.state' @('TerrainCharacter')).data
                if((Get-Date) -gt $deadline){throw 'Terrain grounding timeout'}
            }until($state.below -and [long]$state.tick -ge 30)

            $null=Command 'character.velocity' @('TerrainCharacter','2','0','0')
            $start=(Command 'character.state' @('TerrainCharacter')).data
            do {
                Start-Sleep -Milliseconds 50
                $state=(Command 'character.state' @('TerrainCharacter')).data
                if((Get-Date) -gt $deadline){throw 'Terrain traversal timeout'}
            }until([long]$state.tick-[long]$start.tick -ge 150)

            if($case.name -eq 'gentle' -and ($state.position[0] -le 2 -or $state.footPosition[1] -le 0.5)){throw 'Walkable mesh slope did not climb'}
            if($case.name -eq 'steep' -and ($state.position[0] -ge 1 -or $state.footPosition[1] -ge 1)){throw 'Nonwalkable mesh slope did not block ascent'}
            if($case.name -eq 'unrestricted' -and $state.position[0] -le $terrainEvidence[1].state.position[0]+0.5){throw 'Zero slope limit did not disable slope rejection'}

            $null=Command 'stop'
            $restoredTerrain=(Command 'object.describe' @('TerrainCharacter')).data.position
            if(!(SamePosition $authored $restoredTerrain)){throw 'Terrain Stop did not restore authored pose'}
            $terrainEvidence+=@{name=$case.name;angle=$case.angle;limit=$case.limit;scene=$terrainScene;geometry=$geometry;start=$start;state=$state;restored=$restoredTerrain}
        }
    }
    @{terrainProbe=[bool]$TerrainProbe;terrainEvidence=$terrainEvidence;result='PHYSICS_CHARACTER_HTTP_OK';transitionFailureProbe=[bool]$TransitionFailureProbe;transitionFailureEvidence=$transitionFailureEvidence;scene=$scene;commands=$script:sequence;scriptProbe=[bool]$ScriptProbe;probe=$probe;stepProbe=[bool]$StepProbe;stepEvidence=$stepEvidence;motionProbe=[bool]$MotionProbe;motionEvidence=$motionEvidence;ddolProbe=[bool]$DdolProbe;ddolEvidence=$ddolEvidence;hierarchyProbe=[bool]$HierarchyProbe;hierarchyEvidence=$hierarchyEvidence;initial=$initial;moved=$moved;restored=$restored}|ConvertTo-Json -Depth 30|Set-Content "$out/result.json" -Encoding utf8
    Write-Output "PHYSICS_CHARACTER_HTTP_OK evidence=$out"
} finally {
    if(!$process.HasExited){$process.Kill();$process.WaitForExit()}
}
