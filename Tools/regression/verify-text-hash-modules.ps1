[CmdletBinding()]
param([ValidateSet('Debug', 'Release', 'All')][string]$Configuration = 'All')
$ErrorActionPreference = 'Stop'
$repo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$vs = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
$msbuild = Join-Path $vs 'MSBuild/Current/Bin/amd64/MSBuild.exe'
$configs = if ($Configuration -eq 'All') { @('Debug', 'Release') } else { @($Configuration) }
foreach ($config in $configs) {
    & $msbuild (Join-Path $PSScriptRoot 'TextHashModuleProbe.vcxproj') /t:Build "/p:Configuration=$config" /p:Platform=x64 /m:1 /nologo /v:minimal
    if ($LASTEXITCODE -ne 0) { throw "$config text/hash build failed" }
    & (Join-Path $repo "Build/Validation/UtilityModules/TextHashModuleProbe/x64-$config/TextHashModuleProbe.exe")
    if ($LASTEXITCODE -ne 0) { throw "$config text/hash contracts failed" }
}
