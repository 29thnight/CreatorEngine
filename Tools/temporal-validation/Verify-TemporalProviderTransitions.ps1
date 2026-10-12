[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$EndpointFile,
    [Parameter(Mandatory)][string]$OutputDirectory,
    [ValidateRange(1,600)][int]$TimeoutSeconds = 60,
    [switch]$VerifyFaultRecovery
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$OutputDirectory = [IO.Path]::GetFullPath($OutputDirectory)
if (Test-Path -LiteralPath $OutputDirectory) { throw 'Use a new evidence directory' }
New-Item -ItemType Directory -Path $OutputDirectory | Out-Null
$results = [Collections.Generic.List[object]]::new()
$faults = [Collections.Generic.List[object]]::new()
$passed = $false
try {
    # A genuine SDK dispatch must precede each teardown. Starting and finishing
    # at native rendering also verifies restoration of the saved FXAA choice.
    # Cover every directed edge among native, FSR, DLSS and XeSS, including
    # DLSS teardown/recreation. The former sequence never selected DLSS.
    foreach ($provider in @('none','fsr','dlss','xess','none','dlss','fsr','xess','dlss','none','xess','fsr','none')) {
        $directory = Join-Path $OutputDirectory ('{0:D2}-{1}' -f $results.Count, $provider)
        $timer = [Diagnostics.Stopwatch]::StartNew()
        & (Join-Path $PSScriptRoot 'Invoke-TemporalStage.ps1') -EndpointFile $EndpointFile `
            -Stage upscale -Provider $provider -Quality quality -TimeoutSeconds $TimeoutSeconds `
            -OutputDirectory $directory | Out-Null
        $stage = Get-Content (Join-Path $directory 'stage.json') -Raw | ConvertFrom-Json
        if (!$stage.passed) { throw "Provider transition failed: $provider" }
        $results.Add([ordered]@{
            provider = $provider
            elapsedMilliseconds = $timer.ElapsedMilliseconds
            generation = $stage.snapshot.observedGeneration
            realFrameId = $stage.snapshot.lastRealFrameId
            activeProvider = $stage.snapshot.activeUpscaler
            renderExtent = $stage.snapshot.renderExtent
            displayExtent = $stage.snapshot.displayExtent
        })
    }
    if ($VerifyFaultRecovery) {
        foreach ($provider in @('fsr','dlss','xess')) {
            foreach ($mode in @('capability','dispatch')) {
                $directory = Join-Path $OutputDirectory ("fault-$provider-$mode")
                $fallback = if ($provider -eq 'fsr') { 'none' } else { 'fsr' }
                & (Join-Path $PSScriptRoot 'Invoke-TemporalStage.ps1') -EndpointFile $EndpointFile `
                    -Stage fault-fallback -Provider $provider -FaultMode $mode -ExpectedFallback $fallback `
                    -TimeoutSeconds $TimeoutSeconds -OutputDirectory $directory | Out-Null
                $stage = Get-Content (Join-Path $directory 'stage.json') -Raw | ConvertFrom-Json
                if (!$stage.passed) { throw "Provider fault recovery failed: $provider/$mode" }
                $faults.Add([ordered]@{provider=$provider;mode=$mode;fallback=$fallback;passed=$true})
            }
        }
        & (Join-Path $PSScriptRoot 'Invoke-TemporalStage.ps1') -EndpointFile $EndpointFile `
            -Stage upscale -Provider none -TimeoutSeconds $TimeoutSeconds `
            -OutputDirectory (Join-Path $OutputDirectory 'final-native') | Out-Null
    }
    $passed = $true
} finally {
    [ordered]@{
        passed = $passed
        scope = 'live-submitted-AA-and-provider-teardown; no temporal pixel or display timing acceptance'
        transitions = @($results.ToArray())
        faultRecovery = @($faults.ToArray())
    } | ConvertTo-Json -Depth 8 | Set-Content (Join-Path $OutputDirectory 'transitions.json') -Encoding utf8
}

