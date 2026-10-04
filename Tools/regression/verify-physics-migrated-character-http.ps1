param([string]$EditorExe='', [string]$ScenePath='')
$ErrorActionPreference='Stop'
$repo=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
if(!$EditorExe){$EditorExe=Join-Path $repo 'Bin/x64-Release/Editor/CreatorEditor.exe'}
$exe=Get-Item -LiteralPath $EditorExe
# The launcher stays unchanged when host implementation TUs relink the runtime DLL.
$runtimePath=Join-Path $exe.DirectoryName 'CreatorEditor.runtime.dll'
$implementation=if(Test-Path -LiteralPath $runtimePath){Get-Item -LiteralPath $runtimePath}else{$exe}
$required=@('Editor/EngineEntry/EditorProjectOperations.cpp','Editor/EngineEntry/Commands/AssetAuthoringCommands.cpp','Engine/SceneRuntime/SceneManager.cpp','Editor/EngineEntry/EditorObjectOperations.cpp','Editor/EngineEntry/PhysicsShapeDocument.h','Editor/EngineEntry/Commands/SceneObjectCommands.cpp','Editor/EngineGUIWindow/InspectorWindow.cpp','Engine/SceneRuntime/PhysicsBodyComponent.h','Engine/SceneRuntime/CharacterMovementComponent.h','Engine/SceneRuntime/CharacterMovementComponent.cpp','Engine/SceneRuntime/PhysicsPrimitivePreview.h','Engine/SceneRuntime/EnhancedGizmoSceneBinding.cpp','Engine/SceneRuntime/CharacterMotionPolicy.h','Engine/SceneRuntime/ClrHost.cpp','Engine/SceneRuntime/Scene.cpp','Engine/SceneRuntime/PhysicsTransformPolicy.h','Engine/SceneRuntime/ScenePhysicsSimulation.cpp','Engine/Physics/PhysicsScene.cpp') | ForEach-Object {Get-Item (Join-Path $repo $_)}
if($required | Where-Object {$_.LastWriteTimeUtc -gt $implementation.LastWriteTimeUtc}){throw 'Fresh Editor build required; stale executable is not product evidence'}
if(Get-Process CreatorEditor -ErrorAction SilentlyContinue){throw 'An Editor is already running; the gate needs its own session'}
$id=[guid]::NewGuid().ToString('N')
$out=Join-Path $repo "Build/Obj/Phase19M1Editor/http-$id"
New-Item -ItemType Directory -Force $out | Out-Null
$valid=Join-Path $out 'shapes.json'
$scene=Join-Path $repo 'Build/Obj/Phase19M1/character-converted.creator'
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
    if($ScenePath){$scene=[IO.Path]::GetFullPath($ScenePath)}
    $sourceHash=(Get-FileHash $scene).Hash
    $layerHash=(Get-FileHash $layerFile).Hash
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
    function Command([string]$name,[string[]]$arguments=@(),[switch]$Reject,[switch]$AllowPrecondition) {
        $script:sequence++
        $body=@{command=$name;args=@($arguments);mode='sync';correlationId="physics-migrated-character-$script:sequence"}|ConvertTo-Json -Compress
        try {
            $result=Invoke-RestMethod "$base/command" -Headers $headers -Method Post -ContentType 'application/json' -Body $body -TimeoutSec 120
        } catch {
            if ((!$Reject -and !$AllowPrecondition) -or !$_.ErrorDetails.Message) { throw }
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
        elseif($result.status -ne 'succeeded' -and (!$AllowPrecondition -or $result.status -ne 'preconditions_failed')){throw "$name failed: $($result.code) $($result.message)"}
        return $result
    }

    $null=Command 'scene.switch' @($scene)
    $initial=(Command 'object.describe' @('P0Character')).data
    if(@($initial.components | Where-Object type -eq 'CharacterMovementComponent').Count -ne 1 -or @($initial.components | Where-Object type -eq 'PhysicsBodyComponent').Count){throw 'Character ownership did not migrate'}
    if(@($initial.components | Where-Object type -eq 'CharacterMovementComponent')[0].id -ne '#2169397090'){throw 'Character identity changed'}
    $floor=(Command 'object.describe' @('P0Floor')).data
    $drop=(Command 'object.describe' @('P0Drop')).data
    if(@($floor.components | Where-Object type -eq 'PhysicsBodyComponent').Count -ne 1 -or @($drop.components | Where-Object type -eq 'PhysicsBodyComponent').Count -ne 1){throw 'Companion primitive bodies did not migrate'}
    $null=Command 'play.foreground_override' @('on')
    $cycles=@()
    foreach($cycle in 1..2){
        $null=Command 'play'
        $null=Command 'character.velocity' @('P0Character','1.5','0','0')
        $until=(Get-Date).AddSeconds(15)
        do {
            Start-Sleep -Milliseconds 80
            $state=(Command 'character.state' @('P0Character')).data
            if((Get-Date) -gt $until){throw 'Migrated CCT did not move/ground'}
        } until($state.below -and $state.position[0] -gt 4.1)
        if(!$state.simulating -or $state.tick -le 0 -or $state.desiredVelocity[0] -ne 1.5 -or [Math]::Abs($state.footPosition[1]) -gt .12){throw 'Migrated CCT motion mismatch'}
        $until=(Get-Date).AddSeconds(4)
        $jumpRejections=0
        do {
            $jump=Command 'character.jump' @('P0Character') -AllowPrecondition
            if($jump.status -ne 'succeeded'){
                if($jump.code -ne 'character.control_rejected' -or $jump.message -notmatch 'completed ground contact'){throw 'Unexpected migrated jump rejection'}
                $jumpRejections++
                Start-Sleep -Milliseconds 30
            }
            if((Get-Date) -gt $until){throw 'Migrated jump never accepted completed ground contact'}
        } until($jump.status -eq 'succeeded')
        Start-Sleep -Milliseconds 70
        $airborne=(Command 'character.state' @('P0Character')).data
        if($airborne.below -or $airborne.fallVelocity -le 0){throw 'Migrated jump policy did not rise'}
        $null=Command 'stop'
        $restored=(Command 'object.describe' @('P0Character')).data
        foreach($field in @('position','rotation','scale','layerId','enabled')){
            if(($restored.$field|ConvertTo-Json -Compress) -ne ($initial.$field|ConvertTo-Json -Compress)){throw "Migrated Stop did not restore $field"}
        }
        if(($restored.components.id|ConvertTo-Json -Compress) -ne ($initial.components.id|ConvertTo-Json -Compress)){throw 'Stop changed migrated component IDs'}
        $idle=(Command 'character.state' @('P0Character')).data
        if($idle.simulating -or [long]$idle.tick -ne 0){throw 'Stop retained migrated runtime'}
        if((Get-FileHash $scene).Hash -ne $sourceHash -or (Get-FileHash $layerFile).Hash -ne $layerHash){throw 'Migrated Play/Stop changed authoring files'}
        $cycles+=@{cycle=$cycle;grounded=$state;airborne=$airborne;jumpRejections=$jumpRejections;restored=$restored;idle=$idle}
    }
    @{result='PHYSICS_MIGRATED_CHARACTER_HTTP_OK';cycles=$cycles;commands=$script:sequence;sourceHash=$sourceHash;runtimeHash=(Get-FileHash $runtimePath).Hash;externalInput='explicit CLI 1.5m/s; gameplay input binding remains outside this gate';authoredFilesImmutable=$true}|ConvertTo-Json -Depth 30|Set-Content "$out/result.json" -Encoding utf8
    "PHYSICS_MIGRATED_CHARACTER_HTTP_OK evidence=$out"
    $null=Command 'quit'
} catch {
    @{result='PHYSICS_MIGRATED_CHARACTER_HTTP_FAILED';reason=$_.Exception.Message;commands=$script:sequence}|ConvertTo-Json|Set-Content "$out/failure.json" -Encoding utf8
    throw
} finally {
    if(!$process.HasExited){if(!$process.WaitForExit(5000)){$process.Kill();$process.WaitForExit()}}
    if((Get-FileHash $layerFile).Hash -ne $layerHash){throw 'Gate unexpectedly changed caller layer settings'}
}
