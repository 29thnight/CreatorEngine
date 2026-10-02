param([ValidateSet('Debug','Release','Shipping','ASan','All')][string]$Configuration='All', [switch]$RequireGpu)

$ErrorActionPreference='Stop'
$repo=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$deps=Join-Path $repo 'vcpkg_installed/x64-windows/x64-windows'
$vcvars='C:/Program Files/Microsoft Visual Studio/18/Community/VC/Auxiliary/Build/vcvars64.bat'
$configs=if($Configuration -eq 'All'){@('Debug','Release','Shipping','ASan')}else{@($Configuration)}

foreach($config in $configs){
    $out=Join-Path $repo "Build/Obj/Phase19CookedGeometry/$config"
    New-Item -ItemType Directory -Force $out|Out-Null
    $lib=if($config -eq 'Debug'){Join-Path $deps 'debug'}else{$deps}
    $flags=if($config -eq 'Debug'){'/MDd /Od /D_DEBUG'}elseif($config -eq 'ASan'){'/MD /Od /Zi /fsanitize=address /D_DISABLE_VECTOR_ANNOTATION /D_DISABLE_STRING_ANNOTATION /DNDEBUG'}else{'/MD /O2 /DNDEBUG'}
    $exe=Join-Path $out 'physics-cooked-geometry.exe'
    $sources=@('Engine/Physics/PhysicsScene.cpp','Engine/SceneRuntime/ScenePhysicsSimulation.cpp','Tools/regression/physics_cooked_geometry_probe.cpp')
    $sources+=@('ProfileMarker','ProfileThreadStream','ProfileCapture','ProfileCaptureFile','ProfileAggregate','ProfileReader','ProfileService') | ForEach-Object {'Engine/EngineDiagnostics/'+$_+'.cpp'}
    if($config -eq 'Shipping'){
        $sources=@('Engine/Physics/PhysicsScene.cpp','Engine/SceneRuntime/ScenePhysicsSimulation.cpp','Tools/regression/physics_cooked_geometry_probe.cpp')
    }
    $sources+=@('Engine/RenderEngine/MaterialGraphRuntime.cpp','Engine/Utility_Framework/AuthoringScalarConvert.cpp','Engine/Utility_Framework/AuthoringRymlErrorPolicy.cpp','Engine/RenderEngine/Assets/AssetIdentityProfile.cpp','Engine/RenderEngine/Experiment/Cooked/CookedMaterialProgram.cpp','Engine/RenderEngine/Experiment/Cooked/CookedAssetManifest.cpp','Engine/RenderEngine/Experiment/Cooked/CookedAssetCatalog.cpp')
    $sources+=@('Lattice/Core/LXGraph.cpp','Lattice/Core/LXNodeDefinition.cpp','Lattice/Material/LXMaterialGraph.cpp','Lattice/Material/LXMaterialIR.cpp','Lattice/Material/LXMaterialNodes.cpp','Lattice/Material/LXMaterialOperators.cpp','Lattice/Material/LXMaterialCompiler.cpp')
    $sources+=@('Engine/RenderEngine/Experiment/Cooked/CookSupport.cpp','Engine/RenderEngine/Experiment/Cooked/ModelCookIdentity.cpp','Engine/Utility_Framework/AuthoringParsedDocument.cpp','Engine/Utility_Framework/AuthoringCookedDocument.cpp')
    $sources+=@('Engine/RenderEngine/MaterialGraphProduct.cpp','Engine/RenderEngine/RHI/RHIShaderCompiler.cpp','Engine/RenderEngine/RHI/RHIShaderSource.cpp','Engine/RenderEngine/RHI/RHIShaderReflection.cpp','Engine/RenderEngine/RHI/RHIShaderPermutation.cpp')
    $sourceArgs=($sources | ForEach-Object {'"'+(Join-Path $repo $_)+'"'}) -join ' '
    $cmd='call "'+$vcvars+'" >nul && cl /nologo /MP4 /EHsc /Gy /DNOMINMAX /std:c++latest /Zc:__cplusplus /utf-8 /W4 '+$flags+' /DCE_SHIPPING=0 /DCE_PHYSICS_TESTING=1 /I"'+$repo+'/ThirdParty/Slang/include" /I"'+$repo+'/ThirdParty/Mathematics/include" /external:I"'+$deps+'/include" /external:I"'+$deps+'/include/physx" /I"C:/Users/idene/source/repos/CreatorEngine/Engine/Utility_Framework" /external:W0 /Fo"'+$out+'/" /Fd"'+$out+'/compiler.pdb" /FS /Fe"'+$exe+'" '+$sourceArgs+' /link /OPT:REF /LIBPATH:"'+$lib+'/lib" d3d12.lib dxgi.lib ole32.lib ryml.lib c4core.lib PhysX_64.lib PhysXCommon_64.lib PhysXFoundation_64.lib PhysXCooking_64.lib PhysXCharacterKinematic_static_64.lib PhysXExtensions_static_64.lib PhysXPvdSDK_static_64.lib'
    if($config -eq 'Shipping'){$cmd=$cmd.Replace('/DCE_SHIPPING=0 /DCE_PHYSICS_TESTING=1','/DCE_SHIPPING=1')}
    & $env:ComSpec /d /s /c $cmd *> "$out/build.log"
    if($LASTEXITCODE){Get-Content "$out/build.log" -Tail 30; throw "$config cooked geometry compile failed"}

    foreach($name in @('PhysX_64.dll','PhysXCommon_64.dll','PhysXCooking_64.dll','PhysXFoundation_64.dll','PhysXGpu_64.dll','PhysXDevice64.dll')){
        Copy-Item (Join-Path $lib "bin/$name") $out -Force
    }
    if($config -eq 'ASan'){
        $toolset=Get-ChildItem 'C:/Program Files/Microsoft Visual Studio/18/Community/VC/Tools/MSVC' -Directory | Sort-Object Name -Descending | Select-Object -First 1
        Copy-Item (Join-Path $toolset.FullName 'bin/Hostx64/x64/clang_rt.asan_dynamic-x86_64.dll') $out -Force
    }
    $fixture=Join-Path $out ('fixture-'+[guid]::NewGuid().ToString('N'))
    $process=Start-Process $exe -ArgumentList ('"'+$fixture+'"') -WindowStyle Hidden -PassThru -RedirectStandardOutput "$out/result.jsonl" -RedirectStandardError "$out/stderr.log"
    $handle=$process.Handle
    if(!$process.WaitForExit(60000)){$process.Kill();$process.WaitForExit();throw 'Cooked geometry timeout'}
    $process.WaitForExit()
    if($process.ExitCode){throw "Cooked geometry failed: $(Get-Content "$out/stderr.log" -Raw)"}
    $result=Get-Content "$out/result.jsonl" -Raw|ConvertFrom-Json
    if($result.result -ne 'PHYSICS_COOKED_GEOMETRY_OK'){throw 'Invalid cooked geometry result'}
    if($RequireGpu -and !$result.gpu_verified){throw "Actual GPU import gate required"}
    Write-Output "PHYSICS_COOKED_GEOMETRY_OK $config checks=$($result.checks)"
}
