param(
    [string]$VisualStudioInstallation = ''
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
foreach ($configuration in @('Debug', 'Release')) {
    $rootFile = Join-Path $output "runtime-cooked-$configuration-root.txt"
    if (!(Test-Path -LiteralPath $rootFile)) {
        throw 'Run verify-material-product.ps1 -VerifyAssetCooker first to produce the current cooked fixtures.'
    }
    $cooked = [IO.File]::ReadAllText($rootFile).Trim()
    if (!(Test-Path -LiteralPath (Join-Path $cooked 'Derived\asset-manifest.cemf'))) {
        throw "Cooked fixture is missing: $cooked"
    }
    $buildOutput = @(& $msbuild (Join-Path $PSScriptRoot 'MaterialDataSystemProbe.vcxproj') /m /nologo `
        "/p:Configuration=$configuration" /p:Platform=x64 /p:UseDynamicDebugging=false /p:LinkIncremental=false /v:minimal 2>&1)
    $buildExit = $LASTEXITCODE
    $buildOutput | Set-Content -LiteralPath (Join-Path $output "datasystem-build-$configuration.log") -Encoding utf8
    if ($buildExit -ne 0) { throw "DataSystem probe build failed: $configuration" }
    $package = Join-Path $output ("DataSystemGate-$configuration-" + [Guid]::NewGuid().ToString('N'))
    $assetRoot = Join-Path $package 'Assets'
    New-Item -ItemType Directory -Path $assetRoot -Force | Out-Null
    Copy-Item -LiteralPath (Join-Path $cooked 'Derived') -Destination $assetRoot -Recurse
    # AssetCooker emits Derived only. Packaging also carries the CEMF source
    # identities, which the actual packaged DataSystem registry resolves.
    $inputAssets = Join-Path (Split-Path -Parent $cooked) 'Assets'
    foreach ($name in @('fixture.shadergraph', 'fixture.png')) {
        Copy-Item -LiteralPath (Join-Path $inputAssets $name) -Destination (Join-Path $assetRoot $name)
    }
    $exe = Join-Path $repo "Bin\x64-$configuration\Tools\MaterialDataSystemProbe\MaterialDataSystemProbe.exe"
    $previousPath = $env:PATH
    try {
        $dependencyRoot = if ($configuration -eq 'Debug') { 'vcpkg_installed\x64-windows\debug\bin' } else { 'vcpkg_installed\x64-windows\bin' }
        $env:PATH = (Join-Path $repo $dependencyRoot) + ';' + $previousPath
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
