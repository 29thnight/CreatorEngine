param([string]$Tag='20261026-modopt-paired')
$ErrorActionPreference='Stop';$root=$PSScriptRoot;$out=Join-Path $root 'results'
$culture=[cultureinfo]::InvariantCulture
function N([string]$s){[double]::Parse($s,$culture)}
function Median($v){$a=@($v|Sort-Object);if(!$a.Count){throw 'Empty values'};$a[[int][math]::Floor($a.Count/2)]}
function Stats($v){$a=@($v|Sort-Object);if($a.Count -ne 15){throw 'Expected 15 samples'};@($a[7],($a[13]/$a[1]))}
function Check([double]$a,[double]$b){if([math]::Abs($a-$b) -gt .00015){throw 'Raw/summary mismatch'}}
function ControlledFile([string]$suffix){
 $p="$out/$Tag-$suffix";if(Test-Path $p){return $p}
 # Initial collection used the guard parameter's dynamic Tag; retain its raw names.
 $p="$out/$Tag`_guard-$suffix";if(!(Test-Path $p)){throw "Missing control $suffix"};$p
}
$string=@();$mpsc=@();$faa=@();$inputs=@()
foreach($variant in 'before','inline','final'){
 foreach($run in 1..3){
  if($variant -ne 'inline'){
   foreach($n in 8,23,24){
    $p=ControlledFile "$variant-string-$n-$run.csv";$inputs+=$p
    $rows=@(Get-Content $p|ForEach-Object {,$_.Split(',')});$s=@($rows|Where-Object {$_[0] -eq 'summary'})
    if($s.Count -ne 1){throw 'Incomplete string control'};$s=$s[0]
    $raw=@($rows|Where-Object {$_[0] -eq 'sample_diag'})
    $a=Stats @($raw|ForEach-Object {(N $_[5])/(N $_[4])});$b=Stats @($raw|ForEach-Object {(N $_[6])/(N $_[11])})
    Check ($a[0]/$b[0]) (N $s[6]);Check $a[1] (N $s[7]);Check $b[1] (N $s[8])
    $string+=[pscustomobject]@{variant=$variant;run=$run;n=$n;customNs=$a[0];referenceNs=$b[0];ratio=($a[0]/$b[0]);customSpread=$a[1];referenceSpread=$b[1]}
   }
  }
  $p=ControlledFile "$variant-mpsc-$run.csv";$inputs+=$p
  $rows=@(Get-Content $p|ForEach-Object {,$_.Split(',')});$summaries=@($rows|Where-Object {$_[0] -eq 'mpsc_summary'})
  if($summaries.Count -ne 4){throw 'Incomplete MPSC control'}
  foreach($s in $summaries){
   $raw=@($rows|Where-Object {$_[0] -eq 'mpsc_subraw' -and $_[1] -eq $s[1] -and $_[2] -eq $s[2] -and [int]$_[3] -ge 0})
   $a=Stats @($raw|ForEach-Object {N $_[5]});$b=Stats @($raw|ForEach-Object {N $_[6]})
   Check ($a[0]/$b[0]) (N $s[5]);Check $a[1] (N $s[6]);Check $b[1] (N $s[7])
   $mpsc+=[pscustomobject]@{variant=$variant;run=$run;capacity=[int]$s[1];producers=[int]$s[2];customNs=$a[0];referenceNs=$b[0];ratio=($a[0]/$b[0]);customSpread=$a[1];referenceSpread=$b[1];pushRetries=(Median @($raw|ForEach-Object {N $_[7]}));popRetries=(Median @($raw|ForEach-Object {N $_[8]}));workerStartNs=(Median @($raw|ForEach-Object {N $_[11]}))}
  }
 }
 foreach($run in 1..5){
  $p="$out/$Tag-$variant-ppl-$run.csv";$inputs+=$p
  $rows=@(Get-Content $p|ForEach-Object {,$_.Split(',')});$summaries=@($rows|Where-Object {$_[0] -eq 'summary'})
  if($summaries.Count -ne 6){throw 'Incomplete FAA control'}
  foreach($s in $summaries){
   $key=$s[1..3]-join '/';$raw=@($rows|Where-Object {$_[0] -eq 'raw' -and ($_[1..3]-join '/') -eq $key})
   $a=Stats @($raw|ForEach-Object {N $_[5]});$b=Stats @($raw|ForEach-Object {N $_[6]})
   Check ($a[0]/$b[0]) (N $s[7]);Check $a[1] (N $s[8]);Check $b[1] (N $s[9])
   $la=@($rows|Where-Object {$_[0] -eq 'lifecycle' -and $_[1] -eq 'custom' -and ($_[2..4]-join '/') -eq $key}|Select-Object -Skip 2|ForEach-Object {N $_[7]})
   $lb=@($rows|Where-Object {$_[0] -eq 'lifecycle' -and $_[1] -eq 'ppl' -and ($_[2..4]-join '/') -eq $key}|Select-Object -Skip 2|ForEach-Object {N $_[7]})
   if($la.Count -ne 15 -or $lb.Count -ne 15){throw 'Incomplete lifecycle'}
   $faa+=[pscustomobject]@{variant=$variant;run=$run;key=$key;customNs=$a[0];referenceNs=$b[0];ratio=($a[0]/$b[0]);lifecycleRatio=((Median $la)/(Median $lb));customSpread=$a[1];referenceSpread=$b[1]}
  }
 }
}
$audit=@();$old="$out/20261026-module-accept1-snapshot/modules"
foreach($file in Get-ChildItem "$root/modules" -Recurse -Filter '*.ixx'){
 $relative=[IO.Path]::GetRelativePath("$root/modules",$file.FullName)
 $a=[IO.File]::ReadAllText((Join-Path $old $relative));$b=[IO.File]::ReadAllText($file.FullName)
 $identical=$a -ceq $b
 $inlineOnly=[regex]::Replace($a,'\binline\s+','') -ceq [regex]::Replace($b,'\binline\s+','')
 if(!$inlineOnly){throw "Algorithm source changed: $relative"}
 $audit+=[pscustomobject]@{unit=$relative;identical=$identical;inlineOnly=$inlineOnly}
}
$codegen=@(foreach($v in 'before','inline','final'){
 $p="$out/20261026-modopt-queue-$v-disasm.txt";$inputs+=$p
 [pscustomobject]@{variant=$v;u32Calls=@(Select-String -Path $p -Pattern 'call.*\?(load|store|compare_exchange|fetch_add|exchange)@atomic_u32').Count;emptyHookCalls=@(Select-String -Path $p -Pattern 'call.*\?(after_link|after_head_protected|node_created|node_destroyed)@queue_no_hooks').Count}
})
$guard=Get-Content "$out/$Tag`_guard-guard.json" -Raw|ConvertFrom-Json
if(!$guard.success -or !$guard.aliveAtWorkEnd){throw 'Paired guard coverage failed'}
$receipt=[ordered]@{tag=$Tag;string=$string;mpsc=$mpsc;faa=$faa;sourceAudit=$audit;codegen=$codegen;guard=$guard;inputs=@($inputs|Get-FileHash|Select-Object Path,Hash);policy='Paired diagnostic controls, no failed acceptance sample replacement. Intrinsic operations, memory orders, layouts and algorithms unchanged; only explicit inline specifiers added in four implementation units. Historical pre-control final executable paths may now point to later validation rebuilds; collection-time hashes and invariant checks are retained in paired environment receipt.'}
$receipt|ConvertTo-Json -Depth 10|Set-Content "$out/$Tag-analysis.json" -Encoding utf8
"PAIRED_RAW_VERIFIED: string $($string.Count), MPSC $($mpsc.Count), FAA $($faa.Count) condition/process records; all 23 canonical units equivalent after removing inline specifiers."
