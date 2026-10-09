[CmdletBinding()]
param([string]$Configuration='Release')
$Ddol=$true;$AssemblyReload=$false
$ErrorActionPreference='Stop'

$Sensor='Sphere';$GroupTargets=$true
$repo=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
if(Get-Process CreatorEditor -ErrorAction SilentlyContinue){throw 'Owned Editor session required; close the running Editor first.'}
$out=Join-Path $repo ('Build/Verification/ContactStream/Product-'+[guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Force $out | Out-Null
$exe=Join-Path $repo "Bin/x64-$Configuration/Editor/CreatorEditor.exe"
$process=Start-Process $exe -ArgumentList '--command-service','--console','--allow-user-code' -WorkingDirectory (Split-Path $exe) -WindowStyle Hidden -RedirectStandardOutput "$out/editor.out" -RedirectStandardError "$out/editor.err" -PassThru
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
    function Command([string]$name,[string[]]$arguments=@()) {
        $script:sequence++
        $body=@{command=$name;args=@($arguments);mode='sync';correlationId="contact-stream-$script:sequence"}|ConvertTo-Json -Compress
        $result=Invoke-RestMethod "$base/command" -Headers $headers -Method Post -ContentType 'application/json' -Body $body -TimeoutSec 120
        if($result.operationId){
            $poll=$result.poll; $until=(Get-Date).AddSeconds(120)
            do {Start-Sleep -Milliseconds 100; $result=Invoke-RestMethod "$base$poll" -Headers $headers; if((Get-Date) -gt $until){throw 'Operation timeout'}} until($result.state -eq 'completed')
        }
        @{command=$name;args=@($arguments);result=$result}|ConvertTo-Json -Depth 30 -Compress|Add-Content "$out/results.jsonl" -Encoding utf8
        if($result.status -ne 'succeeded'){throw "$name failed: $($result.code) $($result.message)"}
        return $result
    }
    $destination=Join-Path $out 'Destination.creator'
    $null=Command 'scene.new' @('ContactDestination')
    $null=Command 'object.create' @('ContactHurt')
    $null=Command 'component.add' @('ContactHurt','PhysicsBodyComponent')
    $null=Command 'object.property' @('ContactHurt','PhysicsBodyComponent','m_motion','0')
    $destinationShapes=Join-Path $out 'destination-shapes.json'
    '[{"shapeId":23,"contactRole":"7d352a65-f9bd-4bd5-83cf-97d1d276f740","kind":0,"halfExtent":[0.5,0.5,0.5]}]'|Set-Content $destinationShapes -Encoding utf8
    $null=Command 'physics.shapes' @('ContactHurt','PhysicsBodyComponent',$destinationShapes)
    $null=Command 'object.transform' @('ContactHurt','3','0','0')
    $null=Command 'object.create' @('DestinationCamera')
    $null=Command 'component.add' @('DestinationCamera','CameraComponent')
    $null=Command 'object.transform' @('DestinationCamera','3','2','-10')
    $null=Command 'object.property' @('DestinationCamera','CameraComponent','m_isPrimary','true')
    $null=Command 'object.create' @('ChildHurt')
    $null=Command 'component.add' @('ChildHurt','PhysicsBodyComponent')
    $null=Command 'object.property' @('ChildHurt','PhysicsBodyComponent','m_motion','0')
    $null=Command 'physics.shapes' @('ChildHurt','PhysicsBodyComponent',$destinationShapes)
    $null=Command 'object.transform' @('ChildHurt','3','5','0')
    $null=Command 'scene.save' @($destination)
    $null=Command 'scene.new' @('PhysicsContactStream')
    foreach($name in @('ContactAttack','ContactHurt')) {
        $null=Command 'object.create' @($name)
        $null=Command 'component.add' @($name,'PhysicsBodyComponent')
        $null=Command 'object.property' @($name,'PhysicsBodyComponent','m_gravityEnabled','false')
        $null=Command 'object.property' @($name,'PhysicsBodyComponent','m_motion',$(if($name -eq 'ContactAttack'){'2'}else{'0'}))
        if($SensorTransition -and $name -eq 'ContactAttack'){
            $null=Command 'object.property' @($name,'PhysicsBodyComponent','m_translationLocks','7')
            $null=Command 'object.property' @($name,'PhysicsBodyComponent','m_rotationLocks','7')
        }
        $shapeFile=Join-Path $out "$name.json"
        $shapes=if($name -eq 'ContactAttack'){'[{"shapeId":17,"contactRole":"67adfded-47c8-4ef9-9c38-d1c3497a7421","kind":0,"halfExtent":[0.5,0.5,0.5]},{"shapeId":19,"contactRole":"67adfded-47c8-4ef9-9c38-d1c3497a7421","kind":0,"halfExtent":[0.75,0.75,0.75],"sensor":true,"localPosition":[3,0,0]}]'}else{'[{"shapeId":23,"contactRole":"7d352a65-f9bd-4bd5-83cf-97d1d276f740","kind":0,"halfExtent":[0.5,0.5,0.5]}]'}
        if($name -eq 'ContactAttack' -and $Sensor -eq 'Sphere'){
            $shapes=$shapes.Replace('"shapeId":19,"contactRole":"67adfded-47c8-4ef9-9c38-d1c3497a7421","kind":0,"halfExtent":[0.75,0.75,0.75]', '"shapeId":19,"contactRole":"67adfded-47c8-4ef9-9c38-d1c3497a7421","kind":1,"radius":0.75')
        }
        if($GroupTargets -and $name -eq 'ContactAttack'){
            $shapes=$shapes.Substring(0,$shapes.Length-1)+',{"shapeId":20,"contactRole":"67adfded-47c8-4ef9-9c38-d1c3497a7421","kind":1,"radius":0.75,"sensor":true,"localPosition":[4,0,0]}]'
        }
        [IO.File]::WriteAllText($shapeFile,$shapes)
        $null=Command 'physics.shapes' @($name,'PhysicsBodyComponent',$shapeFile)
        if($name -eq 'ContactHurt'){$null=Command 'object.transform' @($name,'3','0','0')}
        if($name -eq 'ContactHurt'){continue}
        $null=Command 'script.add' @($name,'PhysicsHierarchyContactProbe')
    }
    $null=Command 'object.create' @('ChildHurt')
    $null=Command 'component.add' @('ChildHurt','PhysicsBodyComponent')
    $null=Command 'object.property' @('ChildHurt','PhysicsBodyComponent','m_motion','0')
    $null=Command 'physics.shapes' @('ChildHurt','PhysicsBodyComponent',$destinationShapes)
    $null=Command 'object.transform' @('ChildHurt','3','5','0')
    $null=Command 'object.create' @('ChildAttack')
    $null=Command 'object.parent' @('ChildAttack','ContactAttack')
    $null=Command 'object.transform' @('ChildAttack','0','5','0')
    $null=Command 'component.add' @('ChildAttack','PhysicsBodyComponent')
    $null=Command 'object.property' @('ChildAttack','PhysicsBodyComponent','m_motion','2')
    $null=Command 'object.property' @('ChildAttack','PhysicsBodyComponent','m_gravityEnabled','false')
    $null=Command 'physics.shapes' @('ChildAttack','PhysicsBodyComponent',(Join-Path $out 'ContactAttack.json'))
    $null=Command 'script.add' @('ChildAttack','PhysicsHierarchyContactProbe')
    foreach($name in @('ContactAttack','ChildAttack')) {
        $null=Command 'object.property' @($name,'PhysicsBodyComponent','m_translationLocks','7')
        $null=Command 'object.property' @($name,'PhysicsBodyComponent','m_rotationLocks','7')
    }
    $null=Command 'object.create' @('ContactCamera')
    $null=Command 'component.add' @('ContactCamera','CameraComponent')
    $null=Command 'object.transform' @('ContactCamera','3','2','-10')
    $null=Command 'object.property' @('ContactCamera','CameraComponent','m_isPrimary','true')
    $scene=Join-Path $repo ('Dynamic_CPP/Assets/Scenes/PhysicsContactStream-'+(Split-Path $out -Leaf)+'.creator')
    if(Test-Path -LiteralPath $scene){throw 'Contact fixture already exists; refuse overwriting an authored scene.'}
    $null=Command 'scene.save' @($scene)
    $sceneHash=(Get-FileHash -LiteralPath $scene).Hash
    $destinationHash=(Get-FileHash -LiteralPath $destination).Hash
    $initial=@{}
    foreach($name in @('ContactAttack','ContactHurt','ChildAttack','ChildHurt')){$initial[$name]=(Command 'object.describe' @($name)).data}
    $null=Command 'play.foreground_override' @('on')
    function ProbeState { (Command 'script.invoke' @('PhysicsHierarchyContactProbe','State')).data.returnValue|ConvertFrom-Json }
    function WaitReady([int]$added) {
        $deadline=(Get-Date).AddSeconds(60)
        do {
            Start-Sleep -Milliseconds 100
            $playing=(Command 'play.state').data
            if($playing.failureCount -ne 0){throw "Hierarchy contact session failed: $($playing.lastFailure)"}
            $probe=ProbeState
            if($process.HasExited -or (Get-Date) -gt $deadline){throw 'Hierarchy contact probe timed out'}
        } until($probe.instances.Count -eq 2 -and !@($probe.instances | Where-Object { !$_.ready -or $_.begins -ne 2 -or $_.persists -lt 2 -or $_.runs -ne 1 -or $_.added -ne $added }).Count)
        if($probe.streams -ne 2 -or $probe.disposed -ne 0){throw 'Hierarchy streams duplicated or prematurely disposed'}
        return $probe
    }
    $null=Command 'play'
    $before=WaitReady 1
    $null=Command 'scene.ddol' @('ContactAttack')
    # Both entries intentionally overlap: the child must never transfer twice.
    $null=Command 'scene.ddol' @('ChildAttack')
    $transitions=@()
    for($iteration=1;$iteration -le 3;$iteration++) {
        $null=Command 'scene.switch' @($destination)
        $after=WaitReady ($iteration+1)
        foreach($instance in $after.instances) {
            $original=@($before.instances | Where-Object name -eq $instance.name)
            if($original.Count -ne 1 -or !$instance.bodyComponent -or $instance.bodyComponent -ne $original[0].bodyComponent){throw 'Hierarchy body wrapper identity changed'}
        }
        $parent=(Command 'object.describe' @('ContactAttack')).data
        $child=(Command 'object.describe' @('ChildAttack')).data
        if($child.parent -ne $parent.id -or $parent.children.Count -ne 1 -or $parent.children[0] -ne $child.id -or
           ($child.position -join ',') -ne '0,5,0' -or ($parent.position -join ',') -ne '0,0,0') {
            throw 'Transferred hierarchy or authored local pose changed'
        }
        $transitions+=@{iteration=$iteration;after=$after;parent=$parent;child=$child}
    }
    $null=Command 'stop'
    $deadline=(Get-Date).AddSeconds(30)
    do {Start-Sleep -Milliseconds 100;$state=(Command 'play.state').data;if((Get-Date) -gt $deadline){throw 'Stop timed out'}} while($state.pending -or $state.committed)
    $retired=ProbeState
    if($retired.disposed -ne $retired.streams){throw 'Scene-boundary stream leak after Stop'}
    foreach($name in $initial.Keys) {
        $restored=(Command 'object.describe' @($name)).data
        foreach($field in @('name','position','rotation','scale','enabled')) {
            if(($restored.$field | ConvertTo-Json -Compress) -ne ($initial[$name].$field | ConvertTo-Json -Compress)) {
                throw "Stop did not restore $name/$field"
            }
        }
        if(($restored.components | ConvertTo-Json -Compress) -ne ($initial[$name].components | ConvertTo-Json -Compress)) {
            throw "Stop changed authored component identities: $name"
        }
    }
    $restoredParent=(Command 'object.describe' @('ContactAttack')).data
    $restoredChild=(Command 'object.describe' @('ChildAttack')).data
    if($restoredChild.parent -ne $restoredParent.id){throw 'Stop did not restore parent-child relationship'}
    if((Get-FileHash -LiteralPath $scene).Hash -ne $sceneHash){throw 'Scene bytes changed'}
    if((Get-FileHash -LiteralPath $destination).Hash -ne $destinationHash){throw 'Destination bytes changed'}
    @{result='CONTACT_HIERARCHY_EDITOR_OK';ddol=[bool]$Ddol;assemblyReload=[bool]$AssemblyReload;transitions=$transitions;scene=$scene;destination=$destination;before=$before;after=$after;retired=$retired;commands=$script:sequence}|ConvertTo-Json -Depth 10|Set-Content "$out/result.json" -Encoding utf8
    "CONTACT_HIERARCHY_EDITOR_OK evidence=$out"
    $null=Command 'quit'
} finally {
    if(!$process.WaitForExit(30000)){$process.Kill();$process.WaitForExit()}
}
