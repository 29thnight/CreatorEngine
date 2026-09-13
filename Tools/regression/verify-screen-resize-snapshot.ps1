[CmdletBinding()]
param(
    [ValidateSet('Debug', 'Release', 'All')][string]$Configuration = 'All',
    [string]$VisualStudioInstallation = 'C:\Program Files\Microsoft Visual Studio\18\Community'
)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$vcvars = Join-Path $VisualStudioInstallation 'VC/Auxiliary/Build/vcvars64.bat'
if (-not (Test-Path -LiteralPath $vcvars)) { throw "Missing x64 toolchain: $vcvars" }
$configurations = if ($Configuration -eq 'All') { @('Debug', 'Release') } else { @($Configuration) }
foreach ($current in $configurations) {
    $output = Join-Path $repoRoot "Build/Obj/ScreenResizeSnapshot/$current"
    New-Item -Path $output -ItemType Directory -Force | Out-Null
    $probe = Join-Path $PSScriptRoot 'screen_resize_snapshot_probe.cpp'
    $include = Join-Path $repoRoot 'Engine/RenderEngine/RHI'
    $executable = Join-Path $output 'screen_resize_snapshot_probe.exe'
    $object = Join-Path $output 'screen_resize_snapshot_probe.obj'
    $options = if ($current -eq 'Debug') { '/MDd /Od /RTC1' } else { '/MD /O2' }
    $command = 'call "' + $vcvars + '" >nul && cl.exe /nologo /EHsc /std:c++20 /utf-8 /W4 /WX ' +
        $options + ' /I"' + $include + '" /Fo:"' + $object + '" /Fe:"' + $executable +
        '" "' + $probe + '" && "' + $executable + '"'
    & $env:ComSpec /d /s /c $command
    if ($LASTEXITCODE -ne 0) { throw "$current snapshot contract failed: $LASTEXITCODE" }
}
