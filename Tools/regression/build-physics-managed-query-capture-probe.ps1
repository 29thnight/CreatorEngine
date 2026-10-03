$ErrorActionPreference='Stop'
$repo=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$out=Join-Path $repo 'Build/Obj/Phase19T2QueryBench/CaptureProbe'
New-Item -ItemType Directory -Force $out|Out-Null
$sources=@('Tools/regression/physics_managed_query_capture_probe.cpp')+(@('ProfileMarker','ProfileThreadStream','ProfileCapture','ProfileCaptureFile')|ForEach-Object {'Engine/EngineDiagnostics/'+$_+'.cpp'})
$sourceArgs=($sources|ForEach-Object {'"'+(Join-Path $repo $_)+'"'})-join ' '
$cmd='call "C:/Program Files/Microsoft Visual Studio/18/Community/VC/Auxiliary/Build/vcvars64.bat" >nul && cl /nologo /EHsc /std:c++latest /utf-8 /MD /O2 /DNDEBUG /DCE_SHIPPING=0 /Fo"'+$out+'/" /Fe"'+$out+'/capture-probe.exe" '+$sourceArgs
& $env:ComSpec /d /s /c $cmd > "$out/build.log" 2>&1
if($LASTEXITCODE){throw 'Capture probe build failed'}
