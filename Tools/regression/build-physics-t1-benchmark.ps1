param([ValidateSet('candidate','serial','merge','wake','profile')][string]$Side='candidate', [switch]$LegacyPublication)

$ErrorActionPreference='Stop'
$repo=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$deps=Join-Path $repo 'vcpkg_installed/x64-windows/x64-windows'
$vcvars='C:/Program Files/Microsoft Visual Studio/18/Community/VC/Auxiliary/Build/vcvars64.bat'
$configs=@('Release')
& python "$PSScriptRoot/prepare-physics-t1-serial-benchmark.py" $(if($Side -in @('wake','profile')){'serial'}else{$Side}); if($LASTEXITCODE){throw 'Benchmark fixture generation failed'}

foreach($config in $configs){
    $out=Join-Path $repo "Build/Obj/Phase19T1Bench/$Side"
    New-Item -ItemType Directory -Force $out|Out-Null
    $lib=if($config -eq 'Debug'){Join-Path $deps 'debug'}else{$deps}
    $flags=if($config -eq 'Debug'){'/MDd /Od /D_DEBUG'}elseif($config -eq 'ASan'){'/MD /Od /Zi /fsanitize=address /DNDEBUG'}else{'/MD /O2 /DNDEBUG'}
    $exe=Join-Path $out 'physics-t1-bench.exe'
    $sources=@('Engine/Physics/PhysicsScene.cpp','Engine/SceneRuntime/ScenePhysicsSimulation.cpp','Tools/regression/physics_t1_benchmark.cpp')
    $sources+=@('ProfileMarker','ProfileThreadStream','ProfileCapture','ProfileCaptureFile','ProfileAggregate','ProfileReader','ProfileService') | ForEach-Object {'Engine/EngineDiagnostics/'+$_+'.cpp'}
    if($config -eq 'Shipping'){
        $sources=@('Engine/Physics/PhysicsScene.cpp','Engine/SceneRuntime/ScenePhysicsSimulation.cpp','Tools/regression/physics_t1_benchmark.cpp')
    }
    if($Side -in @('wake','profile')){
        & python "$PSScriptRoot/prepare-physics-t1-$Side-benchmark.py"
        if($LASTEXITCODE){throw 'Wake candidate generation failed'}
        $sources[0]="Build/Obj/Phase19T1Bench/$Side/PhysicsScene.cpp"
        $sources[1]='Build/Obj/Phase19T1Bench/serial/ScenePhysicsSimulation.cpp'
    }else{$sources[1]="Build/Obj/Phase19T1Bench/$Side/ScenePhysicsSimulation.cpp"}
    if($LegacyPublication){
        if($Side -ne 'serial'){throw 'Legacy publication is only a comparison baseline'}
        & python "$PSScriptRoot/prepare-physics-t1-profile-benchmark.py" baseline
        if($LASTEXITCODE){throw 'Publication baseline generation failed'}
        $sources[0]='Build/Obj/Phase19T1Bench/serial/PhysicsScene.cpp'
    }
    $sourceArgs=($sources | ForEach-Object {'"'+(Join-Path $repo $_)+'"'}) -join ' '
    $cmd='call "'+$vcvars+'" >nul && cl /nologo /EHsc /std:c++latest /Zc:__cplusplus /utf-8 /W4 '+$flags+' /DCE_SHIPPING=0 /DCE_PHYSICS_TESTING=1 /I"'+$repo+'/ThirdParty/Mathematics/include" /external:I"'+$deps+'/include" /external:I"'+$deps+'/include/physx" /external:W0 /Fo"'+$out+'/" /Fd"'+$out+'/compiler.pdb" /FS /Fe"'+$exe+'" '+$sourceArgs+' /link /LIBPATH:"'+$lib+'/lib" PhysX_64.lib PhysXCommon_64.lib PhysXFoundation_64.lib PhysXCooking_64.lib PhysXCharacterKinematic_static_64.lib PhysXExtensions_static_64.lib PhysXPvdSDK_static_64.lib'
    if($config -eq 'Shipping'){$cmd=$cmd.Replace('/DCE_SHIPPING=0 /DCE_PHYSICS_TESTING=1','/DCE_SHIPPING=1')}
    & $env:ComSpec /d /s /c $cmd *> "$out/build.log"
    if($LASTEXITCODE){Get-Content "$out/build.log" -Tail 30; throw "$config T1 compile failed"}

    foreach($name in @('PhysX_64.dll','PhysXCommon_64.dll','PhysXCooking_64.dll','PhysXFoundation_64.dll','PhysXGpu_64.dll','PhysXDevice64.dll')){
        Copy-Item (Join-Path $lib "bin/$name") $out -Force
    }
    if($config -eq 'ASan'){
        $toolset=Get-ChildItem 'C:/Program Files/Microsoft Visual Studio/18/Community/VC/Tools/MSVC' -Directory | Sort-Object Name -Descending | Select-Object -First 1
        Copy-Item (Join-Path $toolset.FullName 'bin/Hostx64/x64/clang_rt.asan_dynamic-x86_64.dll') $out -Force
    }
    "T1_BENCH_BUILD_OK $out"
}
