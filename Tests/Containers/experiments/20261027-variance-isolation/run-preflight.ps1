$ErrorActionPreference='Stop'
$root=(Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
$launcher=Join-Path $PSScriptRoot 'build/Release/controlled_process.exe'
$binary=Join-Path $root 'build-modules/modules/Release/module_container_tests.exe'
$env:CE_BENCH_CONTROLLED='1';$env:CE_BENCH_GROUPED='1';$env:CE_BENCH_RESET_HEAP='1'
$env:CE_BENCH_INDEPENDENT='1';$env:CE_BENCH_BATCH_NS='100000000'
$preflightRows=[Collections.Generic.List[object]]::new()
& "$PSScriptRoot/with-guard.ps1" -Tag '20261027-variance-preflight-guard1' -High -Work {
    foreach($repeat in 1..3){
        if($repeat -eq 2){$env:CE_BENCH_REVERSE='1';$order=@('string_find','map_miss','map_hit')}
        else{Remove-Item Env:CE_BENCH_REVERSE -ErrorAction SilentlyContinue;$order=@('map_hit','map_miss','string_find')}
        foreach($name in $order){
            $env:CE_BENCH_ONLY_CASE="$name,65536"
            $file="$root/results/20261027-variance-preflight-$repeat-$name.csv"
            if(Test-Path $file){throw 'Use fresh preflight files'}
            $data=@(& $launcher high $binary)
            if($LASTEXITCODE){throw 'Preflight execution failed'}
            $data|Set-Content $file -Encoding utf8
            $raw=@($data|Where-Object {$_ -like 'raw,*'}|ForEach-Object {,$_.Split(',')})
            if($raw.Count -ne 15){throw 'Expected 15 raw samples'}
            $a=@($raw|ForEach-Object {[double]$_[4]}|Sort-Object)
            $b=@($raw|ForEach-Object {[double]$_[5]}|Sort-Object)
            $preflightRows.Add([pscustomobject]@{repeat=$repeat;case=$name;ratio=$a[7]/$b[7];customSpread=$a[13]/$a[1];stlSpread=$b[13]/$b[1];pass=($a[7]/$b[7] -le 1.10 -and $a[13]/$a[1] -le 1.30 -and $b[13]/$b[1] -le 1.30)})
            "PREFLIGHT $repeat $name $($preflightRows[$preflightRows.Count-1].pass)"
        }
    }
}
$preflightRows|ConvertTo-Json|Set-Content "$root/results/20261027-variance-preflight-summary.json" -Encoding utf8
Remove-Item Env:CE_BENCH_CONTROLLED,Env:CE_BENCH_GROUPED,Env:CE_BENCH_RESET_HEAP,Env:CE_BENCH_INDEPENDENT,Env:CE_BENCH_BATCH_NS,Env:CE_BENCH_ONLY_CASE,Env:CE_BENCH_REVERSE -ErrorAction SilentlyContinue
if($preflightRows.Count -ne 9 -or @($preflightRows|Where-Object {!$_.pass}).Count){throw 'Preflight fails original criteria'}
