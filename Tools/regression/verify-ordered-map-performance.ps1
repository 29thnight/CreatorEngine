param([Parameter(Mandatory)][string]$EvidenceDirectory,[int]$Runs=3)
$ErrorActionPreference='Stop'
$rows=@()
foreach($run in 1..$Runs){
    foreach($mode in @('core','nodes')){
        $file="$EvidenceDirectory/accepted-$run-$mode.csv"
        $raw=@(Import-Csv $file)
        $expected=if($mode -eq 'core'){150}else{30}
        if($raw.Count -ne $expected){throw "Incomplete samples: $file"}
        foreach($group in $raw|Group-Object case,count){
            if($group.Count -ne 15 -or @($group.Group.sample|Sort-Object -Unique).Count -ne 15){throw 'Invalid samples'}
            $a=@($group.Group.customNs|ForEach-Object {[double]$_}|Sort-Object)
            $b=@($group.Group.stlNs|ForEach-Object {[double]$_}|Sort-Object)
            $rows+=[pscustomobject]@{run=$run;mode=$mode;case=$group.Name;ratio=$a[7]/$b[7];customSpread=$a[13]/$a[1];stlSpread=$b[13]/$b[1];
                pass=($a[7]/$b[7] -le 1.10 -and $a[13]/$a[1] -le 1.30 -and $b[13]/$b[1] -le 1.30)}
        }
    }
}
$rows|ConvertTo-Json -Depth 4|Set-Content "$EvidenceDirectory/accepted-summary-$Runs.json" -Encoding utf8
$failed=@($rows|Where-Object {!$_.pass})
if($failed.Count){$failed|Format-Table -AutoSize;throw 'Ordered map performance gate failed; all raw files retained'}
"ORDERED_MAP_PERFORMANCE_PASS cases=12 runs=$Runs samples=15 ratioLimit=1.10 spreadLimit=1.30"
