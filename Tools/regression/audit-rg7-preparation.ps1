#Requires -Version 7.0
param([Parameter(Mandatory)][string]$EvidenceDirectory)
$ErrorActionPreference = 'Stop'
$root = [IO.Path]::GetFullPath($EvidenceDirectory)
. (Join-Path $PSScriptRoot 'CommandResults.ps1')
$results = @(Read-CommandResults (Join-Path $root 'results.jsonl'))
$probe = @($results | Where-Object { $_.command -eq 'dx12.rendergraph' -and $_.data.log -match 'RG7_TRANSIENT_OK' })
if ($probe.Count -ne 1 -or $probe[0].status -ne 'succeeded' -or !$probe[0].data.passed -or
    @($results | Where-Object status -ne 'succeeded').Count -ne 0)
{
    throw 'Missing successful existing Editor RG7 fixture result.'
}
$log = $probe[0].data.log
$matches = [regex]::Matches($log, 'RG7_PREPARE_CPU mode=(\d+) milliseconds=([0-9.]+)')
$rows = @()
foreach ($match in $matches)
{
    $mode = [int]$match.Groups[1].Value
    $milliseconds = [double]::Parse($match.Groups[2].Value, [Globalization.CultureInfo]::InvariantCulture)
    if (!($milliseconds -gt 0) -or [double]::IsInfinity($milliseconds))
    {
        throw "Invalid transient preparation timing: mode $mode"
    }
    $rows += [ordered]@{mode=$mode; transientPrepareCpuMs=$milliseconds}
}
if ($rows.Count -ne 11 -or (@($rows.mode | Sort-Object -Unique) -join ',') -ne '0,1,2,3,4,5,6,7,8,9,10')
{
    throw 'Expected one timing for each of the eleven RG7 GPU fixture modes.'
}
$metadata = Get-Content (Join-Path $root 'binary-metadata.json') -Raw | ConvertFrom-Json
[ordered]@{passed=$true; phaseComplete=$false; binary=$metadata; samples=$rows;
    scope='Single CPU wall-time observation per native Editor GPU fixture mode; lifetime discovery, allocation/cache and resource creation';
    performanceAccepted=$false; peakVramMeasured=$false;
    limits='Modes change aliasing, lifetime extension, parallel recording, cache or poison; these samples are not a statistically controlled product speedup comparison.'} |
    ConvertTo-Json -Depth 12 | Set-Content (Join-Path $root 'preparation-result.json') -Encoding utf8
"RG7_PREPARATION_TIMING_OK $root"
