param([string]$Tag='20261026-final5',[string]$ArchiveRoot='')
$ErrorActionPreference='Stop'
$root=$PSScriptRoot
$culture=[cultureinfo]::InvariantCulture
function Num([string]$v){[double]::Parse($v,$culture)}
function Stats($values){
 $v=@($values|Sort-Object)
 if($v.Count -ne 15){throw "Expected 15 samples, got $($v.Count)"}
 @($v[7],($v[13]/$v[1]))
}
$checked=0
foreach($run in 1..3){
 foreach($kind in @('core','expanded','mpsc')){
  $path=Join-Path $root "results/$Tag-$kind-$run.csv"
  $rows=@(Get-Content $path|ForEach-Object {,$_.Split(',')})
  $queue=$kind -eq 'mpsc'
  $prefix=if($queue){'mpsc_summary'}else{'summary'}
  $summaries=@($rows|Where-Object {$_[0] -eq $prefix})
  $expected=if($queue){4}elseif($kind -eq 'core'){60}else{22}
  if($summaries.Count -ne $expected){throw "Incomplete summaries: $path"}
  foreach($s in $summaries){
   $rawPrefix=if($queue){'mpsc_raw'}else{'sample_diag'}
   $raw=@($rows|Where-Object {$_[0] -eq $rawPrefix -and $_[1] -eq $s[1] -and $_[2] -eq $s[2]})
   if(@($raw|ForEach-Object {$_[3]}|Sort-Object -Unique).Count -ne 15){throw "Missing or duplicate sample: $path $($s[1]),$($s[2])"}
   if($queue){
    $a=Stats @($raw|ForEach-Object {Num $_[4]})
    $b=Stats @($raw|ForEach-Object {Num $_[5]})
    $offset=5
   }else{
    $a=Stats @($raw|ForEach-Object {(Num $_[5])/(Num $_[4])})
    $b=Stats @($raw|ForEach-Object {$roundsB=if($_.Count -gt 11){Num $_[11]}else{Num $_[4]};(Num $_[6])/$roundsB})
    $offset=6
   }
   $actual=@(($a[0]/$b[0]),$a[1],$b[1])
   foreach($i in 0..2){if([math]::Abs($actual[$i]-(Num $s[$offset+$i])) -gt 0.00015){throw "Summary differs from raw: $path $($s[1]),$($s[2])"}}
   ++$checked
  }
 }
}
$receipt=Get-Content (Join-Path $root "results/$Tag-environment.json") -Raw|ConvertFrom-Json
foreach($file in @($receipt.sources)+@($receipt.scripts)+@($receipt.binaries)){
 $path=$file.Path
 if($ArchiveRoot){
  $relative=[IO.Path]::GetRelativePath($root,$path)
  if($relative.StartsWith('..')){
   if(!$receipt.implementationRoot){throw 'Receipt path outside source root'}
   $relative=[IO.Path]::GetRelativePath($receipt.implementationRoot,$path)
   if($relative.StartsWith('..')){throw 'Receipt path outside implementation root'}
   $relative=Join-Path 'implementation' $relative
  }
  if($relative -like 'build\Release\*'){$path=Join-Path $ArchiveRoot (Join-Path 'bin' ([IO.Path]::GetFileName($path)))}else{$path=Join-Path $ArchiveRoot $relative}
 }
 if((Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash -ne $file.Hash){throw "Hash changed: $path"}
}
if($Tag -in @('20261026-final4','20261026-final5')){
 $validated=Get-Content (Join-Path $root 'results/20261026-final2-environment.json') -Raw|ConvertFrom-Json
 $old=@{}
 foreach($file in $validated.sources){$old[$file.Path]=$file.Hash}
 $headers=0
 foreach($file in $receipt.sources|Where-Object {$_.Path -like '*\include\*'}){
  if($old[$file.Path] -ne $file.Hash){throw "Sanitizer-tested header changed: $($file.Path)"}
  ++$headers
 }
 "SANITIZER_HEADER_MATCH: $headers include files identical to final2."
}
if($Tag -eq '20261026-final5'){
 $prior=Get-Content (Join-Path $root 'results/20261026-final4-environment.json') -Raw|ConvertFrom-Json
 $hashes=@{};foreach($file in $prior.binaries){$hashes[$file.Path]=$file.Hash}
 foreach($file in $receipt.binaries){if($hashes[$file.Path] -ne $file.Hash){throw "Isolated binary differs: $($file.Path)"}}
 "ISOLATED_BINARY_MATCH: same three benchmark binaries as final4."
}
"RAW_AND_HASH_VERIFIED: $checked case/process results; 15 samples each; source/script/binary hashes match."
