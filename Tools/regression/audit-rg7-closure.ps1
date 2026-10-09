#Requires -Version 7.0
param([Parameter(Mandatory)][string]$EvidenceDirectory, [string]$Python = 'python')
$ErrorActionPreference = 'Stop'
$root = [IO.Path]::GetFullPath($EvidenceDirectory)
$runs = @()
$identity = $null
foreach ($order in @('OnFirst','OffFirst'))
{
    $path = Join-Path $root $order
    $accepted = Get-Content "$path/product-preparation-result.json" -Raw | ConvertFrom-Json
    if (!$accepted.passed -or !$accepted.memoryAccountingAudited)
    {
        throw "Missing product acceptance: $order"
    }
    foreach ($mode in @('On','Off'))
    {
        $case = "$path/Release-$mode"
        $binary = Get-Content "$case/compiler-binary-hashes.json" -Raw | ConvertFrom-Json
        $key = "$($binary.exe)/$($binary.runtime)"
        if ($identity -and $identity -ne $key)
        {
            throw 'Closure processes used different binaries.'
        }
        $identity = $key
        $samples = @(Get-Content "$case/memory-continuous.jsonl" | ForEach-Object { $_ | ConvertFrom-Json })
        $stages = @(Get-Content "$case/memory-stages.jsonl" | ForEach-Object { $_ | ConvertFrom-Json })
        if ($samples.Count -lt 100 -or @($samples | Where-Object { !$_.valid -or $_.budgetBytes -le 0 }).Count)
        {
            throw "Invalid or insufficient continuous memory series: $case"
        }
        $streams = @()
        foreach ($group in @($samples | Group-Object stream))
        {
            $rows = @($group.Group)
            if ($rows[0].kind -ne 'start' -or $rows[-1].kind -ne 'end')
            {
                throw "Incomplete sampler lifecycle: $case"
            }
            $maximumGap = 0
            for ($i = 1; $i -lt $rows.Count; ++$i)
            {
                $gap = [long]$rows[$i].elapsedMs - [long]$rows[$i-1].elapsedMs
                if ($gap -lt 0)
                {
                    throw 'Sampler monotonic time regressed.'
                }
                $maximumGap = [Math]::Max($maximumGap, $gap)
            }
            if ($maximumGap -gt 1000)
            {
                throw "Continuous series has a gap over one second: $case gap=$maximumGap"
            }
            $streams += [ordered]@{stream=$group.Name; count=$rows.Count; maximumGapMs=$maximumGap;
                durationMs=$rows[-1].elapsedMs; firstUtcMs=$rows[0].utcMs; lastUtcMs=$rows[-1].utcMs;
                sampledUsagePeakBytes=($rows.usedBytes | Measure-Object -Maximum).Maximum;
                finalUsageBytes=$rows[-1].usedBytes}
        }
        $begin = ($samples.utcMs | Measure-Object -Minimum).Minimum
        $end = ($samples.utcMs | Measure-Object -Maximum).Maximum
        foreach ($name in @('scene.switch','editor.renderscale','render.live.capture','quit'))
        {
            $matching = @($stages | Where-Object command -eq $name)
            if (!$matching.Count -or @($matching | Where-Object { $_.utcMs -lt $begin -or $_.utcMs -gt $end }).Count)
            {
                throw "Memory series does not bracket stage $name in $case"
            }
        }
        $summary = $accepted.summaries | Where-Object mode -eq $mode
        $runs += [ordered]@{order=$order; mode=$mode; binary=$binary; samples=$samples.Count;
            streams=$streams; stageCount=$stages.Count;
            sampledUsagePeakBytes=($samples.usedBytes | Measure-Object -Maximum).Maximum;
            preparation=$summary.transientPrepareCpuMs; compile=$summary.compileCpuMs; record=$summary.recordCpuMs;
            heapOwnership=$summary.memoryAccounting; validation=0; exitCode=0}
    }
}
foreach ($mode in @('On','Off'))
{
    & $Python (Join-Path $PSScriptRoot 'compare-material-captures.py') `
        "$root/OnFirst/Release-$mode/preparation-sample-0" `
        "$root/OffFirst/Release-$mode/preparation-sample-0" `
        "$root/cross-order-$mode.json" > "$root/cross-order-$mode.log"
    if ($LASTEXITCODE -ne 0)
    {
        throw "Independent process inputs/pixels changed: $mode"
    }
}
[ordered]@{passed=$true; runs=$runs; processCount=4; targetPeriodMs=100; maximumAllowedGapMs=1000;
    boundedRg7Complete=$true; performanceDecisionComplete=$true; speedupAccepted=$false;
    scope='DXGI process local-segment budget usage, from DX12 device initialization through teardown; shared-heap ownership separately recorded';
    exactResidentPeakMeasured=$false; adoption='keep-default-off';
    reason='No production speedup established; bounded correctness and measurement complete, opt-in retained and further coverage tracked separately';
    limits='Sampled maxima can miss shorter peaks; DXGI usage is not exact physical residency or per-resource committed ownership; sampler overhead and GPU validation are enabled equally in A/B; four processes do not establish broad production performance'} |
    ConvertTo-Json -Depth 30 | Set-Content "$root/closure-result.json" -Encoding utf8
"RG7_CLOSURE_EVIDENCE_OK $root"
