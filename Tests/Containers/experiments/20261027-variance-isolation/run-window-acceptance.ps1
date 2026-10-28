param([string]$Tag='20261026-module-full',[string]$ManifestTag='20261026-final4')
$ErrorActionPreference='Stop'
$root=(Resolve-Path (Join-Path $PSScriptRoot '../..')).Path; . "$root/implementation-root.ps1"; $implementation=Get-ContainerImplementationRoot;$out=Join-Path $root 'results'
if(Get-ChildItem $out -Filter "$Tag-*" -ErrorAction SilentlyContinue){throw 'Use a fresh tag; never overwrite an earlier run'}
# Reuse the exact failing campaign binaries. Only process scheduling control changes.
$baseline=Get-Content "$out/20261027-values-format-full1-before.json" -Raw|ConvertFrom-Json
foreach($file in $baseline){if((Get-FileHash $file.Path).Hash -ne $file.Hash){throw "Baseline input changed: $($file.Path)"}}
$referenceBaseline=Get-Content "$out/20261027-values-format-full1-reference-files.json" -Raw|ConvertFrom-Json
foreach($file in $referenceBaseline){if((Get-FileHash $file.Path).Hash -ne $file.Hash){throw "Compiler/STL/PPL reference changed: $($file.Path)"}}
$launcher=Join-Path $PSScriptRoot 'build/Release/controlled_process.exe'
$frozen=@(Get-ChildItem "$implementation/modules","$implementation/include","$root/tests" -Recurse -File|Get-FileHash -Algorithm SHA256|Select-Object Path,Hash)
$frozen+=@(Get-FileHash "$root/CMakeLists.txt",$PSCommandPath -Algorithm SHA256|Select-Object Path,Hash)
$frozen+=@('module_container_tests','module_library_benchmark','module_mpsc_benchmark','module_queue_acceptance','module_concurrent_tests'|ForEach-Object {Get-FileHash "$root/build-modules/modules/Release/$_.exe"|Select-Object Path,Hash})
$frozen+=@(Get-FileHash $launcher,"$PSScriptRoot/controlled_process.cpp","$PSScriptRoot/with-guard.ps1"|Select-Object Path,Hash)
$frozen+=@(Get-FileHash "$PSScriptRoot/run-window-full-suite.ps1","$PSScriptRoot/run-window-primary.ps1","$PSScriptRoot/run-window-values.ps1","$PSScriptRoot/verify-preserved-state.ps1"|Select-Object Path,Hash)
$frozen|ConvertTo-Json -Depth 4|Set-Content "$out/$Tag-before.json" -Encoding utf8
$cases=@()
foreach($kind in @('core','expanded')){
 $target=if($kind -eq 'core'){'module_container_tests'}else{'module_library_benchmark'}
 $points=@(Get-Content "$out/$ManifestTag-$kind-1.csv"|Where-Object {$_ -like 'summary,*'})
 $expected=if($kind -eq 'core'){60}else{22}
 if($points.Count -ne $expected){throw 'Incomplete case manifest'}
 foreach($row in $points){$p=$row.Split(',');$cases+=[pscustomobject]@{kind=$kind;target=$target;name=$p[1];n=[int]$p[2]}}
}
if(@($cases|Group-Object kind,name,n).Count -ne 82){throw 'Duplicate case manifest'}
$caseDir=Join-Path $out "$Tag-cases"
New-Item -ItemType Directory -Path $caseDir | Out-Null
$cases|ConvertTo-Json|Set-Content "$out/$Tag-manifest.json" -Encoding utf8
$env:CE_BENCH_CONTROLLED='1';$env:CE_BENCH_GROUPED='1';$env:CE_BENCH_RESET_HEAP='1'
$env:CE_BENCH_INDEPENDENT='1';$env:CE_BENCH_BATCH_NS='100000000'
Remove-Item Env:CE_MPSC_BATCH_REPEATS,Env:CE_MPSC_PAUSE,Env:CE_MPSC_START_PAUSE,Env:CE_BENCH_LEGACY,Env:CE_BENCH_LFH,Env:CE_BENCH_UNRESERVED_STATS,Env:CE_BENCH_AFFINITY,Env:CE_BENCH_PRIORITY -ErrorAction SilentlyContinue
foreach($run in 1..3){
 if($run -eq 2){$env:CE_BENCH_REVERSE='1';$order=@($cases);[array]::Reverse($order)}else{Remove-Item Env:CE_BENCH_REVERSE -ErrorAction SilentlyContinue;$order=$cases}
 foreach($kind in @('core','expanded')){Set-Content "$out/$Tag-$kind-$run.csv" "case_process_isolation,$run" -Encoding utf8}
 $index=0
 foreach($point in $order){
  $id="$($point.name),$($point.n)";$env:CE_BENCH_ONLY_CASE=$id
  $intervalStart=Get-Date -Format o
  $data=@(& $launcher high "$root/build-modules/modules/Release/$($point.target).exe")
  $measurementCode=$LASTEXITCODE
  $intervalEnd=Get-Date -Format o
  [ordered]@{run=$run;case=$id;started=$intervalStart;ended=$intervalEnd;exitCode=$measurementCode}|ConvertTo-Json -Compress|Add-Content "$out/$Tag-process-intervals.jsonl"
  $data|Set-Content "$caseDir/$run-$($point.kind)-$($point.name)-$($point.n).csv" -Encoding utf8
  if($measurementCode -ne 0){throw "Measurement failed: $id"}
  $summaries=@($data|Where-Object {$_ -like 'summary,*'})
  if($summaries.Count -ne 1 -or $summaries[0] -notlike "summary,$id,*"){throw "Wrong case: $id"}
  $data|Add-Content "$out/$Tag-$($point.kind)-$run.csv" -Encoding utf8
  $raw=@($data|Where-Object {$_ -like 'raw,*'}|ForEach-Object {,$_.Split(',')})
  if($raw.Count -ne 15){throw 'Expected all 15 raw samples'}
  $a=@($raw|ForEach-Object {[double]$_[4]}|Sort-Object)
  $b=@($raw|ForEach-Object {[double]$_[5]}|Sort-Object)
  if($a[7]/$b[7] -gt 1.10 -or $a[13]/$a[1] -gt 1.30 -or $b[13]/$b[1] -gt 1.30){throw "Original criteria fail: $id; completed raw samples retained"}
  ++$index
  Write-Output "Isolated repeat $run [$index/82] $($summaries[0])"
  Invoke-MeasurementCheckpoint -Stage "general/$run/$index"
 }
 Remove-Item Env:CE_BENCH_ONLY_CASE -ErrorAction SilentlyContinue
 & "$root/build-modules/modules/Release/module_mpsc_benchmark.exe"|Set-Content "$out/$Tag-mpsc-$run.csv" -Encoding utf8
 if($LASTEXITCODE -ne 0){throw 'MPSC measurement failed'}
 Invoke-MeasurementCheckpoint -Stage "mpsc-single/$run"
}
$receipt=[ordered]@{
 implementationRoot=$implementation;timestamp=(Get-Date -Format o);targetBatchNanoseconds=100000000;controlsTag=$ManifestTag
 cpu=(Get-CimInstance Win32_Processor|Select-Object Name,NumberOfCores,NumberOfLogicalProcessors)
 os=(Get-CimInstance Win32_OperatingSystem|Select-Object Caption,Version)
 sources=@(Get-ChildItem "$implementation/modules","$implementation/include","$root/tests" -Recurse -File|Get-FileHash -Algorithm SHA256|Select-Object Path,Hash)
 scripts=@(Get-FileHash "$root/CMakeLists.txt",$PSCommandPath,$launcher,"$PSScriptRoot/with-guard.ps1" -Algorithm SHA256|Select-Object Path,Hash)
 binaries=@('module_container_tests','module_library_benchmark','module_mpsc_benchmark','module_queue_acceptance','module_concurrent_tests'|ForEach-Object {Get-FileHash "$root/build-modules/modules/Release/$_.exe" -Algorithm SHA256|Select-Object Path,Hash})
 generalProcessPriorityClass=128;guardProcessPriorityClass=128;unchangedBinaryBaseline='20261027-values-format-full1'
 policy='Own general benchmark and pause guard processes run High; measuring threads remain AboveNormal. Same binaries as prior failure, no synthetic diagnostic load. All 82 cases, no selection by result, each in a new process, three repeats per case (246 general processes). Named-module consumers, no implementation headers. Fixed agent activity windows between completed processes; no UI/log analysis during timed processes. No rebuild during collection; each selected executable is reused unchanged. Inputs generated outside benchmark calls; selection skips measurements, not fixture generation. Repeat 2 reverses both case order and A/B side order. Independent 100ms target per side, pre-reserved statistics, immediate fixed 1-second warmup per side, own-process heap optimization, logical CPU4 and above-normal measuring thread. All 15 samples retained. Every case/repeat ratio <=1.10 and both-side x[13]/x[1] <=1.30. MPSC unchanged 4 conditions per process, 3 processes, separate physical producer cores. Controls/profiles remain under controlsTag, not relabeled.'
}
$receipt|ConvertTo-Json -Depth 8|Set-Content "$out/$Tag-environment.json" -Encoding utf8
foreach($file in $frozen){if((Get-FileHash $file.Path).Hash -ne $file.Hash){throw "Input changed during measurement: $($file.Path)"}}
Remove-Item Env:CE_BENCH_CONTROLLED,Env:CE_BENCH_GROUPED,Env:CE_BENCH_RESET_HEAP,Env:CE_BENCH_INDEPENDENT,Env:CE_BENCH_BATCH_NS,Env:CE_BENCH_ONLY_CASE,Env:CE_BENCH_REVERSE -ErrorAction SilentlyContinue
