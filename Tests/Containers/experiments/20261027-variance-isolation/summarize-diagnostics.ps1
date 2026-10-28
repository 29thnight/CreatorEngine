param([string]$Prefix='20261027-variance3')
$ErrorActionPreference='Stop'
$root=(Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
$report=@(foreach($file in Get-ChildItem "$root/results" -Filter "$Prefix-*.csv"){
    $rows=@(Get-Content $file.FullName|ForEach-Object {,$_.Split(',')})
    $summary=@($rows|Where-Object {$_[0] -eq 'summary'})
    if($summary.Count -ne 1){continue}
    $s=$summary[0]
    $samples=@($rows|Where-Object {$_[0] -eq 'sample_diag'})
    $contexts=@($rows|Where-Object {$_[0] -eq 'context_diag'})
    if($samples.Count -ne 15 -or $contexts.Count -ne 30){throw "Incomplete diagnostic: $($file.Name)"}
    $gapsA=@($samples|ForEach-Object {[math]::Max(0,([double]$_[5]-[double]$_[7])/1e6)}|Sort-Object)
    $gapsB=@($samples|ForEach-Object {[math]::Max(0,([double]$_[6]-[double]$_[8])/1e6)}|Sort-Object)
    $ca=@($samples|ForEach-Object {[double]$_[9]/[double]$_[4]}|Sort-Object)
    $cb=@($samples|ForEach-Object {[double]$_[10]/[double]$_[11]}|Sort-Object)
    [pscustomobject]@{
        name=$file.BaseName;case=$s[1];ratio=[double]$s[6];customSpread=[double]$s[7];stlSpread=[double]$s[8]
        customNs=[double]$s[4];stlNs=[double]$s[5];customWallCpuGapP90Ms=$gapsA[13];stlWallCpuGapP90Ms=$gapsB[13]
        customCyclesMedian=$ca[7];stlCyclesMedian=$cb[7]
        pageFaults=($contexts|ForEach-Object {[int]$_[5]}|Measure-Object -Sum).Sum
        siblingBusyMean=($contexts|ForEach-Object {[double]$_[8]}|Measure-Object -Average).Average
        performanceMean=($contexts|ForEach-Object {[double]$_[10]}|Measure-Object -Average).Average
        dpcMax=($contexts|ForEach-Object {[double]$_[11]}|Measure-Object -Maximum).Maximum
        interruptMax=($contexts|ForEach-Object {[double]$_[12]}|Measure-Object -Maximum).Maximum
    }
})
$report|ConvertTo-Json -Depth 4|Set-Content "$root/results/$Prefix-diagnostic-summary.json" -Encoding utf8
$report|Export-Csv "$root/results/$Prefix-diagnostic-summary.csv" -NoTypeInformation -Encoding utf8
$report|Format-Table name,customSpread,stlSpread,customWallCpuGapP90Ms,stlWallCpuGapP90Ms,pageFaults -AutoSize
