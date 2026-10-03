[CmdletBinding()]
param([Parameter(Mandatory)][string]$EngineDistribution, [Parameter(Mandatory)][string]$SourceGlb,
    [ValidateSet('Debug','Release')][string]$Configuration='Debug', [Parameter(Mandatory)][string]$Work, [switch]$Shipping)
$ErrorActionPreference='Stop'
$repo=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$Work=[IO.Path]::GetFullPath($Work)
if(Test-Path -LiteralPath $Work){throw 'Fixture directory must be new'}
$meta=Get-Content "$PSScriptRoot/fixtures/PhysicsDagger.glb.meta" -Raw
if($meta -notmatch 'sourceFingerprint: sha256:([0-9a-f]{64})'){throw 'Source fingerprint absent'}
$expected=$Matches[1]
if((Get-FileHash -LiteralPath $SourceGlb).Hash.ToLowerInvariant() -ne $expected){throw 'Supplied GLB does not match the authored convex/model fixture'}
$project=Join-Path $Work 'Project'
$assets=Join-Path $project 'Assets'
New-Item -ItemType Directory -Force "$assets/Scenes","$assets/Script","$assets/Models"|Out-Null
Copy-Item "$repo/Dynamic_CPP/Assets/Shaders" "$assets/Shaders" -Recurse
Copy-Item "$repo/Dynamic_CPP/ProjectSetting" "$project/ProjectSetting" -Recurse
Copy-Item "$PSScriptRoot/fixtures/PhysicsDaggerPlayer.creator","$PSScriptRoot/fixtures/PhysicsDaggerPlayer.creator.meta" "$assets/Scenes"
Copy-Item "$PSScriptRoot/fixtures/PhysicsDaggerConvex.cegeometry","$PSScriptRoot/fixtures/PhysicsDaggerConvex.cegeometry.meta" $assets
Copy-Item -LiteralPath $SourceGlb -Destination "$assets/Models/PhysicsDagger.glb"
Copy-Item "$PSScriptRoot/fixtures/PhysicsDagger.glb.meta" "$assets/Models"
Copy-Item "$repo/GameScripts/DaggerConvexPlayerProbe.cs" "$assets/Script"
$tool=Join-Path $EngineDistribution "Bin/x64-$Configuration/Tools/CreatorBuildTool/CreatorBuildTool.exe"
& $tool select-engine --engine-distribution $EngineDistribution --project $project
if($LASTEXITCODE){throw 'Engine selection failed'}
$shippingArguments=@()
if($Shipping){$shippingArguments+= '--shipping'}
& $tool package-game --repository $repo --config $Configuration --project $project --stage-root "$Work/Staging" --input-mode Project --startup-scene PhysicsDaggerPlayer.creator --engine-distribution $EngineDistribution --smoke-frames 120 --smoke-timeout-sec 300 @shippingArguments
if($LASTEXITCODE){throw 'Dagger package verification failed'}
$pointer=Get-Content "$Work/Staging/Project.current.json" -Raw|ConvertFrom-Json
if($pointer.verification -ne 'passed'){throw 'Package smoke missing'}
$stage=Join-Path "$Work/Staging" $pointer.releaseDirectory
@{stage=$stage;configuration=$Configuration;shipping=[bool]$Shipping;sourceSha256=$expected;distribution=$EngineDistribution}|ConvertTo-Json|Set-Content "$Work/fixture.json" -Encoding utf8
"PHYSICS_DAGGER_PLAYER_STAGE $stage"
