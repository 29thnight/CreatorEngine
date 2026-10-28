param([Parameter(Mandatory)][string]$Tag,[ValidateRange(1,3)][int]$Repeats=3)
$ErrorActionPreference='Stop';$root=$PSScriptRoot
if(Get-ChildItem "$root/results" -Filter "$Tag-*" -ErrorAction SilentlyContinue){throw 'Use a fresh tag'}
$cmake='C:/Program Files/Microsoft Visual Studio/18/Community/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe'
& $cmake -S $root -B "$root/build-modules" -DCE_MODULES=ON *> "$root/results/$Tag-configure.log"
if($LASTEXITCODE){throw 'Configure failed'}
& $cmake --build "$root/build-modules" --config Release --target module_node_map_benchmark *> "$root/results/$Tag-build.log"
if($LASTEXITCODE){throw 'Build failed'}
$exe=Join-Path $root 'build-modules/modules/Release/module_node_map_benchmark.exe'
$before=Get-FileHash $exe;$summary=@();$culture=[cultureinfo]::InvariantCulture
Copy-Item -LiteralPath $exe -Destination "$root/results/$Tag-benchmark.exe"
Copy-Item -LiteralPath "$root/tests/node_map_benchmark.cpp" -Destination "$root/results/$Tag-benchmark.cpp"
foreach($run in 1..$Repeats){
 $policies=if($run%2){@('default','identity')}else{@('identity','default')}
 foreach($policy in $policies){
  $csv="$root/results/$Tag-$policy-$run.csv"
  $arguments=@{FilePath=$exe;WindowStyle='Hidden';PassThru=$true;RedirectStandardOutput=$csv;RedirectStandardError="$csv.stderr"}
  if($policy -eq 'identity'){$arguments.ArgumentList='--identity-control'}
  $process=Start-Process @arguments
  $process.ProcessorAffinity=[IntPtr]16;$process.PriorityClass='AboveNormal';$process.WaitForExit()
  if($process.ExitCode -notin 0,2){throw 'Benchmark process failed'}
  $lines=Get-Content $csv
  if($lines[0] -ne "node_map_control,$policy"){throw 'Wrong control'}
  $rows=@($lines|Where-Object {$_ -like 'node_map_summary,*'})
  if($rows.Count -ne 12 -or @($lines|Where-Object {$_ -like 'node_map_raw,*'}).Count -ne 180){throw 'Incomplete matrix'}
  foreach($line in $rows){$row=$line.Split(',');$summary+=[ordered]@{run=$run;policy=$policy;size=[int]$row[1];operation=[int]$row[2];ratio=[double]::Parse($row[3],$culture);customSpread=[double]::Parse($row[4],$culture);stlSpread=[double]::Parse($row[5],$culture)}}
 }
}
if((Get-FileHash $exe).Hash -ne $before.Hash){throw 'Binary changed'}
[ordered]@{purpose='Hash-cost isolation only; not acceptance of the default node map';binary=$before;repeats=$Repeats;summary=$summary}|ConvertTo-Json -Depth 5|Set-Content "$root/results/$Tag-controls.json" -Encoding utf8
'NODE_MAP_CONTROLS_COLLECTED'
