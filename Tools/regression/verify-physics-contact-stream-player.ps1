[CmdletBinding()]
param([Parameter(Mandatory)][string]$EngineDistribution, [Parameter(Mandatory)][string]$EditorResult, [string]$ReuseWork='', [switch]$Shipping,[string]$ReuseProject='', [ValidateSet('Debug','Release')][string]$Configuration='Release')
$ErrorActionPreference='Stop'
$repo=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$editor=Get-Content -LiteralPath $EditorResult -Raw|ConvertFrom-Json
if($editor.result -ne 'CONTACT_STREAM_EDITOR_OK'){throw 'Passed Editor fixture required'}
$work=if($ReuseWork){[IO.Path]::GetFullPath($ReuseWork)}else{Join-Path $repo ('Build/Verification/ContactStream/Player-'+[guid]::NewGuid().ToString('N'))}
New-Item -ItemType Directory -Force $work|Out-Null
$project=if($ReuseProject){(Resolve-Path -LiteralPath $ReuseProject).Path}else{Join-Path $work 'Project'}
if(!$ReuseWork){
if($ReuseProject){
    if((Get-FileHash -LiteralPath "$project/Assets/Scenes/ContactStream.creator").Hash -ne (Get-FileHash -LiteralPath $editor.scene).Hash){throw 'Reuse scene identity mismatch'}
} else {
New-Item -ItemType Directory -Force "$project/Assets/Scenes","$project/Assets/Script","$project/Assets/Shaders"|Out-Null
Get-ChildItem -LiteralPath "$repo/Dynamic_CPP/Assets/Shaders" | Copy-Item -Destination "$project/Assets/Shaders" -Recurse
Copy-Item "$repo/Dynamic_CPP/ProjectSetting" "$project/ProjectSetting" -Recurse
Copy-Item -LiteralPath $editor.scene -Destination "$project/Assets/Scenes/ContactStream.creator"
Copy-Item -LiteralPath ($editor.scene+'.meta') -Destination "$project/Assets/Scenes/ContactStream.creator.meta"
Copy-Item "$repo/GameScripts/PhysicsContactStreamProbe.cs" "$project/Assets/Script"
if($editor.solidContacts){Copy-Item "$repo/GameScripts/PhysicsSolidContactProbe.cs" "$project/Assets/Script"}
if($editor.contactFault){Copy-Item "$repo/GameScripts/PhysicsContactFaultProbe.cs" "$project/Assets/Script"}
if($editor.roleTransition){Copy-Item "$repo/GameScripts/PhysicsRoleContactProbe.cs" "$project/Assets/Script"}
if($editor.subscriberLifetime){Copy-Item "$repo/GameScripts/PhysicsSubscriberContactProbe.cs" "$project/Assets/Script"}
if($editor.sensorTransition){Copy-Item "$repo/GameScripts/PhysicsSensorTransitionProbe.cs" "$project/Assets/Script"}
if($editor.topology){Copy-Item "$repo/GameScripts/PhysicsTopologyContactProbe.cs" "$project/Assets/Script"}
if($editor.latePrefab){
    New-Item -ItemType Directory -Force "$project/Assets/Prefabs"|Out-Null
    Copy-Item -LiteralPath $editor.latePrefab,($editor.latePrefab+'.meta') -Destination "$project/Assets/Prefabs"
    Copy-Item "$repo/GameScripts/PhysicsLateContactProbe.cs" "$project/Assets/Script"
}
}
# Debug cooker validates a model corpus even for physics-only scenes.
# This small packaging input is not instantiated in the contact test scene.
if(!(Get-ChildItem "$project/Assets" -Recurse -File -Filter '*.glb' | Select-Object -First 1)) {
    New-Item -ItemType Directory -Force "$project/Assets/Models" | Out-Null
    Copy-Item "$repo/Dynamic_CPP/Assets/Models/Prim_Cube.glb","$repo/Dynamic_CPP/Assets/Models/Prim_Cube.glb.meta" "$project/Assets/Models"
}
$tool=Join-Path $EngineDistribution "Bin/x64-$Configuration/Tools/CreatorBuildTool/CreatorBuildTool.exe"
& $tool select-engine --engine-distribution $EngineDistribution --project $project *> "$work/select.log"
if($LASTEXITCODE){throw 'Engine selection failed'}
[string[]]$packageMode=if($Shipping){@('--shipping')}else{@()}
& $tool package-game @packageMode --repository $repo --engine-distribution $EngineDistribution --project $project --config $Configuration --stage-root "$work/Staging" --startup-scene ContactStream.creator --smoke-frames 120 --smoke-timeout-sec 300 *> "$work/package.log"
if($LASTEXITCODE){throw "Contact package failed: $work/package.log"}
} else {
    if(!(Test-Path -LiteralPath "$work/Staging/Project.current.json") -or
       (Get-FileHash -LiteralPath "$project/Assets/Scenes/ContactStream.creator").Hash -ne $editor.sceneSha256){throw 'Reuse fixture identity mismatch'}
}
$pointer=Get-Content "$work/Staging/Project.current.json" -Raw|ConvertFrom-Json
if($pointer.config -ne $Configuration){throw 'Package configuration mismatch'}
if($pointer.verification -ne 'passed'){throw 'Package smoke missing'}
if([bool]$pointer.shipping -ne [bool]$Shipping){throw 'Package Shipping mode mismatch'}
$stage=Join-Path "$work/Staging" $pointer.releaseDirectory
function FileSet([string]$rootPath) {
    @(Get-ChildItem -LiteralPath $rootPath -File -Recurse|ForEach-Object {
        [IO.Path]::GetRelativePath($rootPath,$_.FullName)+'|'+(Get-FileHash -LiteralPath $_.FullName).Hash
    }|Sort-Object)-join "`n"
}
$before=FileSet $stage
$runtime=Join-Path $work 'Runtime'
New-Item -ItemType Directory -Force $runtime|Out-Null
$process=Start-Process "$stage/Player.exe" -ArgumentList '--smoke','2000','--smoke-promotions','8' -WorkingDirectory $stage -WindowStyle Hidden -Environment @{TEMP=$runtime;TMP=$runtime;CE_PHYSICS_RESOURCE_PROBE=$(if($editor.contactFault){'1'}else{'0'})} -RedirectStandardOutput "$work/player.out" -RedirectStandardError "$work/player.err" -PassThru
try {
    $deadline=(Get-Date).AddSeconds(300)
    while(!$process.WaitForExit(500)){
        if((Get-Date) -gt $deadline){throw 'Player contact timeout'}
    }
    if($process.ExitCode -ne 0){throw "Player failed: $($process.ExitCode)"}
    $stdout=[IO.File]::ReadAllText("$work/player.out")
    $probes=@([regex]::Matches($stdout,'\{"marker":"physics.contact.stream"[^\r\n]+\}')|ForEach-Object {$_.Value|ConvertFrom-Json})
    if($probes.Count -ne $(if($editor.solidContacts -or $editor.retirement -or $editor.topology -or $editor.sensorTransition -or $editor.subscriberLifetime -or $editor.roleTransition -or $editor.contactFault){1}else{2}) -or ($probes|Where-Object {!$_.success}) -or @($probes.owner|Sort-Object -Unique).Count -ne $(if($editor.solidContacts -or $editor.retirement -or $editor.topology -or $editor.sensorTransition -or $editor.subscriberLifetime -or $editor.roleTransition -or $editor.contactFault){1}else{2})){throw 'Player contact assertions incomplete'}
    if($editor.solidContacts -and ($probes|Where-Object {!$_.solidContacts -or ($_.begins -join ',') -ne '1,1' -or ($_.ends -join ',') -ne '1,1' -or $_.partialPersists -lt 2 -or $_.stage -ne 3})){throw 'Player solid assertions incomplete'}
    if($editor.retirement -and ($probes|Where-Object {!$_.retired -or $_.deletedAlive -ne [bool]$editor.removeComponent})){throw 'Player deleted endpoint assertions incomplete'}
    if($editor.groupTargets -and !$editor.solidContacts -and !$editor.topology -and !$editor.sensorTransition -and !$editor.subscriberLifetime -and !$editor.roleTransition -and !$editor.contactFault -and ($probes|Where-Object {!$_.grouped -or $_.sensorBegin -ne 2 -or $_.sensorEnd -ne 2 -or $_.groupBegin -ne 1 -or $_.groupEnd -ne 1 -or $_.groupPersist -lt $(if($editor.solidContacts -or $editor.retirement -or $editor.topology -or $editor.sensorTransition -or $editor.subscriberLifetime -or $editor.roleTransition -or $editor.contactFault){1}else{2})})){throw 'Player target grouping assertions incomplete'}
    if($editor.topology -and ($probes|Where-Object {!$_.topology -or $_.oldEnds -ne 2 -or $_.newBegins -ne 2 -or $_.newEnds -ne 2 -or $_.sensorBegin -ne 4 -or $_.sensorEnd -ne 4 -or $_.oldBodyIdentity -eq 0 -or $_.newBodyIdentity -eq 0 -or $_.oldBodyIdentity -eq $_.newBodyIdentity -or $_.groupBegin -lt 1 -or $_.groupBegin -gt 2 -or $_.groupEnd -ne $_.groupBegin})){throw 'Player topology assertions incomplete'}
    if($editor.sensorTransition -and ($probes|Where-Object {!$_.sensorTransition -or ($_.sensorBegins -join ',') -ne '2,1,2' -or ($_.sensorEnds -join ',') -ne '2,1,2' -or $_.solidBegin -ne 1 -or $_.solidEnd -ne 1 -or $_.solidPersist -lt 1 -or $_.groupEnd -ne $_.groupBegin -or @($_.bodies|Sort-Object -Unique).Count -ne 3})){throw 'Player sensor transition assertions incomplete'}
    if($editor.burstTopology -and ($probes|Where-Object {!$_.burst -or $_.replacementCalls -ne 4})){throw 'Player burst topology assertions incomplete'}
    if($editor.removeComponent -and ($probes|Where-Object {!$_.componentRemoved -or $_.staleChecks -ne 6})){throw 'Player component removal wrapper assertions incomplete'}
    if($editor.subscriberLifetime -and ($probes|Where-Object {!$_.subscriberLifetime -or $_.sensorBegin -ne 4 -or $_.sensorEnd -ne 4 -or $_.observerBegin -ne 4 -or $_.observerEnd -ne 2 -or $_.targetBegin -ne 2 -or $_.targetEnd -ne 1 -or $_.beginOnly -ne 4 -or $_.disables -ne 1 -or $_.enables -ne 1 -or $_.seedTick -le $_.enableAfterTick -or $_.reactivatedPersists -lt 1})){throw 'Player subscriber lifetime assertions incomplete'}
    if($editor.roleTransition -and ($probes|Where-Object {!$_.roleTransition -or !$_.finalRoleChanged -or ($_.attackBegins -join ',') -ne '2,1,2' -or ($_.attackEnds -join ',') -ne '2,1,2' -or $_.alternateBegin -ne 1 -or $_.alternateEnd -ne 1 -or $_.alternatePersist -lt 1 -or $_.alternateTargetBegin -ne 1 -or $_.alternateTargetEnd -ne 1 -or $_.targetBegin -ne $_.targetEnd -or @($_.bodies|Sort-Object -Unique).Count -ne 3})){throw 'Player role transition assertions incomplete'}
    if($editor.explicitBinding -and ($probes|Where-Object {!$_.explicitBinding})){throw 'Player scoped role binding assertions incomplete'}
    if($editor.contactFault){
        $cleanup=@([regex]::Matches($stdout,'\{"marker":"physics.contact.fault.cleanup"[^\r\n]+\}')|ForEach-Object {$_.Value|ConvertFrom-Json})
        if($cleanup.Count -ne 2 -or ($cleanup|Where-Object {!$_.disposed})){throw 'Player contact scopes not disposed before EndSimulation'}
        $resources=@([regex]::Matches($stdout,'\[physics.player.resources\] (\{[^\r\n]+\})')|ForEach-Object {$_.Groups[1].Value|ConvertFrom-Json})
        if($resources.Count -ne 1 -or !$resources[0].enabled -or !$resources[0].balanced -or $resources[0].created[0] -eq 0 -or ($resources[0].created -join ',') -ne ($resources[0].released -join ',')){throw 'Player physics resources did not balance'}
        $faults=@([regex]::Matches($stdout,'\{"marker":"physics.contact.fault"[^\r\n]+\}')|ForEach-Object {$_.Value|ConvertFrom-Json})
        if($faults.Count -ne 1 -or ($faults|Where-Object {$_.overflow -ne ($editor.faultCase -eq 'Overflow') -or ($_.overflow -and (!$_.overflowed -or $_.required -lt 2))})){throw 'Player fault injection evidence incomplete'}
        if($probes|Where-Object {!$_.contactFault -or $_.faultCalls -ne 1 -or $_.faultDisables -ne 1 -or $_.healthyContacts -lt 4 -or ($_.overflow -and !$_.readRejected)}){throw 'Player fault isolation assertions incomplete'}
    }
    $lateProbes=@()
    if($editor.latePrefab){
        $lateProbes=@([regex]::Matches($stdout,'\{"marker":"physics.contact.late"[^\r\n]+\}')|ForEach-Object {$_.Value|ConvertFrom-Json})
        if($lateProbes.Count -ne 1 -or ($lateProbes|Where-Object {!$_.success -or $_.begins -ne 1 -or $_.beginOnly -ne 1 -or $_.persists -lt 2 -or $_.firstTick -le $_.spawnTick})){throw 'Player late overlap assertions incomplete'}
    }
    $smokeRows=@([regex]::Matches($stdout,'\[player\.smoke\] (\{[^\r\n]+\})')|ForEach-Object {$_.Groups[1].Value|ConvertFrom-Json})
    $progress=@([regex]::Matches($stdout,'\[player\.smoke\.progress\] (\{[^\r\n]+\})')|ForEach-Object {$_.Groups[1].Value|ConvertFrom-Json})
    if($smokeRows.Count -ne 1 -or !$smokeRows[0].ready -or $smokeRows[0].displayPromotions -lt 8 -or
       $smokeRows[0].frames -lt 2000 -or $progress.Count -eq 0 -or $progress[-1].completed -le 0 -or
       $smokeRows[0].submittedGameFrameId -le 0){throw 'Completed display evidence absent'}
    if($Shipping -and ($stdout -notmatch '\[player.service\] compiled=no enabled=no' -or (Get-ChildItem -LiteralPath $runtime -Recurse -File -Filter endpoint.json))){throw 'Shipping service isolation failed'}
    if((FileSet $stage) -ne $before){throw 'Player changed packaged inputs'}
    @{result='CONTACT_STREAM_PLAYER_OK';shipping=[bool]$Shipping;exitCode=$process.ExitCode;packageImmutable=$true;stage=$stage;probes=$probes;lateProbes=$lateProbes;smoke=$smokeRows[0];editorResult=$EditorResult}|ConvertTo-Json -Depth 10|Set-Content "$work/result.json" -Encoding utf8
    "CONTACT_STREAM_PLAYER_OK evidence=$work"
} finally {
    if(!$process.HasExited){$process.Kill();$process.WaitForExit()}
}
