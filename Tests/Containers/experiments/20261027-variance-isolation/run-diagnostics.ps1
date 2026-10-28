$ErrorActionPreference='Stop'
$root=(Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
$launcher=Join-Path $PSScriptRoot 'build/Release/controlled_process.exe'
$binary=Join-Path $PSScriptRoot 'build/Release/variance_context.exe'
$env:CE_BENCH_CONTROLLED='1';$env:CE_BENCH_GROUPED='1';$env:CE_BENCH_RESET_HEAP='1'
$env:CE_BENCH_INDEPENDENT='1';$env:CE_BENCH_BATCH_NS='100000000'
Remove-Item Env:CE_ARRAY_BURN,Env:CE_BENCH_REVERSE -ErrorAction SilentlyContinue
$conditions=@(
    @{name='baseline';mode='';cpu=0},
    @{name='preemption';mode='integer';cpu=4},
    @{name='smt';mode='integer';cpu=5},
    @{name='other-integer';mode='integer';cpu=0},
    @{name='other-memory';mode='memory';cpu=0}
)
foreach($point in @('map_hit','map_miss','string_find')){
    $env:CE_BENCH_ONLY_CASE="$point,65536"
    foreach($condition in $conditions){
        foreach($priority in @('normal','high')){
            $tag="20261027-variance3-$point-$($condition.name)-$priority"
            $diagnosticBody={
                & $launcher $priority $binary | Set-Content "$root/results/$tag.csv" -Encoding utf8
                if($LASTEXITCODE){throw "Diagnostic failed: $tag"}
            }
            $diagnosticBody=$diagnosticBody.GetNewClosure()
            $guardBody={ & "$PSScriptRoot/with-guard.ps1" -Tag "$tag-guard" -High:($priority -eq 'high') -TtlMilliseconds 300000 -Work $diagnosticBody }.GetNewClosure()
            # Ready the lower-priority load before starting a High CPU5 guard;
            # otherwise the intentionally starved load may not signal readiness.
            if($condition.mode){ & "$PSScriptRoot/with-load.ps1" -Tag $tag -Mode $condition.mode -Cpu $condition.cpu -Work $guardBody }
            else{ & $guardBody }
            "DIAGNOSTIC $tag collected"
        }
    }
}
Get-ChildItem $PSScriptRoot -File | Get-FileHash | ConvertTo-Json | Set-Content "$root/results/20261027-variance3-diagnostic-inputs.json" -Encoding utf8
Remove-Item Env:CE_BENCH_CONTROLLED,Env:CE_BENCH_GROUPED,Env:CE_BENCH_RESET_HEAP,Env:CE_BENCH_INDEPENDENT,Env:CE_BENCH_BATCH_NS,Env:CE_BENCH_ONLY_CASE -ErrorAction SilentlyContinue
