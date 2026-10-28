param([Parameter(Mandatory)][string]$Tag)
$ErrorActionPreference='Stop'
$receipt=Get-Content "$PSScriptRoot/results/$Tag-optimization.json" -Raw|ConvertFrom-Json
if(!$receipt.coverageComplete -or !$receipt.grouped -or $receipt.conditions -ne 54 -or @($receipt.summary|Where-Object {!$_.pass}).Count){throw 'Canonical 54-condition acceptance incomplete'}
function Median($values){$s=@($values|Sort-Object);$s[[int][math]::Floor($s.Count/2)]}
$rows=@(foreach($group in $receipt.summary|Group-Object size,operation){
 [ordered]@{size=$group.Group[0].size;operation=$group.Group[0].operation;ratio=(Median $group.Group.ratio);ownNs=(Median $group.Group.ownNs);stlNs=(Median $group.Group.stlNs);maxOwnSpread=($group.Group.ownSpread|Measure-Object -Maximum).Maximum;maxStlSpread=($group.Group.stlSpread|Measure-Object -Maximum).Maximum;passCount=@($group.Group|Where-Object pass).Count}
})
$result=[ordered]@{pass=$true;tag=$Tag;accepted=54;total=54;originalMatrixAccepted=36;reserveAccepted=9;growthAccepted=9;maxRatio=($receipt.summary.ratio|Measure-Object -Maximum).Maximum;maxOwnSpread=($receipt.summary.ownSpread|Measure-Object -Maximum).Maximum;maxStlSpread=($receipt.summary.stlSpread|Measure-Object -Maximum).Maximum;cases=$rows}
$result|ConvertTo-Json -Depth 6|Set-Content "$PSScriptRoot/results/$Tag-verdict.json" -Encoding utf8
$rows|ForEach-Object {[pscustomobject]$_}|Sort-Object size,operation|Format-Table
"NODE_MAP_ACCEPTANCE_PASS: 54/54; maxRatio=$($result.maxRatio)"
