param([string]$CMake='C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe',[switch]$SkipValidation,[switch]$SkipBenchmarks,[string]$Tag='20261026-final4',[ValidateRange(20000000,1000000000)][long]$BatchNanoseconds=100000000)
$ErrorActionPreference='Stop';$root=$PSScriptRoot; . "$PSScriptRoot/implementation-root.ps1"; $implementation=Get-ContainerImplementationRoot;$out=Join-Path $root 'results';$tag=$Tag
function Run([string]$Log,[string]$Exe,[string[]]$Arguments){& $Exe @Arguments 2>&1 | Tee-Object -FilePath "$out\$tag-$Log.txt" -Append;if($LASTEXITCODE -ne 0){throw "Failed: $Exe $Arguments"}}
if((Get-ChildItem $out -Filter "$tag-*.csv" -ErrorAction SilentlyContinue) -or (Test-Path "$out/$tag-environment.json")){throw 'Use a fresh tag; preserve earlier measurements'}
$ctest=Join-Path (Split-Path $CMake) 'ctest.exe'
Run 'canonical-configure' $CMake @('-S',$root,'-B',"$root/build","-DCE_IMPLEMENTATION_ROOT=$implementation")
Run 'canonical-benchmarks' $CMake @('--build',"$root/build",'--config','Release','--target','container_tests','library_benchmark','mpsc_benchmark')
if(!$SkipValidation){
 Run 'release' $CMake @('--build',"$root\build",'--config','Release')
 Run 'release' $ctest @('--test-dir',"$root\build",'-C','Release','--output-on-failure')
 Run 'mpsc-repeat' $ctest @('--test-dir',"$root\build",'-C','Release','-R','bounded_mpsc','--repeat','until-fail:20','--output-on-failure')
 foreach($cfg in @('build-asan','build-clang')){
  Run $cfg $CMake @('--build',"$root\$cfg",'--config','Release','--target','library_tests','container_tests','bounded_mpsc_tests','windows_header_probe','library_header_probes','allocation_alignment')
  foreach($name in @('^correctness$','^library_correctness$','bounded_mpsc','windows_header_composition','allocation_alignment')){Run $cfg $ctest @('--test-dir',"$root\$cfg",'-C','Release','-R',$name,'--output-on-failure')}
 }
 $unix=(& wsl.exe -d Ubuntu -- wslpath -a ($root.Replace('\','/')) | Out-String).Trim()
 if($LASTEXITCODE -ne 0){throw 'WSL mapping'}
 foreach($kind in @('release','sanitizers','tsan')){
  $dir="/tmp/creatorcontainers-$tag-$kind";$flags=@()
  if($kind -eq 'sanitizers'){$flags=@('-DCMAKE_CXX_FLAGS=-fsanitize=address,undefined -fno-omit-frame-pointer','-DCMAKE_EXE_LINKER_FLAGS=-fsanitize=address,undefined')}
  if($kind -eq 'tsan'){$flags=@('-DCMAKE_CXX_FLAGS=-fsanitize=thread -g -O1','-DCMAKE_EXE_LINKER_FLAGS=-fsanitize=thread')}
  Run "linux-$kind" 'wsl.exe' (@('-d','Ubuntu','--','cmake','-S',$unix,'-B',$dir,'-DCMAKE_CXX_COMPILER=clang++','-DCMAKE_BUILD_TYPE=RelWithDebInfo')+$flags)
  $targets=if($kind -eq 'tsan'){@('bounded_mpsc_tests')}else{@('bounded_mpsc_tests','library_tests','container_tests','library_header_probes','allocation_alignment','allocation_alignment_override')}
  Run "linux-$kind" 'wsl.exe' (@('-d','Ubuntu','--','cmake','--build',$dir,'--target')+$targets+@('-j2'))
  $tests=if($kind -eq 'tsan'){@('bounded_mpsc')}else{@('bounded_mpsc','^library_correctness$','^correctness$','allocation_alignment')}
  foreach($name in $tests){Run "linux-$kind" 'wsl.exe' @('-d','Ubuntu','--','env','TSAN_OPTIONS=halt_on_error=1','UBSAN_OPTIONS=halt_on_error=1','ASAN_OPTIONS=detect_leaks=1:halt_on_error=1','ctest','--test-dir',$dir,'-R',$name,'--repeat','until-fail:5','--output-on-failure')}
 }
}
if(!$SkipBenchmarks){
 $env:CE_BENCH_BATCH_NS="$BatchNanoseconds"
 $env:CE_BENCH_INDEPENDENT='1'
 Remove-Item Env:CE_BENCH_ONLY_CASE -ErrorAction SilentlyContinue
 foreach($run in 1..3){
  # Incremental controls: calibration, affinity, priority, then heap isolation.
  foreach($mode in @('legacy','adaptive','affinity','controlled','isolated','normalized')){
   Remove-Item Env:CE_BENCH_LEGACY,Env:CE_BENCH_CONTROLLED,Env:CE_BENCH_AFFINITY,Env:CE_BENCH_GROUPED,Env:CE_BENCH_REVERSE,Env:CE_BENCH_LFH,Env:CE_BENCH_RESET_HEAP,Env:CE_BENCH_UNRESERVED_STATS,Env:CE_BENCH_PRIORITY -ErrorAction SilentlyContinue
   if($mode -eq 'legacy'){$env:CE_BENCH_LEGACY='1'}
   if($mode -eq 'affinity'){$env:CE_BENCH_AFFINITY='1'}
   if($mode -in @('controlled','isolated','normalized')){$env:CE_BENCH_CONTROLLED='1'}
   if($mode -in @('isolated','normalized')){$env:CE_BENCH_GROUPED='1';if($run -eq 2){$env:CE_BENCH_REVERSE='1'}}
   if($mode -eq 'normalized'){$env:CE_BENCH_RESET_HEAP='1'}
   & "$root\build\Release\variance_probe_baseline.exe" | Set-Content "$out\$tag-variance-$mode-$run.csv"
   if($LASTEXITCODE -ne 0){throw 'Variance probe failed'}
  }
  Remove-Item Env:CE_BENCH_LEGACY,Env:CE_BENCH_AFFINITY -ErrorAction SilentlyContinue
  $env:CE_BENCH_CONTROLLED='1';$env:CE_BENCH_GROUPED='1';$env:CE_BENCH_RESET_HEAP='1'
  & "$root\build\Release\array_profile_baseline.exe" | Set-Content "$out\$tag-array-before-$run.csv"
  & "$root\build\Release\array_profile.exe" | Set-Content "$out\$tag-array-after-$run.csv"
  foreach($pair in @(@('container_tests','core'),@('library_benchmark','expanded'),@('mpsc_benchmark','mpsc'))){
   Write-Output "Measuring $($pair[1]), process $run"
   & "$root\build\Release\$($pair[0]).exe" | Set-Content "$out\$tag-$($pair[1])-$run.csv"
   if($LASTEXITCODE -ne 0){throw "Measurement failed: $pair"}
  }
 }
 Remove-Item Env:CE_BENCH_CONTROLLED,Env:CE_BENCH_GROUPED,Env:CE_BENCH_REVERSE,Env:CE_BENCH_LFH,Env:CE_BENCH_RESET_HEAP,Env:CE_BENCH_UNRESERVED_STATS,Env:CE_BENCH_PRIORITY -ErrorAction SilentlyContinue
}
$receipt=[ordered]@{implementationRoot=$implementation;targetBatchNanoseconds=$BatchNanoseconds;timestamp=(Get-Date -Format o);cpu=(Get-CimInstance Win32_Processor|Select-Object Name,NumberOfCores,NumberOfLogicalProcessors);os=(Get-CimInstance Win32_OperatingSystem|Select-Object Caption,Version);sources=@(Get-ChildItem "$implementation\include","$root\tests" -Recurse -File|Get-FileHash -Algorithm SHA256|Select-Object Path,Hash);scripts=@(Get-FileHash "$root\CMakeLists.txt","$root\run-mpsc-variance.ps1" -Algorithm SHA256|Select-Object Path,Hash);binaries=@(Get-ChildItem "$root\build\Release" -Filter '*.exe'|Get-FileHash -Algorithm SHA256|Select-Object Path,Hash);policy='No sample deletion. Core/expanded: independently calibrated minimum batches per side according to targetBatchNanoseconds; normalized per-work times with both round counts recorded, statistics buffers pre-reserved; own process heap caches optimized before each general-side warmup; immediate 1-second warmup per side, grouped A/B order with process 2 reversed. Measuring thread logical CPU4 plus separate physical cores for MPSC producers; above-normal priorities on benchmark threads only. Other user apps untouched. Acceptance: every process median ratio <=1.10 and both-side P90/P10 <=1.30. MPSC: 128B, 262144 values/producer, 1P/4P1C, capacity64/1024, alternating same-process A/B, queue constructor/destructor excluded.'}
$receipt|ConvertTo-Json -Depth 8|Set-Content -Encoding utf8 "$out\$tag-environment.json"



Remove-Item Env:CE_BENCH_BATCH_NS,Env:CE_BENCH_INDEPENDENT -ErrorAction SilentlyContinue
