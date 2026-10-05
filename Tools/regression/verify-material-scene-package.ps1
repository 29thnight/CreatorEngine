param(
    [ValidateSet('Debug', 'Release')][string]$Configuration = 'Debug',
    [string]$EngineDistribution = '',
    [ValidateRange(180, 1200)][int]$SmokeTimeoutSeconds = 600
)

# Run after verify-material-scene-cook and building Player/Editor/AssetPacker/BuildTool.
# Every project, engine distribution and package belongs to this isolated case.
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$repo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$output = Join-Path $repo 'Build/Obj/MaterialProductProbe'
$cookRoot = [IO.File]::ReadAllText((Join-Path $output "scene-cook-$Configuration-root.txt")).Trim()
$caseRoot = Join-Path $output ("ScenePkg-$Configuration-" + [Guid]::NewGuid().ToString('N'))
$sourceFiles = @(
    (Get-Content (Join-Path $output 'scene-cook-source-hashes.json') -Raw | ConvertFrom-Json).path
    Get-ChildItem (Join-Path $repo 'Player'), (Join-Path $repo 'BuildTool') -Recurse -File |
        Where-Object Extension -In '.cpp', '.h', '.cs', '.csproj', '.vcxproj' |
        Where-Object FullName -NotMatch '[\\/](obj|bin)[\\/]' | Select-Object -ExpandProperty FullName
    $PSCommandPath
    (Join-Path $PSScriptRoot 'sync-material-editor-scale.ps1')
    (Join-Path $repo 'Engine/SceneRuntime/MeshRenderer.cpp')
    (Join-Path $repo 'Engine/SceneRuntime/MeshRenderer.h')
) | Sort-Object -Unique
$sourceSnapshot = @($sourceFiles | ForEach-Object {
    [pscustomobject]@{ path = $_; hash = (Get-FileHash -LiteralPath $_ -Algorithm SHA256).Hash }
})
$project = Join-Path $caseRoot 'Project'
$assets = Join-Path $project 'Assets'
foreach ($directory in @('Models', 'Scenes', 'Materials', 'Script', 'HDR', 'Shaders/DefaultPassShader')) {
    New-Item -ItemType Directory -Path (Join-Path $assets $directory) -Force | Out-Null
}
New-Item -ItemType Directory -Path (Join-Path $project 'ProjectSetting') | Out-Null
& (Join-Path $PSScriptRoot 'sync-material-editor-scale.ps1') -Project $project
foreach ($name in @('AssetIdentity.asset', 'CollisionMatrix.asset', 'Layers.celayers', 'TagManager.asset')) {
    Copy-Item -LiteralPath (Join-Path $repo "Dynamic_CPP/ProjectSetting/$name") -Destination (Join-Path $project "ProjectSetting/$name")
}
Copy-Item -Path (Join-Path $repo 'Dynamic_CPP/Assets/Shaders/DefaultPassShader/*') -Destination (Join-Path $assets 'Shaders/DefaultPassShader') -Recurse
foreach ($extension in @('.hdr', '.hdr.meta')) {
    Copy-Item -LiteralPath (Join-Path $repo "Dynamic_CPP/Assets/HDR/kloofendal_43d_clear_puresky_4k$extension") -Destination (Join-Path $assets "HDR/kloofendal_43d_clear_puresky_4k$extension")
}
foreach ($name in @('core.shadergraph', 'core.shadergraph.meta', 'layered.shadergraph', 'layered.shadergraph.meta', 'fixture.png', 'fixture.png.meta')) {
    Copy-Item -LiteralPath (Join-Path $cookRoot "Assets/$name") -Destination (Join-Path $assets $name)
}
foreach ($name in @('fixture.asset', 'fixture.asset.meta')) {
    Copy-Item -LiteralPath (Join-Path $cookRoot "Assets/$name") -Destination (Join-Path $assets "Materials/$name")
}
foreach ($extension in @('.glb', '.glb.meta')) {
    Copy-Item -LiteralPath (Join-Path $repo "Dynamic_CPP/Assets/Models/Prim_Plane$extension") -Destination (Join-Path $assets "Models/Prim_Plane$extension")
}
$generation = '8c05d8c6-f8bb-8ebb-8b1d-90e2d3b66e45/8'
$generationParent = Join-Path $project 'Library/ModelAssetGenerations/8c05d8c6-f8bb-8ebb-8b1d-90e2d3b66e45'
New-Item -ItemType Directory -Path $generationParent -Force | Out-Null
Copy-Item -LiteralPath (Join-Path $repo "Dynamic_CPP/Library/ModelAssetGenerations/$generation") -Destination $generationParent -Recurse
$probeSource = @'
using CreatorEngine;

public sealed class PackageSmokeProbe : Component
{
    public override void OnInitialized()
    {
        System.Console.WriteLine("[SMOKE] managed OnInitialized: PackageSmokeProbe");
    }

    public override void OnBeginSimulation()
    {
        System.Console.WriteLine("[SMOKE] managed OnBeginSimulation: PackageSmokeProbe");
    }
}
'@
[IO.File]::WriteAllText((Join-Path $assets 'Script/PackageSmokeProbe.cs'), $probeSource)
$originalScene = [IO.File]::ReadAllText((Join-Path $repo 'Dynamic_CPP/Assets/Scenes/FT_Primitives.creator'))
$entities = [Regex]::Split($originalScene, '(?m)(?=^  - Entity:)')
$sceneText = ($entities[0..4] -join '').Replace('m_name: FT_Primitives', 'm_name: LX_CookFixture').Replace(
    'm_childrenIndices: [1, 2, 3, 4, 5, 6, 7, 8, 9, 10]', 'm_childrenIndices: [1, 2, 3]').Replace('m_isPrimary: false', 'm_isPrimary: true')
$inlineMaterial = [IO.File]::ReadAllText((Join-Path $assets 'Materials/fixture.asset')).Replace(
    '44444444-4444-4444-8444-444444444444', '00000000-0000-0000-0000-000000000000')
$replacement = '        m_modelGuid: 8c05d8c6-f8bb-8ebb-8b1d-90e2d3b66e45' + "`n        m_Material:`n" +
    (($inlineMaterial -split '\r?\n' | ForEach-Object { '          ' + $_ }) -join "`n") + "`n"
$sceneText = [Regex]::Replace($sceneText, '(?ms)^        m_Material:\r?\n.*?(?=^        m_Mesh:)', $replacement)
$scene = Join-Path $assets 'Scenes/LX_CookFixture.creator'
[IO.File]::WriteAllText($scene, $sceneText)
'guid: 66666666-6666-4666-8666-666666666666' | Set-Content "$scene.meta" -Encoding utf8
$tool = Join-Path $repo "Bin/x64-$Configuration/Tools/CreatorBuildTool/CreatorBuildTool.exe"
if ([string]::IsNullOrWhiteSpace($EngineDistribution)) {
    $publish = @(& $tool publish-engine --repository $repo --config $Configuration --output-root (Join-Path $caseRoot 'Engine') --no-pointer 2>&1)
    $publishExit = $LASTEXITCODE
    $publish | Set-Content (Join-Path $caseRoot 'engine.log') -Encoding utf8
    if ($publishExit -ne 0) { throw "Engine distribution failed: $($publish -join "`n")" }
    $distribution = (Get-ChildItem (Join-Path $caseRoot 'Engine') -Directory | Where-Object Name -NotLike '.candidate-*').FullName
} else {
    $distribution = [IO.Path]::GetFullPath($EngineDistribution)
}
$selection = @(& $tool select-engine --engine-distribution $distribution --project $project 2>&1)
$selectionExit = $LASTEXITCODE
$selection | Set-Content (Join-Path $caseRoot 'selection.log') -Encoding utf8
if ($selectionExit -ne 0) { throw "Isolated project engine selection failed: $($selection -join "`n")" }
$previousValidation = $env:CREATOR_DX12_VALIDATION
try {
    $env:CREATOR_DX12_VALIDATION = 'gpu'
    $package = @(& $tool package-game --repository $repo --engine-distribution $distribution --project $project `
        --config $Configuration --stage-root (Join-Path $caseRoot 'Stage') --startup-scene LX_CookFixture.creator `
        --render-backend dx12 --smoke-offscreen --smoke-frames 120 --smoke-promotions 120 --smoke-timeout-sec $SmokeTimeoutSeconds `
        --log-path (Join-Path $caseRoot 'package-live.log') 2>&1)
    $packageExit = $LASTEXITCODE
} finally {
    $env:CREATOR_DX12_VALIDATION = $previousValidation
}
$package | Set-Content (Join-Path $caseRoot 'package.log') -Encoding utf8
if ($packageExit -ne 0) { throw "Automatic material package/Player failed: $($package -join "`n")" }
$pointer = Get-Content (Join-Path $caseRoot 'Stage/Project.current.json') -Raw | ConvertFrom-Json
$packageRoot = Join-Path $caseRoot "Stage/$($pointer.releaseDirectory)"
$manifest = Get-Content (Join-Path $packageRoot 'package-manifest.json') -Raw | ConvertFrom-Json
if ($manifest.verification -ne 'passed' -or $manifest.cook.byFolder.MaterialPrograms -ne 3 -or
    $manifest.cook.byFolder.Materials -ne 1 -or $manifest.cook.SourceCounts.'--shadergraph' -ne 3 -or
    $manifest.smoke.textParserCalls -ne 0 -or $manifest.smoke.cookedSceneDocuments -lt 1 -or
    !($package -match '\[lx.scene.program\] source=cooked graph=11111111-1111-4111-8111-111111111111 ready=[1-9]\d* sceneCompiles=0')) {
    throw 'Published package is missing the real cooked Scene material readiness evidence.'
}
# Runtime and PAK bytes must still match the manifest after the Player run.
foreach ($entry in $manifest.runtimeEntries) {
    if ((Get-FileHash (Join-Path $packageRoot $entry.path) -Algorithm SHA256).Hash.ToLowerInvariant() -ne $entry.sha256) {
        throw "Player modified runtime entry: $($entry.path)"
    }
}
if ((Get-FileHash (Join-Path $packageRoot 'GameAssets.pak') -Algorithm SHA256).Hash.ToLowerInvariant() -ne $manifest.pakFileSha256) {
    throw 'Player modified the package.'
}
$caseRoot | Set-Content (Join-Path $output "scene-package-$Configuration-root.txt") -Encoding utf8
foreach ($entry in $sourceSnapshot) {
    if ((Get-FileHash -LiteralPath $entry.path -Algorithm SHA256).Hash -ne $entry.hash) { throw "Package source changed: $($entry.path)" }
}
$sourceSnapshot | ConvertTo-Json | Set-Content (Join-Path $caseRoot 'source-hashes.json') -Encoding utf8
"LX_MATERIAL_SCENE_PACKAGE_OK configuration=$Configuration programs=3 materials=1 frames=$($manifest.smoke.gameThreadFrames) display=$($manifest.smoke.displayFrame) promotions=$($manifest.smoke.promotions) sceneCompiles=0 textParserCalls=0 payloadPreserved=true sources=$($sourceSnapshot.Count) drift=0"
exit 0
