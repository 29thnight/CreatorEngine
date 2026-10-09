[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$EndpointFile,
    [Parameter(Mandatory)][string]$OutputDirectory,
    [ValidateRange(3,120)][int]$Seconds = 30,
    [ValidateRange(5,1000)][int]$PollMilliseconds = 20,
    [ValidateRange(5,600)][int]$TimeoutSeconds = 180
)

# Authored, unexecuted. Attach to a separately authorized running development
# Player. This records engine provenance/observations, not physical latency.
# Run the external instrument separately; retain its untouched raw export.
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$endpoint = Get-Content -LiteralPath $EndpointFile -Raw | ConvertFrom-Json
if ([int]$endpoint.port -lt 1 -or [int]$endpoint.port -gt 65535 -or !$endpoint.token) {
    throw 'Invalid command-service endpoint'
}
$OutputDirectory = [IO.Path]::GetFullPath($OutputDirectory)
if (Test-Path -LiteralPath $OutputDirectory) { throw 'Evidence output must be a new directory' }
New-Item -ItemType Directory -Path $OutputDirectory | Out-Null
$base = 'http://127.0.0.1:' + [int]$endpoint.port
$headers = @{ Authorization = 'Bearer ' + $endpoint.token }
$session = [Microsoft.PowerShell.Commands.WebRequestSession]::new()
$responses = Join-Path $OutputDirectory 'presenter-responses.jsonl'
$profile = Join-Path $OutputDirectory 'actual.ceprof'
$recording = $false
$report = [ordered]@{ schema='temporal.observation-capture.v1'; complete=$false;
    physicalLatencyMeasured=$false; displayIntervalsMeasured=$false; pollMilliseconds=$PollMilliseconds;
    note='Sampled actual presenter observations; missing frame identities are unavailable, not interpolated' }

function Invoke-EvidenceCommand([string]$Command, [string[]]$Arguments = @()) {
    $body = @{ command=$Command; args=@($Arguments); mode='sync' } | ConvertTo-Json -Compress
    $result = Invoke-RestMethod "$base/command" -Method Post -Headers $headers -WebSession $session `
        -ContentType 'application/json' -Body $body -TimeoutSec $TimeoutSeconds
    if ($result.PSObject.Properties['operationId']) {
        if (!$result.poll.StartsWith('/operations/')) { throw 'Unexpected command poll route' }
        $poll = [string]$result.poll
        $limit = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
        do {
            Start-Sleep -Milliseconds 50
            $result = Invoke-RestMethod "$base$poll" -Headers $headers -WebSession $session -TimeoutSec $TimeoutSeconds
            if ([DateTime]::UtcNow -ge $limit) { throw "$Command operation timeout" }
        } until ($result.state -eq 'completed')
    }
    @{command=$Command; arguments=$Arguments; result=$result} | ConvertTo-Json -Depth 40 -Compress |
        Add-Content -LiteralPath $responses -Encoding utf8
    if ($result.status -ne 'succeeded') { throw "$Command failed: $($result.code) $($result.message)" }
    return $result.data
}

function Wait-EvidenceRecording([string]$State) {
    $limit = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
    do {
        $data = Invoke-EvidenceCommand 'profile.save' @('status')
        if ($data.recording.state -eq 'failed') { throw 'Profile writer failed' }
        if ([DateTime]::UtcNow -ge $limit) { throw "Profile writer did not reach $State" }
        if ($data.recording.state -ne $State) { Start-Sleep -Milliseconds 50 }
    } until ($data.recording.state -eq $State)
    return $data
}

try {
    $before = Invoke-EvidenceCommand 'temporal.status'
    if ($before.target -ne 'player_swapchain' -or $before.faultInjection.mode -ne 'none' -or
        !$before.playerObserved -or !$before.rendererObserved -or $before.nativeCaptureExclusionActive) {
        throw 'A running non-faulted development Player with submitted renderer/presenter state is required'
    }
    $generation = [string]$before.requestedGeneration
    Invoke-EvidenceCommand 'profile.record' | Out-Null
    $recording = $true
    Wait-EvidenceRecording 'recording' | Out-Null
    $timer = [Diagnostics.Stopwatch]::StartNew()
    while ($timer.Elapsed.TotalSeconds -lt $Seconds) {
        $data = Invoke-EvidenceCommand 'temporal.status' @($generation)
        if ($data.requestState -eq 'superseded' -or [string]$data.requestedGeneration -ne $generation -or
            $data.faultInjection.mode -ne 'none' -or $data.nativeCaptureExclusionActive) {
            throw 'Controls changed or a capture/fault override contaminated the interval'
        }
        Start-Sleep -Milliseconds $PollMilliseconds
    }
    Invoke-EvidenceCommand 'profile.pause' | Out-Null
    Wait-EvidenceRecording 'finalized' | Out-Null
    $recording = $false
    $saved = Invoke-EvidenceCommand 'profile.save' @($profile)
    $limit = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
    while ($saved.saveState -ne 'saved') {
        if ($saved.saveState -eq 'failed' -or [DateTime]::UtcNow -ge $limit) { throw 'Profile save failed or timed out' }
        Start-Sleep -Milliseconds 50
        $saved = Invoke-EvidenceCommand 'profile.save' @('status')
    }
    if (!$saved.complete -or $saved.frames -le 0) { throw 'Saved recording is incomplete or empty' }
    $report.complete = $true
    $report.requestGeneration = $generation
    $report.profile = @{ path='actual.ceprof'; sha256=(Get-FileHash -LiteralPath $profile -Algorithm SHA256).Hash.ToLowerInvariant() }
    $report.presenterObservations = @{ path='presenter-responses.jsonl'; sha256=(Get-FileHash -LiteralPath $responses -Algorithm SHA256).Hash.ToLowerInvariant() }
} catch {
    $report.error = $_.Exception.Message
    throw
} finally {
    if ($recording) {
        try {
            Wait-EvidenceRecording 'recording' | Out-Null
            Invoke-EvidenceCommand 'profile.pause' | Out-Null
            Wait-EvidenceRecording 'finalized' | Out-Null
        } catch {
            $report.cleanupError = $_.Exception.Message
        }
    }
    $report | ConvertTo-Json -Depth 10 | Set-Content (Join-Path $OutputDirectory 'capture.json') -Encoding utf8
}
$report | ConvertTo-Json -Depth 10
