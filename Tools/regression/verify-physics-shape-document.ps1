param([ValidateSet('Debug','Release','ASan','All')][string]$Configuration='All', [string]$MigrationShapes='', [string]$CapsuleMigrationShapes='', [string]$GeometryMigrationShapes='')
$ErrorActionPreference='Stop'
$repo=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$deps=Join-Path $repo 'vcpkg_installed/x64-windows/x64-windows'
$vcvars='C:/Program Files/Microsoft Visual Studio/18/Community/VC/Auxiliary/Build/vcvars64.bat'
$configs=if($Configuration -eq 'All'){@('Debug','Release','ASan')}else{@($Configuration)}
foreach($config in $configs){
    $out=Join-Path $repo "Build/Obj/Phase19ShapeAuthoring/$config"
    New-Item -ItemType Directory -Force $out|Out-Null
    $lib=if($config -eq 'Debug'){Join-Path $deps 'debug'}else{$deps}
    $flags=if($config -eq 'Debug'){'/MDd /Od /D_DEBUG'}elseif($config -eq 'ASan'){'/MD /Od /Zi /fsanitize=address /D_DISABLE_VECTOR_ANNOTATION /D_DISABLE_STRING_ANNOTATION /DNDEBUG'}else{'/MD /O2 /DNDEBUG'}
    $exe=Join-Path $out 'physics-shape-document.exe'
    $sources=@('Tools/regression/physics_shape_document_probe.cpp','Engine/Utility_Framework/AuthoringParsedDocument.cpp','Engine/Utility_Framework/AuthoringCookedDocument.cpp','Engine/Utility_Framework/AuthoringScalarConvert.cpp','Engine/Utility_Framework/AuthoringRymlErrorPolicy.cpp')
    $sourceArgs=($sources | ForEach-Object {'"'+(Join-Path $repo $_)+'"'}) -join ' '
    $cmd='call "'+$vcvars+'" >nul && cl /nologo /MP4 /EHsc /std:c++latest /utf-8 /DNOMINMAX '+$flags+' /I"'+$repo+'/Engine/Utility_Framework" /I"'+$repo+'/ThirdParty/Mathematics/include" /external:I"'+$deps+'/include" /external:I"'+$deps+'/include/physx" /external:W0 /Fo"'+$out+'/" /Fd"'+$out+'/compiler.pdb" /FS /Fe"'+$exe+'" '+$sourceArgs+' /link /LIBPATH:"'+$lib+'/lib" ryml.lib c4core.lib'
    & $env:ComSpec /d /s /c $cmd *> "$out/build.log"
    if($LASTEXITCODE){Get-Content "$out/build.log" -Tail 20; throw 'Shape document compile failed'}

    if($config -eq 'ASan'){
        $toolset=Get-ChildItem 'C:/Program Files/Microsoft Visual Studio/18/Community/VC/Tools/MSVC' -Directory | Sort-Object Name -Descending | Select-Object -First 1
        Copy-Item (Join-Path $toolset.FullName 'bin/Hostx64/x64/clang_rt.asan_dynamic-x86_64.dll') $out -Force
    }
    if($GeometryMigrationShapes){ if(!$MigrationShapes -or !$CapsuleMigrationShapes){throw 'Primitive and capsule inputs required'}; & $exe $MigrationShapes $CapsuleMigrationShapes $GeometryMigrationShapes *> "$out/result.log" }elseif($CapsuleMigrationShapes){ if(!$MigrationShapes){throw 'Primitive migration input required'}; & $exe $MigrationShapes $CapsuleMigrationShapes *> "$out/result.log" }elseif($MigrationShapes){ & $exe $MigrationShapes *> "$out/result.log" }else{ & $exe *> "$out/result.log" }
    if($LASTEXITCODE){Get-Content "$out/result.log"; throw 'Shape document probe failed'}
    Get-Content "$out/result.log" | ForEach-Object {"$config $_"}
}
