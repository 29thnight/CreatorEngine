param([Parameter(Mandatory)][string]$Tag)
$ErrorActionPreference='Stop';$root=(Resolve-Path (Join-Path $PSScriptRoot '../..')).Path;$out=Join-Path $root 'results'
if(Get-ChildItem $out -Filter "$Tag-primary-*" -ErrorAction SilentlyContinue){throw 'Use a fresh primary tag'}
$exe=Join-Path $root 'build-modules/modules/Release/module_mpsc_benchmark.exe'
$hash=Get-FileHash $exe|Select-Object Path,Hash;$started=Get-Date -Format o
$env:CE_BENCH_CONTROLLED='1';$env:CE_MPSC_BATCH_REPEATS='8'
Remove-Item Env:CE_BENCH_ONLY_CASE,Env:CE_BENCH_BATCH_NS,Env:CE_MPSC_PAUSE,Env:CE_MPSC_START_PAUSE -ErrorAction SilentlyContinue
try{
 foreach($run in 1..3){
  Invoke-MeasurementCheckpoint -Stage "primary-start/$run" -Force
  & $exe | Set-Content "$out/$Tag-primary-$run.csv" -Encoding utf8
  if($LASTEXITCODE -ne 0){throw 'Primary MPSC measurement failed'}
  Get-Content "$out/$Tag-primary-$run.csv"|Where-Object {$_ -like 'mpsc_summary,*'}
  Invoke-MeasurementCheckpoint -Stage "primary-end/$run"
 }
 if((Get-FileHash $exe).Hash -ne $hash.Hash){throw 'Binary changed during primary collection'}
 [ordered]@{started=$started;ended=(Get-Date -Format o);protocol='Original yield-batch8; CPU4 measuring thread, other physical worker cores, no SMT helper; 3 processes, all four cases, predefined warmup3 retained15 batches8, all subtransfers retained';binary=$hash}|ConvertTo-Json -Depth 4|Set-Content "$out/$Tag-primary-environment.json" -Encoding utf8
}finally{
 Remove-Item Env:CE_BENCH_CONTROLLED,Env:CE_MPSC_BATCH_REPEATS -ErrorAction SilentlyContinue
}
