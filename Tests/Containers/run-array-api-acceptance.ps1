param([Parameter(Mandatory)][string]$Tag,[ValidateRange(1,5)][int]$Repeats=3)
$ErrorActionPreference='Stop'
$root=$PSScriptRoot
. "$root/implementation-root.ps1"
$implementation=Get-ContainerImplementationRoot
$out=Join-Path $root 'results'
if(Get-ChildItem $out -Filter "$Tag-*" -ErrorAction SilentlyContinue){throw 'Use a fresh tag'}
$cmake='C:/Program Files/Microsoft Visual Studio/18/Community/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe'
& $cmake -S $root -B "$root/build-modules" -DCE_MODULES=ON "-DCE_IMPLEMENTATION_ROOT=$implementation" *> "$out/$Tag-configure.log"
if($LASTEXITCODE){throw 'Configure failed'}
& $cmake --build "$root/build-modules" --config Release --target module_array_migration_tests *> "$out/$Tag-build.log"
if($LASTEXITCODE){throw 'Build failed'}
$exe=Join-Path $root 'build-modules/modules/Release/module_array_migration_tests.exe'
& $exe *> "$out/$Tag-correctness.log"
if($LASTEXITCODE){throw 'Array migration correctness failed'}
$inputs=@(Get-ChildItem "$implementation/modules" -Recurse -File -Filter '*.ixx')+@(
 Get-Item "$implementation/modules/module_config.h","$root/modules/CMakeLists.txt",$PSCommandPath,
 "$root/tests/array_migration.cpp","$root/build-modules/modules/module_array_migration_tests.cpp",$exe)
$hashes=@($inputs|Get-FileHash|Select-Object Path,Hash)
$hashes|ConvertTo-Json|Set-Content "$out/$Tag-inputs.json" -Encoding utf8
$flags=@('CE_ARRAY_API_COUNT_ONLY','CE_ARRAY_API_LARGE_ONLY','CE_ARRAY_ALIGNMENT_DIAGNOSTICS','CE_ARRAY_ALIGNED_CONTROL','CE_ARRAY_PLAIN_CONTROL')
$old=@{};foreach($name in $flags){$old[$name]=[Environment]::GetEnvironmentVariable($name);Remove-Item "Env:$name" -ErrorAction SilentlyContinue}
$accepted=$true;$summary=@();$culture=[cultureinfo]::InvariantCulture
try{
 foreach($run in 1..$Repeats){
  $csv=Join-Path $out "$Tag-$run.csv"
  $process=Start-Process $exe -ArgumentList '--bench' -WindowStyle Hidden -PassThru -RedirectStandardOutput $csv -RedirectStandardError "$out/$Tag-$run.stderr"
  $process.ProcessorAffinity=[IntPtr]16;$process.PriorityClass='AboveNormal'
  $process.WaitForExit()
  if($process.ExitCode -notin 0,2){throw "Benchmark process failed: $($process.ExitCode)"}
  $rows=@(Get-Content $csv|Where-Object {$_ -like 'array_api_summary,*'}|ForEach-Object {,$_.Split(',')})
  $raw=@(Get-Content $csv|Where-Object {$_ -like 'array_api_raw,*'}|ForEach-Object {,$_.Split(',')})
  if($rows.Count -ne 12 -or $raw.Count -ne 180){throw 'Incomplete 12-case/15-sample matrix'}
  foreach($row in $rows){
   $samples=@($raw|Where-Object {$_[1] -eq $row[1] -and $_[2] -eq $row[2]})
   if(@($samples|ForEach-Object {$_[3]}|Sort-Object -Unique).Count -ne 15){throw 'Missing raw samples'}
   $a=@($samples|ForEach-Object {[double]::Parse($_[4],$culture)}|Sort-Object)
   $b=@($samples|ForEach-Object {[double]::Parse($_[5],$culture)}|Sort-Object)
   $ratio=$a[7]/$b[7];$sa=$a[13]/$a[1];$sb=$b[13]/$b[1]
   foreach($pair in @(@($ratio,$row[3]),@($sa,$row[4]),@($sb,$row[5]))){
    if([Math]::Abs($pair[0]-[double]::Parse($pair[1],$culture)) -gt 0.00001){throw 'Summary differs from raw samples'}
   }
   $pass=$ratio -le 1.10 -and $sa -le 1.30 -and $sb -le 1.30
   $accepted=$accepted -and $pass
   $summary+=[ordered]@{run=$run;name=$row[1];size=[int]$row[2];ratio=$ratio;customSpread=$sa;stlSpread=$sb;pass=$pass}
  }
  "ARRAY_API_REPEAT_$run`: pass=$(@($summary|Where-Object {$_['run'] -eq $run -and $_['pass']}).Count)/12"
 }
 foreach($file in $hashes){if((Get-FileHash $file.Path).Hash -ne $file.Hash){throw "Input changed: $($file.Path)"}}
 $receipt=[ordered]@{pass=$accepted;repeats=$Repeats;casesPerRepeat=12;samplesPerCase=15;implementationRoot=$implementation;
  cpuAffinity=16;priority='AboveNormal';targetBatchNanoseconds=100000000;ratioLimit=1.10;spreadLimit=1.30;
  cpu=(Get-CimInstance Win32_Processor|Select-Object Name,NumberOfCores,NumberOfLogicalProcessors);
  inputs=$hashes;summary=$summary}
 $receipt|ConvertTo-Json -Depth 6|Set-Content "$out/$Tag-acceptance.json" -Encoding utf8
 if(!$accepted){throw 'Array API acceptance failed; all samples are preserved'}
}finally{foreach($name in $flags){[Environment]::SetEnvironmentVariable($name,$old[$name])}}
