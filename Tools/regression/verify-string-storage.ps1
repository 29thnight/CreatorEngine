[CmdletBinding()]
param(
    [ValidateSet('Debug', 'Release', 'All')]
    [string]$Configuration = 'All',
    [string]$VisualStudioInstallation = ''
)

$ErrorActionPreference = 'Stop'
$repo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
if ([string]::IsNullOrWhiteSpace($VisualStudioInstallation)) {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
    $VisualStudioInstallation = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
}
$msbuild = Join-Path $VisualStudioInstallation 'MSBuild/Current/Bin/amd64/MSBuild.exe'
$configs = if ($Configuration -eq 'All') { @('Debug', 'Release') } else { @($Configuration) }
foreach ($config in $configs) {
    & $msbuild (Join-Path $PSScriptRoot 'CoreValuesModuleProbe.vcxproj') /t:Build "/p:Configuration=$config" /p:Platform=x64 /m:1 /nologo /v:minimal
    if ($LASTEXITCODE -ne 0) { throw "$config core utility build failed" }
    & (Join-Path $repo "Build/Validation/CoreValues/x64-$config/CoreValuesModuleProbe.exe")
    if ($LASTEXITCODE -ne 0) { throw "$config core utility contracts failed" }
}
