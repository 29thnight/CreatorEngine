#Requires -Version 7.0
param(
    [Parameter(Mandatory)][string]$EvidenceDirectory,
    [ValidateSet('Debug','Release')][string[]]$Configurations = @('Release'),
    [ValidateRange(1,32)][int]$ExpectedSamples = 8,
    [string]$Python = 'python',
    [switch]$RequireMemoryAccounting
)
$ErrorActionPreference = 'Stop'
$root = [IO.Path]::GetFullPath($EvidenceDirectory)
function CaptureInputs($manifest)
{
    $draws = @($manifest.draws | ForEach-Object {
        [ordered]@{route=$_.route; modelId=$_.modelId; meshId=$_.meshId; modelGeneration=$_.modelGeneration;
            world=$_.world; pose=$_.pose; features=$_.lattice.features; coverage=$_.lattice.coverage;
            uniformBytes=$_.lattice.uniformBytes; textures=$_.lattice.textures} | ConvertTo-Json -Depth 30 -Compress
    } | Sort-Object)
    return ([ordered]@{width=$manifest.width; height=$manifest.height; camera=$manifest.camera;
        lights=$manifest.lights; captureMode=$manifest.captureMode; sampleIndex=$manifest.sampleIndex;
        historyPolicy=$manifest.historyPolicy; skyBoxName=[IO.Path]::GetFileName($manifest.skyBoxPath);
        skyBoxEnabled=$manifest.skyBoxEnabled; viewFlags=$manifest.viewFlags;
        cameraInputContract=$manifest.cameraInputContract; drawInputContract=$manifest.drawInputContract;
        ibl=$manifest.ibl; draws=$draws} | ConvertTo-Json -Depth 30 -Compress)
}
function Distribution($values)
{
    $sorted = @($values | Sort-Object)
    $lower = [int][Math]::Floor(($sorted.Count - 1) / 2)
    $upper = [int][Math]::Floor($sorted.Count / 2)
    return [ordered]@{count=$sorted.Count; minimum=$sorted[0]; maximum=$sorted[-1];
        median=($sorted[$lower]+$sorted[$upper])/2;
        p95=$sorted[[int][Math]::Ceiling($sorted.Count * 0.95)-1]}
}
$summary = @()
foreach ($configuration in $Configurations)
{
    $runs = @{}
    $identities = @()
    $baselineInputs = $null
    foreach ($mode in @('Off','On'))
    {
        $path = Join-Path $root "$configuration-$mode"
        $result = Get-Content "$path/result.json" -Raw | ConvertFrom-Json
        $binary = Get-Content "$path/compiler-binary-hashes.json" -Raw | ConvertFrom-Json
        $environment = Get-Content "$path/rg7-environment.json" -Raw | ConvertFrom-Json
        $expectedAliasing = if ($mode -eq 'On') { '1' } else { '0' }
        if (!$result.complete -or $result.exitCode -ne 0 -or $result.failures.Count -ne 0 -or
            !$result.validation.layerEnabled -or $result.validation.mode -ne 'gpu' -or
            $result.validation.problems -ne 0 -or $result.validation.droppedMessages -ne 0 -or
            $environment.aliasing -ne $expectedAliasing -or $environment.extendedLifetimes -ne '0' -or
            $result.preparationSamples.Count -ne $ExpectedSamples)
        {
            throw "Incomplete preparation run: $path"
        }
        $identities += "$($binary.exe)/$($binary.runtime)"
        $prepare = @(); $compile = @(); $record = @(); $usage = @()
        $memoryRows = @(); $domains = @(); $deviceBytes = @(); $retainedBytes = @(); $cachedBytes = @()
        for ($index = 0; $index -lt $ExpectedSamples; ++$index)
        {
            $sample = $result.preparationSamples[$index]
            $manifest = Get-Content "$($sample.path)/manifest.json" -Raw | ConvertFrom-Json
            $measurement = $manifest.measurement
            $inputs = CaptureInputs $manifest
            if ($baselineInputs -and $inputs -cne $baselineInputs)
            {
                throw "Controlled product inputs changed: $path sample $index"
            }
            $baselineInputs = $inputs
            if ($sample.index -ne $index -or $manifest.width -lt 1000 -or $manifest.height -lt 500 -or
                $manifest.draws.Count -ne 5 -or @($manifest.draws | Where-Object route -ne 'lattice').Count -ne 0 -or
                $measurement.transientPrepareScope -ne 'lifetime-discovery-allocation-cache-resource-creation' -or
                !$measurement.memory.available -or $measurement.memory.scope -ne 'device-budget-snapshot-not-transient-peak')
            {
                throw "Unexpected capture inputs/scope: $path sample $index"
            }
            foreach ($value in @($measurement.cpuTransientPrepareMs,$measurement.cpuGraphCompileMs,$measurement.cpuRecordMs))
            {
                if (!([double]$value -gt 0) -or [double]::IsInfinity([double]$value))
                {
                    throw "Invalid preparation/compile/record timing: $path sample $index"
                }
            }
            if ([Math]::Abs([double]$measurement.cpuTransientPrepareMs - [double]$manifest.graph.transientPrepareCpuMs) -gt 0.00001)
            {
                throw 'Capture measurement and graph preparation statistics differ.'
            }
            if ($mode -eq 'On' -and ($manifest.graph.aliasHeapCreates -ne 0 -or
                $manifest.graph.transientAllocationQueries -ne 0 -or $manifest.graph.aliasHeapReuses -le 0 -or
                $manifest.graph.aliasResourceReuses -le 0))
            {
                throw "Product sample is not warm native heap reuse: $path sample $index"
            }
            $prepare += [double]$measurement.cpuTransientPrepareMs
            $compile += [double]$measurement.cpuGraphCompileMs
            $record += [double]$measurement.cpuRecordMs
            $usage += [uint64]$measurement.memory.usedMB
            if ($RequireMemoryAccounting)
            {
                $heaps = $measurement.aliasHeapMemory
                if (!$heaps.PSObject.Properties['domainId'] -or
                    $heaps.scope -ne 'unique-group-owned-native-heaps-not-resident' -or
                    !$measurement.memory.PSObject.Properties['usedBytes'] -or
                    !$measurement.memory.usageAvailable -or !$measurement.memory.budgetAvailable -or
                    [uint64]$measurement.memory.usedMB -ne [uint64][Math]::Floor([double]$measurement.memory.usedBytes / 1048576) -or
                    [uint64]$measurement.memory.budgetMB -ne [uint64][Math]::Floor([double]$measurement.memory.budgetBytes / 1048576) -or
                    [uint64]$heaps.retainedBytes -ne ([uint64]$heaps.cachedBytes + [uint64]$heaps.leasedBytes) -or
                    [uint64]$heaps.peakRetainedBytes -lt [uint64]$heaps.retainedBytes -or
                    ($mode -eq 'On' -and ($heaps.domainId -le 0 -or $heaps.retainedBytes -le 0)) -or
                    ($mode -eq 'Off' -and ($heaps.domainId -ne 0 -or $heaps.retainedBytes -ne 0)))
                {
                    throw "Missing/inconsistent unique heap domain or byte-precision device usage: $path sample $index"
                }
                $domains += [uint64]$heaps.domainId
                $deviceBytes += [uint64]$measurement.memory.usedBytes
                $retainedBytes += [uint64]$heaps.retainedBytes
                $cachedBytes += [uint64]$heaps.cachedBytes
                $memoryRows += [ordered]@{index=$index; startedUtc=$sample.startedUtc; completedUtc=$sample.completedUtc;
                    device=$measurement.memory; heaps=$heaps}
            }
        }
        $memorySummary = $null
        if ($RequireMemoryAccounting)
        {
            if (@($domains | Select-Object -Unique).Count -ne 1)
            {
                throw 'Warm preparation capture changed accounting domain.'
            }
            $viewDomains = @{}
            $viewRecords = @()
            foreach ($target in @('scene','game','preview'))
            {
                $graph = Get-Content "$path/graph-$target.json" -Raw | ConvertFrom-Json
                if (!$graph.aliasHeapMemory.PSObject.Properties['domainId'] -or
                    $graph.aliasHeapMemory.scope -ne 'unique-group-owned-native-heaps-not-resident' -or
                    [uint64]$graph.aliasHeapMemory.retainedBytes -ne
                        ([uint64]$graph.aliasHeapMemory.cachedBytes + [uint64]$graph.aliasHeapMemory.leasedBytes) -or
                    [uint64]$graph.aliasHeapMemory.peakRetainedBytes -lt [uint64]$graph.aliasHeapMemory.retainedBytes -or
                    ($mode -eq 'On' -and ($graph.aliasHeapMemory.domainId -le 0 -or $graph.aliasHeapMemory.retainedBytes -le 0)) -or
                    ($mode -eq 'Off' -and ($graph.aliasHeapMemory.domainId -ne 0 -or $graph.aliasHeapMemory.retainedBytes -ne 0)))
                {
                    throw "Missing product graph memory domain: $path/$target"
                }
                $viewRecords += [ordered]@{target=$target; frame=$graph.frame; memory=$graph.aliasHeapMemory}
                if ($graph.aliasHeapMemory.domainId -gt 0)
                {
                    # Keep one observation per domain. Views publish at different
                    # frames, so these must not be summed as a simultaneous peak.
                    $viewDomains["$($graph.aliasHeapMemory.domainId)"] = $graph.aliasHeapMemory
                }
            }
            $memorySummary = [ordered]@{domainId=$domains[0]; deviceUsageBytes=(Distribution $deviceBytes);
                retainedAliasHeapBytes=(Distribution $retainedBytes); cachedAliasHeapBytes=(Distribution $cachedBytes);
                viewObservations=$viewRecords; uniqueViewDomainCount=$viewDomains.Count;
                deduplicatedViewDomains=@($viewDomains.Values); simultaneousEnginePeak=$false}
            $memoryRows | ConvertTo-Json -Depth 20 | Set-Content "$path/memory-samples.json" -Encoding utf8
        }
        $runs[$mode] = $result
        $summary += [ordered]@{configuration=$configuration; mode=$mode; processCount=1;
            width=$result.preparationSamples[0].width; height=$result.preparationSamples[0].height;
            transientPrepareCpuMs=(Distribution $prepare); compileCpuMs=(Distribution $compile);
            recordCpuMs=(Distribution $record); sampledDeviceUsageMiB=(Distribution $usage);
            memoryAccounting=$memorySummary;
            cacheState='post-readiness repeated captures; ON native heap reuse audited'; validation=0; exitCode=0}
    }
    if ($identities[0] -ne $identities[1])
    {
        throw 'OFF/ON binary identity differs.'
    }
    for ($index = 0; $index -lt $ExpectedSamples; ++$index)
    {
        & $Python (Join-Path $PSScriptRoot 'compare-material-captures.py') `
            $runs.Off.preparationSamples[$index].path $runs.On.preparationSamples[$index].path `
            "$root/preparation-comparison-$configuration-$index.json" > "$root/preparation-comparison-$configuration-$index.log"
        if ($LASTEXITCODE -ne 0)
        {
            throw "OFF/ON preparation capture pixels differ: $configuration sample $index"
        }
    }
}
[ordered]@{passed=$true; phaseComplete=$false; aliasingDefault='off'; summaries=$summary;
    memoryAccountingAudited=$RequireMemoryAccounting.IsPresent;
    performanceAccepted=$false; exactPeakVramMeasured=$false;
    scope='Repeated controlled product Scene captures with GPU validation enabled; includes diagnostic readbacks; one process per mode';
    limits='Observed timings and sampled process device usage maximum only; cold creation, continuous lifecycle sampling, exact resident peak and general performance adoption remain unaccepted.'} |
    ConvertTo-Json -Depth 20 | Set-Content "$root/product-preparation-result.json" -Encoding utf8
"RG7_PRODUCT_PREPARATION_OK $root"
