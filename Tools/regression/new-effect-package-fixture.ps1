#Requires -Version 7.0
[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$OutputDirectory,
    [ValidateSet('Debug','Release')][string]$Configuration = 'Debug'
)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$repo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$output = [IO.Path]::GetFullPath($OutputDirectory)
if (Test-Path -LiteralPath $output) { throw 'Use a new fixture directory.' }
$fixture = Join-Path $output 'Project'
$utf8 = [Text.UTF8Encoding]::new($false)
New-Item -ItemType Directory -Path "$fixture/Assets/Scenes","$fixture/ProjectSetting" -Force | Out-Null
# Keep only the authored root/camera/light and remove its managed probe. A
# lifecycle-only package needs a real rendered scene, but no mesh/material cook.
$entities = [regex]::Split([IO.File]::ReadAllText("$repo/Dynamic_CPP/Assets/Scenes/FT_Primitives.creator"), '(?m)(?=^  - Entity:)')
if ($entities.Count -lt 4 -or $entities[2] -notmatch 'm_name: MainCamera' -or $entities[3] -notmatch 'm_name: SunLight') {
    throw 'Primitive source fixture layout changed.'
}
$scene = ($entities[0..3] -join '').Replace('m_childrenIndices: [1, 2, 3, 4, 5, 6, 7, 8, 9, 10]', 'm_childrenIndices: [1, 2]').Replace('m_isPrimary: false', 'm_isPrimary: true')
$scene = [regex]::Replace($scene, '(?ms)^      - ScriptComponent:.*?(?=^    m_isStatic:)', '')
[IO.File]::WriteAllText("$fixture/Assets/Scenes/EffectPackage.creator", $scene, $utf8)
[IO.File]::WriteAllText("$fixture/Assets/Scenes/EffectPackage.creator.meta", "guid: c10a1000-0000-4000-8000-000000000001`n", $utf8)
foreach ($name in @('AssetIdentity.asset','CollisionMatrix.asset','Layers.celayers','TagManager.asset')) {
    Copy-Item -LiteralPath "$repo/Dynamic_CPP/ProjectSetting/$name" -Destination "$fixture/ProjectSetting/$name"
}
$settings = [IO.File]::ReadAllText("$repo/Tools/packaging/templates/EngineSettings.runtime.yml").Replace('FT_Primitives.creator', 'EffectPackage.creator').Replace('m_isSkyboxEnabled: true', 'm_isSkyboxEnabled: false').Replace('kloofendal_43d_clear_puresky_4k.hdr', 'None')
[IO.File]::WriteAllText("$fixture/ProjectSetting/EngineSettings.asset", $settings, $utf8)
Copy-Item -LiteralPath "$repo/Dynamic_CPP/Assets/Shaders" -Destination "$fixture/Assets" -Recurse
$cooker = "$repo/Bin/x64-$Configuration/Tools/AssetCooker/AssetCooker.exe"
$packer = "$repo/Bin/x64-$Configuration/Tools/AssetPacker/AssetPacker.exe"
& $cooker --asset-root "$fixture/Assets" --output "$output/Cooked" --scene "$fixture/Assets/Scenes/EffectPackage.creator"
if ($LASTEXITCODE -ne 0) { throw 'Particle package fixture cook failed.' }
Copy-Item -LiteralPath $fixture -Destination "$output/RuntimeInput" -Recurse
Copy-Item -Path "$output/Cooked/*" -Destination "$output/RuntimeInput/Assets" -Recurse -Force
& $cooker --compile-runtime-documents --runtime-root "$output/RuntimeInput"
if ($LASTEXITCODE -ne 0) { throw 'Runtime document compilation failed.' }
& $packer --assets "$output/RuntimeInput/Assets" --settings "$output/RuntimeInput/ProjectSetting" --output "$output/GameAssets.pak"
if ($LASTEXITCODE -ne 0) { throw 'Fixture PAK creation failed.' }
@{ fixtureProject=$fixture; cookedPak="$output/GameAssets.pak"; pakSha256=(Get-FileHash -LiteralPath "$output/GameAssets.pak").Hash; configuration=$Configuration } | ConvertTo-Json | Set-Content -LiteralPath "$output/fixture.json" -Encoding utf8
"EFFECT_PACKAGE_FIXTURE_OK $output"
