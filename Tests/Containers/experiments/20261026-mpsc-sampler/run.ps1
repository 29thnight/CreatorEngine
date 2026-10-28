param([string]$Tag='20261026-mpsc-sampler')
$ErrorActionPreference='Stop'
$root=(Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
$out=Join-Path $root 'results'
if((Get-ChildItem $out -Filter "$Tag-*.csv" -ErrorAction SilentlyContinue) -or (Test-Path "$out/$Tag-environment.json")){throw 'Use a fresh tag'}
$modes=@(
 [pscustomobject]@{name='yield-single';pause=$false;startPause=$false;batch=1},
 [pscustomobject]@{name='start-pause-single';pause=$false;startPause=$true;batch=1},
 [pscustomobject]@{name='pause-single';pause=$true;startPause=$true;batch=1},
 [pscustomobject]@{name='yield-batch8';pause=$false;startPause=$false;batch=8},
 [pscustomobject]@{name='pause-batch8';pause=$true;startPause=$true;batch=8}
)
$env:CE_BENCH_CONTROLLED='1'
Remove-Item Env:CE_BENCH_ONLY_CASE,Env:CE_BENCH_BATCH_NS,Env:CE_MPSC_PAUSE,Env:CE_MPSC_START_PAUSE,Env:CE_MPSC_BATCH_REPEATS -ErrorAction SilentlyContinue
foreach($run in 1..3){
 $order=@($modes);if($run -eq 2){[array]::Reverse($order)}
 foreach($mode in $order){
  if($mode.pause){$env:CE_MPSC_PAUSE='1'}else{Remove-Item Env:CE_MPSC_PAUSE -ErrorAction SilentlyContinue}
  if($mode.startPause){$env:CE_MPSC_START_PAUSE='1'}else{Remove-Item Env:CE_MPSC_START_PAUSE -ErrorAction SilentlyContinue}
  $env:CE_MPSC_BATCH_REPEATS="$($mode.batch)"
  & "$root/build/Release/mpsc_benchmark.exe"|Set-Content "$out/$Tag-$($mode.name)-$run.csv" -Encoding utf8
  if($LASTEXITCODE -ne 0){throw "Failed: $($mode.name), $run"}
  Write-Output "MPSC mode=$($mode.name) repeat=$run"
  Get-Content "$out/$Tag-$($mode.name)-$run.csv"|Where-Object {$_ -like 'mpsc_summary,*'}|Write-Output
 }
}
$receipt=[ordered]@{
 timestamp=(Get-Date -Format o)
 cpu=(Get-CimInstance Win32_Processor|Select-Object Name,NumberOfCores,NumberOfLogicalProcessors)
 os=(Get-CimInstance Win32_OperatingSystem|Select-Object Caption,Version)
 sources=@(Get-ChildItem "$root/include","$root/tests" -Recurse -File|Get-FileHash -Algorithm SHA256|Select-Object Path,Hash)
 scripts=@(Get-FileHash "$root/CMakeLists.txt","$PSScriptRoot/run.ps1" -Algorithm SHA256|Select-Object Path,Hash)
 binaries=@(Get-FileHash "$root/build/Release/mpsc_benchmark.exe" -Algorithm SHA256|Select-Object Path,Hash)
 policy='Same instrumented binary: yield start+retry, pause32 start+yield retry, pause32 start+retry, yield batch8 and pause32 batch8; all four capacity/producer cases, all three process repeats, mode order reversed in repeat2. Predefined final throughput protocol is yield-batch8 (original wait policy), while pause modes are causal controls. Same fixed 128B payload, 262144 items per producer in every transfer, ready barrier and join included as before; queue construction/destruction and thread construction excluded. Three predefined warmup batches, 15 retained batches, all sub-transfer times and outer retry counts/start delays logged. Batch8 reports mean time per original transfer, not one contiguous wall-clock interval; no samples discarded. Busy polling is a dedicated-core benchmark policy, not an engine CPU/latency acceptance claim. Library headers unchanged.'
}
$receipt|ConvertTo-Json -Depth 8|Set-Content "$out/$Tag-environment.json" -Encoding utf8
Remove-Item Env:CE_BENCH_CONTROLLED,Env:CE_MPSC_PAUSE,Env:CE_MPSC_START_PAUSE,Env:CE_MPSC_BATCH_REPEATS -ErrorAction SilentlyContinue
