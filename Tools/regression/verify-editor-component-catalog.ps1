[CmdletBinding()]
param([string]$VisualStudioInstallation = 'C:\Program Files\Microsoft Visual Studio\18\Community')
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$repo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$work = Join-Path $repo 'Build/Obj/EditorComponentCatalog'
New-Item -ItemType Directory -Force -Path $work | Out-Null
$vcvars = Join-Path $VisualStudioInstallation 'VC/Auxiliary/Build/vcvars64.bat'
$probe = Join-Path $PSScriptRoot 'editor_component_catalog_probe.cpp'
$exe = Join-Path $work 'component_catalog_probe.exe'
$object = Join-Path $work 'component_catalog_probe.obj'
$command = 'call "' + $vcvars + '" >nul && cl.exe /nologo /EHsc /std:c++20 /utf-8 /W4 ' +
    '/I"' + (Join-Path $repo 'Editor/EngineEntry') + '" /Fo:"' + $object + '" /Fe:"' + $exe + '" "' + $probe + '"'
& $env:ComSpec /d /s /c $command
if ($LASTEXITCODE -ne 0) { throw 'Component catalog probe build failed.' }
& $exe
if ($LASTEXITCODE -ne 0) { throw 'Component catalog search or category regression failed.' }
