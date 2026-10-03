param(
    [string]$VisualStudioInstallation = '',
    [switch]$SkipProjectReferences
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
$dependencies = Join-Path $repo 'vcpkg_installed\x64-windows\x64-windows'
if (!(Test-Path -LiteralPath (Join-Path $dependencies 'include\reflgen\runtime\registry.h'))) {
    $dependencies = Join-Path $repo 'vcpkg_installed\x64-windows'
}
foreach ($name in @('fixture.shadergraph', 'fixture.shadergraph.meta')) {
    if (!(Test-Path -LiteralPath (Join-Path $output $name))) {
        throw 'Run verify-material-product.ps1 first to export the source graph fixture.'
    }
}
foreach ($configuration in @('Debug', 'Release')) {
    $buildArguments = @((Join-Path $PSScriptRoot 'MaterialDataSystemProbe.vcxproj'), '/m:2', '/nologo',
        "/p:Configuration=$configuration", '/p:Platform=x64', '/p:UseDynamicDebugging=false',
        '/p:LinkIncremental=false', '/v:minimal')
    if ($SkipProjectReferences) { $buildArguments += '/p:BuildProjectReferences=false' }
    $buildOutput = @(& $msbuild @buildArguments 2>&1)
    $buildExit = $LASTEXITCODE
    $buildOutput | Set-Content -LiteralPath (Join-Path $output "datasystem-build-$configuration.log") -Encoding utf8
    if ($buildExit -ne 0) { throw "DataSystem probe build failed: $configuration" }
    $package = Join-Path $output ("DataSystemGate-$configuration-" + [Guid]::NewGuid().ToString('N'))
    $assetRoot = Join-Path $package 'Assets'
    New-Item -ItemType Directory -Path $assetRoot -Force | Out-Null
    foreach ($name in @('fixture.shadergraph', 'fixture.shadergraph.meta')) {
        Copy-Item -LiteralPath (Join-Path $output $name) -Destination (Join-Path $assetRoot $name)
    }
    [IO.File]::WriteAllBytes((Join-Path $assetRoot 'fixture.png'), [Convert]::FromBase64String(
        'iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAQAAAC1HAwCAAAAC0lEQVR42mP8/x8AAwMCAO+jVI4AAAAASUVORK5CYII='))
    'guid: 22222222-2222-4222-8222-222222222222' |
        Set-Content -LiteralPath (Join-Path $assetRoot 'fixture.png.meta') -Encoding utf8
    # Product runtime requires the generated contract. Compile through the
    # real Scene host instead of repackaging the low-level product fixture.
    $cooker = Join-Path $repo "Bin\x64-$configuration\Tools\AssetCooker\AssetCooker.exe"
    $cooked = Join-Path $package 'Cooked'
    $exe = Join-Path $repo "Bin\x64-$configuration\Tools\MaterialDataSystemProbe\MaterialDataSystemProbe.exe"
    $previousPath = $env:PATH
    try {
        $dependencyBin = if ($configuration -eq 'Debug') { 'debug\bin' } else { 'bin' }
        $env:PATH = (Join-Path $dependencies $dependencyBin) + ';' + $previousPath
        $cookOutput = @(& $cooker --asset-root $assetRoot --output $cooked `
            --shadergraph fixture.shadergraph --material-shader-root `
            (Join-Path $repo 'Dynamic_CPP\Assets\Shaders\DefaultPassShader') `
            --texture (Join-Path $assetRoot 'fixture.png') 2>&1)
        $cookExit = $LASTEXITCODE
        $cookOutput | Set-Content -LiteralPath (Join-Path $package 'cook.log') -Encoding utf8
        if ($cookExit -ne 0 -or !($cookOutput -match 'materialPrograms=1')) {
            throw "DataSystem fixture cook failed ($configuration): $($cookOutput -join "`n")"
        }
        Copy-Item -LiteralPath (Join-Path $cooked 'Derived') -Destination $assetRoot -Recurse
        $result = @(& $exe $assetRoot 2>&1)
        $resultExit = $LASTEXITCODE
    } finally {
        $env:PATH = $previousPath
    }
    $result | Set-Content -LiteralPath (Join-Path $output "datasystem-$configuration.log") -Encoding utf8
    if ($resultExit -ne 0 -or @($result | Where-Object { $_ -match '^LX_MATERIAL_DATASYSTEM_OK ' }).Count -ne 1) {
        throw "DataSystem runtime failed ($configuration): $($result -join "`n")"
    }
    "LX_MATERIAL_DATASYSTEM_CONFIGURATION_OK configuration=$configuration"
    $result | Where-Object { $_ -match '^LX_MATERIAL_DATASYSTEM_OK ' }
}
exit 0
