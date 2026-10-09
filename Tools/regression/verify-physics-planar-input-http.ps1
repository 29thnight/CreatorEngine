[CmdletBinding()]
param([string]$Configuration='Release')
$ErrorActionPreference='Stop'
$repo=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
if(Get-Process CreatorEditor -ErrorAction SilentlyContinue){throw 'Owned Editor session required'}
$out=Join-Path $repo ('Build/Verification/ContactStream/PlanarEditor-'+[guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Force $out|Out-Null
$exe=Join-Path $repo "Bin/x64-$Configuration/Editor/CreatorEditor.exe"
$process=Start-Process $exe -ArgumentList '--command-service','--console','--allow-user-code' -WorkingDirectory (Split-Path $exe) -WindowStyle Hidden -RedirectStandardOutput "$out/editor.out" -RedirectStandardError "$out/editor.err" -PassThru
$script:sequence=0
try {
    $endpoint=Join-Path $repo 'Dynamic_CPP/Library/CommandService/endpoint.json'
    $until=(Get-Date).AddSeconds(180)
    $info=$null
    do {
        if($process.HasExited){throw 'Editor exited before endpoint'}
        if(Test-Path $endpoint){$candidate=Get-Content $endpoint -Raw|ConvertFrom-Json;if($candidate.pid -eq $process.Id){$info=$candidate}}
        if(!$info){Start-Sleep -Milliseconds 150}
    }until($info -or (Get-Date) -gt $until)
    if(!$info){throw 'Owned endpoint unavailable'}
    $base="http://127.0.0.1:$($info.port)"
    $headers=@{Authorization="Bearer $($info.token)"}
    function Command([string]$name,[string[]]$arguments=@()) {
        $script:sequence++
        $body=@{command=$name;args=@($arguments);mode='sync';correlationId="planar-$script:sequence"}|ConvertTo-Json -Compress
        $result=Invoke-RestMethod "$base/command" -Headers $headers -Method Post -ContentType 'application/json' -Body $body -TimeoutSec 120
        if($result.operationId){
            $deadline=(Get-Date).AddSeconds(120)
            do {Start-Sleep -Milliseconds 100;$result=Invoke-RestMethod "$base$($result.poll)" -Headers $headers;if((Get-Date) -gt $deadline){throw 'Operation timeout'}}until($result.state -eq 'completed')
        }
        @{command=$name;args=$arguments;result=$result}|ConvertTo-Json -Depth 30 -Compress|Add-Content "$out/commands.jsonl" -Encoding utf8
        if($result.status -ne 'succeeded'){throw "$name failed: $($result.message)"}
        return $result
    }
    $null=Command 'scene.new' @('PlanarInputGate')
    $null=Command 'object.create' @('CharacterGateFloor')
    $null=Command 'component.add' @('CharacterGateFloor','PhysicsBodyComponent')
    $null=Command 'object.property' @('CharacterGateFloor','PhysicsBodyComponent','m_motion','0')
    $null=Command 'object.transform' @('CharacterGateFloor','0','-0.5','0')
    $shapes=Join-Path $out 'shapes.json'
    '[{"shapeId":12,"kind":0,"halfExtent":[20,0.5,20]}]'|Set-Content $shapes -Encoding utf8
    $null=Command 'physics.shapes' @('CharacterGateFloor','PhysicsBodyComponent',$shapes)
    $null=Command 'object.create' @('CharacterGateActor')
    $null=Command 'object.transform' @('CharacterGateActor','0','3','0','0','35','0')
    $null=Command 'component.add' @('CharacterGateActor','CharacterMovementComponent')
    $null=Command 'object.property' @('CharacterGateActor','CharacterMovementComponent','m_acceleration','6')
    $null=Command 'script.add' @('CharacterGateActor','CharacterPlanarInputProbe')
    $null=Command 'object.create' @('PlanarCamera')
    $null=Command 'component.add' @('PlanarCamera','CameraComponent')
    $null=Command 'object.transform' @('PlanarCamera','3','2','-10')
    $null=Command 'object.property' @('PlanarCamera','CameraComponent','m_isPrimary','true')
    $scene=Join-Path $repo ('Dynamic_CPP/Assets/Scenes/'+(Split-Path $out -Leaf)+'.creator')
    $null=Command 'scene.save' @($scene)
    # Saving a new path updates the Scene root name; settle that authoring change before the baseline.
    $null=Command 'scene.save' @($scene)
    Copy-Item $scene -Destination "$out/authored.creator"
    $hash=(Get-FileHash $scene).Hash
    $initial=(Command 'object.describe' @('CharacterGateActor')).data
    $null=Command 'play.foreground_override' @('on')
    $cycles=@()
    for($cycle=1;$cycle -le 2;$cycle++) {
        $cycleHash=(Get-FileHash $scene).Hash
        $null=Command 'play'
        $until=(Get-Date).AddSeconds(90)
        do {
            Start-Sleep -Milliseconds 200
            $stdout=[string](Get-Content -LiteralPath "$out/editor.out" -Raw)
            $probes=@([regex]::Matches($stdout,'\[physics.player.planar\] (\{[^\r\n]+\})')|ForEach-Object {$_.Groups[1].Value|ConvertFrom-Json})
            if($probes|Where-Object {!$_.complete -or $_.failed -ne 0 -or $_.passed -ne 16}){throw 'Planar script assertions failed'}
            if((Get-Date) -gt $until){throw 'Planar script timed out'}
        }until($probes.Count -eq $cycle)
        $live=(Command 'object.describe' @('CharacterGateActor')).data
        if([Math]::Abs($live.position[0]-$initial.position[0]) -lt .5){throw 'No live movement'}
        $null=Command 'stop'
        $until=(Get-Date).AddSeconds(30)
        do {Start-Sleep -Milliseconds 100;$state=(Command 'play.state').data;if((Get-Date) -gt $until){throw 'Stop timeout'}}until(!$state.pending -and !$state.committed -and !$state.gameStart)
        if($state.failureCount -ne 0){throw 'Play lifecycle failed'}
        $restored=(Command 'object.describe' @('CharacterGateActor')).data
        foreach($field in @('position','rotation','scale')){
            if($initial.$field.Count -ne $restored.$field.Count){throw "Restored $field layout differs"}
            for($axis=0;$axis -lt $initial.$field.Count;$axis++){
                if([Math]::Abs($initial.$field[$axis]-$restored.$field[$axis]) -gt .0001){throw "Stop failed to restore $field"}
            }
        }
        if($initial.enabled -ne $restored.enabled -or $initial.layerId -ne $restored.layerId){throw 'Activation/layer restore failed'}
        $idle=(Command 'character.state' @('CharacterGateActor')).data
        if($idle.simulating -or $idle.tick -ne 0 -or $idle.forced -or $idle.fallVelocity -ne 0 -or @($idle.desiredVelocity|Where-Object {$_ -ne 0}).Count){throw 'Stop retained character runtime state'}
        if((Command 'script.invoke' @('CharacterPlanarInputProbe','Retired')).data.returnValue -ne 'retired'){throw 'Captured wrapper retargeted after Stop'}
        if((Get-FileHash $scene).Hash -ne $cycleHash){throw 'Play/Stop modified the file before save'}
        $null=Command 'scene.save' @($scene)
        & python (Join-Path $PSScriptRoot 'verify_scene_document_equivalence.py') "$out/authored.creator" $scene
        if($LASTEXITCODE){throw 'Play/Stop saved mutated authoring values'}
        $cycles+=@{cycle=$cycle;probe=$probes[-1];live=$live;restored=$restored;idle=$idle;wrapper='retired'}
    }
    New-Item -ItemType Directory -Force "$out/Fixture"|Out-Null
    Copy-Item $scene,($scene+'.meta') -Destination "$out/Fixture"
    @{result='PHYSICS_PLANAR_EDITOR_OK';scene=$scene;sceneSha256=$hash;initial=$initial;cycles=$cycles;commands=$script:sequence}|ConvertTo-Json -Depth 30|Set-Content "$out/result.json" -Encoding utf8
    $null=Command 'quit'
    "PHYSICS_PLANAR_EDITOR_OK evidence=$out"
} finally {
    if(!$process.WaitForExit(30000)){$process.Kill();$process.WaitForExit()}
}
