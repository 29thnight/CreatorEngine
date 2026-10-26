#Requires -Version 7.0
$ErrorActionPreference='Stop'
$repo=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$out=Join-Path $repo 'Build/Obj/RenderScheduling-Debug'
New-Item -ItemType Directory -Path $out -Force | Out-Null
$vcvars='C:/Program Files/Microsoft Visual Studio/18/Community/VC/Auxiliary/Build/vcvars64.bat'
$command='call "'+$vcvars+'" >nul && cl /nologo /EHsc /std:c++latest /utf-8 /W4 /MDd /Od /RTC1 /D_DEBUG /DCE_CONTRACT_STANDALONE /Fo"'+$out+'/" /Fe"'+$out+'/probe.exe" "'+$repo+'/Tools/regression/render_scheduling_probe.cpp" "'+$repo+'/Engine/RenderEngine/RenderThreadScheduling.cpp"'
& $env:ComSpec /d /s /c $command
if ($LASTEXITCODE) { throw 'Render scheduling build failed' }
& "$out/probe.exe"
if ($LASTEXITCODE) { throw 'Render scheduling regression failed' }
