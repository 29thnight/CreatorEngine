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
$dependencyOptions = @()
if ($SkipDependencyRestore) { $dependencyOptions += '/p:VcpkgManifestInstall=false' }
foreach ($configuration in @('Debug', 'Release')) {
    $buildOutput = @(& $msbuild (Join-Path $PSScriptRoot 'MaterialMeshSurfaceProbe.vcxproj') /m:2 /nologo `
        "/p:Configuration=$configuration" /p:Platform=x64 /p:UseDynamicDebugging=false /p:LinkIncremental=false @dependencyOptions /v:minimal 2>&1)
    $buildExit = $LASTEXITCODE
    $buildOutput | Set-Content -LiteralPath (Join-Path $output "mesh-surface-build-$configuration.log") -Encoding utf8
    if ($buildExit -ne 0) { throw "Material spatial surface probe build failed: $configuration" }
    $exe = Join-Path $repo "Bin\x64-$configuration\Tools\MaterialMeshSurfaceProbe\MaterialMeshSurfaceProbe.exe"
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
    $result | Set-Content -LiteralPath (Join-Path $output "mesh-surface-$configuration.log") -Encoding utf8
    if ($resultExit -ne 0 -or @($result | Where-Object { $_ -match '^LX_MATERIAL_MESH_SURFACE_OK checks=\d+ gpuComponents=\d+ masks=8 poses=3 points=37 submitted=32 compiled=22 partitionVertices=8200 samplePoints=82 ' }).Count -ne 1) {
        throw "Material spatial surface runtime failed ($configuration): $($result -join "`n")"
    }
    "LX_MATERIAL_MESH_SURFACE_CONFIGURATION_OK configuration=$configuration"
    $result | Where-Object { $_ -match '^LX_MATERIAL_MESH_SURFACE_OK ' }
}
exit 0
