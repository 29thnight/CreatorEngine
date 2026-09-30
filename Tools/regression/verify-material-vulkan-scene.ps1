param([switch]$SkipDependencyRestore)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$repo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$msbuild = 'C:/Program Files/Microsoft Visual Studio/18/Community/MSBuild/Current/Bin/MSBuild.exe'
$output = Join-Path $repo 'Build/Obj/MaterialProductProbe'
$sources = @(
    Get-ChildItem (Join-Path $repo 'Engine/RenderEngine'), (Join-Path $repo 'Engine/Utility_Framework'),
        (Join-Path $repo 'Lattice'), (Join-Path $repo 'Dynamic_CPP/Assets/Shaders/DefaultPassShader') -Recurse -File |
        Where-Object Extension -In '.cpp', '.h', '.slang', '.vcxproj', '.props', '.targets'
    Get-Item (Join-Path $PSScriptRoot 'material_raster_surface_probe.cpp'),
        (Join-Path $PSScriptRoot 'MaterialVulkanSceneProbe.vcxproj'), $PSCommandPath
    Get-ChildItem $PSScriptRoot -Filter 'material_scene_*tests.inl'
) | Sort-Object FullName -Unique
$snapshot = @($sources | ForEach-Object {
    [pscustomobject]@{ path = $_.FullName; hash = (Get-FileHash $_.FullName -Algorithm SHA256).Hash }
})
$snapshot | ConvertTo-Json | Set-Content (Join-Path $output 'vulkan-scene-source-hashes.json') -Encoding utf8
$dependencyOptions = @()
if ($SkipDependencyRestore) { $dependencyOptions += '/p:VcpkgManifestInstall=false' }
foreach ($configuration in @('Debug', 'Release')) {
    $build = @(& $msbuild (Join-Path $PSScriptRoot 'MaterialVulkanSceneProbe.vcxproj') /m:2 /nologo `
        "/p:Configuration=$configuration" /p:Platform=x64 /p:UseDynamicDebugging=false /p:LinkIncremental=false `
        @dependencyOptions /v:minimal 2>&1)
    $buildExit = $LASTEXITCODE
    $build | Set-Content (Join-Path $output "vulkan-scene-build-$configuration.log") -Encoding utf8
    if ($buildExit -ne 0) { throw "Native Vulkan Scene build failed: $configuration" }
    $exe = Join-Path $repo "Bin/x64-$configuration/Tools/MaterialVulkanSceneProbe/MaterialVulkanSceneProbe.exe"
    $previousPath = $env:PATH
    try {
        $dependencyRoot = if ($configuration -eq 'Debug') { 'vcpkg_installed/x64-windows/debug/bin' } else { 'vcpkg_installed/x64-windows/bin' }
        $env:PATH = (Join-Path $repo $dependencyRoot) + ';' + $previousPath
        $modes = @('--scene-only', '--shadow-decal-only', '--subsurface-only', '--refraction-only', '--volume-only', '--cooked-scene')
        foreach ($mode in $modes) {
            $arguments = @($repo, $mode)
            if ($mode -eq '--cooked-scene') {
                $cookRoot = (Get-Content (Join-Path $output "scene-cook-$configuration-root.txt") -Raw).Trim()
                $arguments += Join-Path $cookRoot 'Accepted'
            }
            $result = @(& $exe @arguments 2>&1 | Tee-Object -FilePath (Join-Path $output "vulkan-scene-$configuration$mode.log"))
            $resultExit = $LASTEXITCODE
            if ($resultExit -ne 0 -or @($result | Where-Object {
                $_ -eq 'LX_MATERIAL_VULKAN_SCENE_VALIDATION_OK validation=0 encoderDrops=0'
            }).Count -ne 1 -or @($result | Where-Object { $_ -match '^LX_MATERIAL_.*_OK ' }).Count -lt 2) {
                throw "Native Vulkan actual Scene failed: $configuration/$mode $($result -join "`n")"
            }
            $result | Where-Object { $_ -match '^LX_MATERIAL_.*_OK ' }
        }
    } finally { $env:PATH = $previousPath }
    foreach ($entry in $snapshot) {
        if ((Get-FileHash $entry.path -Algorithm SHA256).Hash -ne $entry.hash) { throw "Vulkan source drift: $($entry.path)" }
    }
    "LX_MATERIAL_VULKAN_SCENE_CONFIGURATION_OK configuration=$configuration modes=$($modes.Count) sources=$($snapshot.Count) drift=0"
}
