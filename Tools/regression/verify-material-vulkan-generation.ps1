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
    Get-Item (Join-Path $PSScriptRoot 'material_vulkan_generation_probe.cpp'),
        (Join-Path $PSScriptRoot 'MaterialVulkanGenerationProbe.vcxproj'), $PSCommandPath,
        (Join-Path $repo 'Directory.Build.props'), (Join-Path $repo 'Directory.Build.targets'),
        (Join-Path $repo 'EngineOutput.props')
) | Sort-Object FullName -Unique
$snapshot = @($sources | ForEach-Object {
    [pscustomobject]@{ path = $_.FullName; hash = (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash }
})
$snapshot | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $output 'vulkan-generation-source-hashes.json') -Encoding utf8
$dependencyOptions = @()
if ($SkipDependencyRestore) { $dependencyOptions += '/p:VcpkgManifestInstall=false' }
foreach ($configuration in @('Debug', 'Release')) {
    $buildOutput = @(& $msbuild (Join-Path $PSScriptRoot 'MaterialVulkanGenerationProbe.vcxproj') /m:2 /nologo `
        "/p:Configuration=$configuration" /p:Platform=x64 /p:UseDynamicDebugging=false /p:LinkIncremental=false `
        @dependencyOptions /v:minimal 2>&1)
    $buildExit = $LASTEXITCODE
    $buildOutput | Set-Content -LiteralPath (Join-Path $output "vulkan-generation-build-$configuration.log") -Encoding utf8
    if ($buildExit -ne 0) { throw "Material Vulkan generation probe build failed: $configuration" }
    $exe = Join-Path $repo "Bin\x64-$configuration\Tools\MaterialVulkanGenerationProbe\MaterialVulkanGenerationProbe.exe"
    $previousPath = $env:PATH
    try {
        $dependencyRoot = if ($configuration -eq 'Debug') { 'vcpkg_installed\x64-windows\x64-windows\debug\bin' } else { 'vcpkg_installed\x64-windows\x64-windows\bin' }
        $env:PATH = (Join-Path $repo $dependencyRoot) + ';' + $previousPath
        $result = @(& $exe $repo 2>&1)
        $resultExit = $LASTEXITCODE
    } finally {
        $env:PATH = $previousPath
    }
    $result | Set-Content -LiteralPath (Join-Path $output "vulkan-generation-$configuration.log") -Encoding utf8
    if ($resultExit -ne 0 -or @($result | Where-Object {
        $_ -match '^LX_MATERIAL_VULKAN_GENERATION_OK checks=[1-9]\d* pixels=768 scenePrograms=2 sceneReadyRequests=30 scenePsoWorkers=[1-9]\d* submissions=[1-9]\d* workers=[1-9]\d* stale=65 failures=2 validation=0 adapter=.+$'
    }).Count -ne 1) {
        throw "Material Vulkan generation runtime failed ($configuration): $($result -join "`n")"
    }
    foreach ($entry in $snapshot) {
        if ((Get-FileHash -LiteralPath $entry.path -Algorithm SHA256).Hash -ne $entry.hash) {
            throw "Material Vulkan generation source changed during validation: $($entry.path)"
        }
    }
    "LX_MATERIAL_VULKAN_GENERATION_CONFIGURATION_OK configuration=$configuration sources=$($snapshot.Count)"
    $result | Where-Object { $_ -match '^LX_MATERIAL_VULKAN_GENERATION_OK ' }
}
exit 0
