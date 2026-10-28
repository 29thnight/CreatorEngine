param([Parameter(Mandatory)][string]$Tag)
$ErrorActionPreference='Stop';$root=$PSScriptRoot
$r=Get-Content "$root/results/$Tag-ablation.json" -Raw|ConvertFrom-Json
if(!$r.coverageComplete -or $r.conditions -ne 180){throw 'Incomplete experiment'}
function Median($values){$s=@($values|Sort-Object);$s[[int][math]::Floor($s.Count/2)]}
$effects=@(foreach($size in 64,4096,65536){foreach($operation in 0,1,2,3){
 $b=@($r.summary|Where-Object {$_.variant -eq 'baseline' -and $_.size -eq $size -and $_.operation -eq $operation})
 $baseline=Median @($b.ownNs)
 foreach($variant in 'canonical','baseline','reuse','erase','both'){
  $rows=@($r.summary|Where-Object {$_.variant -eq $variant -and $_.size -eq $size -and $_.operation -eq $operation})
  if($rows.Count -ne 3){throw 'Missing process repeat'}
  $paired=@(foreach($row in $rows){$reference=$b|Where-Object run -eq $row.run;100*(1-$row.ownNs/$reference.ownNs)})
  [ordered]@{size=$size;operation=$operation;variant=$variant;ownNs=(Median $rows.ownNs);stlRatio=(Median $rows.ratio);improvementPercent=100*(1-(Median $rows.ownNs)/$baseline);pairedMin=($paired|Measure-Object -Minimum).Minimum;pairedMax=($paired|Measure-Object -Maximum).Maximum;passCount=@($rows|Where-Object pass).Count;maxOwnSpread=($rows.ownSpread|Measure-Object -Maximum).Maximum;maxStlSpread=($rows.stlSpread|Measure-Object -Maximum).Maximum}
 }
}})
$effects|ConvertTo-Json -Depth 5|Set-Content "$root/results/$Tag-effects.json" -Encoding utf8
$effects|Where-Object {$_.operation -in 0,3}|ForEach-Object {[pscustomobject]$_}|Format-Table size,operation,variant,ownNs,stlRatio,improvementPercent,pairedMin,pairedMax
