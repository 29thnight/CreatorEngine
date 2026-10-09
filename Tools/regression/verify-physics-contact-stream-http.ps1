[CmdletBinding()]
param([string]$Configuration='Release', [ValidateSet('Box','Sphere')][string]$Sensor='Sphere', [string]$CaptureReader='', [switch]$SolidContacts, [switch]$LateOverlap, [switch]$GroupTargets, [switch]$Retirement, [switch]$Topology, [switch]$SensorTransition, [switch]$BurstTopology, [switch]$RemoveComponent, [switch]$SubscriberLifetime, [switch]$RoleTransition, [switch]$ExplicitBinding, [ValidateSet('None','Overflow','Exception')][string]$FaultCase='None')
$ErrorActionPreference='Stop'
if($SolidContacts -and ($GroupTargets -or $LateOverlap -or $Retirement -or $Topology -or $SensorTransition -or $BurstTopology -or $RemoveComponent -or $SubscriberLifetime -or $RoleTransition -or $ExplicitBinding -or $FaultCase -ne 'None')){throw 'Use a separate solid fixture'}
$ContactFault=$FaultCase -ne 'None'
if($ContactFault){
    if($RoleTransition -or $ExplicitBinding -or $SubscriberLifetime -or $Retirement -or $Topology -or $SensorTransition -or $LateOverlap -or $RemoveComponent -or $BurstTopology){throw 'Use a separate contact fault fixture'}
    $GroupTargets=$true
}
if($ExplicitBinding){$RoleTransition=$true}
if($RoleTransition -and ($SubscriberLifetime -or $Retirement -or $Topology -or $SensorTransition -or $LateOverlap -or $RemoveComponent -or $BurstTopology)){throw 'Use a separate role transition fixture'}
if($RoleTransition){$GroupTargets=$true}
if($SubscriberLifetime -and ($Retirement -or $Topology -or $SensorTransition -or $LateOverlap -or $RemoveComponent -or $BurstTopology)){throw 'Use a separate subscriber fixture'}
if($BurstTopology){$Topology=$true}
if($RemoveComponent){$Retirement=$true}
if($SensorTransition -and ($Retirement -or $Topology -or $LateOverlap)){throw 'Use a separate sensor transition fixture'}
if($Retirement -and $Topology){throw 'Use separate deletion and topology fixtures'}
if($SolidContacts -or $Retirement -or $Topology -or $SensorTransition -or $SubscriberLifetime -or $RoleTransition){$GroupTargets=$true}
if($LateOverlap -and $GroupTargets){throw 'Use separate late-overlap and target-grouping fixtures'}
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
    $null=Command 'scene.new' @('PhysicsContactStream')
    foreach($name in @('ContactAttack','ContactHurt')) {
        $null=Command 'object.create' @($name)
        $null=Command 'component.add' @($name,'PhysicsBodyComponent')
        $null=Command 'object.property' @($name,'PhysicsBodyComponent','m_gravityEnabled','false')
        $null=Command 'object.property' @($name,'PhysicsBodyComponent','m_motion',$(if($name -eq 'ContactAttack'){'2'}else{'0'}))
        if(($SensorTransition -or $SolidContacts) -and $name -eq 'ContactAttack'){
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
        if($SolidContacts){
            $shapes=if($name -eq 'ContactAttack'){'[{"shapeId":19,"contactRole":"67adfded-47c8-4ef9-9c38-d1c3497a7421","kind":0,"halfExtent":[0.5,0.5,0.5],"localPosition":[3,0,-1]},{"shapeId":20,"contactRole":"67adfded-47c8-4ef9-9c38-d1c3497a7421","kind":0,"halfExtent":[0.5,0.5,0.5],"localPosition":[3,0,1]}]'}else{'[{"shapeId":23,"contactRole":"7d352a65-f9bd-4bd5-83cf-97d1d276f740","kind":0,"halfExtent":[0.5,0.5,1.5]}]'}
        }
        [IO.File]::WriteAllText($shapeFile,$shapes)
        $null=Command 'physics.shapes' @($name,'PhysicsBodyComponent',$shapeFile)
        if($name -eq 'ContactHurt'){$null=Command 'object.transform' @($name,'3','0','0')}
        if(($SolidContacts -or $Retirement -or $Topology -or $SensorTransition -or $SubscriberLifetime -or $RoleTransition) -and $name -eq 'ContactHurt'){continue}
        $attached=(Command 'script.add' @($name,$(if($SolidContacts){'PhysicsSolidContactProbe'}elseif($ContactFault){'PhysicsContactFaultProbe'}elseif($RoleTransition){'PhysicsRoleContactProbe'}elseif($SubscriberLifetime){'PhysicsSubscriberContactProbe'}elseif($SensorTransition){'PhysicsSensorTransitionProbe'}elseif($Topology){'PhysicsTopologyContactProbe'}else{'PhysicsContactStreamProbe'}))).data
        if($name -eq 'ContactAttack'){$attackScript=$attached.instanceId}
        if($ContactFault -and $FaultCase -eq 'Overflow'){
            $fields=(Command 'script.fields' @("$($attached.instanceId)")).data.fields
            $field=@($fields|Where-Object {($_.name -replace '[^a-zA-Z]','').ToLowerInvariant() -eq 'overflow'})
            if($field.Count -ne 1){throw 'Overflow field unavailable'}
            $null=Command 'script.set' @("$($attached.instanceId)","$($field[0].index)",'true')
        }
        if($BurstTopology){
            $fields=(Command 'script.fields' @("$($attached.instanceId)")).data.fields
            $field=@($fields|Where-Object {($_.name -replace '[^a-zA-Z]','').ToLowerInvariant() -eq 'burstreplacements'})
            if($field.Count -ne 1){throw 'BurstReplacements field unavailable'}
            $null=Command 'script.set' @("$($attached.instanceId)","$($field[0].index)",'true')
        }
        if($ExplicitBinding){
            $fields=(Command 'script.fields' @("$($attached.instanceId)")).data.fields
            $field=@($fields|Where-Object {($_.name -replace '[^a-zA-Z]','').ToLowerInvariant() -eq 'explicitbinding'})
            if($field.Count -ne 1){throw 'ExplicitBinding field unavailable'}
            $null=Command 'script.set' @("$($attached.instanceId)","$($field[0].index)",'true')
        }
        if($RemoveComponent){
            $fields=(Command 'script.fields' @("$($attached.instanceId)")).data.fields
            $field=@($fields|Where-Object {($_.name -replace '[^a-zA-Z]','').ToLowerInvariant() -eq 'removebodyonly'})
            if($field.Count -ne 1){throw 'RemoveBodyOnly field unavailable'}
            $null=Command 'script.set' @("$($attached.instanceId)","$($field[0].index)",'true')
        }
        if($Retirement){
            $fields=(Command 'script.fields' @("$($attached.instanceId)")).data.fields
            $field=@($fields|Where-Object {($_.name -replace '[^a-zA-Z]','').ToLowerInvariant() -eq 'retiretarget'})
            if($field.Count -ne 1){throw 'RetireTarget field unavailable'}
            $null=Command 'script.set' @("$($attached.instanceId)","$($field[0].index)",'true')
        }
        if($GroupTargets -and !$SolidContacts -and !$Topology -and !$SensorTransition -and !$SubscriberLifetime -and !$RoleTransition -and !$ContactFault){
            $fields=(Command 'script.fields' @("$($attached.instanceId)")).data.fields
            $field=@($fields|Where-Object {($_.name -replace '[^a-zA-Z]','').ToLowerInvariant() -eq 'grouptargets'})
            if($field.Count -ne 1){throw 'GroupTargets field unavailable'}
            $null=Command 'script.set' @("$($attached.instanceId)","$($field[0].index)",'true')
        }
    }
    if($SubscriberLifetime){
        $null=Command 'object.create' @('ContactObserver')
        $null=Command 'script.add' @('ContactObserver','PhysicsSubscriberContactProbe')
    }
    $latePrefab=''
    if($LateOverlap){
        $prefabName='LateContact-'+(Split-Path $out -Leaf)
        $null=Command 'object.create' @('LateObserverTemplate')
        $null=Command 'script.add' @('LateObserverTemplate','PhysicsLateContactProbe')
        $null=Command 'prefab.create' @('LateObserverTemplate',$prefabName)
        $null=Command 'object.delete' @('LateObserverTemplate')
        $latePrefab=Join-Path $repo "Dynamic_CPP/Assets/Prefabs/$prefabName.prefab"
        if(!(Test-Path -LiteralPath $latePrefab)){throw 'Late observer prefab was not saved'}
        $fields=(Command 'script.fields' @("$attackScript")).data.fields
        $field=@($fields|Where-Object {($_.name -replace '[^a-zA-Z]','').ToLowerInvariant() -eq 'lateobserverprefab'})
        if($field.Count -ne 1){throw 'Late prefab field unavailable'}
        $null=Command 'script.set' @("$attackScript","$($field[0].index)",$prefabName)
    }
    $null=Command 'object.create' @('ContactCamera')
    $null=Command 'component.add' @('ContactCamera','CameraComponent')
    $null=Command 'object.transform' @('ContactCamera','3','2','-10')
    $null=Command 'object.property' @('ContactCamera','CameraComponent','m_isPrimary','true')
    $scene=Join-Path $repo ('Dynamic_CPP/Assets/Scenes/PhysicsContactStream-'+(Split-Path $out -Leaf)+'.creator')
    if(Test-Path -LiteralPath $scene){throw 'Contact fixture already exists; refuse overwriting an authored scene.'}
    $null=Command 'scene.save' @($scene)
    $sceneHash=(Get-FileHash -LiteralPath $scene).Hash
    $initial=@{}
    foreach($name in @('ContactAttack','ContactHurt')){$initial[$name]=(Command 'object.describe' @($name)).data}
    $null=Command 'play.foreground_override' @('on')
    $cycles=@()
    for($cycle=1;$cycle -le 2;$cycle++) {
        if($CaptureReader){
            $null=Command 'profile.record'
            $until=(Get-Date).AddSeconds(30)
            do {
                Start-Sleep -Milliseconds 100
                $profile=(Command 'profile.stats').data
                if($profile.recording.state -eq 'failed' -or (Get-Date) -gt $until){throw 'Recording start failed'}
            } until($profile.recording.state -eq 'recording')
        }
        $null=Command 'play'
        $until=(Get-Date).AddSeconds(90)
        $diagnosticAt=Get-Date
        do {
            Start-Sleep -Milliseconds 200
            $stdout=[string](Get-Content "$out/editor.out" -Raw)
            $probes=@([regex]::Matches($stdout,'\{"marker":"physics.contact.stream"[^\r\n]+\}')|ForEach-Object {$_.Value|ConvertFrom-Json})
            if($probes|Where-Object {!$_.success}){throw 'Contact stream assertions failed'}
            if((Get-Date) -ge $diagnosticAt){
                $playing=(Command 'play.state').data
                if($playing.failureCount -gt 0){throw "Simulation failed: $($playing.lastFailure)"}
                $null=Command 'script.invoke' @('PhysicsContactStreamProbe','State')
                $diagnosticAt=(Get-Date).AddSeconds(5)
            }
            if($process.HasExited -or (Get-Date) -gt $until){throw 'Contact stream Editor timed out'}
        } until($probes.Count -eq $cycle*$(if($SolidContacts -or $Retirement -or $Topology -or $SensorTransition -or $SubscriberLifetime -or $RoleTransition -or $ContactFault){1}else{2}))
        if($CaptureReader){
            $null=Command 'profile.pause'
            $until=(Get-Date).AddSeconds(30)
            do {
                Start-Sleep -Milliseconds 100
                $profile=(Command 'profile.stats').data
                if($profile.recording.state -eq 'failed' -or (Get-Date) -gt $until){throw 'Recording finalization failed'}
            } until($profile.recording.state -eq 'finalized')
            $captureFile=Join-Path $out "cycle-$cycle.ceprof"
            $null=Command 'profile.save' @($captureFile)
            $until=(Get-Date).AddSeconds(30)
            do {
                Start-Sleep -Milliseconds 100
                $saved=(Command 'profile.save' @('status')).data
                if((Get-Date) -gt $until){throw 'Capture export timed out'}
            } until($saved.saveState -eq 'saved')
            & $CaptureReader $captureFile *> "$out/capture-$cycle.json"
            if($LASTEXITCODE){throw "Contact capture incomplete or missing Scene/tick markers: $LASTEXITCODE"}
        }
        $null=Command 'stop'
        $until=(Get-Date).AddSeconds(30)
        do {Start-Sleep -Milliseconds 100; $state=(Command 'play.state').data; if((Get-Date) -gt $until){throw 'Stop timed out'}} while($state.pending -or $state.committed)
        foreach($name in $initial.Keys) {
            $restored=(Command 'object.describe' @($name)).data
            foreach($property in @('position','rotation','scale')) {
                if(($initial[$name].$property|ConvertTo-Json -Compress) -ne ($restored.$property|ConvertTo-Json -Compress)){throw "Stop did not restore $name $property"}
            }
        }
        $retired=(Command 'script.invoke' @('PhysicsContactStreamProbe','Retired')).data.returnValue|ConvertFrom-Json
        $expectedStreams=$cycle*$(if($ContactFault){2}elseif($SubscriberLifetime -or $RoleTransition){4}elseif($SolidContacts -or $Retirement -or $Topology -or $SensorTransition){2}elseif($LateOverlap -or $GroupTargets){4}else{2})
        if($retired.streams -ne $expectedStreams -or $retired.disposed -ne $retired.streams){throw 'Scope did not dispose all contact streams'}
        $cycles+=@{cycle=$cycle;retired=$retired}
    }
    if((Get-FileHash -LiteralPath $scene).Hash -ne $sceneHash){throw 'Play/Stop modified scene bytes'}
    if($SolidContacts -and ($probes|Where-Object {!$_.solidContacts -or ($_.begins -join ',') -ne '1,1' -or ($_.ends -join ',') -ne '1,1' -or $_.partialPersists -lt 2 -or $_.stage -ne 3})){throw 'solid assertions incomplete'}
    if($Retirement -and ($probes|Where-Object {!$_.retired -or $_.deletedAlive -ne [bool]$RemoveComponent})){throw 'Deleted endpoint assertions incomplete'}
    if($GroupTargets -and !$SolidContacts -and !$Topology -and !$SensorTransition -and !$SubscriberLifetime -and !$RoleTransition -and !$ContactFault -and ($probes|Where-Object {!$_.grouped -or $_.sensorBegin -ne 2 -or $_.sensorEnd -ne 2 -or $_.groupBegin -ne 1 -or $_.groupEnd -ne 1 -or $_.groupPersist -lt $(if($SolidContacts -or $Retirement -or $Topology -or $SensorTransition -or $SubscriberLifetime -or $RoleTransition -or $ContactFault){1}else{2})})){throw 'Target grouping assertions incomplete'}
    if($Topology -and ($probes|Where-Object {!$_.topology -or $_.oldEnds -ne 2 -or $_.newBegins -ne 2 -or $_.newEnds -ne 2 -or $_.sensorBegin -ne 4 -or $_.sensorEnd -ne 4 -or $_.oldBodyIdentity -eq 0 -or $_.newBodyIdentity -eq 0 -or $_.oldBodyIdentity -eq $_.newBodyIdentity -or $_.groupBegin -lt 1 -or $_.groupBegin -gt 2 -or $_.groupEnd -ne $_.groupBegin})){throw 'Topology assertions incomplete'}
    if($SensorTransition -and ($probes|Where-Object {!$_.sensorTransition -or ($_.sensorBegins -join ',') -ne '2,1,2' -or ($_.sensorEnds -join ',') -ne '2,1,2' -or $_.solidBegin -ne 1 -or $_.solidEnd -ne 1 -or $_.solidPersist -lt 1 -or $_.groupEnd -ne $_.groupBegin -or @($_.bodies|Sort-Object -Unique).Count -ne 3})){throw 'Sensor transition assertions incomplete'}
    if($BurstTopology -and ($probes|Where-Object {!$_.burst -or $_.replacementCalls -ne 4})){throw 'Burst topology assertions incomplete'}
    if($RemoveComponent -and ($probes|Where-Object {!$_.componentRemoved -or $_.staleChecks -ne 6})){throw 'Component removal wrapper assertions incomplete'}
    if($SubscriberLifetime -and ($probes|Where-Object {!$_.subscriberLifetime -or $_.sensorBegin -ne 4 -or $_.sensorEnd -ne 4 -or $_.observerBegin -ne 4 -or $_.observerEnd -ne 2 -or $_.targetBegin -ne 2 -or $_.targetEnd -ne 1 -or $_.beginOnly -ne 4 -or $_.disables -ne 1 -or $_.enables -ne 1 -or $_.seedTick -le $_.enableAfterTick -or $_.reactivatedPersists -lt 1})){throw 'Subscriber lifetime assertions incomplete'}
    if($RoleTransition -and ($probes|Where-Object {!$_.roleTransition -or !$_.finalRoleChanged -or ($_.attackBegins -join ',') -ne '2,1,2' -or ($_.attackEnds -join ',') -ne '2,1,2' -or $_.alternateBegin -ne 1 -or $_.alternateEnd -ne 1 -or $_.alternatePersist -lt 1 -or $_.alternateTargetBegin -ne 1 -or $_.alternateTargetEnd -ne 1 -or $_.targetBegin -ne $_.targetEnd -or @($_.bodies|Sort-Object -Unique).Count -ne 3})){throw 'Role transition assertions incomplete'}
    if($ExplicitBinding -and ($probes|Where-Object {!$_.explicitBinding})){throw 'Scoped role binding assertions incomplete'}
    if($ContactFault){
        if($state.failureCount -ne 0){throw 'Isolated contact fault stopped the Play session'}
        $cleanup=@([regex]::Matches((Get-Content "$out/editor.out" -Raw),'\{"marker":"physics.contact.fault.cleanup"[^\r\n]+\}')|ForEach-Object {$_.Value|ConvertFrom-Json})
        if($cleanup.Count -ne 4 -or ($cleanup|Where-Object {!$_.disposed})){throw 'Contact fault scopes not disposed before EndSimulation'}
        $faults=@([regex]::Matches((Get-Content "$out/editor.out" -Raw),'\{"marker":"physics.contact.fault"[^\r\n]+\}')|ForEach-Object {$_.Value|ConvertFrom-Json})
        if($faults.Count -ne 2 -or ($faults|Where-Object {$_.overflow -ne ($FaultCase -eq 'Overflow') -or ($_.overflow -and (!$_.overflowed -or $_.required -lt 2))})){throw 'Fault injection evidence incomplete'}
        if($probes|Where-Object {!$_.contactFault -or $_.faultCalls -ne 1 -or $_.faultDisables -ne 1 -or $_.healthyContacts -lt 4 -or ($_.overflow -and !$_.readRejected)}){throw 'Fault isolation assertions incomplete'}
    }
    $lateProbes=@()
    if($LateOverlap){
        $lateProbes=@([regex]::Matches((Get-Content -LiteralPath "$out/editor.out" -Raw),'\{"marker":"physics.contact.late"[^\r\n]+\}')|ForEach-Object {$_.Value|ConvertFrom-Json})
        if($lateProbes.Count -ne 2 -or ($lateProbes|Where-Object {!$_.success -or $_.begins -ne 1 -or $_.beginOnly -ne 1 -or $_.persists -lt 2 -or $_.firstTick -le $_.spawnTick})){throw 'Late overlap assertions incomplete'}
    }
    @{result='CONTACT_STREAM_EDITOR_OK';solidContacts=[bool]$SolidContacts;scene=$scene;sceneSha256=$sceneHash;cycles=$cycles;probes=$probes;lateProbes=$lateProbes;latePrefab=$latePrefab;groupTargets=[bool]$GroupTargets;retirement=[bool]$Retirement;removeComponent=[bool]$RemoveComponent;topology=[bool]$Topology;burstTopology=[bool]$BurstTopology;sensorTransition=[bool]$SensorTransition;subscriberLifetime=[bool]$SubscriberLifetime;roleTransition=[bool]$RoleTransition;explicitBinding=[bool]$ExplicitBinding;contactFault=$ContactFault;faultCase=$FaultCase;commands=$script:sequence}|ConvertTo-Json -Depth 10|Set-Content "$out/result.json" -Encoding utf8
    "CONTACT_STREAM_EDITOR_OK evidence=$out"
    $null=Command 'quit'
} finally {
    if(!$process.WaitForExit(30000)){$process.Kill();$process.WaitForExit()}
}
