$ErrorActionPreference='Stop'
$diagRepoRoot=(Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
$diagOut=Join-Path $diagRepoRoot 'results'
if(Test-Path "$diagOut/20261026-small-final-context-0.csv"){throw 'Diagnostic series already exists'}
$env:CE_BENCH_CONTROLLED='1';$env:CE_BENCH_GROUPED='1';$env:CE_BENCH_RESET_HEAP='1';$env:CE_BENCH_INDEPENDENT='1';$env:CE_BENCH_BATCH_NS='100000000';$env:CE_BENCH_ONLY_CASE='array_growth,64'
Remove-Item Env:CE_BENCH_REVERSE -ErrorAction SilentlyContinue
foreach($mode in 0,1,2){
 $env:CE_ARRAY_BURN="$mode"
 & "$PSScriptRoot/build/Release/array_context.exe"|Set-Content "$diagOut/20261026-small-final-context-$mode.csv" -Encoding utf8
 if($LASTEXITCODE -ne 0){throw "Context probe failed: $mode"}
}
Remove-Item Env:CE_ARRAY_BURN -ErrorAction SilentlyContinue
& "$PSScriptRoot/build/Release/array_phase.exe"|Set-Content "$diagOut/20261026-small-final-phase-normal.csv" -Encoding utf8
if($LASTEXITCODE -ne 0){throw 'Phase probe failed'}
& "$PSScriptRoot/with-guard.ps1" -Tag 20261026-small-final-diagnostic -TtlMilliseconds 180000 -Work {
 $env:CE_ARRAY_BURN='1'
 & "$PSScriptRoot/build/Release/array_context.exe"|Set-Content "$diagOut/20261026-small-final-context-guard-heavy.csv" -Encoding utf8
 if($LASTEXITCODE -ne 0){throw 'Guard context probe failed'}
 Remove-Item Env:CE_ARRAY_BURN -ErrorAction SilentlyContinue
 & "$PSScriptRoot/build/Release/array_phase.exe"|Set-Content "$diagOut/20261026-small-final-phase-guard.csv" -Encoding utf8
 if($LASTEXITCODE -ne 0){throw 'Guard phase probe failed'}
}
Get-ChildItem $PSScriptRoot -File|Where-Object {$_.Extension -in @('.cpp','.h','.ps1','.txt')}|Get-FileHash|Select-Object Path,Hash|ConvertTo-Json|Set-Content "$diagOut/20261026-small-diagnostic-sources.json" -Encoding utf8
Remove-Item Env:CE_BENCH_CONTROLLED,Env:CE_BENCH_GROUPED,Env:CE_BENCH_RESET_HEAP,Env:CE_BENCH_INDEPENDENT,Env:CE_BENCH_BATCH_NS,Env:CE_BENCH_ONLY_CASE -ErrorAction SilentlyContinue
