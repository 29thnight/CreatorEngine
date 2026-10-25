[CmdletBinding()]
param([ValidateSet('Debug', 'Release', 'All')][string]$Configuration = 'All')
$ErrorActionPreference = 'Stop'
$repo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$vs = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
$msbuild = Join-Path $vs 'MSBuild/Current/Bin/amd64/MSBuild.exe'
$configs = if ($Configuration -eq 'All') { @('Debug', 'Release') } else { @($Configuration) }
foreach ($config in $configs) {
    foreach ($name in @('DynamicBitsetModuleProbe', 'WorkPartitionModuleProbe', 'SpinLockModuleProbe', 'TimingContainerModuleProbe', 'BoundedMemoryModuleProbe', 'EncodingModuleProbe', 'GraphCatalogModuleProbe', 'ProcessServicesModuleProbe')) {
        & $msbuild (Join-Path $PSScriptRoot "$name.vcxproj") /t:Build "/p:Configuration=$config" /p:Platform=x64 /m:1 /nologo /v:minimal
        if ($LASTEXITCODE -ne 0) { throw "$config $name build failed" }
        & (Join-Path $repo "Build/Validation/UtilityModules/$name/x64-$config/$name.exe")
        if ($LASTEXITCODE -ne 0) { throw "$config $name execution failed" }
        if ($name -eq 'ProcessServicesModuleProbe') {
            & (Join-Path $repo "Build/Validation/UtilityModules/$name/x64-$config/$name.exe") player
            if ($LASTEXITCODE -ne 0) { throw "$config $name Player mode failed" }
        }
        if ($name -eq 'BoundedMemoryModuleProbe') {
            foreach ($mode in '--death-arena-before-scope', '--death-outer-before-inner') {
                & (Join-Path $repo "Build/Validation/UtilityModules/$name/x64-$config/$name.exe") $mode
                if ($LASTEXITCODE -ne 73) { throw "$config $name failed to reject $mode" }
            }
        }
    }
}
