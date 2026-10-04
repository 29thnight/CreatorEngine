param([string]$EditorExe='')
$ErrorActionPreference='Stop'
$repo=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
if(!$EditorExe){$EditorExe=Join-Path $repo 'Bin/x64-Release/Editor/CreatorEditor.exe'}
$exe=Get-Item -LiteralPath $EditorExe
# The launcher stays unchanged when host implementation TUs relink the runtime DLL.
$runtimePath=Join-Path $exe.DirectoryName 'CreatorEditor.runtime.dll'
$implementation=if(Test-Path -LiteralPath $runtimePath){Get-Item -LiteralPath $runtimePath}else{$exe}
$required=@('Editor/EngineEntry/EditorProjectOperations.cpp','Editor/EngineEntry/Commands/AssetAuthoringCommands.cpp','Engine/SceneRuntime/SceneManager.cpp','Editor/EngineEntry/EditorObjectOperations.cpp','Editor/EngineEntry/PhysicsShapeDocument.h','Editor/EngineEntry/Commands/SceneObjectCommands.cpp','Editor/EngineGUIWindow/InspectorWindow.cpp','Engine/SceneRuntime/PhysicsBodyComponent.h','Engine/SceneRuntime/CharacterMovementComponent.h','Engine/SceneRuntime/CharacterMovementComponent.cpp','Engine/SceneRuntime/PhysicsPrimitivePreview.h','Engine/SceneRuntime/EnhancedGizmoSceneBinding.cpp','Engine/SceneRuntime/CharacterMotionPolicy.h','Engine/SceneRuntime/ClrHost.cpp','Engine/SceneRuntime/ScenePhysicsSimulation.cpp','Engine/Physics/PhysicsScene.cpp') | ForEach-Object {Get-Item (Join-Path $repo $_)}
if($required | Where-Object {$_.LastWriteTimeUtc -gt $implementation.LastWriteTimeUtc}){throw 'Fresh Editor build required; stale executable is not product evidence'}
if(Get-Process CreatorEditor -ErrorAction SilentlyContinue){throw 'An Editor is already running; the gate needs its own session'}
$id=[guid]::NewGuid().ToString('N')
$out=Join-Path $repo "Build/Obj/Phase19L0Editor/http-$id"
New-Item -ItemType Directory -Force $out | Out-Null
$valid=Join-Path $out 'shapes.json'
$scene=Join-Path $repo "Dynamic_CPP/Assets/Scenes/PhysicsLayerGate-$id.creator"
$launchArguments=@('--command-service','--console')
$launchArguments+='--allow-user-code'
$process=Start-Process $exe.FullName -ArgumentList $launchArguments -WorkingDirectory $exe.DirectoryName -WindowStyle Hidden -RedirectStandardOutput "$out/editor.out" -RedirectStandardError "$out/editor.err" -PassThru
$script:sequence=0
$layerFile=Join-Path $repo 'Dynamic_CPP/ProjectSetting/Layers.celayers'
$originalLayers=[IO.File]::ReadAllBytes($layerFile)
[IO.File]::WriteAllBytes((Join-Path $out 'original.celayers'),$originalLayers)
$script:checks=0
$evidence=@{}
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
        $body=@{command=$name;args=@($arguments);mode='sync';correlationId="physics-layer-$script:sequence"}|ConvertTo-Json -Compress
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

    function Check([bool]$value,[string]$message) {
        if(!$value){throw $message}

        $script:checks++
    }

    function BodyState {
        return (Command 'script.invoke' @('PhysicsLayerProbe','BodyState')).data.returnValue | ConvertFrom-Json
    }

    function ResetActors {
        Check ((Command 'script.invoke' @('PhysicsLayerProbe','ResetBody')).data.returnValue -eq 'None') 'Body reset rejected'
        $null=Command 'character.teleport' @('LayerGateCharacter','-4','4','0')
    }

    function WaitActors([bool]$grounded) {
        $until=(Get-Date).AddSeconds(15)

        do {
            Start-Sleep -Milliseconds 100
            $body=BodyState
            $character=(Command 'character.state' @('LayerGateCharacter')).data
            $ready=if($grounded){
                $body.y -gt 0.45 -and $body.y -lt 0.6 -and [Math]::Abs($body.velocityY) -lt 0.1 -and
                $character.below -and [Math]::Abs($character.footPosition[1]) -lt 0.08
            } else {
                $body.y -lt -2 -and $character.position[1] -lt -2 -and !$character.below
            }

            if((Get-Date) -gt $until){throw "Actor collision state timeout grounded=$grounded body=$($body|ConvertTo-Json -Compress) character=$($character|ConvertTo-Json -Compress)"}
        } until($ready)

        Check ($character.simulating -and [long]$character.tick -gt 0) 'Character did not execute fixed simulation'

        return @{body=$body;character=$character}
    }

    function CheckFiles {
        Check ((Get-FileHash $layerFile).Hash -eq $baselineLayerHash) 'Play changed authored layer file'
        Check ((Get-FileHash $scene).Hash -eq $baselineSceneHash) 'Play changed saved scene'
        Check ((Get-FileHash $tagFile).Hash -eq $baselineTagHash) 'Physics policy changed tags-only authoring'
    }

    function StopAndCheck {
        $null=Command 'stop'
        $until=(Get-Date).AddSeconds(15)

        do {
            Start-Sleep -Milliseconds 100
            $state=(Command 'play.state').data

            if((Get-Date) -gt $until){throw 'Stop did not reach idle'}
        } while($state.gameStart -or $state.committed -or $state.pending)

        foreach($name in $initial.Keys){
            $restored=(Command 'object.describe' @($name)).data

            foreach($field in @('position','rotation','scale','layerId','enabled')){
                Check (($restored.$field|ConvertTo-Json -Compress) -eq ($initial[$name].$field|ConvertTo-Json -Compress)) "Stop did not restore $name $field"
            }

            Check (($restored.components.id|ConvertTo-Json -Compress) -eq ($initial[$name].components.id|ConvertTo-Json -Compress)) "Stop changed component identity $name"
        }

        $idle=(Command 'character.state' @('LayerGateCharacter')).data
        Check (!$idle.simulating -and [long]$idle.tick -eq 0) 'Stop retained character simulation'
        CheckFiles
    }

    $null=Command 'scene.new' @("PhysicsLayerGate-$id")
    $null=Command 'object.create' @('LayerGateFloor')
    $null=Command 'component.add' @('LayerGateFloor','PhysicsBodyComponent')
    $null=Command 'object.property' @('LayerGateFloor','PhysicsBodyComponent','m_motion','0')
    $null=Command 'object.transform' @('LayerGateFloor','0','-0.5','0')
    [IO.File]::WriteAllText($valid,'[{"shapeId":12,"kind":0,"halfExtent":[20,0.5,20]}]')
    $null=Command 'physics.shapes' @('LayerGateFloor','PhysicsBodyComponent',$valid)
    $null=Command 'entity.layer' @('LayerGateFloor','Default')
    $null=Command 'object.create' @('LayerGateBody')
    $null=Command 'component.add' @('LayerGateBody','PhysicsBodyComponent')
    $null=Command 'object.transform' @('LayerGateBody','4','4','0')
    $null=Command 'entity.layer' @('LayerGateBody','Enemy')
    $null=Command 'script.add' @('LayerGateBody','PhysicsLayerProbe')
    $null=Command 'object.create' @('LayerGateCharacter')
    $null=Command 'component.add' @('LayerGateCharacter','CharacterMovementComponent')
    $null=Command 'object.transform' @('LayerGateCharacter','-4','4','0')
    $null=Command 'entity.layer' @('LayerGateCharacter','Player')
    $null=Command 'layer.collision' @('Default','Enemy','true')
    $null=Command 'layer.collision' @('Default','Player','true')
    $baselineCatalog=(Command 'layer.list').data
    $null=Command 'scene.save' @($scene)
    $initial=@{}

    foreach($name in @('LayerGateFloor','LayerGateBody','LayerGateCharacter')){
        $initial[$name]=(Command 'object.describe' @($name)).data
    }

    $tagFile=Join-Path $repo 'Dynamic_CPP/ProjectSetting/TagManager.asset'
    $baselineLayerHash=(Get-FileHash $layerFile).Hash
    $baselineSceneHash=(Get-FileHash $scene).Hash
    $baselineTagHash=(Get-FileHash $tagFile).Hash

    $null=Command 'layer.collision' @('Default','Enemy','false')
    Check ((Get-FileHash $layerFile).Hash -ne $baselineLayerHash) 'Edit policy change was not persisted'
    Check ((Command 'undo').data.changed) 'Edit policy Undo missing'
    CheckFiles
    Check ((Command 'redo').data.changed) 'Edit policy Redo missing'
    Check ((Get-FileHash $layerFile).Hash -ne $baselineLayerHash) 'Edit policy Redo was not persisted'
    Check ((Command 'undo').data.changed) 'Edit policy second Undo missing'
    CheckFiles

    $null=Command 'play.foreground_override' @('on')
    $null=Command 'play'
    $evidence.grounded=WaitActors $true
    $null=Command 'play.pause'
    $beforeUndo=(Command 'undo.state').data
    $bodyIdentity=$evidence.grounded.body.componentId

    $renamed=(Command 'layer.rename' @('Enemy',"LayerGateEnemy-$id")).data
    $oldLayer=@($baselineCatalog.layers|Where-Object name -eq 'Enemy')[0]
    $newLayer=@($renamed.layers|Where-Object name -eq "LayerGateEnemy-$id")[0]
    Check ($oldLayer.id -eq $newLayer.id -and $oldLayer.slot -eq $newLayer.slot) 'Rename changed stable layer identity or mask'
    Check ((Command 'object.describe' @('LayerGateBody')).data.layerId -eq $oldLayer.id) 'Rename changed entity membership'
    Check ((Command 'undo').data.changed) 'Rename Undo missing'
    CheckFiles

    $blocked=(Command 'layer.collision' @('Default','Enemy','false')).data
    $blocked=(Command 'layer.collision' @('Default','Player','false')).data
    Check ([long]$blocked.revision -gt [long]$baselineCatalog.revision) 'Policy revision did not advance'
    $afterUndo=(Command 'undo.state').data
    Check ($afterUndo.gameUndo -eq $beforeUndo.gameUndo+2 -and $afterUndo.editUndo -eq $beforeUndo.editUndo) 'Play policy edits entered wrong Undo stack'
    CheckFiles
    ResetActors
    $null=Command 'play.resume'
    $evidence.blocked=WaitActors $false
    Check ($evidence.blocked.body.componentId -eq $bodyIdentity) 'Refilter replaced body component'

    $null=Command 'play.pause'
    Check ((Command 'undo').data.changed) 'Character filter Undo missing'
    Check ((Command 'undo').data.changed) 'Body filter Undo missing'
    $afterRestore=(Command 'layer.list').data
    Check ([long]$afterRestore.revision -gt [long]$blocked.revision) 'Undo did not publish a new revision'
    ResetActors
    $null=Command 'play.resume'
    $evidence.undo=WaitActors $true
    Check ($evidence.undo.body.componentId -eq $bodyIdentity) 'Undo replaced body component'
    CheckFiles

    $null=Command 'play.pause'
    Check ((Command 'redo').data.changed) 'Body filter Redo missing'
    Check ((Command 'redo').data.changed) 'Character filter Redo missing'
    ResetActors
    $null=Command 'play.resume'
    $evidence.redo=WaitActors $false
    StopAndCheck
    $restoredCatalog=(Command 'layer.list').data
    Check (($restoredCatalog.layers|ConvertTo-Json -Compress) -eq ($baselineCatalog.layers|ConvertTo-Json -Compress)) 'Stop did not restore layer definitions'
    Check ([long]$restoredCatalog.revision -gt [long]$afterRestore.revision) 'Stop reused an old revision'

    $null=Command 'play'
    $evidence.replay=WaitActors $true
    StopAndCheck
    $evidence.authoredFilesImmutable=$true
    @{result='PHYSICS_LAYER_HTTP_OK';checks=$script:checks;commands=$script:sequence;scene=$scene;evidence=$evidence;runtimeDllHash=(Get-FileHash $runtimePath).Hash;probeSourceHash=(Get-FileHash (Join-Path $repo 'GameScripts/PhysicsLayerProbe.cs')).Hash}|ConvertTo-Json -Depth 30|Set-Content "$out/result.json" -Encoding utf8
    "PHYSICS_LAYER_HTTP_OK checks=$script:checks evidence=$out"
    $null=Command 'quit'
} catch {
    @{result='PHYSICS_LAYER_HTTP_FAILED';checks=$script:checks;commands=$script:sequence;reason=$_.Exception.Message;observedLayerHash=(Get-FileHash $layerFile).Hash;baselineLayerHash=$baselineLayerHash;evidence=$evidence}|ConvertTo-Json -Depth 30|Set-Content "$out/failure.json" -Encoding utf8

    throw
} finally {
    if(!$process.HasExited){
        if(!$process.WaitForExit(5000)){$process.Kill();$process.WaitForExit()}
    }

    # Restore the exact caller-owned project input only after the owned Editor has exited.
    [IO.File]::WriteAllBytes($layerFile,$originalLayers)
}
