param([ValidateSet('Debug','Release','Shipping','ASan','All')][string]$Configuration='All')

$ErrorActionPreference='Stop'
$repo=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$vcvars='C:/Program Files/Microsoft Visual Studio/18/Community/VC/Auxiliary/Build/vcvars64.bat'
$configs=if($Configuration -eq 'All'){@('Debug','Release','Shipping','ASan')}else{@($Configuration)}

foreach($config in $configs){
    $out=Join-Path $repo "Build/Obj/Phase19L0/$config"
    New-Item -ItemType Directory -Force $out|Out-Null
    $flags=if($config -eq 'Debug'){'/MDd /Od /D_DEBUG'}elseif($config -eq 'ASan'){'/MD /Od /Zi /fsanitize=address /DNDEBUG'}else{'/MD /O2 /DNDEBUG'}
    $shipping=if($config -eq 'Shipping'){1}else{0}
    $exe=Join-Path $out 'layer-catalog.exe'
    $source=Join-Path $repo 'Tools/regression/layer_catalog_probe.cpp'
    $cmd='call "'+$vcvars+'" >nul && cl /nologo /EHsc /std:c++latest /Zc:__cplusplus /utf-8 /W4 /WX '+$flags+' /DCE_SHIPPING='+$shipping+' /I"'+$repo+'/ThirdParty/Mathematics/include" /Fo"'+$out+'/" /Fd"'+$out+'/compiler.pdb" /FS /Fe"'+$exe+'" "'+$source+'"'
    & $env:ComSpec /d /s /c $cmd *> "$out/build.log"
    if($LASTEXITCODE){Get-Content "$out/build.log" -Tail 30;throw "$config catalog compile failed"}

    if($config -eq 'ASan'){
        $toolset=Get-ChildItem 'C:/Program Files/Microsoft Visual Studio/18/Community/VC/Tools/MSVC' -Directory | Sort-Object Name -Descending | Select-Object -First 1
        Copy-Item (Join-Path $toolset.FullName 'bin/Hostx64/x64/clang_rt.asan_dynamic-x86_64.dll') $out -Force
    }

    $fixture=Join-Path $out 'Layers.celayers'
    $process=Start-Process $exe -ArgumentList ('"'+$fixture+'"') -WindowStyle Hidden -PassThru -RedirectStandardOutput "$out/result.jsonl" -RedirectStandardError "$out/stderr.log"
    $handle=$process.Handle
    if(!$process.WaitForExit(60000)){$process.Kill();$process.WaitForExit();throw 'Layer catalog timeout'}
    $process.WaitForExit()
    if($process.ExitCode){throw "Catalog failed: $(Get-Content "$out/stderr.log" -Raw)"}

    $result=Get-Content "$out/result.jsonl" -Raw|ConvertFrom-Json
    if($result.result -ne 'LAYER_CATALOG_OK'){throw 'Invalid catalog result'}
    Write-Output "LAYER_CATALOG_OK $config checks=$($result.checks)"
}
