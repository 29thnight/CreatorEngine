$ErrorActionPreference='Stop'
$repo=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$out=Join-Path $repo 'Build/Verification/ContactStream/CaptureReader'
New-Item -ItemType Directory -Force $out|Out-Null
$sources=@('Tools/regression/physics_contact_capture_probe.cpp') + (@('ProfileMarker','ProfileThreadStream','ProfileCapture','ProfileCaptureFile','ProfileRecording')|ForEach-Object {'Engine/EngineDiagnostics/'+$_+'.cpp'})
$sourceArgs=($sources|ForEach-Object {'"'+(Join-Path $repo $_)+'"'}) -join ' '
$command='call "C:/Program Files/Microsoft Visual Studio/18/Community/VC/Auxiliary/Build/vcvars64.bat" >nul && cl /nologo /EHsc /std:c++latest /utf-8 /MD /O2 /DNDEBUG /DCE_SHIPPING=0 /Fo"'+$out+'/" /Fe"'+$out+'/contact-capture.exe" '+$sourceArgs
& $env:ComSpec /d /s /c $command *> "$out/build.log"
if($LASTEXITCODE){throw "Contact capture reader compile failed: $out/build.log"}
