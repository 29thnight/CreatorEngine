#Requires -Version 7.0
param(
    [string]$VisualStudioInstallation = '',
    [ValidateSet('Debug', 'Release')][string]$Configuration = 'Debug'
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$repo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$encoderSource = Get-Content (Join-Path $repo 'Engine/RenderEngine/RHI/RHIEncoder.h') -Raw
$softwareSource = Get-Content (Join-Path $repo 'Engine/RenderEngine/RHI/RHISoftwareEncoder.h') -Raw
$baseMethods = @([regex]::Matches($encoderSource, 'virtual\s+(?:void|bool|uint64_t)\s+(\w+)\s*\(') |
    ForEach-Object { $_.Groups[1].Value } | Sort-Object -Unique)
$overrides = @([regex]::Matches($softwareSource, '(?:void|bool|uint64_t)\s+(\w+)\s*\([^;{}]*\)\s*(?:const\s*)?override') |
    ForEach-Object { $_.Groups[1].Value } | Sort-Object -Unique)
$missing = @($baseMethods | Where-Object { $_ -notin $overrides })
$extra = @($overrides | Where-Object { $_ -notin $baseMethods })
if ($missing.Count -gt 0 -or $extra.Count -gt 0 -or $baseMethods.Count -ne 39)
{
    throw "RHIEncoder coverage changed: missing=$($missing -join ',') extra=$($extra -join ',') count=$($baseMethods.Count)"
}
if ([string]::IsNullOrWhiteSpace($VisualStudioInstallation))
{
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
    $VisualStudioInstallation = @(& $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath)[0]
}
$vcvars = Join-Path $VisualStudioInstallation 'VC/Auxiliary/Build/vcvars64.bat'
$suffix = if ($Configuration -eq 'Debug') { '' } else { '-Release' }
$output = Join-Path $repo "Build/Obj/RHISoftwareCommandsProbe$suffix"
$runtimeFlags = if ($Configuration -eq 'Debug') { '/MDd /D_DEBUG' } else { '/MD /DNDEBUG' }
$optimization = if ($Configuration -eq 'Debug') { '/Od' } else { '/O2' }
New-Item -ItemType Directory -Force -Path $output | Out-Null
@{methods=$baseMethods;overrides=$overrides;methodCount=$baseMethods.Count} |
    ConvertTo-Json -Depth 4 | Set-Content (Join-Path $output 'coverage.json')
$sources = @('Tools/regression/rhi_software_commands_probe.cpp',
    'Engine/Utility_Framework/Arena.cpp',
    'Engine/RenderEngine/RenderArena.cpp', 'Engine/RenderEngine/RHI/RHISoftwareCommandList.cpp', 'Engine/RenderEngine/RHI/RHISoftwareEncoder.cpp',
    'Engine/RenderEngine/RHI/RHISoftwareBatch.cpp', 'Engine/RenderEngine/RHI/RHISoftwareRecorder.cpp')
$quoted = ($sources | ForEach-Object { '"' + (Join-Path $repo $_) + '"' }) -join ' '
$exe = Join-Path $output 'rhi_software_commands_probe.exe'
$mimallocObject = Join-Path $output 'mimalloc-static.obj'
$mimallocCompile = 'call "' + $vcvars + '" >nul && cl.exe /nologo /TC ' + $runtimeFlags + ' /O2 /Zi /DMI_STATIC_LIB ' +
    '/I"' + (Join-Path $repo 'ThirdParty/mimalloc/include') + '" /c "' +
    (Join-Path $repo 'ThirdParty/mimalloc/src/static.c') + '" /Fo:"' + $mimallocObject + '"'
& $env:ComSpec /d /s /c $mimallocCompile
if ($LASTEXITCODE -ne 0)
{
    throw 'Vendored mimalloc build failed.'
}

$compile = 'call "' + $vcvars + '" >nul && cl.exe /nologo /EHsc /std:c++latest /permissive- /utf-8 ' +
    '/DCE_CONTRACT_STANDALONE /DCE_SHIPPING=1 /DNOMINMAX /DWIN32_LEAN_AND_MEAN /W4 /WX /Zi ' + $runtimeFlags + ' ' + $optimization + ' ' +
    '/Fo:"' + $output + '/" /Fd:"' + $output + '/probe.pdb" /Fe:"' + $exe + '" ' + $quoted + ' "' + $mimallocObject + '" advapi32.lib'
& $env:ComSpec /d /s /c $compile
if ($LASTEXITCODE -ne 0)
{
    throw 'Software RHI command probe build failed.'
}
$result = @(& $exe 2>&1)
$probeExit = $LASTEXITCODE
$result | Set-Content (Join-Path $output 'result.log') -Encoding utf8
if ($probeExit -ne 0 -or @($result | Where-Object { $_ -match '^RHI_SOFTWARE_COMMANDS_OK ' }).Count -ne 1)
{
    throw "Software RHI command probe failed: $($result -join '`n')"
}
$result
