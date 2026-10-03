param(
    [string]$VisualStudioInstallation = '',
    [switch]$SkipDependencyRestore,
    [switch]$SkipBuild,
    [ValidateSet('Debug', 'Release')][string[]]$Configurations = @('Debug', 'Release')
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$repo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
if (!$SkipBuild -and [string]::IsNullOrWhiteSpace($VisualStudioInstallation)) {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
    $VisualStudioInstallation = @(& $vswhere -latest -products '*' `
        -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath)[0]
}
$output = Join-Path $repo 'Build/Obj/MaterialProductProbe'
New-Item -ItemType Directory -Path $output -Force | Out-Null
$sources = @(
    Get-ChildItem (Join-Path $repo 'Engine/RenderEngine'), (Join-Path $repo 'Engine/Utility_Framework'),
        (Join-Path $repo 'Lattice'), (Join-Path $repo 'Dynamic_CPP/Assets/Shaders/DefaultPassShader') `
        -Recurse -File | Where-Object Extension -In '.cpp', '.h', '.slang', '.shadermeta', '.vcxproj', '.props', '.targets'
    Get-ChildItem $PSScriptRoot -File | Where-Object Name -Match '^material_(raster|forward|scene|ibl)|^principled_layered_reference|^MaterialRasterSurfaceProbe.vcxproj$'
    Get-Item $PSCommandPath, (Join-Path $repo 'Directory.Build.props'),
        (Join-Path $repo 'Directory.Build.targets'), (Join-Path $repo 'EngineOutput.props')
) | Sort-Object FullName -Unique
$snapshot = @($sources | ForEach-Object {
    [pscustomobject]@{ path = $_.FullName; hash = (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash }
})
$snapshot | ConvertTo-Json | Set-Content (Join-Path $output 'forward-transport-source-hashes.json') -Encoding utf8
$dependencyOptions = @()
if ($SkipDependencyRestore) { $dependencyOptions += '/p:VcpkgManifestInstall=false' }
foreach ($configuration in $Configurations) {
    if (!$SkipBuild) {
        $msbuild = Join-Path $VisualStudioInstallation 'MSBuild/Current/Bin/MSBuild.exe'
        $build = @(& $msbuild (Join-Path $PSScriptRoot 'MaterialRasterSurfaceProbe.vcxproj') /m:2 /nologo `
            "/p:Configuration=$configuration" /p:Platform=x64 /p:UseDynamicDebugging=false `
            /p:LinkIncremental=false @dependencyOptions /v:minimal 2>&1)
        $buildExit = $LASTEXITCODE
        $build | Set-Content (Join-Path $output "forward-transport-build-$configuration.log") -Encoding utf8
        if ($buildExit -ne 0) { throw "Forward transport build failed: DX12/$configuration" }
    }
    $exe = Join-Path $repo "Bin/x64-$configuration/Tools/MaterialRasterSurfaceProbe/MaterialRasterSurfaceProbe.exe"
    $previousPath = $env:PATH
    $previousValidation = $env:CREATOR_DX12_VALIDATION
    try {
        $dependencyRoot = if ($configuration -eq 'Debug') { 'debug/bin' } else { 'bin' }
        $env:PATH = (Join-Path $repo "vcpkg_installed/x64-windows/x64-windows/$dependencyRoot") + ';' + $previousPath
        $env:CREATOR_DX12_VALIDATION = 'gpu'
        $result = @(& $exe $repo --forward-transport 2>&1 |
            Tee-Object -FilePath (Join-Path $output "forward-transport-$configuration.log"))
        $runExit = $LASTEXITCODE
    } finally {
        $env:PATH = $previousPath
        $env:CREATOR_DX12_VALIDATION = $previousValidation
    }
    $result | Set-Content (Join-Path $output "forward-transport-$configuration.log") -Encoding utf8
    if ($runExit -ne 0 -or @($result | Where-Object {
        $_ -match '^LX_MATERIAL_FORWARD_TRANSPORT_OK frames=24 components=2268 checks=[1-9]\d* graphCompiles=0 validation=0$'
    }).Count -ne 1) { throw "Forward transport runtime failed: DX12/$configuration" }
    foreach ($entry in $snapshot) {
        if ((Get-FileHash -LiteralPath $entry.path -Algorithm SHA256).Hash -ne $entry.hash) {
            throw "Forward transport source changed during validation: $($entry.path)"
        }
    }
    "LX_MATERIAL_FORWARD_TRANSPORT_CONFIGURATION_OK backend=DX12 configuration=$configuration sources=$($snapshot.Count)"
    $result | Where-Object { $_ -match '^LX_MATERIAL_FORWARD_TRANSPORT_OK ' }
}
exit 0
