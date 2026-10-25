[CmdletBinding()]
param([ValidateSet('Debug','Release')][string]$Configuration = 'Release')
$ErrorActionPreference = 'Stop'
$repo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$vs = & "${env:ProgramFiles(x86)}/Microsoft Visual Studio/Installer/vswhere.exe" -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vs) { throw 'MSVC toolchain unavailable' }
$vcvars = Join-Path $vs 'VC/Auxiliary/Build/vcvars64.bat'
$output = Join-Path $repo "Build/Tests/GuidRegistry/x64-$Configuration"
New-Item -ItemType Directory -Path $output -Force | Out-Null
$dependencies = Join-Path $repo 'vcpkg_installed/x64-windows/x64-windows'
if (-not (Test-Path "$dependencies/include/reflgen/runtime/registry.h")) {
    $dependencies = Join-Path $repo 'vcpkg_installed/x64-windows'
}
$options = if ($Configuration -eq 'Debug') { '/Od /Zi /MDd' } else { '/O2 /MD' }
$contracts = Join-Path $repo "Build/Generated/Contracts/x64-$Configuration"
if (-not (Test-Path "$contracts/CreatorContractConfig.h")) {
    throw "Build EngineContracts $Configuration x64 first (missing generated contract configuration)."
}
$sources = @('Tools/regression/GuidRegistryTests.cpp', 'Tools/regression/GuidRegistryPeer.cpp',
    'Engine/Utility_Framework/Reflection/TypeTrait.cpp')
$quoted = ($sources | ForEach-Object { '"' + (Join-Path $repo $_) + '"' }) -join ' '
$compile = 'call "' + $vcvars + '" >nul && cl.exe /nologo /EHsc /std:c++latest /utf-8 /W4 /DCE_CONTRACT_STANDALONE /DNOMINMAX /DWIN32_LEAN_AND_MEAN ' + $options +
    ' /I"' + $contracts + '" /I"' + (Join-Path $dependencies 'include') +
    '" /Fo"' + $output + '/" /Fe"' + $output + '/GuidRegistryTests.exe" ' + $quoted + ' /link ole32.lib'
Push-Location $output
try {
    & $env:ComSpec /d /s /c $compile *> "$output/build.log"
    if ($LASTEXITCODE) { throw "GUID registry build failed: $output/build.log" }
    & "$output/GuidRegistryTests.exe" *> "$output/test.log"
    if ($LASTEXITCODE) { throw "GUID registry regression failed: $output/test.log" }
    Get-Content "$output/test.log"
} finally { Pop-Location }
