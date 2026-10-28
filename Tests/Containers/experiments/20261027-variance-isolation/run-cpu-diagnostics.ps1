$ErrorActionPreference='Stop'
$root=(Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
$launcher=Join-Path $PSScriptRoot 'build/Release/controlled_cpu.exe'
$binary=Join-Path $PSScriptRoot 'build/Release/variance_cpu_context.exe'
$env:CE_BENCH_CONTROLLED='1';$env:CE_BENCH_GROUPED='1';$env:CE_BENCH_RESET_HEAP='1'
$env:CE_BENCH_INDEPENDENT='1';$env:CE_BENCH_BATCH_NS='100000000'
Remove-Item Env:CE_BENCH_REVERSE,Env:CE_ARRAY_BURN -ErrorAction SilentlyContinue
foreach($measuringCpu in 4,8){
    foreach($loadMode in 'baseline','stream'){
        foreach($caseName in 'map_hit','map_miss','string_find'){
            $env:CE_BENCH_ONLY_CASE="$caseName,65536"
            $diagnosticTag="20261027-execution-$measuringCpu-$loadMode-$caseName"
            $body={
                $outputFile="$root/results/$diagnosticTag.csv"
                if(Test-Path $outputFile){throw 'Use fresh CPU diagnostics'}
                & $launcher high $measuringCpu $binary | Set-Content $outputFile -Encoding utf8
                if($LASTEXITCODE){throw 'CPU diagnostics failed'}
            }.GetNewClosure()
            $guardBody={ & "$PSScriptRoot/with-cpu-guard.ps1" -Tag "$diagnosticTag-guard" -High -Cpu ($measuringCpu+1) -TtlMilliseconds 300000 -Work $body }.GetNewClosure()
            if($loadMode -eq 'stream'){
                & "$PSScriptRoot/with-stream-load.ps1" -Tag $diagnosticTag -Mode stream -Cpu 0 -Work $guardBody
            }else{ & $guardBody }
            "CPU_DIAGNOSTIC $diagnosticTag collected"
        }
    }
}
Remove-Item Env:CE_BENCH_CONTROLLED,Env:CE_BENCH_GROUPED,Env:CE_BENCH_RESET_HEAP,Env:CE_BENCH_INDEPENDENT,Env:CE_BENCH_BATCH_NS,Env:CE_BENCH_ONLY_CASE -ErrorAction SilentlyContinue
