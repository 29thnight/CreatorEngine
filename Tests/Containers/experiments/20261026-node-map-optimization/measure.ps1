param([Parameter(Mandatory)][string]$Tag,[switch]$Grouped,[ValidateRange(1,3)][int]$Repeats=3,[string[]]$Variants=@('baseline','pool','index','combined'))
$ErrorActionPreference='Stop'
$root=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
. "$root/implementation-root.ps1"
$implementation=Get-ContainerImplementationRoot
$out=Join-Path $root 'results';$suiteTag=$Tag
if(Get-ChildItem $out -Filter "$Tag-*" -ErrorAction SilentlyContinue){throw 'Use a fresh tag'}
$exe=Join-Path $root 'build-node-opt/experiments/20261026-node-map-optimization/Release/node_map_optimization.exe'
$inputFiles=@(Get-ChildItem "$implementation/modules","$implementation/include" -Recurse -File)+
 @(Get-ChildItem $PSScriptRoot -File)+@(Get-Item $exe,"$root/CMakeLists.txt")
$inputs=@($inputFiles|Get-FileHash|Select-Object Path,Hash)
$inputs|ConvertTo-Json|Set-Content "$out/$Tag-inputs.json" -Encoding utf8
foreach($input in $inputs){
 $relative=[IO.Path]::GetRelativePath((Split-Path $root),$input.Path)
 $destination=Join-Path "$out/$Tag-snapshot" $relative
 New-Item -ItemType Directory -Force (Split-Path $destination)|Out-Null
 Copy-Item -LiteralPath $input.Path -Destination $destination
}
$summary=[Collections.Generic.List[object]]::new();$culture=[cultureinfo]::InvariantCulture
& "$root/experiments/20261026-small-array/with-guard.ps1" -Tag "$Tag-guard" -Work {
 foreach($run in 1..$Repeats){
  $order=@($Variants);if($run -eq 2){[array]::Reverse($order)}elseif($run -eq 3 -and $order.Count>1){$order=@($order[1..($order.Count-1)])+@($order[0])}
  foreach($variant in $order){
   $csv="$out/$suiteTag-$variant-$run.csv"
   $arguments=@($variant);if($Grouped){$arguments+='--grouped'};if($run -eq 2){$arguments+='--reverse'}
   $process=Start-Process $exe -ArgumentList $arguments -WindowStyle Hidden -PassThru -RedirectStandardOutput $csv -RedirectStandardError "$csv.stderr"
   $process.ProcessorAffinity=[IntPtr]16;$process.PriorityClass='AboveNormal';$process.WaitForExit()
   if($process.ExitCode -notin 0,2){throw "Process failed: $variant / $run"}
   $lines=Get-Content $csv
   if($lines[0] -ne "optimization_variant,$variant"){throw 'Wrong variant'}
   $rows=@($lines|Where-Object {$_ -like 'node_map_summary,*'}|ForEach-Object {,$_.Split(',')})
   $raw=@($lines|Where-Object {$_ -like 'node_map_raw,*'}|ForEach-Object {,$_.Split(',')})
   if($rows.Count -ne 15 -or $raw.Count -ne 225){throw 'Incomplete matrix'}
   foreach($row in $rows){
    $samples=@($raw|Where-Object {$_[1] -eq $row[1] -and $_[2] -eq $row[2]})
    if(@($samples|ForEach-Object {$_[3]}|Sort-Object -Unique).Count -ne 15){throw 'Missing samples'}
    $a=@($samples|ForEach-Object {[double]::Parse($_[4],$culture)}|Sort-Object)
    $b=@($samples|ForEach-Object {[double]::Parse($_[5],$culture)}|Sort-Object)
    $ratio=$a[7]/$b[7];$cs=$a[13]/$a[1];$ss=$b[13]/$b[1]
    if([math]::Abs($ratio-[double]::Parse($row[3],$culture)) -gt 0.000002){throw 'Raw/summary mismatch'}
    $pass=$ratio -le 1.10 -and $cs -le 1.30 -and $ss -le 1.30
    if($pass -ne ($row[6] -eq 'PASS')){throw 'Acceptance mismatch'}
    $summary.Add([ordered]@{run=$run;variant=$variant;size=[int]$row[1];operation=[int]$row[2];ownNs=$a[7];stlNs=$b[7];ratio=$ratio;ownSpread=$cs;stlSpread=$ss;pass=$pass})
   }
   "OPTIMIZATION_REPEAT $run $variant collected 15 conditions; pass=$(@($summary|Where-Object {$_.run -eq $run -and $_.variant -eq $variant -and $_.pass}).Count)/15"
  }
 }
}
foreach($input in $inputs){if((Get-FileHash $input.Path).Hash -ne $input.Hash){throw "Changed input: $($input.Path)"}}
if($summary.Count -ne $Variants.Count*$Repeats*15){throw 'Incomplete 5 x 3 x 12 experiment'}
[ordered]@{coverageComplete=$true;compiler='MSVC 19.50.35730.0 x64 Release /O2';binary=(Get-FileHash $exe);conditions=$summary.Count;grouped=[bool]$Grouped;samplesPerSide=15;policy='Single executable, allocation/index controls; independent process repeats, reverse order and side order on repeat2. Original alternating30ms or explicitly recorded grouped100ms with 1-second per-side warmup and own-process heap optimization. CPU4, CPU5 helper; all samples retained.';inputs=$inputs;summary=$summary}|ConvertTo-Json -Depth 7|Set-Content "$out/$Tag-optimization.json" -Encoding utf8
'NODE_MAP_OPTIMIZATION_COMPLETE'
