param([string]$Tag='20261026-modopt-paired')
$ErrorActionPreference='Stop';$root=$PSScriptRoot;$out=Join-Path $root 'results'
$controlsTag=$Tag
if(Get-ChildItem $out -Filter "$Tag-*" -ErrorAction SilentlyContinue){throw 'Use a fresh tag'}
$paths=@{before="$root/experiments/20261026-module-opt/before";inline="$root/experiments/20261026-module-opt/inline";final="$root/build-modules/modules/Release"}
$frozen=@(foreach($v in $paths.Keys){foreach($name in 'module_container_tests','module_queue_acceptance','module_mpsc_benchmark'){Get-FileHash "$($paths[$v])/$name.exe"|Select-Object Path,Hash}})
$started=Get-Date -Format o
& "$root/experiments/20261026-small-array/with-guard.ps1" -Tag "$Tag`_guard" -Work {
 $env:CE_BENCH_CONTROLLED='1';$env:CE_BENCH_GROUPED='1';$env:CE_BENCH_RESET_HEAP='1';$env:CE_BENCH_INDEPENDENT='1'
 $env:CE_BENCH_BATCH_NS='100000000'
 Remove-Item Env:CE_MPSC_BATCH_REPEATS,Env:CE_MPSC_PAUSE,Env:CE_MPSC_START_PAUSE -ErrorAction SilentlyContinue
 foreach($run in 1..3){
  $order=if($run -eq 2){@('final','inline','before')}else{@('before','inline','final')}
  foreach($v in $order){
   if($v -ne 'inline'){
    foreach($n in 8,23,24){
     $env:CE_BENCH_ONLY_CASE="string_find,$n"
     & "$($paths[$v])/module_container_tests.exe"|Set-Content "$out/$controlsTag-$v-string-$n-$run.csv" -Encoding utf8
     if($LASTEXITCODE -ne 0){throw 'String control failed'}
    }
   }
   Remove-Item Env:CE_BENCH_ONLY_CASE -ErrorAction SilentlyContinue
   & "$($paths[$v])/module_mpsc_benchmark.exe"|Set-Content "$out/$controlsTag-$v-mpsc-$run.csv" -Encoding utf8
   if($LASTEXITCODE -ne 0){throw 'MPSC control failed'}
   Write-Output "PAIRED $run $v completed"
  }
 }
}
Remove-Item Env:CE_BENCH_CONTROLLED,Env:CE_BENCH_GROUPED,Env:CE_BENCH_RESET_HEAP,Env:CE_BENCH_INDEPENDENT,Env:CE_BENCH_BATCH_NS,Env:CE_BENCH_ONLY_CASE -ErrorAction SilentlyContinue
foreach($run in 1..5){
 $order=if($run%2){@('before','inline','final')}else{@('final','inline','before')}
 foreach($v in $order){
  & "$($paths[$v])/module_queue_acceptance.exe"|Set-Content "$out/$Tag-$v-ppl-$run.csv" -Encoding utf8
  if($LASTEXITCODE -ne 0){throw 'FAA control failed'}
 }
}
foreach($file in $frozen){if((Get-FileHash $file.Path).Hash -ne $file.Hash){throw 'Control binary changed'}}
[ordered]@{started=$started;ended=(Get-Date -Format o);binaries=$frozen;policy='All variants interleaved, repeat2 reverses variant order for string/MPSC; FAA alternates variant order each process group. Same executables throughout, no build during collection. Controls are diagnostics, not substitutions for failed acceptance samples.'}|ConvertTo-Json -Depth 5|Set-Content "$out/$Tag-environment.json" -Encoding utf8
