[CmdletBinding()]
param([string]$VisualStudioInstallation = 'C:\Program Files\Microsoft Visual Studio\18\Community')
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$work = Join-Path $repoRoot 'Build/Obj/EditorIconAlignment'
New-Item -ItemType Directory -Force -Path $work | Out-Null
$vcvars = Join-Path $VisualStudioInstallation 'VC/Auxiliary/Build/vcvars64.bat'
$dependencies = Join-Path $repoRoot 'vcpkg_installed/x64-windows/x64-windows'
$include = Join-Path $repoRoot 'Editor/ImGuiHelper'
$probe = Join-Path $PSScriptRoot 'editor_icon_alignment_probe.cpp'
$exe = Join-Path $work 'editor_icon_alignment_probe.exe'
$object = Join-Path $work 'editor_icon_alignment_probe.obj'
$library = Join-Path $dependencies 'debug/lib/imguid.lib'
$command = 'call "' + $vcvars + '" >nul && cl.exe /nologo /EHsc /std:c++20 /utf-8 /MDd /Od /RTC1 /W4 ' +
    '/I"' + $include + '" /I"' + (Join-Path $dependencies 'include') + '" /Fo:"' + $object +
    '" /Fe:"' + $exe + '" "' + $probe + '" "' + $library + '" user32.lib gdi32.lib imm32.lib'
& $env:ComSpec /d /s /c $command
if ($LASTEXITCODE -ne 0) { throw 'Icon alignment probe build failed.' }
$fonts = @((Join-Path $repoRoot 'Resources/Editor/Fonts/Inter-Regular.ttf'))
foreach ($name in @('Verdana.ttf', 'malgun.ttf')) {
    $candidate = Join-Path ([Environment]::GetFolderPath('Fonts')) $name
    if (Test-Path -LiteralPath $candidate) { $fonts += $candidate }
}
& $exe (Join-Path $repoRoot 'Resources/Editor/Fonts/MaterialSymbolsOutlined-Editor.ttf') @fonts
if ($LASTEXITCODE -ne 0) { throw 'Icon/text raster alignment failed.' }
