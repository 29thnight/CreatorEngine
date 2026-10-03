param(
    [string]$VisualStudioInstallation = '',
    [switch]$SkipDependencyRestore,
    [ValidateSet('Debug', 'Release')][string[]]$Configurations = @('Debug', 'Release'),
    [ValidateSet('DX12', 'Vulkan')][string[]]$Backends = @('DX12', 'Vulkan')
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
        -Recurse -File | Where-Object Extension -In '.cpp', '.h', '.slang', '.shadermeta', '.vcxproj', '.props', '.targets'
    Get-ChildItem $PSScriptRoot -File | Where-Object Name -Match '^material_(raster|forward|scene|ibl)|^principled_layered_reference|^Material(RasterSurface|VulkanScene)Probe.vcxproj$'
    Get-Item $PSCommandPath, (Join-Path $repo 'Directory.Build.props'),
        (Join-Path $repo 'Directory.Build.targets'), (Join-Path $repo 'EngineOutput.props')
) | Sort-Object FullName -Unique
$snapshot = @($sources | ForEach-Object {
    [pscustomobject]@{ path = $_.FullName; hash = (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash }
})
$snapshot | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $output 'forward-blend-source-hashes.json') -Encoding utf8
$dependencyOptions = @()
if ($SkipDependencyRestore) { $dependencyOptions += '/p:VcpkgManifestInstall=false' }
foreach ($configuration in $Configurations) {
    foreach ($backend in $Backends) {
        $project = if ($backend -eq 'DX12') { 'MaterialRasterSurfaceProbe' } else { 'MaterialVulkanSceneProbe' }
        $prefix = if ($backend -eq 'DX12') { 'forward-blend' } else { 'forward-blend-vulkan' }
        $build = @(& $msbuild (Join-Path $PSScriptRoot "$project.vcxproj") /m:2 /nologo `
            "/p:Configuration=$configuration" /p:Platform=x64 /p:UseDynamicDebugging=false `
            /p:LinkIncremental=false @dependencyOptions /v:minimal 2>&1)
        $buildExit = $LASTEXITCODE
        $build | Set-Content -LiteralPath (Join-Path $output "$prefix-build-$configuration.log") -Encoding utf8
        if ($buildExit -ne 0) { throw "Forward Blend build failed: $backend/$configuration" }
        $exe = Join-Path $repo "Bin\x64-$configuration\Tools\$project\$project.exe"
        $previousPath = $env:PATH
        $previousValidation = $env:CREATOR_DX12_VALIDATION
        try {
            $dependencyRoot = if ($configuration -eq 'Debug') { 'debug\bin' } else { 'bin' }
            $env:PATH = (Join-Path $repo "vcpkg_installed\x64-windows\x64-windows\$dependencyRoot") + ';' + $previousPath
            $env:CREATOR_DX12_VALIDATION = 'gpu'
            $result = @(& $exe $repo --forward-blend 2>&1)
            $runExit = $LASTEXITCODE
        } finally {
            $env:PATH = $previousPath
            $env:CREATOR_DX12_VALIDATION = $previousValidation
        }
        $result | Set-Content -LiteralPath (Join-Path $output "$prefix-$configuration.log") -Encoding utf8
        if ($runExit -ne 0 -or @($result | Where-Object {
            $_ -match '^LX_MATERIAL_FORWARD_BLEND_OK frames=12 pixels=4608 checks=[1-9]\d* graphCompiles=0 mixed=true parallel=true overflowLights=65 validation=0$'
        }).Count -ne 1) { throw "Forward Blend runtime failed: $backend/$configuration; see $prefix-$configuration.log" }
        foreach ($entry in $snapshot) {
            if ((Get-FileHash -LiteralPath $entry.path -Algorithm SHA256).Hash -ne $entry.hash) {
                throw "Forward Blend source changed during validation: $($entry.path)"
            }
        }
        "LX_MATERIAL_FORWARD_BLEND_CONFIGURATION_OK backend=$backend configuration=$configuration sources=$($snapshot.Count)"
        $result | Where-Object { $_ -match '^LX_MATERIAL_FORWARD_BLEND_OK ' }
    }
}
exit 0
