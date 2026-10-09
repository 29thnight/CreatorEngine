[CmdletBinding()]
param([Parameter(Mandatory)][string]$Stage, [int]$TimeoutSeconds=120, [switch]$Shipping, [switch]$Ddol, [switch]$Hierarchy, [switch]$Offscreen, [switch]$Mesh, [switch]$GeometryDdol, [string]$DestinationScene='', [ValidateRange(1,1000000)][int]$SmokeFrames=2000, [switch]$PlanarInput)
$ErrorActionPreference='Stop'
if($PlanarInput -and ($Ddol -or $Hierarchy -or $Mesh -or $GeometryDdol -or $DestinationScene)){throw 'PlanarInput requires an isolated character gate'}
$repo=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$Stage=[IO.Path]::GetFullPath($Stage)
$out=Join-Path $repo ('Build/Obj/Phase19Player/run-'+[guid]::NewGuid().ToString('N'))
$runtime=Join-Path $out 'Runtime'
New-Item -ItemType Directory -Force $runtime | Out-Null
$before=@{}
Get-ChildItem -LiteralPath $Stage -File -Recurse | ForEach-Object {$before[$_.FullName]=(Get-FileHash -LiteralPath $_.FullName).Hash}
if($Hierarchy -or $GeometryDdol){$Ddol=$true}
if($GeometryDdol){
    $Mesh=$true
    # The geometry transfer contract expects a destination with one mesh asset.
    # Reloading the three-asset startup scene cannot satisfy that contract.
    if(!$DestinationScene){$DestinationScene='PhysicsCharacterMesh.creator'}
}
$launchArguments=if($Shipping -or $Ddol){@('--smoke',"$SmokeFrames",'--smoke-promotions','8')}else{@('--command-service')}
if($DestinationScene -and !$Ddol){throw 'DestinationScene requires Ddol'}
if($Ddol){$launchArguments+=@('--smoke-ddol-character','CharacterGateActor')}
if($Hierarchy){$launchArguments+=@('--smoke-ddol-hierarchy')}
if($GeometryDdol){$launchArguments+=@('--smoke-ddol-geometry')}
if($DestinationScene){$launchArguments+=@('--smoke-reload-destination',$DestinationScene)}
if($Offscreen){
    if(!$Shipping -and !$Ddol){throw 'Offscreen gate requires smoke mode'}
    $launchArguments+=@('--smoke-offscreen')
}
$process=Start-Process -FilePath (Join-Path $Stage 'Player.exe') -ArgumentList $launchArguments -WorkingDirectory $Stage -WindowStyle Hidden -Environment @{TEMP=$runtime;TMP=$runtime} -RedirectStandardOutput "$out/player.out" -RedirectStandardError "$out/player.err" -PassThru
@{pid=$process.Id;stage=$Stage;arguments=$launchArguments;hierarchy=[bool]$Hierarchy;ddol=[bool]$Ddol;shipping=[bool]$Shipping;offscreen=[bool]$Offscreen} | ConvertTo-Json -Depth 10 | Set-Content "$out/launch.json" -Encoding utf8
try {
    $deadline=(Get-Date).AddSeconds($TimeoutSeconds)
    $info=$null
    $policy=$null
    $finalPolicy=$null
    if(!$Shipping -and !$Ddol){
        do {
            if($process.HasExited){throw "Player exited before verification: $($process.ExitCode)"}
            foreach($endpoint in Get-ChildItem -LiteralPath $runtime -Recurse -Filter endpoint.json -ErrorAction SilentlyContinue){
                $candidate=Get-Content -LiteralPath $endpoint.FullName -Raw | ConvertFrom-Json
                if($candidate.pid -eq $process.Id){$info=$candidate;break}
            }
            if(!$info){Start-Sleep -Milliseconds 150}
        } while(!$info -and (Get-Date) -lt $deadline)
        if(!$info){throw 'Owned Player endpoint missing'}
        $headers=@{Authorization="Bearer $($info.token)"}
        $base="http://127.0.0.1:$($info.port)"
        function Command([string]$name){
            $result=Invoke-RestMethod "$base/command" -Method Post -Headers $headers -ContentType 'application/json' -Body (@{command=$name;args=@();mode='sync'}|ConvertTo-Json -Compress)
            if($result.status -ne 'succeeded'){throw "$name failed"}
            $result|ConvertTo-Json -Depth 20 -Compress|Add-Content "$out/commands.jsonl" -Encoding utf8
            return $result
        }
        $health=Invoke-RestMethod "$base/health" -Headers $headers
        if($health.role -ne 'player'){throw 'Wrong host role'}
        $policy=(Command 'player.scene').data
        if(!$policy.simulating -or $policy.editorSceneLoaded -ne $false -or $policy.hasAuthoringSnapshot -ne $false){throw 'Player entered Editor restoration mode'}
    }

    $probe=$null
    do {
        $stdout=Get-Content "$out/player.out" -Raw
        $probePattern=if($PlanarInput){'\[physics.player.planar\] (\{[^\r\n]+\})'}else{'\[physics.player\] (\{[^\r\n]+\})'}
        if($stdout -match $probePattern){$probe=$Matches[1]|ConvertFrom-Json;break}
        if($process.HasExited){throw "Player exited before motion evidence: $($process.ExitCode)"}
        Start-Sleep -Milliseconds 100
    } while((Get-Date) -lt $deadline)
    $expectedChecks=if($PlanarInput){16}else{12}
    if(!$probe -or !$probe.complete -or $probe.failed -ne 0 -or $probe.passed -ne $expectedChecks){throw 'Cooked character motion probe failed or never completed'}
    $meshEvidence=$null
    if($Mesh){
        $artifacts=@(Get-ChildItem $runtime -Recurse -File -Filter '*.cepg')
        $expectedArtifacts=if($GeometryDdol){3}else{1}
        if($artifacts.Count -ne $expectedArtifacts){throw 'Unexpected mounted cooked geometry artifact count'}
        if(Get-ChildItem $runtime -Recurse -File -Filter '*.cegeometry'){throw 'Player contains authoring geometry fallback'}
        $meshEvidence=@{artifactCount=$artifacts.Count;artifactSha256=@($artifacts|ForEach-Object {(Get-FileHash $_.FullName).Hash});authoringGeometryFiles=0;motionChecks=$probe.passed}
    }
    if(!$Shipping -and !$Ddol){
        $finalPolicy=(Command 'player.scene').data
        if($finalPolicy.hasAuthoringSnapshot -ne $false -or $finalPolicy.editorSceneLoaded -ne $false){throw 'Runtime created an authoring backup'}
        $null=Command 'quit'
    }

    while(!$process.WaitForExit(1000)){
        if((Get-Date) -ge $deadline){throw 'Player graceful quit timed out'}
    }
    $hierarchyProbe=$null
    $ddolProbe=$null
    if($Ddol){
        $stdout=Get-Content "$out/player.out" -Raw
        if($stdout -match '\[physics.player.ddol\] (\{[^\r\n]+\})'){$ddolProbe=$Matches[1]|ConvertFrom-Json}
        if(!$ddolProbe -or !$ddolProbe.complete -or $ddolProbe.failed -ne 0 -or $ddolProbe.passed -ne 8){throw 'Cooked Player DDOL probe failed or missing'}
        if($stdout -notmatch '\[player.smoke.reload\] activated=true gameStart=true pending=false compositionSubmittedAfterActivation=true'){throw 'Missing submitted destination composition'}
    }

    if($Hierarchy){
        if($stdout -match '\[physics.player.hierarchy\] (\{[^\r\n]+\})'){$hierarchyProbe=$Matches[1]|ConvertFrom-Json}
        if(!$hierarchyProbe -or !$hierarchyProbe.complete -or $hierarchyProbe.failed -ne 0 -or $hierarchyProbe.nodes -ne 5 -or $hierarchyProbe.passed -ne 56){throw 'Cooked Player hierarchy/handle probe failed or missing'}
    }

    $geometryProbe=$null
    $geometryMotion=@{}
    if($GeometryDdol){
        if($stdout -match '\[physics.player.geometry\] (\{[^\r\n]+\})'){$geometryProbe=$Matches[1]|ConvertFrom-Json}
        if(!$geometryProbe -or !$geometryProbe.complete -or $geometryProbe.failed -ne 0 -or $geometryProbe.nodes -ne 3 -or $geometryProbe.passed -ne 29){throw 'Cooked mesh DDOL probe failed'}
        foreach($kind in @('convex','heightfield')){
            if($stdout -notmatch ('\[physics.player.'+$kind+'\] (\{[^\r\n]+\})')){throw "Missing $kind motion evidence"}
            $motion=$Matches[1]|ConvertFrom-Json
            if(!$motion.complete -or $motion.failed -ne 0 -or $motion.passed -ne 12){throw "$kind motion gate failed"}
            $geometryMotion[$kind]=$motion
        }
    }
    if($process.ExitCode -ne 0){throw "Player quit failed: $($process.ExitCode)"}
    $afterFiles=@(Get-ChildItem -LiteralPath $Stage -File -Recurse)
    if($afterFiles.Count -ne $before.Count){throw 'Player modified immutable package file set'}
    foreach($file in $afterFiles){if($before[$file.FullName] -ne (Get-FileHash -LiteralPath $file.FullName).Hash){throw 'Player modified package bytes'}}
    $logs=Get-ChildItem -LiteralPath $runtime -Recurse -File -Filter '*.html' | ForEach-Object {Get-Content $_.FullName -Raw}
    $combined=($logs -join "`n")+(Get-Content "$out/player.out" -Raw)
    if($combined -notmatch '\[scene.document\] source=cooked' -or $combined -notmatch '\[asset.catalog\] source=cemf'){
        throw 'Missing cooked Scene/CEMF product evidence'
    }
    if($DestinationScene){
        $meta=Join-Path $PSScriptRoot "fixtures/$DestinationScene.meta"
        $guid=(Select-String -LiteralPath $meta -Pattern '^guid: (.+)$').Matches[0].Groups[1].Value
        if($combined -notmatch ('\[scene.document\] source=cooked guid='+[regex]::Escape($guid))){throw 'Cooked destination GUID was not loaded'}
        if($combined -notmatch '\[runtime.text-parser\] calls=0(?!\d)'){throw 'Destination used runtime text parser'}
    }

    $smokeEvidence=$null
    if($Shipping){
        if($combined -notmatch '\[player.service\] compiled=no enabled=no' -or
           $combined -notmatch '\[runtime.text-parser\] calls=0(?!\d)'){
            throw 'Missing Shipping service isolation, parser or graceful smoke completion evidence'
        }
        if($combined -notmatch '\[SMOKE\] frame limit reached[^\r\n]*\((\d+) GT frames, display frame (\d+), promotions (\d+)\)' -or
           [int]$Matches[1] -lt $SmokeFrames -or [int]$Matches[2] -lt 2 -or [int]$Matches[3] -lt 8){
            throw 'Shipping did not complete requested frames and display promotions'
        }
        $smokeEvidence=@{gameFrames=[int]$Matches[1];displayFrame=[int]$Matches[2];promotions=[int]$Matches[3];serviceCompiled=$false;textParserCalls=0}
        if(Get-ChildItem -LiteralPath $runtime -Recurse -Filter endpoint.json){throw 'Shipping created a command endpoint'}
    }

    @{result='PHYSICS_CHARACTER_PLAYER_OK';planarInput=[bool]$PlanarInput;stage=$Stage;shipping=[bool]$Shipping;smoke=$smokeEvidence;ddol=[bool]$Ddol;destinationScene=$DestinationScene;ddolProbe=$ddolProbe;hierarchy=[bool]$Hierarchy;offscreen=[bool]$Offscreen;mesh=$meshEvidence;geometryProbe=$geometryProbe;geometryMotion=$geometryMotion;hierarchyProbe=$hierarchyProbe;probe=$probe;initialPolicy=$policy;finalPolicy=$finalPolicy;exitCode=$process.ExitCode;immutableFiles=$before.Count;runtimeSha256=(Get-FileHash (Join-Path $Stage 'Player.runtime.dll')).Hash} | ConvertTo-Json -Depth 20 | Set-Content "$out/result.json" -Encoding utf8
    Write-Output "PHYSICS_CHARACTER_PLAYER_OK evidence=$out"
} finally {
    if(!$process.HasExited){$process.Kill();$process.WaitForExit()}
}
