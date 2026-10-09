#Requires -Version 7.0
param([Parameter(Mandatory)][string]$EvidenceDirectory)
$ErrorActionPreference = 'Stop'
$root = [IO.Path]::GetFullPath($EvidenceDirectory)
. (Join-Path $PSScriptRoot 'CommandResults.ps1')
$results = @(Read-CommandResults "$root/results.jsonl")
$probe = @($results | Where-Object { $_.command -eq 'dx12.rendergraph' -and $_.data.log -match 'RG7_TRANSIENT_OK' })
if ($probe.Count -ne 1 -or !$probe[0].data.passed -or
    @($results | Where-Object status -ne 'succeeded').Count -ne 0)
{
    throw 'Incomplete existing Editor heap memory regression.'
}
$log = $probe[0].data.log
if ($log -notmatch 'RG7_HEAP_CACHE_MEMORY_OK peakBytes=131072 drainedBytes=0 warm=unique externalOwner=retained' -or
    $log -notmatch 'RG7_HEAP_MEMORY_OK mode=0 peakBytes=196608 drainBytes=0 cached=after-completion' -or
    $log -notmatch 'RG7_HEAP_MEMORY_OK mode=1 peakBytes=65536 drainBytes=0 cached=after-completion' -or
    $log -notmatch 'RG7_DEVICE_LOSS_OK[^\r\n]*validation=0')
{
    throw 'Missing unique heap/cache/delayed GPU/native loss accounting acceptance.'
}
$binary = Get-Content "$root/binary-metadata.json" -Raw | ConvertFrom-Json
[ordered]@{passed=$true; phaseComplete=$false; binary=$binary;
    cachePeakBytes=131072; gpuDelayedThreeViewPeakBytes=196608; postNativeFailurePeakBytes=65536;
    finalRetainedBytes=0; finalCachedBytes=0; exactResidentVramMeasured=$false;
    scope='Unique group-owned shared native heap bytes; imported/committed heaps excluded; producer ownership preserved';
    acceptance='warm reuse counts once; partial creation failure peak/drain; delayed GPU and failed native submission stay leased; cache return after completion; external group ownership survives clear; native loss retained until abandon'} |
    ConvertTo-Json -Depth 12 | Set-Content "$root/heap-memory-result.json" -Encoding utf8
"RG7_HEAP_MEMORY_ACCOUNTING_OK $root"
