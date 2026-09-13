[CmdletBinding()]
param([string]$VisualStudioInstallation = 'C:\Program Files\Microsoft Visual Studio\18\Community')
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$repo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$work = Join-Path $repo 'Build/Obj/EditorTextFallback'
New-Item -ItemType Directory -Force -Path $work | Out-Null
$vcvars = Join-Path $VisualStudioInstallation 'VC/Auxiliary/Build/vcvars64.bat'
$dependencies = Join-Path $repo 'vcpkg_installed/x64-windows/x64-windows'
$probe = Join-Path $PSScriptRoot 'editor_text_fallback_probe.cpp'
$exe = Join-Path $work 'text_fallback_probe.exe'
$object = Join-Path $work 'text_fallback_probe.obj'
$command = 'call "' + $vcvars + '" >nul && cl.exe /nologo /EHsc /std:c++20 /utf-8 /MDd /Od /RTC1 /W4 ' +
    '/I"' + (Join-Path $repo 'Editor/ImGuiHelper') + '" /I"' + (Join-Path $dependencies 'include') + '" /Fo:"' + $object +
    '" /Fe:"' + $exe + '" "' + $probe + '" "' + (Join-Path $dependencies 'debug/lib/imguid.lib') + '" user32.lib gdi32.lib imm32.lib'
& $env:ComSpec /d /s /c $command
if ($LASTEXITCODE -ne 0) { throw 'Text fallback probe build failed.' }
$korean = Join-Path ([Environment]::GetFolderPath('Fonts')) 'malgun.ttf'
if (-not (Test-Path -LiteralPath $korean)) { throw 'This Korean coverage gate requires Windows Malgun Gothic.' }
& $exe (Join-Path $repo 'Resources/Editor/Fonts/Inter-Regular.ttf') $korean (Join-Path $repo 'Resources/Editor/Fonts/MaterialSymbolsOutlined-Editor.ttf')
if ($LASTEXITCODE -ne 0) { throw 'Text fallback coverage or primary font preservation failed.' }
