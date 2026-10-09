param([ValidateSet('Debug','Release','Shipping','ASan','All')][string]$Configuration='All', [switch]$RequireGpu)

$ErrorActionPreference='Stop'
$repo=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$deps=Join-Path $repo 'vcpkg_installed/x64-windows/x64-windows'
$vcvars='C:/Program Files/Microsoft Visual Studio/18/Community/VC/Auxiliary/Build/vcvars64.bat'
$configs=if($Configuration -eq 'All'){@('Debug','Release','Shipping','ASan')}else{@($Configuration)}

foreach($config in $configs){
    $out=Join-Path $repo "Build/Obj/Phase19B2/$config"
    New-Item -ItemType Directory -Force $out|Out-Null
    $lib=if($config -eq 'Debug'){Join-Path $deps 'debug'}else{$deps}
    $flags=if($config -eq 'Debug'){'/MDd /Od /D_DEBUG'}elseif($config -eq 'ASan'){'/MD /Od /Zi /fsanitize=address /DNDEBUG'}else{'/MD /O2 /DNDEBUG'}
    $exe=Join-Path $out 'physics-b2.exe'
    $sources=@('Engine/Physics/PhysicsScene.cpp','Engine/SceneRuntime/ScenePhysicsSimulation.cpp','Tools/regression/physics_b2_probe.cpp')
    $sources+=@('ProfileMarker','ProfileThreadStream','ProfileCapture','ProfileCaptureFile','ProfileRecording','ProfileAggregate','ProfileReader','ProfileService') | ForEach-Object {'Engine/EngineDiagnostics/'+$_+'.cpp'}
    if($config -eq 'Shipping'){
        $sources=@('Engine/Physics/PhysicsScene.cpp','Engine/SceneRuntime/ScenePhysicsSimulation.cpp','Tools/regression/physics_b2_probe.cpp')
    }
    $sourceArgs=($sources | ForEach-Object {'"'+(Join-Path $repo $_)+'"'}) -join ' '
    $cmd='call "'+$vcvars+'" >nul && cl /nologo /EHsc /std:c++latest /Zc:__cplusplus /utf-8 /W4 '+$flags+' /DCE_SHIPPING=0 /DCE_PHYSICS_TESTING=1 /I"'+$repo+'/ThirdParty/Mathematics/include" /external:I"'+$deps+'/include" /external:I"'+$deps+'/include/physx" /external:W0 /Fo"'+$out+'/" /Fd"'+$out+'/compiler.pdb" /FS /Fe"'+$exe+'" '+$sourceArgs+' /link /LIBPATH:"'+$lib+'/lib" PhysX_64.lib PhysXCommon_64.lib PhysXFoundation_64.lib PhysXCooking_64.lib PhysXCharacterKinematic_static_64.lib PhysXExtensions_static_64.lib PhysXPvdSDK_static_64.lib'
    if($config -eq 'Shipping'){$cmd=$cmd.Replace('/DCE_SHIPPING=0 /DCE_PHYSICS_TESTING=1','/DCE_SHIPPING=1')}
    & $env:ComSpec /d /s /c $cmd *> "$out/build.log"
    if($LASTEXITCODE){Get-Content "$out/build.log" -Tail 30; throw "$config B2 compile failed"}

    foreach($name in @('PhysX_64.dll','PhysXCommon_64.dll','PhysXCooking_64.dll','PhysXFoundation_64.dll','PhysXGpu_64.dll','PhysXDevice64.dll')){
        Copy-Item (Join-Path $lib "bin/$name") $out -Force
    }
    if($config -eq 'ASan'){
        $toolset=Get-ChildItem 'C:/Program Files/Microsoft Visual Studio/18/Community/VC/Tools/MSVC' -Directory | Sort-Object Name -Descending | Select-Object -First 1
        Copy-Item (Join-Path $toolset.FullName 'bin/Hostx64/x64/clang_rt.asan_dynamic-x86_64.dll') $out -Force
    }
    $capture=Join-Path $out 'baseline.ceprof'
    if(Test-Path -LiteralPath $capture){Remove-Item -LiteralPath $capture}
    $process=Start-Process $exe -ArgumentList ('"'+$capture+'"') -WindowStyle Hidden -PassThru -RedirectStandardOutput "$out/result.jsonl" -RedirectStandardError "$out/stderr.log"
    $handle=$process.Handle
    if(!$process.WaitForExit(60000)){$process.Kill();$process.WaitForExit();throw 'B2 probe timeout'}
    $process.WaitForExit()
    if($process.ExitCode){throw "B2 probe failed (exit $($process.ExitCode)): $(Get-Content "$out/stderr.log" -Raw)"}
    $result=Get-Content "$out/result.jsonl" -Raw|ConvertFrom-Json
    if($result.result -ne 'PHYSICS_B2_OK' -or ($config -ne 'Shipping' -and !(Test-Path $capture))){throw 'Invalid B2 result'}
    if($RequireGpu -and !$result.gpu_verified){throw 'Actual GPU B2 verification required'}
    Write-Output "PHYSICS_B2_OK $config checks=$($result.checks) gpu_verified=$($result.gpu_verified)"
}
