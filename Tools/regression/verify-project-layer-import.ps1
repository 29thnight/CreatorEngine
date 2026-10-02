param([ValidateSet('Debug','Release','Shipping','ASan','All')][string]$Configuration='All')

$ErrorActionPreference='Stop'
$repo=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$vcvars='C:/Program Files/Microsoft Visual Studio/18/Community/VC/Auxiliary/Build/vcvars64.bat'
$configs=if($Configuration -eq 'All'){@('Debug','Release','Shipping','ASan')}else{@($Configuration)}

foreach($config in $configs){
    $out=Join-Path $repo "Build/Obj/Phase19L0Import/$config"
    New-Item -ItemType Directory -Force $out|Out-Null
    $flags=if($config -eq 'Debug'){'/MDd /Od /D_DEBUG'}elseif($config -eq 'ASan'){'/MD /Od /Zi /fsanitize=address /D_DISABLE_VECTOR_ANNOTATION /D_DISABLE_STRING_ANNOTATION /DNDEBUG'}else{'/MD /O2 /DNDEBUG'}
    $shipping=if($config -eq 'Shipping'){1}else{0}
    $exe=Join-Path $out 'layer-import.exe'
    $source=Join-Path $repo 'Tools/regression/project_layer_import_probe.cpp'
    $deps=Join-Path $repo 'vcpkg_installed/x64-windows/x64-windows'
    $lib=if($config -eq 'Debug'){Join-Path $deps 'debug'}else{$deps}
    $sources=@('Engine/Utility_Framework/AuthoringParsedDocument.cpp','Engine/Utility_Framework/AuthoringCookedDocument.cpp','Engine/Utility_Framework/AuthoringRymlErrorPolicy.cpp','Engine/Utility_Framework/AuthoringScalarConvert.cpp')
    $sourceArgs=($sources | ForEach-Object {'"'+(Join-Path $repo $_)+'"'}) -join ' '
    $cmd='call "'+$vcvars+'" >nul && cl /nologo /EHsc /std:c++latest /Zc:__cplusplus /utf-8 /W4 /WX '+$flags+' /DCE_SHIPPING='+$shipping+' /I"'+$repo+'/ThirdParty/Mathematics/include" /external:I"'+$deps+'/include" /external:W0 /Fo"'+$out+'/" /Fd"'+$out+'/compiler.pdb" /FS /Fe"'+$exe+'" "'+$source+'" '+$sourceArgs+' /link /LIBPATH:"'+$lib+'/lib" ryml.lib c4core.lib'
    & $env:ComSpec /d /s /c $cmd *> "$out/build.log"
    if($LASTEXITCODE){Get-Content "$out/build.log" -Tail 30;throw "$config catalog compile failed"}

    if($config -eq 'ASan'){
        $toolset=Get-ChildItem 'C:/Program Files/Microsoft Visual Studio/18/Community/VC/Tools/MSVC' -Directory | Sort-Object Name -Descending | Select-Object -First 1
        Copy-Item (Join-Path $toolset.FullName 'bin/Hostx64/x64/clang_rt.asan_dynamic-x86_64.dll') $out -Force
    }

    $fixture=$out
    foreach($dll in @('ryml.dll','c4core.dll')){if(Test-Path (Join-Path $lib ('bin/'+$dll))){Copy-Item (Join-Path $lib ('bin/'+$dll)) $out -Force}}
    $inputRoot=Join-Path $repo 'Dynamic_CPP/ProjectSetting'
    $process=Start-Process $exe -ArgumentList ('"'+$inputRoot+'" "'+$fixture+'"') -WindowStyle Hidden -PassThru -RedirectStandardOutput "$out/result.jsonl" -RedirectStandardError "$out/stderr.log"
    $handle=$process.Handle
    if(!$process.WaitForExit(60000)){$process.Kill();$process.WaitForExit();throw 'Layer catalog timeout'}
    $process.WaitForExit()
    if($process.ExitCode){throw "Catalog failed: $(Get-Content "$out/stderr.log" -Raw)"}

    $result=Get-Content "$out/result.jsonl" -Raw|ConvertFrom-Json
    if($result.result -ne 'PROJECT_LAYER_IMPORT_OK'){throw 'Invalid catalog result'}
    Write-Output "PROJECT_LAYER_IMPORT_OK $config checks=$($result.checks)"
}
