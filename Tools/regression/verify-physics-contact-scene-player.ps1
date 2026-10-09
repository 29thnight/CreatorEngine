[CmdletBinding()]
param([Parameter(Mandatory)][string]$EngineDistribution,[Parameter(Mandatory)][string]$EditorResult,[switch]$Shipping,[string]$ReuseProject='', [ValidateSet('Debug','Release')][string]$Configuration='Release')
$ErrorActionPreference='Stop'
$repo=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$editor=Get-Content -LiteralPath $EditorResult -Raw|ConvertFrom-Json
if($editor.result -ne 'CONTACT_SCENE_EDITOR_OK'){throw 'Passed Editor fixture required'}
if($editor.assemblyReload){throw 'This Player gate verifies scene transitions; Editor assembly reload evidence is not a scene transition fixture'}
$work=Join-Path $repo ('Build/Verification/ContactStream/ScenePlayer-'+[guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Force $work|Out-Null
$project=if($ReuseProject){(Resolve-Path -LiteralPath $ReuseProject).Path}else{Join-Path $work 'Project'}

if($ReuseProject){
    if((Get-FileHash -LiteralPath "$project/Assets/Scenes/ContactStream.creator").Hash -ne (Get-FileHash -LiteralPath $editor.scene).Hash){throw 'Reuse scene identity mismatch'}
} else {
New-Item -ItemType Directory -Force "$project/Assets/Scenes","$project/Assets/Script","$project/Assets/Shaders"|Out-Null
Get-ChildItem -LiteralPath "$repo/Dynamic_CPP/Assets/Shaders" | Copy-Item -Destination "$project/Assets/Shaders" -Recurse
Copy-Item "$repo/Dynamic_CPP/ProjectSetting" "$project/ProjectSetting" -Recurse
Copy-Item -LiteralPath $editor.scene -Destination "$project/Assets/Scenes/ContactStream.creator"
Copy-Item -LiteralPath ($editor.scene+'.meta') -Destination "$project/Assets/Scenes/ContactStream.creator.meta"
Copy-Item "$repo/GameScripts/PhysicsSceneContactProbe.cs" "$project/Assets/Script"
Copy-Item -LiteralPath $editor.destination -Destination "$project/Assets/Scenes/ContactDestination.creator"
if(Test-Path -LiteralPath ($editor.destination+'.meta')){
    Copy-Item -LiteralPath ($editor.destination+'.meta') -Destination "$project/Assets/Scenes/ContactDestination.creator.meta"
} else {
    "guid: $([guid]::NewGuid())`nimportSettings:`n  extension: .creator`n  timestamp: $([DateTime]::UtcNow.ToFileTimeUtc())"|Set-Content "$project/Assets/Scenes/ContactDestination.creator.meta" -Encoding utf8
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
$pointer=Get-Content "$work/Staging/Project.current.json" -Raw|ConvertFrom-Json
if($pointer.config -ne $Configuration){throw 'Package configuration mismatch'}
if($pointer.verification -ne 'passed'){throw 'Package smoke failed'}
if([bool]$pointer.shipping -ne [bool]$Shipping){throw 'Package Shipping mode mismatch'}
$stage=Join-Path "$work/Staging" $pointer.releaseDirectory
function FileSet($root){@(Get-ChildItem $root -File -Recurse|ForEach-Object {[IO.Path]::GetRelativePath($root,$_.FullName)+'|'+(Get-FileHash $_.FullName).Hash}|Sort-Object)-join "`n"}
$beforeFiles=FileSet $stage
$out=$work
$runtime=Join-Path $work 'Runtime'
New-Item -ItemType Directory -Path $runtime|Out-Null
$launchArguments=@('--smoke','2000','--smoke-promotions','8','--smoke-reload')
if($editor.ddol){$launchArguments+=@('--smoke-reload-destination','ContactDestination.creator')}
$process=Start-Process "$stage/Player.exe" -ArgumentList $launchArguments -WorkingDirectory $stage -WindowStyle Hidden -Environment @{TEMP=$runtime;TMP=$runtime;CE_PHYSICS_RESOURCE_PROBE='1';CE_PHYSICS_CONTACT_SCENE_PROBE='1';CE_PHYSICS_CONTACT_DDOL=$(if($editor.ddol){'1'}else{'0'})} -RedirectStandardOutput "$out/player.out" -RedirectStandardError "$out/player.err" -PassThru
try {
    $deadline=(Get-Date).AddSeconds(300)
    while(!$process.WaitForExit(500)){if((Get-Date) -gt $deadline){throw 'Reload Player timed out'}}
    if($process.ExitCode -ne 0){throw 'Player failed'}
    if((FileSet $stage) -ne $beforeFiles){throw 'Player modified packaged inputs'}
    $stdout=Get-Content "$out/player.out" -Raw
    $probes=@([regex]::Matches($stdout,'\[physics.contact.scene\] (\{[^\r\n]+\})')|ForEach-Object {$_.Groups[1].Value|ConvertFrom-Json})
    $ends=@([regex]::Matches($stdout,'\[physics.contact.scene.end\] (\{[^\r\n]+\})')|ForEach-Object {$_.Groups[1].Value|ConvertFrom-Json})
    if($probes.Count -ne 2 -or !$probes[0].ready -or !$probes[1].ready -or $probes[0].runs -ne 1 -or $probes[1].runs -ne $(if($editor.ddol){1}else{2}) -or $probes[1].added -ne $(if($editor.ddol){2}else{1}) -or $probes[1].disposed -ne $(if($editor.ddol){0}else{1}) -or $probes[1].streams -ne $(if($editor.ddol){1}else{2}) -or $probes[1].begins -ne 2 -or $probes[1].persists -lt 2){throw 'Player reload contact assertions incomplete'}
    if($probes[0].bodyComponent -eq 0 -or $probes[1].bodyComponent -eq 0 -or ($editor.ddol -and $probes[0].bodyComponent -ne $probes[1].bodyComponent)){throw 'Player body wrapper identity mismatch'}
    if($ends.Count -ne $(if($editor.ddol){1}else{2}) -or $ends[-1].disposed -ne $(if($editor.ddol){1}else{2})){throw 'Player reload stream cleanup incomplete'}
    $resources=[regex]::Match($stdout,'\[physics.player.resources\] (\{[^\r\n]+\})').Groups[1].Value|ConvertFrom-Json
    if(!$resources.enabled -or !$resources.balanced -or $resources.created[0] -eq 0 -or ($resources.created -join ',') -ne ($resources.released -join ',')){throw 'Player physics resources unbalanced'}
    $smoke=[regex]::Match($stdout,'\[player.smoke\] (\{[^\r\n]+\})').Groups[1].Value|ConvertFrom-Json
    if(!$smoke.ready -or $smoke.frames -lt 2000 -or $smoke.displayPromotions -lt 8 -or $stdout -notmatch '\[player.smoke.reload\] activated=true'){throw 'Player reload/display acceptance incomplete'}
    if($Shipping -and ($stdout -notmatch '\[player.service\] compiled=no enabled=no' -or (Get-ChildItem -LiteralPath $runtime -Recurse -File -Filter endpoint.json))){throw 'Shipping service isolation failed'}
    @{result='CONTACT_SCENE_PLAYER_OK';shipping=[bool]$Shipping;editorResult=$EditorResult;ddol=[bool]$editor.ddol;stage=$stage;probes=$probes;ends=$ends;resources=$resources;smoke=$smoke;exitCode=$process.ExitCode;packageImmutable=$true}|ConvertTo-Json -Depth 10|Set-Content "$out/result.json" -Encoding utf8
    "CONTACT_SCENE_PLAYER_OK evidence=$out"
} finally {
    if(!$process.HasExited){$process.Kill();$process.WaitForExit()}
}
