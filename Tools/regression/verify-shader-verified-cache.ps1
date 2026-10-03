param([string]$VisualStudioInstallation='', [string]$OutputRoot='')
Set-StrictMode -Version Latest
$ErrorActionPreference='Stop'
$repo=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
if(!$VisualStudioInstallation){$vswhere=Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe';$VisualStudioInstallation=@(& $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath)[0]}
if(!$OutputRoot){$OutputRoot=Join-Path $repo 'Build/Obj/ShaderVerifiedCacheProbe'}
$output=[IO.Path]::GetFullPath($OutputRoot)
New-Item -ItemType Directory -Force -Path $output|Out-Null
$vcvars=Join-Path $VisualStudioInstallation 'VC/Auxiliary/Build/vcvars64.bat'
$flags='/nologo /MP2 /EHsc /std:c++latest /O2 /W4 /utf-8 /DNOMINMAX /DWIN32_LEAN_AND_MEAN /I"'+(Join-Path $repo 'Engine/RenderEngine/RHI')+'" /I"'+(Join-Path $repo 'Engine/Utility_Framework')+'" /I"'+(Join-Path $repo 'ThirdParty/Slang/include')+'" /Fo:"'+$output+'/" '
foreach($test in @('codec','probe')){
 $sources=if($test -eq 'codec'){@('Tools/regression/shader_verified_cache_tests.cpp')}else{@('Tools/regression/shader_verified_cache_probe.cpp','Engine/RenderEngine/RHI/RHIShaderCompiler.cpp','Engine/RenderEngine/RHI/RHIShaderSource.cpp','Engine/RenderEngine/RHI/RHIShaderPermutation.cpp','Engine/RenderEngine/RHI/RHIShaderReflection.cpp')}
 $quoted=($sources|ForEach-Object {'"'+(Join-Path $repo $_)+'"'}) -join ' '
 $exe=Join-Path $output ("cache-$test.exe")
 $command='call "'+$vcvars+'" >nul && cl.exe '+$flags+'/Fe:"'+$exe+'" '+$quoted
 $build=@(& $env:ComSpec /d /s /c $command 2>&1);$code=$LASTEXITCODE
 $build|Set-Content (Join-Path $output "build-$test.log") -Encoding utf8
 if($code -ne 0){throw "Cache $test build failed; see $output/build-$test.log"}
}
& (Join-Path $output 'cache-codec.exe')
if($LASTEXITCODE -ne 0){throw 'Codec checks failed'}
$work=Join-Path $output ('Run-'+[Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $work|Out-Null
$oldLocal=$env:LOCALAPPDATA
try{
 $env:LOCALAPPDATA=Join-Path $work 'AppData'
 $probe=Join-Path $output 'cache-probe.exe'
 foreach($mode in @('populate','restart','invalidation','corruption')){
  $result=@(& $probe $repo $work $mode 2>&1);$code=$LASTEXITCODE
  $result|Set-Content (Join-Path $work "$mode.log") -Encoding utf8
  if($code -ne 0){throw "Cache $mode checks failed: $($result -join "`n")"}
  $result
 }
 $env:LOCALAPPDATA=Join-Path $work 'RaceAppData'
 $owned=@()
 for($i=0;$i -lt 2;$i++){
  $owned+=Start-Process -FilePath $probe -ArgumentList @('"'+$repo+'"','"'+$work+'"','race') -WindowStyle Hidden -PassThru -RedirectStandardOutput (Join-Path $work "race-$i.log") -RedirectStandardError (Join-Path $work "race-$i.stderr.log")
 }
 foreach($process in $owned){if(!$process.WaitForExit(30000)){throw "Cache race process $($process.Id) timeout"};$process.Refresh();if($process.ExitCode -ne 0){throw "Cache race process $($process.Id) failed"}}
 $result=@(& $probe $repo $work race 2>&1);$code=$LASTEXITCODE
 $result|Set-Content (Join-Path $work 'race-restart.log') -Encoding utf8
 if($code -ne 0 -or !($result -match 'compiles=0')){throw 'Concurrent record restart failed'}
 $result
 "SHADER_VERIFIED_CACHE_GATE_OK artifacts=$work"
}finally{$env:LOCALAPPDATA=$oldLocal}
