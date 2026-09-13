[CmdletBinding()]
param(
    [ValidateSet('Debug', 'Release', 'All')][string]$Configuration = 'All',
    [string]$VisualStudioInstallation = 'C:\Program Files\Microsoft Visual Studio\18\Community'
)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$vcvars = Join-Path $VisualStudioInstallation 'VC/Auxiliary/Build/vcvars64.bat'
$dependencies = Join-Path $repoRoot 'vcpkg_installed/x64-windows/x64-windows'
if (-not (Test-Path -LiteralPath $vcvars)) { throw "Missing x64 toolchain: $vcvars" }
if (-not (Test-Path -LiteralPath (Join-Path $dependencies 'include/spdlog/spdlog.h'))) {
    throw "Missing repository spdlog dependency: $dependencies"
}
$configurations = if ($Configuration -eq 'All') { @('Debug', 'Release') } else { @($Configuration) }
foreach ($current in $configurations) {
    $output = Join-Path $repoRoot "Build/Obj/LogStorage/$current"
    New-Item -Path $output -ItemType Directory -Force | Out-Null
    $probe = Join-Path $PSScriptRoot 'log_store_probe.cpp'
    $executable = Join-Path $output 'log_store_probe.exe'
    $object = Join-Path $output 'log_store_probe.obj'
    $pdb = Join-Path $output 'log_store_probe.pdb'
    $options = if ($current -eq 'Debug') { '/MDd /Od /RTC1' } else { '/MD /O2' }
    $command = 'call "' + $vcvars + '" >nul && cl.exe /nologo /EHsc /std:c++latest /utf-8 /W4 /WX ' +
        $options + ' /DFMT_HEADER_ONLY /I"' + (Join-Path $repoRoot 'Engine/Utility_Framework') +
        '" /I"' + (Join-Path $repoRoot 'Editor/EngineGUIWindow') +
        '" /I"' + (Join-Path $repoRoot 'Editor/EditorWindow') +
        '" /external:I"' + (Join-Path $dependencies 'include') + '" /external:W0 /Fo:"' + $object +
        '" /Fd:"' + $pdb + '" /Fe:"' + $executable + '" "' + $probe + '"'
    & $env:ComSpec /d /s /c $command
    if ($LASTEXITCODE -ne 0) { throw "$current log storage probe compilation failed: $LASTEXITCODE" }
    & $executable (Join-Path $output 'log_store_probe.html')
    if ($LASTEXITCODE -ne 0) { throw "$current log storage regression failed: $LASTEXITCODE" }
    Write-Output "$current log storage regression passed."
}
