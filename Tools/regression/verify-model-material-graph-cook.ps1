#Requires -Version 7.0
param([ValidateSet('Debug', 'Release')][string]$Configuration = 'Debug')
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$repo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$probeOutput = Join-Path $repo 'Build/Obj/MaterialProductProbe'
$nativeRoot = (Get-Content (Join-Path $probeOutput "model-sot-$Configuration-root.txt") -Raw).Trim()
$project = Join-Path $nativeRoot ('CookProject-' + [Guid]::NewGuid().ToString('N').Substring(0, 8))
$assets = Join-Path $project 'Assets'
New-Item -ItemType Directory $assets | Out-Null
# The native Editor fixture mounts an old Derived catalog. Cook authoring sources only.
Get-ChildItem (Join-Path $nativeRoot 'Project/Assets') -Force |
    Where-Object Name -NE 'Derived' | Copy-Item -Destination $assets -Recurse
Copy-Item (Join-Path $nativeRoot 'Project/Library') (Join-Path $project 'Library') -Recurse
Copy-Item (Join-Path $nativeRoot 'Project/ProjectSetting') (Join-Path $project 'ProjectSetting') -Recurse
$output = Join-Path $project 'Cooked'
$cooker = Join-Path $repo "Bin/x64-$Configuration/Tools/AssetCooker/AssetCooker.exe"
$arguments = @('--asset-root', $assets, '--output', $output, '--generation-root',
    (Join-Path $project 'Library/ModelAssetGenerations'), '--model',
    (Join-Path $assets 'Models/CreatorRobot.glb'), '--model', (Join-Path $assets 'Models/Prim_Plane.glb'),
    '--scene', (Join-Path $assets 'Scenes/ModelMaterialSoT.creator'))
# No --shadergraph arguments: every model material must provide its own graph dependency.
$process = Start-Process $cooker -ArgumentList $arguments -WindowStyle Hidden -Wait -PassThru `
    -RedirectStandardOutput (Join-Path $project 'cook.stdout.log') `
    -RedirectStandardError (Join-Path $project 'cook.stderr.log')
if ($process.ExitCode -ne 0) {
    throw "Model graph cook failed: $(Get-Content (Join-Path $project 'cook.stderr.log') -Raw)"
}
$derived = Join-Path $output 'Derived'
$programs = @(Get-ChildItem (Join-Path $derived 'MaterialPrograms') -Recurse -Filter '*.lxmaterial')
if ($programs.Count -ne 5) { throw "Expected five automatically cooked model material programs; got $($programs.Count)" }
if (!(Test-Path (Join-Path $derived 'asset-manifest.cemf'))) { throw 'Missing cooked dependency manifest' }
if (@(Get-ChildItem (Join-Path $derived 'Scenes') -Recurse -Filter '*.creator').Count -ne 1) { throw 'Missing cooked Scene' }
"LX_MODEL_MATERIAL_COOK_OK configuration=$Configuration programs=$($programs.Count) exit=0 root=$project"
