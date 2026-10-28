param([Parameter(Mandatory)][string]$Tag,[ValidateRange(1,5)][int]$Repeats=3,
 [ValidateRange(20000000,500000000)][long]$BatchNanoseconds=30000000)
$ErrorActionPreference='Stop'
$root=$PSScriptRoot
. "$root/implementation-root.ps1"
$implementation=Get-ContainerImplementationRoot
$out=Join-Path $root 'results'
if(Get-ChildItem $out -Filter "$Tag-*" -ErrorAction SilentlyContinue){throw 'Use a fresh tag'}
$cmake='C:/Program Files/Microsoft Visual Studio/18/Community/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe'
& $cmake -S $root -B "$root/build-modules" -DCE_MODULES=ON "-DCE_IMPLEMENTATION_ROOT=$implementation" *> "$out/$Tag-configure.log"
if($LASTEXITCODE){throw 'Configure failed'}
& $cmake --build "$root/build-modules" --config Release --target module_fixed_array_benchmark *> "$out/$Tag-build.log"
if($LASTEXITCODE){throw 'Build failed'}
$exe=Join-Path $root 'build-modules/modules/Release/module_fixed_array_benchmark.exe'
$inputs=@(Get-ChildItem "$implementation/modules" -Recurse -File -Filter '*.ixx')+@(Get-Item $PSCommandPath,"$root/tests/fixed_array_migration.cpp","$implementation/modules/module_config.h","$root/modules/CMakeLists.txt","$root/CMakeLists.txt","$root/build-modules/modules/module_fixed_array_benchmark.cpp",$exe)
$hashes=@($inputs|Get-FileHash|Select-Object Path,Hash)
$hashes|ConvertTo-Json|Set-Content "$out/$Tag-inputs.json" -Encoding utf8
$snapshot=Join-Path $out "$Tag-snapshot"
foreach($input in $hashes){
 $relative=[IO.Path]::GetRelativePath((Split-Path $root),$input.Path)
 $destination=Join-Path $snapshot $relative
 New-Item -ItemType Directory -Force (Split-Path $destination)|Out-Null
 Copy-Item -LiteralPath $input.Path -Destination $destination
}
$summary=@();$pass=$true;$culture=[cultureinfo]::InvariantCulture
$previousBatch=$env:CE_FIXED_BATCH_NS
$env:CE_FIXED_BATCH_NS=[string]$BatchNanoseconds
try{
foreach($run in 1..$Repeats){
 $csv=Join-Path $out "$Tag-$run.csv"
 $process=Start-Process $exe -WindowStyle Hidden -PassThru -RedirectStandardOutput $csv -RedirectStandardError "$out/$Tag-$run.stderr"
 $process.ProcessorAffinity=[IntPtr]16;$process.PriorityClass='AboveNormal';$process.WaitForExit()
 if($process.ExitCode -notin 0,2){throw 'Benchmark process failed'}
 $rows=@(Get-Content $csv|Where-Object {$_ -like 'fixed_summary,*'}|ForEach-Object {,$_.Split(',')})
 $raw=@(Get-Content $csv|Where-Object {$_ -like 'fixed_raw,*'}|ForEach-Object {,$_.Split(',')})
 if($rows.Count -ne 9 -or $raw.Count -ne 135){throw 'Incomplete matrix'}
 foreach($row in $rows){
  $samples=@($raw|Where-Object {$_[1] -eq $row[1] -and $_[2] -eq $row[2]})
  if(@($samples|ForEach-Object {$_[3]}|Sort-Object -Unique).Count -ne 15){throw 'Missing samples'}
  $a=@($samples|ForEach-Object {[double]::Parse($_[4],$culture)}|Sort-Object)
  $b=@($samples|ForEach-Object {[double]::Parse($_[5],$culture)}|Sort-Object)
  $ratio=$a[7]/$b[7];$cs=$a[13]/$a[1];$ss=$b[13]/$b[1]
  $accepted=$ratio -le 1.10 -and $cs -le 1.30 -and $ss -le 1.30
  if($accepted -ne ($row[6] -eq 'PASS')){throw 'Summary disagrees with raw samples'}
  $pass=$pass -and $accepted
  $summary+=[ordered]@{run=$run;size=[int]$row[1];operation=[int]$row[2];ratio=$ratio;customSpread=$cs;stlSpread=$ss;pass=$accepted}
 }
}
foreach($input in $hashes){if((Get-FileHash $input.Path).Hash -ne $input.Hash){throw "Changed input: $($input.Path)"}}
$receipt=[ordered]@{pass=$pass;cases=$summary.Count;batchNanoseconds=$BatchNanoseconds;cpuTimingCollected=$true;ended=(Get-Date -Format o);inputs=$hashes;summary=$summary}
$receipt|ConvertTo-Json -Depth 6|Set-Content "$out/$Tag-acceptance.json" -Encoding utf8
"FIXED_ARRAY_ACCEPTANCE: $pass; $($summary.Count) cases"
if(!$pass){exit 2}
}finally{$env:CE_FIXED_BATCH_NS=$previousBatch}
