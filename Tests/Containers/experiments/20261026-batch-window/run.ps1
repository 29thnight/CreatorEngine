$ErrorActionPreference='Stop';$root=$PSScriptRoot
$env:CE_BENCH_CONTROLLED='1';$env:CE_BENCH_GROUPED='1';$env:CE_BENCH_RESET_HEAP='1'
foreach($run in 1..3){
 if($run -eq 2){$env:CE_BENCH_REVERSE='1';$windows=@(100000000,20000000)}else{Remove-Item Env:CE_BENCH_REVERSE -ErrorAction SilentlyContinue;$windows=@(20000000,100000000)}
 foreach($window in $windows){
  $env:CE_BENCH_BATCH_NS="$window"
  foreach($case in @(@('core','map_miss',64),@('core','map_hit',8),@('core','array_growth',65536),@('expanded','radix_sort_u64',256))){
   $env:CE_BENCH_ONLY_CASE="$($case[1]),$($case[2])"
   $path="$root/../../results/20261026-window-$window-$run-$($case[1])-$($case[2]).csv"
   & "$root/build/Release/$($case[0]).exe" | Set-Content $path
   if($LASTEXITCODE -ne 0){throw 'Probe failed'}
   Get-Content $path | Where-Object {$_ -like 'summary,*'} | ForEach-Object {"$window,$run,$_"}
  }
 }
}
Remove-Item Env:CE_BENCH_CONTROLLED,Env:CE_BENCH_GROUPED,Env:CE_BENCH_RESET_HEAP,Env:CE_BENCH_BATCH_NS,Env:CE_BENCH_ONLY_CASE,Env:CE_BENCH_REVERSE -ErrorAction SilentlyContinue
