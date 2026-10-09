param([ValidateSet('Debug','Release','Shipping','All')][string]$Configuration='All')
$ErrorActionPreference='Stop'
$repo=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$deps=Join-Path $repo 'vcpkg_installed/x64-windows/x64-windows'
$vcvars='C:/Program Files/Microsoft Visual Studio/18/Community/VC/Auxiliary/Build/vcvars64.bat'
$configs=if($Configuration -eq 'All'){@('Debug','Release','Shipping')}else{@($Configuration)}
foreach($config in $configs){
    $out=Join-Path $repo "Build/Obj/Phase19P1/$config"
    New-Item -ItemType Directory -Force $out|Out-Null
    $lib=if($config -eq 'Debug'){Join-Path $deps 'debug'}else{$deps}
    $flags=if($config -eq 'Debug'){'/MDd /Od /D_DEBUG'}else{'/MD /O2 /DNDEBUG'}
    $exe=Join-Path $out 'physics-p1.exe'
    $cmd='call "'+$vcvars+'" >nul && cl /nologo /EHsc /std:c++latest /Zc:__cplusplus /utf-8 /W4 '+$flags+' /DCE_SHIPPING=0 /I"'+$repo+'/ThirdParty/Mathematics/include" /external:I"'+$deps+'/include" /external:I"'+$deps+'/include/physx" /external:W0 /Fo"'+$out+'/" /Fd"'+$out+'/compiler.pdb" /FS /Fe"'+$exe+'" "'+$repo+'/Engine/Physics/PhysicsScene.cpp" "'+$PSScriptRoot+'/physics_p1_probe.cpp" /link /LIBPATH:"'+$lib+'/lib" /LIBPATH:"'+$repo+'/Build/Lib/x64-'+$config+'" EngineDiagnostics.lib PhysX_64.lib PhysXCommon_64.lib PhysXFoundation_64.lib PhysXCooking_64.lib PhysXCharacterKinematic_static_64.lib PhysXExtensions_static_64.lib PhysXPvdSDK_static_64.lib'
    $diagnostics=@('ProfileMarker','ProfileThreadStream','ProfileCapture','ProfileCaptureFile','ProfileRecording','ProfileAggregate','ProfileReader','ProfileService') | ForEach-Object {'"'+$repo+'/Engine/EngineDiagnostics/'+$_+'.cpp"'}
    $cmd=$cmd.Replace(' /link ', ' '+($diagnostics -join ' ')+' /link ').Replace(' EngineDiagnostics.lib ',' ').Replace('/DCE_SHIPPING=0','/DCE_SHIPPING=0 /DCE_PHYSICS_TESTING=1')
    if($config -eq 'Shipping'){
        $cmd=$cmd.Replace('/DCE_SHIPPING=0','/DCE_SHIPPING=1').Replace('physics_p1_probe.cpp','physics_p1_shipping_probe.cpp')
        foreach($source in $diagnostics){$cmd=$cmd.Replace(' '+$source,'')}
    }
    & $env:ComSpec /d /s /c $cmd
    if($LASTEXITCODE){throw "$config P1 compile failed"}
    foreach($name in @('PhysX_64.dll','PhysXCommon_64.dll','PhysXCooking_64.dll','PhysXFoundation_64.dll','PhysXGpu_64.dll','PhysXDevice64.dll')){Copy-Item (Join-Path $lib "bin/$name") $out -Force}
    $capture=Join-Path $out 'baseline.ceprof'
    if(Test-Path -LiteralPath $capture){Remove-Item -LiteralPath $capture}
    $process=Start-Process $exe -ArgumentList ('"'+$capture+'"') -WindowStyle Hidden -PassThru -RedirectStandardOutput "$out/result.jsonl" -RedirectStandardError "$out/stderr.log"
    $handle=$process.Handle
    if(!$process.WaitForExit(60000)){$process.Kill();$process.WaitForExit();throw 'P1 probe timeout'}
    $process.WaitForExit()
    if($process.ExitCode){throw "P1 probe failed: $(Get-Content "$out/stderr.log" -Raw)"}
    $result=Get-Content "$out/result.jsonl" -Raw|ConvertFrom-Json
    if($result.result -ne 'PHYSICS_P1_OK' -or $result.sdk_tasks -le 0){throw 'Invalid P1 result'}
    if($config -ne 'Shipping' -and !(Test-Path $capture)){throw 'Missing P1 capture'}
    Write-Output "PHYSICS_P1_OK $config checks=$($result.checks) tasks=$($result.sdk_tasks) gpu_verified=$($result.gpu_verified)"
}
