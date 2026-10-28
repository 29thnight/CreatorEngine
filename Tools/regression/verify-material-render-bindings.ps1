param(
    [string]$VisualStudioInstallation = '',
    [string]$OutputDirectory = ''
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
$output = if ($OutputDirectory) { [IO.Path]::GetFullPath($OutputDirectory) } else { Join-Path $repo 'Build\Obj\MaterialProductProbe' }
if ($OutputDirectory -and (Test-Path -LiteralPath $output)) { throw 'Use a fresh evidence directory.' }
New-Item -ItemType Directory -Path $output -Force | Out-Null
$runs = @()
foreach ($configuration in @('Debug', 'Release')) {
    $buildOutput = @(& $msbuild (Join-Path $PSScriptRoot 'MaterialRenderBindingsProbe.vcxproj') /m /nologo `
        "/p:Configuration=$configuration" /p:Platform=x64 /p:UseDynamicDebugging=false /p:LinkIncremental=false /v:minimal 2>&1)
    $buildExit = $LASTEXITCODE
    $buildOutput | Set-Content -LiteralPath (Join-Path $output "render-bindings-build-$configuration.log") -Encoding utf8
    if ($buildExit -ne 0) { throw "Material render binding probe build failed: $configuration" }
    $exe = Join-Path $repo "Bin\x64-$configuration\Tools\MaterialRenderBindingsProbe\MaterialRenderBindingsProbe.exe"
    $previousPath = $env:PATH
    $previousValidation = $env:CREATOR_DX12_VALIDATION
    try {
        $dependencyRoot = if ($configuration -eq 'Debug') { 'vcpkg_installed\x64-windows\x64-windows\debug\bin' } else { 'vcpkg_installed\x64-windows\x64-windows\bin' }
        $env:PATH = (Join-Path $repo "Bin/x64-$configuration/Runtime/Common") + ';' +
            (Join-Path $repo $dependencyRoot) + ';' + (Join-Path $repo 'ThirdParty/DotNetHost/bin') + ';' + $previousPath
        $env:CREATOR_DX12_VALIDATION = 'gpu'
        $result = @(& $exe $repo 2>&1)
        $resultExit = $LASTEXITCODE
    } finally {
        $env:PATH = $previousPath
        $env:CREATOR_DX12_VALIDATION = $previousValidation
    }
    $result | Set-Content -LiteralPath (Join-Path $output "render-bindings-$configuration.log") -Encoding utf8
    if ($resultExit -ne 0 -or @($result | Where-Object { $_ -match '^LX_MATERIAL_RENDER_BINDINGS_OK ' }).Count -ne 1) {
        throw "Material render binding runtime failed ($configuration), exit=$resultExit : $($result -join "`n")"
    }
    $line = @($result | Where-Object { $_ -match '^LX_MATERIAL_RENDER_BINDINGS_OK ' })[0]
    $allocationChecks = if ($configuration -eq 'Debug') { 1 } else { 0 }
    if ($line -notmatch "poolAllocationChecks=$allocationChecks poolReleaseChecks=$allocationChecks nativeCacheAllocationChecks=$allocationChecks nativeCacheReleaseChecks=$allocationChecks$") { throw 'The rebuilt pool allocation/release regressions did not run.' }
    $runs += [ordered]@{configuration=$configuration;exitCode=$resultExit;binarySHA256=(Get-FileHash $exe).Hash;verdict=$line;debugCallingThreadAllocationChecks=$allocationChecks;debugBeforeNativeReleaseChecks=$allocationChecks}
    "LX_MATERIAL_RENDER_BINDINGS_CONFIGURATION_OK configuration=$configuration"
    $result | Where-Object { $_ -match '^LX_MATERIAL_RENDER_BINDINGS_OK ' }
}
$paths = @('Engine/RenderEngine/MaterialGraphTransientBufferPool.h','Engine/RenderEngine/RHI/DX12/DX12RootSignatureCache.h','Engine/RenderEngine/RHI/DX12/DX12DescriptorHeaps.h','Tools/regression/material_render_bindings_probe.cpp',
    'Tools/regression/support/MaterialPipelineSlot.cpp','Tools/regression/support/MaterialPipelineSlot.h',
    'Tools/regression/verify-material-render-bindings.ps1')
[ordered]@{pass=$true;runs=$runs;scope='Actual DX12 device, material render binding and transient buffer lifetime tests; Debug CRT counts warmed calling-thread pool allocations.';
    sources=@($paths|ForEach-Object {[ordered]@{path=$_;sha256=(Get-FileHash (Join-Path $repo $_)).Hash}})} |
    ConvertTo-Json -Depth 8 | Set-Content (Join-Path $output 'result.json') -Encoding utf8
exit 0
