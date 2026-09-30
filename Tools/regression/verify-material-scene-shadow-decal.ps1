param(
    [string]$VisualStudioInstallation = '',
    [switch]$SkipDependencyRestore
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$repo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
if ([string]::IsNullOrWhiteSpace($VisualStudioInstallation)) {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    $VisualStudioInstallation = @(& $vswhere -latest -products '*' `
        -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath)[0]
}
$msbuild = Join-Path $VisualStudioInstallation 'MSBuild\Current\Bin\MSBuild.exe'
$output = Join-Path $repo 'Build\Obj\MaterialProductProbe'
New-Item -ItemType Directory -Path $output -Force | Out-Null
$sources = @(
    Get-ChildItem (Join-Path $repo 'Engine\RenderEngine'), (Join-Path $repo 'Engine\Utility_Framework'),
        (Join-Path $repo 'Lattice'), (Join-Path $repo 'Dynamic_CPP\Assets\Shaders\DefaultPassShader') `
        -Recurse -File | Where-Object Extension -In '.cpp', '.h', '.slang', '.vcxproj', '.props', '.targets'
    Get-Item (Join-Path $PSScriptRoot 'material_raster_surface_probe.cpp'),
        (Join-Path $PSScriptRoot 'material_scene_subsurface_tests.inl'),
        (Join-Path $PSScriptRoot 'material_scene_refraction_tests.inl'),
        (Join-Path $PSScriptRoot 'material_scene_volume_tests.inl'),
        (Join-Path $PSScriptRoot 'material_scene_shadow_decal_tests.inl'),
        (Join-Path $PSScriptRoot 'material_scene_decal_tests.inl'),
        (Join-Path $PSScriptRoot 'material_ibl_reference.h'),
        (Join-Path $PSScriptRoot 'principled_layered_reference.h'),
        (Join-Path $PSScriptRoot 'MaterialRasterSurfaceProbe.vcxproj'), $PSCommandPath,
        (Join-Path $repo 'Directory.Build.props'), (Join-Path $repo 'Directory.Build.targets'),
        (Join-Path $repo 'EngineOutput.props')
) | Sort-Object FullName -Unique
$snapshot = @($sources | ForEach-Object {
    [pscustomobject]@{ path = $_.FullName; hash = (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash }
})
$snapshot | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $output 'shadow-decal-source-hashes.json') -Encoding utf8
$dependencyOptions = @()
if ($SkipDependencyRestore) { $dependencyOptions += '/p:VcpkgManifestInstall=false' }
foreach ($configuration in @('Debug', 'Release')) {
    $buildOutput = @(& $msbuild (Join-Path $PSScriptRoot 'MaterialRasterSurfaceProbe.vcxproj') /m:2 /nologo `
        "/p:Configuration=$configuration" /p:Platform=x64 /p:UseDynamicDebugging=false /p:LinkIncremental=false `
        @dependencyOptions /v:minimal 2>&1)
    $buildExit = $LASTEXITCODE
    $buildOutput | Set-Content -LiteralPath (Join-Path $output "shadow-decal-build-$configuration.log") -Encoding utf8
    if ($buildExit -ne 0) { throw "Material Scene Shadow/Decal build failed: $configuration" }
    $exe = Join-Path $repo "Bin\x64-$configuration\Tools\MaterialRasterSurfaceProbe\MaterialRasterSurfaceProbe.exe"
    $previousPath = $env:PATH
    $previousValidation = $env:CREATOR_DX12_VALIDATION
    try {
        $dependencyRoot = if ($configuration -eq 'Debug') { 'vcpkg_installed\x64-windows\debug\bin' } else { 'vcpkg_installed\x64-windows\bin' }
        $env:PATH = (Join-Path $repo $dependencyRoot) + ';' + $previousPath
        $env:CREATOR_DX12_VALIDATION = 'gpu'
        foreach ($mode in @('shadow-decal', 'volume', 'refraction', 'subsurface', 'raster')) {
            $arguments = @($repo)
            if ($mode -eq 'subsurface') { $arguments += '--subsurface-only' }
            if ($mode -eq 'refraction') { $arguments += '--refraction-only' }
            if ($mode -eq 'volume') { $arguments += '--volume-only' }
            if ($mode -eq 'shadow-decal') { $arguments += '--shadow-decal-only' }
            $result = @(& $exe @arguments 2>&1)
            $resultExit = $LASTEXITCODE
            $result | Set-Content -LiteralPath (Join-Path $output "shadow-decal-gate-$mode-$configuration.log") -Encoding utf8
            $signature = if ($mode -eq 'shadow-decal') {
                '^LX_MATERIAL_SCENE_DECAL_OK frames=114 altered=[1-9]\d* preserved=[1-9]\d* failures=72 checks=[1-9]\d* gpuComponents=[1-9]\d* validation=0$'
            } elseif ($mode -eq 'volume') {
                '^LX_MATERIAL_SCENE_VOLUME_OK frames=33 pixels=[1-9]\d* interior=[1-9]\d* scattering=[1-9]\d* failures=66 checks=[1-9]\d* gpuComponents=[1-9]\d* validation=0$'
            } elseif ($mode -eq 'refraction') {
                '^LX_MATERIAL_SCENE_REFRACTION_OK frames=24 pixels=[1-9]\d* hits=[1-9]\d* misses=[1-9]\d* shifted=[1-9]\d* tir=[1-9]\d* maskedHoles=[1-9]\d* hybridPixels=[1-9]\d* failures=48 roughChecks=[1-9]\d* roughMixed=[1-9]\d* checks=[1-9]\d* gpuComponents=[1-9]\d* validation=0$'
            } elseif ($mode -eq 'subsurface') {
                '^LX_MATERIAL_SCENE_SUBSURFACE_OK frames=12 pixels=[1-9]\d* spread=[1-9]\d* boundaries=[1-9]\d* maskedHoles=[1-9]\d* checks=[1-9]\d* gpuComponents=[1-9]\d* validation=0$'
            } else {
                '^LX_MATERIAL_RASTER_SURFACE_OK checks=[1-9]\d* gpuComponents=[1-9]\d* .*sceneCompositionFrames=56 .*sceneGenerationFrames=12 .*$'
            }
            if ($resultExit -ne 0 -or @($result | Where-Object { $_ -match $signature }).Count -ne 1 -or
                @($result | Where-Object { $_ -match 'DebugLayer=on GPUValidation=on' }).Count -lt 1) {
                throw "Material $mode runtime failed ($configuration): $($result -join "`n")"
            }
            if ($mode -eq 'shadow-decal' -and @($result | Where-Object { $_ -match '^LX_MATERIAL_SCENE_SHADOW_OK frames=42 covered=[1-9]\d* checks=[1-9]\d* gpuComponents=[1-9]\d* validation=0$' }).Count -ne 1) {
                throw 'Native LX shadow marker missing'
            }
            $result | Where-Object { $_ -match '^LX_MATERIAL_.*_OK ' }
        }
    } finally {
        $env:PATH = $previousPath
        $env:CREATOR_DX12_VALIDATION = $previousValidation
    }
    foreach ($entry in $snapshot) {
        if ((Get-FileHash -LiteralPath $entry.path -Algorithm SHA256).Hash -ne $entry.hash) {
            throw "Scene Shadow/Decal source changed during verification: $($entry.path)"
        }
    }
    "LX_MATERIAL_SCENE_SHADOW_DECAL_CONFIGURATION_OK configuration=$configuration"
}
"LX_MATERIAL_SCENE_SHADOW_DECAL_GATE_OK sources=$($snapshot.Count) drift=0 configurations=Debug,Release"
exit 0
