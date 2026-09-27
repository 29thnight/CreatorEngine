[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$EngineDistribution,
    [string]$Work = ''
)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$repo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$EngineDistribution = [IO.Path]::GetFullPath($EngineDistribution)
if (-not $Work) { $Work = Join-Path $repo ('Build/Obj/AnimationPlayer-' + [guid]::NewGuid().ToString('N')) }
$Work = [IO.Path]::GetFullPath($Work)
if (Test-Path -LiteralPath $Work) { throw 'Use a new work directory; fixture input must be independent' }
$project = Join-Path $Work 'Project'
$assets = Join-Path $project 'Assets'
$generations = Join-Path $project 'Library/ModelAssetGenerations'
New-Item -ItemType Directory -Path $assets,$generations -Force | Out-Null

# Keep the test independent of unrelated legacy scenes, prefabs and audio stamps.
# The authored scene carries the camera, light and CreatorRobot mesh closure.
$sourceAssets = Join-Path $repo 'Dynamic_CPP/Assets'
foreach ($entry in Get-ChildItem -LiteralPath $sourceAssets -Force) {
    if ($entry.Name -in 'Scenes','Prefabs','Sound','Script','Derived') { continue }
    Copy-Item -LiteralPath $entry.FullName -Destination (Join-Path $assets $entry.Name) -Recurse -Force
}
Copy-Item -LiteralPath (Join-Path $repo 'Dynamic_CPP/ProjectSetting') -Destination (Join-Path $project 'ProjectSetting') -Recurse -Force
New-Item -ItemType Directory -Path (Join-Path $assets 'Scenes'),(Join-Path $assets 'Script') -Force | Out-Null
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'fixtures/AnimationPlayer.creator'),
    (Join-Path $PSScriptRoot 'fixtures/AnimationPlayer.creator.meta') -Destination (Join-Path $assets 'Scenes')
Copy-Item -LiteralPath (Join-Path $repo 'GameScripts/PackageSmokeProbe.cs') -Destination (Join-Path $assets 'Script')

$cooker = Join-Path $EngineDistribution 'Bin/x64-Release/Tools/AssetCooker/AssetCooker.exe'
$buildTool = Join-Path $EngineDistribution 'Bin/x64-Release/Tools/CreatorBuildTool/CreatorBuildTool.exe'
foreach ($path in @($cooker,$buildTool)) {
    if (-not (Test-Path -LiteralPath $path)) { throw "Missing engine tool: $path" }
}
& $buildTool select-engine --engine-distribution $EngineDistribution --project $project
if ($LASTEXITCODE -ne 0) { throw 'Could not pin the fixture project to the selected engine' }
$models = @(Get-ChildItem -LiteralPath $assets -Recurse -File |
    Where-Object { $_.Extension -in '.fbx','.glb','.gltf' } | Sort-Object FullName)
foreach ($model in $models) {
    & $cooker --author-model-asset --asset-root $assets --output $generations --model $model.FullName
    if ($LASTEXITCODE -ne 0) { throw "Model authoring failed: $($model.FullName)" }
}
& (Join-Path $repo 'Tools/build.ps1') -Config Release -Project $project -StageRoot (Join-Path $Work 'Staging') `
    -InputMode Project -StartupScene AnimationPlayer.creator -SmokeFrames 120 -SmokeTimeoutSec 300 `
    -EngineDistribution $EngineDistribution
if ($LASTEXITCODE -ne 0) { throw 'Player package build failed' }
$pointer = Get-Content -LiteralPath (Join-Path $Work 'Staging/Project.current.json') -Raw | ConvertFrom-Json
if ($pointer.verification -ne 'passed') { throw 'Package verification did not pass' }
Write-Output "ANIMATION_PLAYER_STAGE $(Join-Path $Work ('Staging/' + $pointer.releaseDirectory))"
