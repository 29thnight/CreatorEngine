param([string]$Tag='20261026-module-accept1',[string]$GuardTag='20261026-modguard-run1')
$ErrorActionPreference='Stop';$root=$PSScriptRoot;$out=Join-Path $root 'results'
$culture=[cultureinfo]::InvariantCulture
function N([string]$s){[double]::Parse($s,$culture)}
function Median($v){$s=@($v|Sort-Object);if(!$s.Count){throw 'Empty values'};$s[[int][math]::Floor($s.Count/2)]}
function Stats($v){$s=@($v|Sort-Object);if($s.Count -ne 15 -or $s[1] -le 0){throw 'Invalid 15 samples'};@($s[7],($s[13]/$s[1]))}
function Close([double]$a,[double]$b){if([math]::Abs($a-$b) -gt 0.00015){throw "Summary disagrees with raw: $a / $b"}}
function RoundedRatio([double]$a,[double]$b,[double]$reported){
 # Concurrent's historical CSV prints ns with only two decimals, ratio with four.
 $lo=($a-0.005)/($b+0.005)-0.000051;$hi=($a+0.005)/($b-0.005)+0.000051
 if($reported -lt $lo -or $reported -gt $hi){throw 'Concurrent summary outside raw quantization interval'}
}
& "$root/verify-mpsc-results.ps1" -Tag $Tag
$before=Get-Content "$out/$Tag-before.json" -Raw|ConvertFrom-Json
foreach($file in $before){if((Get-FileHash $file.Path).Hash -ne $file.Hash){throw "Input changed: $($file.Path)"}}
$references=Get-Content "$out/$Tag-reference-files.json" -Raw|ConvertFrom-Json
foreach($file in $references){if((Get-FileHash $file.Path).Hash -ne $file.Hash){throw 'Compiler/STL/PPL reference changed'}}
foreach($target in @('module_container_tests','module_library_benchmark','module_mpsc_benchmark','module_queue_acceptance')){
 $source=Get-Content "$root/build-modules/modules/$target.cpp" -Raw
 if($source -notmatch 'import Creator\.Containers;' -or $source -match '#include\s*[<"]ce[/\\]'){throw 'Benchmark is not a clean module consumer'}
}
$guard=Get-Content "$out/$GuardTag-guard.json" -Raw|ConvertFrom-Json
if(!$guard.success -or !$guard.aliveAtWorkEnd){throw 'Guard coverage failed'}
foreach($file in @($guard.helper,$guard.script)){if((Get-FileHash $file.Path).Hash -ne $file.Hash){throw 'Guard hash mismatch'}}
$start=[DateTimeOffset]::Parse($guard.started).UtcDateTime;$end=[DateTimeOffset]::Parse($guard.workEnded).UtcDateTime
$files=@(Get-ChildItem "$out/$Tag-cases" -File -Filter '*.csv')
if($files.Count -ne 246){throw 'Expected 246 general processes'}
foreach($file in @($files)+@(1..3|ForEach-Object {Get-Item "$out/$Tag-mpsc-$_.csv"})){
 if($file.LastWriteTimeUtc -lt $start -or $file.LastWriteTimeUtc -gt $end){throw "Outside guard lifetime: $($file.Name)"}
}
$manifest=Get-Content "$out/$Tag-manifest.json" -Raw|ConvertFrom-Json
$keys=@($manifest|ForEach-Object {"$($_.kind)/$($_.name)/$($_.n)"}|Sort-Object)
if($keys.Count -ne 82 -or @($keys|Sort-Object -Unique).Count -ne 82){throw 'Invalid manifest'}
$general=@();$mpsc=@()
foreach($run in 1..3){
 $observed=@()
 foreach($kind in @('core','expanded')){
  $rows=@(Get-Content "$out/$Tag-$kind-$run.csv"|ForEach-Object {,$_.Split(',')})
  foreach($s in @($rows|Where-Object {$_[0] -eq 'summary'})){
   $key="$kind/$($s[1])/$($s[2])";$observed+=$key
   $general+=[pscustomobject]@{key=$key;run=$run;customNs=(N $s[4]);referenceNs=(N $s[5]);ratio=(N $s[6]);customSpread=(N $s[7]);referenceSpread=(N $s[8]);accepted=((N $s[6]) -le 1.10 -and (N $s[7]) -le 1.30 -and (N $s[8]) -le 1.30)}
  }
 }
 if(Compare-Object $keys @($observed|Sort-Object)){throw 'Case manifest mismatch'}
 foreach($line in Get-Content "$out/$Tag-mpsc-$run.csv"){
  $s=$line.Split(',');if($s[0] -ne 'mpsc_summary'){continue}
  $mpsc+=[pscustomobject]@{key="$($s[1])/$($s[2])";run=$run;customNs=(N $s[3]);referenceNs=(N $s[4]);ratio=(N $s[5]);customSpread=(N $s[6]);referenceSpread=(N $s[7]);accepted=((N $s[5]) -le 1.10 -and (N $s[6]) -le 1.30 -and (N $s[7]) -le 1.30)}
 }
}
$ppl=@();$pplKeys=@('8/1/1','128/1/1','8/4/1','128/4/1','8/4/4','128/4/4')
foreach($run in 1..5){
 $rows=@(Get-Content "$out/$Tag-ppl-$run.csv"|ForEach-Object {,$_.Split(',')})
 $summaries=@($rows|Where-Object {$_[0] -eq 'summary'})
 if($summaries.Count -ne 6){throw 'Expected 6 FAA queue summaries'}
 $observed=@()
 foreach($s in $summaries){
  $key=$s[1..3]-join '/';$observed+=$key
  $raw=@($rows|Where-Object {$_[0] -eq 'raw' -and ($_[1..3]-join '/') -eq $key})
  if(@($raw|ForEach-Object {$_[4]}|Sort-Object -Unique).Count -ne 15){throw 'Invalid PPL sample indices'}
  $a=Stats @($raw|ForEach-Object {N $_[5]});$b=Stats @($raw|ForEach-Object {N $_[6]})
  Close ($a[0]/$b[0]) (N $s[7]);Close $a[1] (N $s[8]);Close $b[1] (N $s[9])
  $life=@($rows|Where-Object {$_[0] -eq 'lifecycle' -and ($_[2..4]-join '/') -eq $key})
  $la=@($life|Where-Object {$_[1] -eq 'custom'}|Select-Object -Skip 2)
  $lb=@($life|Where-Object {$_[1] -eq 'ppl'}|Select-Object -Skip 2)
  if($la.Count -ne 15 -or $lb.Count -ne 15){throw 'Invalid lifecycle count'}
  $lifeRatio=(Median @($la|ForEach-Object {N $_[7]}))/(Median @($lb|ForEach-Object {N $_[7]}))
  $ppl+=[pscustomobject]@{key=$key;run=$run;customNs=$a[0];referenceNs=$b[0];ratio=($a[0]/$b[0]);customSpread=$a[1];referenceSpread=$b[1];lifecycleRatio=$lifeRatio;accepted=($a[0]/$b[0] -le 1.10 -and $lifeRatio -le 1.10)}
 }
 if(Compare-Object @($pplKeys|Sort-Object) @($observed|Sort-Object)){throw 'PPL manifest mismatch'}
}
function GroupResults($rows,[int]$runs){
 foreach($g in $rows|Group-Object key){
  if($g.Count -ne $runs){throw "Incomplete repeats: $($g.Name)"}
  [pscustomobject]@{key=$g.Name;accepted=(@($g.Group|Where-Object {!$_.accepted}).Count -eq 0);minRatio=($g.Group.ratio|Measure-Object -Minimum).Minimum;maxRatio=($g.Group.ratio|Measure-Object -Maximum).Maximum;maxCustomSpread=($g.Group.customSpread|Measure-Object -Maximum).Maximum;maxReferenceSpread=($g.Group.referenceSpread|Measure-Object -Maximum).Maximum;rows=@($g.Group|Sort-Object run)}
 }
}
$g=@(GroupResults $general 3);$q=@(GroupResults $mpsc 3);$f=@(GroupResults $ppl 5)
$concurrent=@()
$concurrentReceipt="$out/$Tag-concurrent-before.json"
if(!(Test-Path $concurrentReceipt)){$concurrentReceipt="$out/module-concurrent-before.json"}
$concurrentBinary=Get-Content $concurrentReceipt -Raw|ConvertFrom-Json
if((Get-FileHash $concurrentBinary.Path).Hash -ne $concurrentBinary.Hash){throw 'Concurrent binary changed'}
foreach($run in 1..3){
 $rows=@(Get-Content "$out/$Tag-concurrent-$run.csv"|ForEach-Object {,$_.Split(',')})
 $summaries=@($rows|Where-Object {$_[0] -eq 'summary'})
 if($summaries.Count -ne 8){throw 'Expected SPSC/legacy spin MPMC 8 cases'}
 foreach($s in $summaries){
  $key=$s[1..4]-join '/'
  $raw=@($rows|Where-Object {$_[0] -eq 'raw' -and ($_[1..4]-join '/') -eq $key})
  if($raw.Count -ne 9 -or @($raw|ForEach-Object {$_[5]}|Sort-Object -Unique).Count -ne 9){throw 'Invalid 9-sample concurrent case'}
  $a=@($raw|ForEach-Object {N $_[6]}|Sort-Object);$b=@($raw|ForEach-Object {N $_[7]}|Sort-Object)
  RoundedRatio $a[4] $b[4] (N $s[8]);RoundedRatio $a[7] $a[1] (N $s[9]);RoundedRatio $b[7] $b[1] (N $s[10])
  $concurrent+=[pscustomobject]@{key=$key;run=$run;customNs=$a[4];referenceNs=$b[4];ratio=(N $s[8]);customSpread=(N $s[9]);referenceSpread=(N $s[10]);accepted=((N $s[8]) -le 1.10);varianceWarning=((N $s[9]) -gt 1.25 -or (N $s[10]) -gt 1.25)}
 }
}
$c=@(GroupResults $concurrent 3)
$primary=@();$primaryEnvironment=Get-Content "$out/$Tag-primary-environment.json" -Raw|ConvertFrom-Json
if((Get-FileHash $primaryEnvironment.binary.Path).Hash -ne $primaryEnvironment.binary.Hash){throw 'Primary MPSC binary changed'}
if([DateTimeOffset]::Parse($primaryEnvironment.started) -lt [DateTimeOffset]::Parse($guard.ended)){throw 'Primary overlaps SMT guard'}
foreach($run in 1..3){
 $rows=@(Get-Content "$out/$Tag-primary-$run.csv"|ForEach-Object {,$_.Split(',')})
 $policy=@($rows|Where-Object {$_[0] -eq 'mpsc_policy'})
 if($policy.Count -ne 1 -or $policy[0][6] -ne '8' -or $policy[0][2] -ne 'os_yield' -or $policy[0][4] -ne 'os_yield'){throw 'Wrong primary protocol'}
 $summaries=@($rows|Where-Object {$_[0] -eq 'mpsc_summary'})
 if($summaries.Count -ne 4){throw 'Primary case count mismatch'}
 foreach($s in $summaries){
  $sub=@($rows|Where-Object {$_[0] -eq 'mpsc_subraw' -and $_[1] -eq $s[1] -and $_[2] -eq $s[2]})
  if(@($sub|Where-Object {[int]$_[3] -lt 0}).Count -ne 24){throw 'Expected three warmup batches'}
  $a=@();$b=@()
  foreach($index in 0..14){
   $parts=@($sub|Where-Object {[int]$_[3] -eq $index})
   if($parts.Count -ne 8 -or @($parts|ForEach-Object {$_[4]}|Sort-Object -Unique).Count -ne 8){throw 'Incomplete primary sub-transfers'}
   $x=($parts|ForEach-Object {N $_[5]}|Measure-Object -Sum).Sum/8
   $y=($parts|ForEach-Object {N $_[6]}|Measure-Object -Sum).Sum/8
   $raw=@($rows|Where-Object {$_[0] -eq 'mpsc_raw' -and $_[1] -eq $s[1] -and $_[2] -eq $s[2] -and [int]$_[3] -eq $index})
   if($raw.Count -ne 1 -or [math]::Abs($x-(N $raw[0][4])) -gt 1 -or [math]::Abs($y-(N $raw[0][5])) -gt 1){throw 'Batch mean mismatch'}
   $a+=$x;$b+=$y
  }
  $a=Stats $a;$b=Stats $b
  Close ($a[0]/$b[0]) (N $s[5]);Close $a[1] (N $s[6]);Close $b[1] (N $s[7])
  $primary+=[pscustomobject]@{key="batch8/$($s[1])/$($s[2])";run=$run;customNs=$a[0];referenceNs=$b[0];ratio=($a[0]/$b[0]);customSpread=$a[1];referenceSpread=$b[1];accepted=($a[0]/$b[0] -le 1.10 -and $a[1] -le 1.30 -and $b[1] -le 1.30)}
 }
}
$p=@(GroupResults $primary 3)
$receipt=[ordered]@{
 tag=$Tag;guardTag=$GuardTag;verified=$true;generalAccepted=@($g|Where-Object accepted).Count;generalTotal=82;mpscAccepted=@($q|Where-Object accepted).Count;mpscTotal=4;faaAccepted=@($f|Where-Object accepted).Count;faaTotal=6
 concurrentAccepted=@($c|Where-Object accepted).Count;concurrentTotal=8
 mpscPrimaryAccepted=@($p|Where-Object accepted).Count;mpscPrimaryTotal=4
 pass=(@($g+$q+$f+$c+$p|Where-Object {!$_.accepted}).Count -eq 0)
 policy='General/MPSC: every repeat ratio<=1.10, both-side sorted15 x[13]/x[1]<=1.30. FAA: original 5-process gate, transfer and lifecycle ratio<=1.10 each repeat; spreads disclosed, not used to change original gate. No samples removed.'
 general=$g;mpsc=$q;faa=$f;concurrent=$c;mpscPrimary=$p;engineIntegrated=$false
 evidence=@(Get-FileHash "$out/$Tag-before.json","$out/$Tag-environment.json","$out/$GuardTag-guard.json","$out/$Tag-reference-files.json","$out/$Tag-primary-environment.json",$PSCommandPath,"$root/make-module-acceptance-report.ps1","$root/run-module-mpsc-primary.ps1"|Select-Object Path,Hash)
 rawFiles=@(Get-ChildItem "$out/$Tag-cases","$out" -File|Where-Object {$_.Name -like "$Tag-*-*.csv" -or $_.Directory.Name -eq "$Tag-cases"}|Get-FileHash|Select-Object Path,Hash)
}
$receipt|ConvertTo-Json -Depth 12|Set-Content "$out/$Tag-verdict.json" -Encoding utf8
"MODULE_ACCEPTANCE: general $($receipt.generalAccepted)/82, MPSC primary $($receipt.mpscPrimaryAccepted)/4, MPSC single $($receipt.mpscAccepted)/4, FAA $($receipt.faaAccepted)/6, SPSC/spin-MPMC $($receipt.concurrentAccepted)/8; PASS=$($receipt.pass)"
foreach($failure in $g+$q+$f+$c+$p|Where-Object {!$_.accepted}){"FAILED $($failure.key): ratioMax=$($failure.maxRatio), spreadMax=$($failure.maxCustomSpread)/$($failure.maxReferenceSpread)"}
