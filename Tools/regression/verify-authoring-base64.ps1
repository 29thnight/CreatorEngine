[CmdletBinding()]
param(
    [ValidateSet('Debug', 'Release', 'All')][string]$Configuration = 'All',
    [string]$VisualStudioInstallation = ''
)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$module = Join-Path $repoRoot 'Engine/Contracts/Core/Creator.Core.Base64.ixx'
$project = Join-Path $PSScriptRoot 'EncodingModuleProbe.vcxproj'
$dataSystem = Join-Path $repoRoot 'Engine/RenderEngine/DataSystem.cpp'
foreach ($required in @($module, $project, $dataSystem)) {
    if (-not (Test-Path -LiteralPath $required -PathType Leaf)) { throw "Base64 input is missing: $required" }
}
if (Select-String -LiteralPath $dataSystem -Pattern 'YAML::(?:Encode|Decode)Base64' -Quiet) {
    throw 'DataSystem still delegates base64 payloads to yaml-cpp.'
}
foreach ($pattern in 'import Creator.Core.Base64;', 'ce::base64::encode', 'ce::base64::decode') {
    if (-not (Select-String -LiteralPath $dataSystem -SimpleMatch -Pattern $pattern -Quiet)) {
        throw "DataSystem is missing the direct codec dependency: $pattern"
    }
}
if ([string]::IsNullOrWhiteSpace($VisualStudioInstallation)) {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
    $VisualStudioInstallation = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    if (-not $VisualStudioInstallation) { throw 'Visual Studio C++ toolchain was not found.' }
}
$msbuild = Join-Path $VisualStudioInstallation 'MSBuild/Current/Bin/amd64/MSBuild.exe'
$configurations = if ($Configuration -eq 'All') { @('Debug', 'Release') } else { @($Configuration) }
foreach ($current in $configurations) {
    & $msbuild $project /t:Build "/p:Configuration=$current" /p:Platform=x64 "/p:SolutionDir=$repoRoot\" /m:1 /nologo /v:minimal
    if ($LASTEXITCODE -ne 0) { throw "Base64 $current module build failed." }
    & (Join-Path $repoRoot "Build/Validation/UtilityModules/EncodingModuleProbe/x64-$current/EncodingModuleProbe.exe")
    if ($LASTEXITCODE -ne 0) { throw "Base64 $current contract execution failed." }
}
