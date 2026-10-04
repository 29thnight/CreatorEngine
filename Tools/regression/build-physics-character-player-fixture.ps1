[CmdletBinding()]
param([Parameter(Mandatory)][string]$EngineDistribution, [ValidateSet('Debug','Release')][string]$Configuration='Debug', [string]$Work='', [switch]$Shipping, [switch]$Hierarchy, [switch]$Mesh, [switch]$Geometry, [switch]$Transition)
$ErrorActionPreference='Stop'
$repo=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
if(!$Work){$Work=Join-Path $repo ('Build/Obj/PhysicsCharacterPlayer-'+[guid]::NewGuid().ToString('N'))}
$Work=[IO.Path]::GetFullPath($Work)
if(Test-Path -LiteralPath $Work){throw 'Fixture work directory must be new'}
$project=Join-Path $Work 'Project'
$assets=Join-Path $project 'Assets'
New-Item -ItemType Directory -Force (Join-Path $assets 'Scenes'),(Join-Path $assets 'Script'),(Join-Path $assets 'Models') | Out-Null
Copy-Item -LiteralPath (Join-Path $repo 'Dynamic_CPP/Assets/Shaders') -Destination (Join-Path $assets 'Shaders') -Recurse
Copy-Item -LiteralPath (Join-Path $repo 'Dynamic_CPP/ProjectSetting') -Destination (Join-Path $project 'ProjectSetting') -Recurse
if($Transition){$Geometry=$true}
if($Geometry){$Mesh=$true}
if($Mesh -and $Hierarchy -and !$Transition){throw 'Mesh and Hierarchy fixtures are independent'}
$startup=if($Hierarchy){'PhysicsCharacterHierarchy.creator'}elseif($Transition){'PhysicsCharacterPlayer.creator'}elseif($Geometry){'PhysicsCharacterGeometry.creator'}elseif($Mesh){'PhysicsCharacterMesh.creator'}else{'PhysicsCharacterPlayer.creator'}
foreach($file in @('PhysicsCharacterHierarchy.creator','PhysicsCharacterHierarchy.creator.meta','PhysicsCharacterPlayer.creator','PhysicsCharacterPlayer.creator.meta','PhysicsCharacterDestination.creator','PhysicsCharacterDestination.creator.meta')){
    Copy-Item -LiteralPath (Join-Path $PSScriptRoot "fixtures/$file") -Destination (Join-Path $assets 'Scenes')
}
Copy-Item -LiteralPath (Join-Path $repo 'GameScripts/CharacterPlayerProbe.cs') -Destination (Join-Path $assets 'Script')
if($Mesh){
    foreach($file in @('PhysicsCharacterMesh.creator','PhysicsCharacterMesh.creator.meta')){
        Copy-Item -LiteralPath (Join-Path $PSScriptRoot "fixtures/$file") -Destination (Join-Path $assets 'Scenes')
    }
    foreach($file in @('PhysicsPlayerFloor.cegeometry','PhysicsPlayerFloor.cegeometry.meta')){
        Copy-Item -LiteralPath (Join-Path $PSScriptRoot "fixtures/$file") -Destination $assets
    }
}
if($Geometry){
    foreach($file in @('PhysicsCharacterGeometry.creator','PhysicsCharacterGeometry.creator.meta')){
        Copy-Item -LiteralPath (Join-Path $PSScriptRoot "fixtures/$file") -Destination (Join-Path $assets 'Scenes')
    }
    foreach($file in @('PhysicsPlayerConvex.cegeometry','PhysicsPlayerConvex.cegeometry.meta','PhysicsPlayerHeightfield.cegeometry','PhysicsPlayerHeightfield.cegeometry.meta')){
        Copy-Item -LiteralPath (Join-Path $PSScriptRoot "fixtures/$file") -Destination $assets
    }
}
# The package requires a model corpus; this small embedded GLB is not instantiated in the physics Scene.
foreach($file in @('Prim_Cube.glb','Prim_Cube.glb.meta')){
    Copy-Item -LiteralPath (Join-Path $repo "Dynamic_CPP/Assets/Models/$file") -Destination (Join-Path $assets 'Models')
}
$tool=Join-Path $EngineDistribution "Bin/x64-$Configuration/Tools/CreatorBuildTool/CreatorBuildTool.exe"
& $tool select-engine --engine-distribution $EngineDistribution --project $project
if($LASTEXITCODE){throw 'Fixture engine selection failed'}
[string[]]$shippingArguments=if($Shipping){@('--shipping')}else{@()}
& $tool package-game --repository $repo --config $Configuration --project $project --stage-root (Join-Path $Work 'Staging') --input-mode Project --startup-scene $startup --engine-distribution $EngineDistribution --smoke-frames 120 --smoke-timeout-sec 300 @shippingArguments
if($LASTEXITCODE){throw 'Character Player packaging failed'}
$pointer=Get-Content (Join-Path $Work 'Staging/Project.current.json') -Raw | ConvertFrom-Json
if($pointer.verification -ne 'passed'){throw 'Package smoke did not pass'}
$stage=Join-Path $Work ('Staging/'+$pointer.releaseDirectory)
@{stage=$stage;distribution=$EngineDistribution;configuration=$Configuration;shipping=[bool]$Shipping;hierarchy=[bool]$Hierarchy;mesh=[bool]$Mesh;geometry=[bool]$Geometry;transition=[bool]$Transition} | ConvertTo-Json | Set-Content (Join-Path $Work 'fixture.json') -Encoding utf8
Write-Output "PHYSICS_CHARACTER_PLAYER_STAGE $stage"
