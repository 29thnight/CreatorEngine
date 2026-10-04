param(
    [string]$VisualStudioInstallation = '',
    [switch]$SkipDependencyRestore,
    [ValidateSet('Debug','Release')][string[]]$Configurations = @('Debug','Release')
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
$dependencyOptions = @()
if ($SkipDependencyRestore) { $dependencyOptions += '/p:VcpkgManifestInstall=false' }
foreach ($configuration in $Configurations) {
    $buildOutput = @(& $msbuild (Join-Path $PSScriptRoot 'MaterialRasterSurfaceProbe.vcxproj') /m:2 /nologo `
        "/p:Configuration=$configuration" /p:Platform=x64 /p:UseDynamicDebugging=false /p:LinkIncremental=false @dependencyOptions /v:minimal 2>&1)
    $buildExit = $LASTEXITCODE
    $buildOutput | Set-Content -LiteralPath (Join-Path $output "raster-surface-build-$configuration.log") -Encoding utf8
    if ($buildExit -ne 0) { throw "Material spatial surface probe build failed: $configuration" }
    $exe = Join-Path $repo "Bin\x64-$configuration\Tools\MaterialRasterSurfaceProbe\MaterialRasterSurfaceProbe.exe"
    $previousPath = $env:PATH
    $previousValidation = $env:CREATOR_DX12_VALIDATION
    try {
        $dependencyRoot = if ($configuration -eq 'Debug') { 'vcpkg_installed\x64-windows\debug\bin' } else { 'vcpkg_installed\x64-windows\bin' }
        $env:PATH = (Join-Path $repo $dependencyRoot) + ';' + $previousPath
        $env:CREATOR_DX12_VALIDATION = 'gpu'
        $result = @(& $exe $repo 2>&1)
        $resultExit = $LASTEXITCODE
    } finally {
        $env:PATH = $previousPath
        $env:CREATOR_DX12_VALIDATION = $previousValidation
    }
    $result | Set-Content -LiteralPath (Join-Path $output "raster-surface-$configuration.log") -Encoding utf8
    if ($resultExit -ne 0 -or @($result | Where-Object { $_ -match '^LX_MATERIAL_RASTER_SURFACE_OK checks=\d+ gpuComponents=\d+ frames=7 compiled=24 covered=\d+ background=\d+ far=\d+ fractionalLods=\d+ .* graphFrames=21 graphLists=35 graphFailures=6 sharedDepthFrames=24 sharedDepthPixels=[1-9]\d* coplanarPixels=[1-9]\d* skinnedDepthFrames=6 meshFailures=4 sceneInputFrames=24 sceneInputFailures=24 sceneCompositionFrames=65 sceneCompositionPixels=[1-9]\d* sceneCompositionFailures=366 sceneLookupBaked=[1-9]\d* sceneLookupReused=[1-9]\d* sceneLookupMutationFrames=10 sceneLookupFullFrames=2 sceneLookupFullPixels=[1-9]\d* sceneLookupFullColdMs=[\d.eE+-]+ sceneLookupFullWarmMs=[\d.eE+-]+ sceneTextureFrames=8 sceneTexturePixels=[1-9]\d* sceneTextureFractionalLods=[1-9]\d* sceneTextureClampedLods=[1-9]\d* maxTextureFilterError=[\d.eE+-]+ sceneGenerationFrames=11 sceneGenerationFallbacks=[1-9]\d* sceneGenerationAborts=1 sceneGenerationPendingSubmissions=1 sceneGenerationStale=1 sceneGenerationCompiles=6 sceneGenerationWorkers=6 sceneGenerationPsoWorkers=[1-9]\d* sceneLookupProgressiveFrames=10 sceneLookupProvisional=[1-9]\d* sceneLookupRefined=[1-9]\d* sceneLookupProvisionalPixels=[1-9]\d*$' }).Count -ne 1) {
        throw "Material spatial surface runtime failed ($configuration): $($result -join "`n")"
    }
    "LX_MATERIAL_RASTER_SURFACE_CONFIGURATION_OK configuration=$configuration"
    $result | Where-Object { $_ -match '^LX_MATERIAL_RASTER_SURFACE_OK ' }
}
exit 0
