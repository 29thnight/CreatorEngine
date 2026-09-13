[CmdletBinding()]
param([string]$VisualStudioInstallation='C:\Program Files\Microsoft Visual Studio\18\Community')
Set-StrictMode -Version Latest
$ErrorActionPreference='Stop'
$repo=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$work=Join-Path $repo 'Build/Obj/EditorWorkspace'
New-Item -ItemType Directory -Force -Path $work | Out-Null
$deps=Join-Path $repo 'vcpkg_installed/x64-windows/x64-windows'
$exe=Join-Path $work 'workspace_probe.exe'
$vcvars=Join-Path $VisualStudioInstallation 'VC/Auxiliary/Build/vcvars64.bat'
$command='call "'+$vcvars+'" >nul && cl.exe /nologo /EHsc /std:c++20 /utf-8 /MDd /Od /W4 /DNOMINMAX '+
    '/I"'+(Join-Path $repo 'Editor/EngineGUIWindow')+'" /I"'+(Join-Path $repo 'Editor/ImGuiHelper')+'" '+
    '/I"'+(Join-Path $deps 'include')+'" /Fo"'+$work+'/" /Fe"'+$exe+'" '+
    '"'+(Join-Path $PSScriptRoot 'editor_workspace_probe.cpp')+'" '+
    '"'+(Join-Path $repo 'Editor/EngineGUIWindow/EditorWorkspaceFile.cpp')+'" '+
    '"'+(Join-Path $deps 'debug/lib/imguid.lib')+'" user32.lib gdi32.lib imm32.lib'
& $env:ComSpec /d /s /c $command
if($LASTEXITCODE -ne 0){throw 'Workspace probe build failed'}
$run=Join-Path $work ([Guid]::NewGuid().ToString('N'))
& $exe (Join-Path $PSScriptRoot 'fixtures/imgui-ini') $run
if($LASTEXITCODE -ne 0){throw 'Workspace storage regression failed'}
