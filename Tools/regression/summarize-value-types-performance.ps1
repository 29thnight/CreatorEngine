param([Parameter(Mandatory)][string]$Prefix)
$ErrorActionPreference = 'Stop'
$expectedCases = @('pair_write_read', 'tuple_write_read', 'tuple_apply', 'tuple_compare', 'tie_compare', 'member_apply', 'tuple_sort')
$rows = @(foreach ($run in 1..3) {
    $samples = @(Import-Csv "$Prefix-$run.csv")
    if ($samples.Count -ne 105) { throw "Incomplete process $run" }
    if (@(Compare-Object ($samples.case | Sort-Object -Unique) ($expectedCases | Sort-Object)).Count) { throw "Unexpected case inventory: $run" }
    foreach ($group in ($samples | Group-Object case)) {
        if ($group.Count -ne 15) { throw "Incomplete case $($group.Name)" }
        if (@(Compare-Object @($group.Group.sample | ForEach-Object { [int]$_ } | Sort-Object) @(0..14)).Count) {
            throw "Missing or duplicate sample indices: $($group.Name)"
        }
        $custom = @($group.Group.custom_ns | ForEach-Object { [double]$_ } | Sort-Object)
        $standard = @($group.Group.stl_ns | ForEach-Object { [double]$_ } | Sort-Object)
        $ratio = $custom[7] / $standard[7]
        $customSpread = $custom[13] / $custom[1]
        $standardSpread = $standard[13] / $standard[1]
        [pscustomobject]@{
            run = $run; case = $group.Name
            customMedianNs = $custom[7]; stlMedianNs = $standard[7]
            ratio = $ratio; customSpread = $customSpread; stlSpread = $standardSpread
            pass = ($ratio -le 1.10 -and $customSpread -le 1.30 -and $standardSpread -le 1.30)
        }
    }
})
if ($rows.Count -ne 21) { throw 'Expected seven cases in three processes' }
$rows | ConvertTo-Json -Depth 4 | Set-Content "$Prefix-summary.json" -Encoding utf8
$rows | Format-Table run, case, ratio, customSpread, stlSpread, pass
if (@($rows | Where-Object { !$_.pass }).Count) { throw 'Value types performance acceptance failed' }
'VALUE_TYPES_PERFORMANCE_OK cases=7 processes=3 samples=15 ratioLimit=1.10 spreadLimit=1.30'
