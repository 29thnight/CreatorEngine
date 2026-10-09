param([string]$EditorExe='')
$ErrorActionPreference='Stop'
$repo=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
if(!$EditorExe){$EditorExe=Join-Path $repo 'Bin/x64-Debug/Editor/CreatorEditor.exe'}
$exe=Get-Item -LiteralPath $EditorExe
# The launcher stays unchanged when host implementation TUs relink the runtime DLL.
$runtimePath=Join-Path $exe.DirectoryName 'CreatorEditor.runtime.dll'
$implementation=if(Test-Path -LiteralPath $runtimePath){Get-Item -LiteralPath $runtimePath}else{$exe}
$required=@('Editor/EngineEntry/EditorObjectOperations.cpp','Editor/EngineEntry/PhysicsShapeDocument.h','Editor/EngineEntry/Commands/SceneObjectCommands.cpp','Editor/EngineGUIWindow/InspectorWindow.cpp','Engine/SceneRuntime/PhysicsBodyComponent.h','Engine/SceneRuntime/PhysicsShapeDefinition.h') | ForEach-Object {Get-Item (Join-Path $repo $_)}
if($required | Where-Object {$_.LastWriteTimeUtc -gt $implementation.LastWriteTimeUtc}){throw 'Fresh Editor build required; stale executable is not product evidence'}
if(Get-Process CreatorEditor -ErrorAction SilentlyContinue){throw 'An Editor is already running; the gate needs its own session'}
$id=[guid]::NewGuid().ToString('N')
$out=Join-Path $repo "Build/Obj/Phase19ShapeAuthoring/http-$id"
New-Item -ItemType Directory -Force $out | Out-Null
$valid=Join-Path $out 'shapes.json'
$invalid=Join-Path $out 'invalid.json'
$prefabInput=Join-Path $out 'prefab-shapes.json'
[IO.File]::WriteAllText($valid,'[{"shapeId":12,"kind":0,"contactRole":"67adfded-47c8-4ef9-9c38-d1c3497a7421","halfExtent":[0.5,0.5,0.5]},{"shapeId":33,"kind":1,"radius":1,"sensor":true,"localPosition":[3,0,0],"layerOverride":"1"}]')
[IO.File]::WriteAllText($prefabInput,([IO.File]::ReadAllText($valid).Replace('"shapeId":12','"shapeId":44').Replace('"shapeId":33','"shapeId":55').Replace('67adfded-47c8-4ef9-9c38-d1c3497a7421','7d352a65-f9bd-4bd5-83cf-97d1d276f740')))
[IO.File]::WriteAllText($invalid,'[{"shapeId":12,"kind":0,"contactRole":"67adfded-47c8-4ef9-9c38-d1c3497a7421","halfExtent":[-1,1,1]}]')
$scene=Join-Path $repo "Dynamic_CPP/Assets/Scenes/PhysicsShapeGate-$id.creator"
$process=Start-Process $exe.FullName -ArgumentList '--command-service','--console' -WorkingDirectory $exe.DirectoryName -WindowStyle Hidden -RedirectStandardOutput "$out/editor.out" -RedirectStandardError "$out/editor.err" -PassThru
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
        $body=@{command=$name;args=@($arguments);mode='sync';correlationId="physics-shapes-$script:sequence"}|ConvertTo-Json -Compress
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
    $null=Command 'scene.new' @("PhysicsShapeGate-$id")
    $null=Command 'object.create' @('ShapeGateBody')
    $null=Command 'component.add' @('ShapeGateBody','PhysicsBodyComponent')
    $null=Command 'object.transform' @('ShapeGateBody','0','5','0')
    $null=Command 'scene.save' @($scene)
    $null=Command 'scene.save' @($scene)
    $bodyBefore=[IO.File]::ReadAllText($scene)
    $bodyDepth=(Command 'undo.state').data.editUndo
    foreach($edit in @(@('m_mass','0'),@('m_mass','-1'),@('m_linearDamping','-1'),@('m_angularDamping','-1'),@('m_translationLocks','8'),@('m_rotationLocks','8'),@('m_motion','3'))){
        $null=Command 'object.property' @('ShapeGateBody','PhysicsBodyComponent',$edit[0],$edit[1]) -Reject
        if((Command 'undo.state').data.editUndo -ne $bodyDepth){throw 'Rejected body edit changed Undo'}
    }
    $null=Command 'scene.save' @($scene)
    if([IO.File]::ReadAllText($scene) -ne $bodyBefore){throw 'Rejected body edit changed authored values'}
    $null=Command 'object.property' @('ShapeGateBody','PhysicsBodyComponent','m_mass','2')
    if((Command 'undo.state').data.editUndo -ne $bodyDepth+1){throw 'Body edit did not create one Undo entry'}
    $null=Command 'undo'
    $null=Command 'scene.save' @($scene)
    if([IO.File]::ReadAllText($scene) -ne $bodyBefore){throw 'Body Undo did not restore original document'}
    $null=Command 'redo'
    $null=Command 'scene.save' @($scene)
    if([IO.File]::ReadAllText($scene) -notmatch 'm_mass:\s*2(?!\d)'){throw 'Body Redo did not restore mass'}
    $depth=(Command 'undo.state').data.editUndo
    $applied=Command 'physics.shapes' @('ShapeGateBody','PhysicsBodyComponent',$valid)
    if(!$applied.data.changed -or $applied.data.count -ne 2){throw 'Complete shape list was not applied'}
    if((Command 'undo.state').data.editUndo -ne $depth+1){throw 'Shape edit must create one Undo entry'}
    $null=Command 'physics.shapes' @('ShapeGateBody','PhysicsBodyComponent',$invalid) -Reject
    if((Command 'undo.state').data.editUndo -ne $depth+1){throw 'Rejected edit changed Undo history'}
    $null=Command 'undo'
    $null=Command 'scene.save' @($scene)
    if([IO.File]::ReadAllText($scene) -match 'shapeId:\s*12'){throw 'Undo did not restore old shape list'}
    $null=Command 'redo'
    $null=Command 'scene.save' @($scene)
    $before=[IO.File]::ReadAllText($scene)
    if($before -notmatch 'shapeId:\s*12' -or $before -notmatch 'shapeId:\s*33'){throw 'Redo shape serialization failed'}
    if($before -notmatch 'contactRole:\s*67adfded-47c8-4ef9-9c38-d1c3497a7421'){throw 'Shape role serialization failed'}
    $null=Command 'prefab.create' @('ShapeGateBody',"PhysicsShapeGate-$id")
    $null=Command 'prefab.instantiate' @("PhysicsShapeGate-$id",'ShapeGatePrefab')
    $null=Command 'physics.shapes' @('ShapeGatePrefab','PhysicsBodyComponent',$prefabInput)
    $overrides=Command 'prefab.overrides' @('ShapeGatePrefab')
    if(($overrides.data|ConvertTo-Json -Depth 30) -notmatch 'm_shapes'){throw 'Prefab shape override was not recorded'}
    if(($overrides.data|ConvertTo-Json -Depth 30) -notmatch '7d352a65-f9bd-4bd5-83cf-97d1d276f740'){throw 'Prefab role override not recorded'}
    $null=Command 'scene.save' @($scene)
    $reloadRequest=Command 'scene.switch' @($scene)
    if(!$reloadRequest.data.activationRequested){throw 'Prefab reload activation not requested'}
    $reloadDeadline=(Get-Date).AddSeconds(120)
    do {
        Start-Sleep -Milliseconds 100
        $reloadStatus=Command 'scene.load.status' @("$($reloadRequest.data.requestId)")
        if((Get-Date) -gt $reloadDeadline){throw 'Prefab reload timeout'}
    }until($reloadStatus.data.complete)
    if($reloadStatus.data.state -ne 'Ready'){throw 'Prefab reload failed'}
    Start-Sleep -Milliseconds 600
    $reloadedOverrides=Command 'prefab.overrides' @('ShapeGatePrefab')
    if (($reloadedOverrides.data.overrides | ConvertTo-Json -Depth 30 -Compress) -ne
        ($overrides.data.overrides | ConvertTo-Json -Depth 30 -Compress)) { throw 'Prefab shape override changed after save/load' }
    $null=Command 'scene.save' @($scene)
    if([IO.File]::ReadAllText($scene) -notmatch '67adfded-47c8-4ef9-9c38-d1c3497a7421'){throw 'Shape role lost after reload'}
    $initial=(Command 'object.describe' @('ShapeGateBody')).data.position
    $null=Command 'play.foreground_override' @('on')
    $null=Command 'play'
    $playDepth=(Command 'undo.state').data.editUndo
    $null=Command 'object.property' @('ShapeGateBody','PhysicsBodyComponent','m_mass','3') -Reject
    if((Command 'undo.state').data.editUndo -ne $playDepth){throw 'Play body edit changed Undo'}
    $null=Command 'physics.shapes' @('ShapeGateBody','PhysicsBodyComponent',$valid) -Reject
    $until=(Get-Date).AddSeconds(15)
    do {Start-Sleep -Milliseconds 100; $position=(Command 'object.describe' @('ShapeGateBody')).data.position} while($position[1] -ge $initial[1]-0.05 -and (Get-Date) -lt $until)
    if($position[1] -ge $initial[1]-0.05){throw 'Physics simulation did not move the body'}
    $null=Command 'stop'
    $restored=(Command 'object.describe' @('ShapeGateBody')).data.position
    if(($restored|ConvertTo-Json -Compress) -ne ($initial|ConvertTo-Json -Compress)){throw 'Editor Stop did not restore pre-Play Transform'}
    @{result='PHYSICS_SHAPE_HTTP_OK';scene=$scene;commands=$script:sequence}|ConvertTo-Json -Compress|Set-Content "$out/result.json" -Encoding utf8
    Write-Output "PHYSICS_SHAPE_HTTP_OK evidence=$out"
} finally {
    if(!$process.HasExited){$process.Kill();$process.WaitForExit()}
}
