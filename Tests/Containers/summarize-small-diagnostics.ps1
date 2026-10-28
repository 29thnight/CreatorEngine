$ErrorActionPreference='Stop';$root=$PSScriptRoot;$out=Join-Path $root 'results';$culture=[cultureinfo]::InvariantCulture
function Num([string]$v){[double]::Parse($v,$culture)}
function Median($v){$a=@($v|Sort-Object);$a[[int][math]::Floor($a.Count/2)]}
$contexts=@();$phases=@()
foreach($entry in @(@('0','no_burn'),@('1','sibling_integer_burn'),@('2','other_core_integer_burn'),@('guard-heavy','guard_and_sibling_burn'))){
 $rows=@(Get-Content "$out/20261026-small-final-context-$($entry[0]).csv"|ForEach-Object {,$_.Split(',')})
 $summary=@($rows|Where-Object {$_[0] -eq 'summary'})
 $ctx=@($rows|Where-Object {$_[0] -eq 'context_diag'})
 if($ctx.Count -ne 30 -or $summary.Count -ne 1){throw 'Incomplete context experiment'}
 foreach($row in $ctx){if((Num $row[8]) -lt 0 -or (Num $row[10]) -lt 0){throw 'Required PDH counter unavailable'}}
 $contexts+=[pscustomobject]@{mode=$entry[1];aMedian=(Num $summary[0][4]);bMedian=(Num $summary[0][5]);aSpread=(Num $summary[0][7]);bSpread=(Num $summary[0][8]);pageFaults=($ctx|ForEach-Object {Num $_[5]}|Measure-Object -Sum).Sum;workingSetAbsoluteMax=($ctx|ForEach-Object {[math]::Abs((Num $_[6]))}|Measure-Object -Maximum).Maximum;privateAbsoluteMax=($ctx|ForEach-Object {[math]::Abs((Num $_[7]))}|Measure-Object -Maximum).Maximum;siblingAverage=($ctx|ForEach-Object {Num $_[8]}|Measure-Object -Average).Average;performanceAverage=($ctx|ForEach-Object {Num $_[10]}|Measure-Object -Average).Average;dpcMax=($ctx|ForEach-Object {Num $_[11]}|Measure-Object -Maximum).Maximum;interruptMax=($ctx|ForEach-Object {Num $_[12]}|Measure-Object -Maximum).Maximum}
}
foreach($mode in @('normal','guard')){
 $rows=@(Get-Content "$out/20261026-small-final-phase-$mode.csv"|ForEach-Object {,$_.Split(',')})
 $phase=@($rows|Where-Object {$_[0] -eq 'phase'})
 foreach($g in ($phase|Group-Object {$_[1]})){
  $v=@($g.Group);if($v.Count -ne 15){throw 'Incomplete phase samples'}
  $phases+=[pscustomobject]@{mode=$mode;name=$g.Name;ns=(Median @($v|ForEach-Object {Num $_[4]}));calls=(Median @($v|ForEach-Object {Num $_[7]}));bytes=(Median @($v|ForEach-Object {Num $_[8]}));allocateNs=(Median @($v|ForEach-Object {Num $_[9]}));freeNs=(Median @($v|ForEach-Object {Num $_[10]}));faults=($v|ForEach-Object {Num $_[6]}|Measure-Object -Sum).Sum}
 }
 if(@($phases|Where-Object {$_.mode -eq $mode}).Count -ne 6){throw 'Expected six phase paths'}
}
$binaries=@(Get-FileHash "$root/experiments/20261026-small-array/build/Release/array_context.exe","$root/experiments/20261026-small-array/build/Release/array_phase.exe","$root/experiments/20261026-small-array/build/Release/smt_guard.exe"|Select-Object Path,Hash)
[ordered]@{context=$contexts;phases=$phases;binaries=$binaries;sources=(Get-Content "$out/20261026-small-diagnostic-sources.json" -Raw|ConvertFrom-Json);policy='Diagnostic-only binaries. Context counter intervals include before/after query overhead. Traced allocator includes clock overhead; phase totals must not be added/subtracted as pure component costs. Final acceptance uses unchanged original native binaries.'}|ConvertTo-Json -Depth 8|Set-Content "$out/20261026-small-array-diagnostics.json" -Encoding utf8
"SMALL_DIAGNOSTICS_VERIFIED: 4 context conditions, 6 paths x 2 resource conditions x 15 samples."
