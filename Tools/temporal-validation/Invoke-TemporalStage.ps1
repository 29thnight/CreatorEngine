[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$EndpointFile,
    [ValidateSet('discover','support','metadata','motion','reset','upscale','fg','disable','latency','fallback','golden-baseline')]
    [string]$Stage = 'support',
    [ValidateSet('none','fsr','dlss','xess')][string]$Provider = 'fsr',
    [ValidateSet('native-aa','quality','balanced','performance','ultra-performance')][string]$Quality = 'quality',
    [string]$RuntimeDirectory,
    [string]$DlssProjectId,
    [ValidateRange(1,600)][int]$TimeoutSeconds = 60,
    [string]$OutputDirectory = (Join-Path $PSScriptRoot ('results/' + (Get-Date -Format 'yyyyMMdd-HHmmss-fff')))
)

# Authored for later authorized execution. This script never builds or launches an engine.
# Attach to an already running Editor or development Player command service.
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$endpoint = Get-Content -LiteralPath $EndpointFile -Raw | ConvertFrom-Json
if ([int]$endpoint.port -lt 1 -or [int]$endpoint.port -gt 65535 -or !$endpoint.token) {
    throw 'Invalid command-service endpoint'
}
$base = 'http://127.0.0.1:' + [int]$endpoint.port
$headers = @{ Authorization = 'Bearer ' + $endpoint.token }
$session = [Microsoft.PowerShell.Commands.WebRequestSession]::new()
New-Item -ItemType Directory -Path $OutputDirectory -ErrorAction Stop | Out-Null
$script:sequence = 0

function Invoke-TemporalCommand([string]$Name, [string[]]$Arguments = @()) {
    $script:sequence++
    $body = @{ command=$Name; args=@($Arguments); mode='sync'; correlationId="temporal-$script:sequence" } | ConvertTo-Json -Compress
    $result = Invoke-RestMethod "$base/command" -Method Post -Headers $headers -WebSession $session `
        -ContentType 'application/json' -Body $body -TimeoutSec $TimeoutSeconds
    # A command's HTTP operation may complete before its render/present generation.
    if ($result.PSObject.Properties['operationId']) {
        $poll = [string]$result.poll
        if (!$poll.StartsWith('/operations/')) { throw 'Unexpected operation poll route' }
        $limit = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
        do {
            Start-Sleep -Milliseconds 100
            $result = Invoke-RestMethod "$base$poll" -Headers $headers -WebSession $session -TimeoutSec $TimeoutSeconds
            if ([DateTime]::UtcNow -ge $limit) { throw "$Name command operation timed out" }
        } until ($result.state -eq 'completed')
    }
    @{command=$Name; arguments=$Arguments; result=$result} | ConvertTo-Json -Depth 40 -Compress |
        Add-Content -LiteralPath (Join-Path $OutputDirectory 'responses.jsonl') -Encoding utf8
    if ($result.status -ne 'succeeded') {
        throw "$Name failed: $($result.code) $($result.message)"
    }
    return $result
}

function Wait-TemporalGeneration([string]$Generation) {
    $limit = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
    do {
        $result = Invoke-TemporalCommand 'temporal.status' @($Generation)
        if ($result.data.requestState -eq 'superseded') { throw "Generation $Generation was superseded by another request" }
        if ($result.data.acknowledged) { return $result.data }
        if ([DateTime]::UtcNow -ge $limit) { throw "Live consumers did not acknowledge generation $Generation" }
        Start-Sleep -Milliseconds 100
    } while ($true)
}

function Set-Temporal([string]$Command, [string[]]$Arguments = @()) {
    $receipt = Invoke-TemporalCommand $Command $Arguments
    return Wait-TemporalGeneration ([string]$receipt.data.receiptGeneration)
}

function Require-RealMetadata($Data) {
    if (!$Data.rendererObserved -or [uint64]$Data.lastRealFrameId -eq 0 -or $Data.frameKind -ne 'real' -or
        $Data.renderExtent.width -le 0 -or $Data.renderExtent.height -le 0 -or
        $Data.displayExtent.width -le 0 -or $Data.displayExtent.height -le 0) {
        throw 'Production real-frame identity/extents are missing; no metadata pass'
    }
}

$report = [ordered]@{ stage=$Stage; passed=$false; scope='live-diagnostic-only'; pixelAcceptance=$false;
    performanceAcceptance=$false; latencyAcceptance=$false; timestampUtc=[DateTime]::UtcNow.ToString('o') }
try {
    $discovery = Invoke-RestMethod "$base/commands" -Headers $headers -WebSession $session -TimeoutSec $TimeoutSeconds
    $discovery | ConvertTo-Json -Depth 30 | Set-Content (Join-Path $OutputDirectory 'commands.json') -Encoding utf8
    if ($RuntimeDirectory) {
        $arguments = @($RuntimeDirectory)
        if ($DlssProjectId) { $arguments += $DlssProjectId }
        Set-Temporal 'temporal.runtime' $arguments | Out-Null
    } elseif ($DlssProjectId) {
        throw 'DlssProjectId requires RuntimeDirectory'
    }
    $before = (Invoke-TemporalCommand 'temporal.status').data
    $data = $before
    switch ($Stage) {
        'discover' {
            # The actual registries are written above; invoke every read-only surface to expose missing registration.
            foreach ($command in @('support','metadata','motion','latency','fallback','status')) {
                Invoke-TemporalCommand "temporal.$command" | Out-Null
            }
        }
        'support' { $data = (Invoke-TemporalCommand 'temporal.support').data }
        'metadata' { $data = (Invoke-TemporalCommand 'temporal.metadata').data; Require-RealMetadata $data }
        'motion' {
            $data = (Invoke-TemporalCommand 'temporal.motion').data
            Require-RealMetadata $data
            foreach ($path in @('static','skinned','instanced','decal','alpha')) {
                if (!$data.motionCoverage.$path) { throw "Production motion coverage is missing for $path" }
            }
            if (!$data.motionVectorsValid) { throw 'Production motion inputs are not valid' }
        }
        'reset' {
            $data = Set-Temporal 'temporal.reset'
            Require-RealMetadata $data
            if ([uint64]$data.historyRevision -le [uint64]$before.historyRevision) {
                throw 'Acknowledged request did not advance production history revision'
            }
        }
        'upscale' {
            $data = Set-Temporal 'temporal.upscale' @($Provider,$Quality)
            Require-RealMetadata $data
            if ($Provider -ne 'none' -and ($data.upscaleState -ne 'active' -or $data.activeUpscaler -ne $Provider)) {
                throw "Requested upscaler is not active: $($data.upscaleState), effective=$($data.activeUpscaler)"
            }
            if ($Provider -eq 'none' -and $data.activeUpscaler -ne 'none') { throw 'Upscaler remains active' }
        }
        'fg' {
            if ($before.target -ne 'player_swapchain') { throw 'FG stage requires development Player; Editor is forbidden' }
            $data = Set-Temporal 'temporal.fg' @($Provider)
            if ($Provider -eq 'none') {
                if ($data.activeFrameGenerator -ne 'none') { throw 'Frame generator remains active' }
            } else {
                $limit = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
                # FSR exposes completed SDK output, not a native presentation
                # statistics API. Keep that evidence separate rather than guess.
                $counter = if ($Provider -eq 'fsr') { 'generatedSubmissionCount' } else { 'generatedPresentationCount' }
                $report.generatedEvidence = if ($Provider -eq 'fsr') { 'sdk-output-completed' } else { 'sdk-reported-presentations' }
                $report.presentationAcceptance = $false
                while ($data.activeFrameGenerator -ne $Provider -or
                    [uint64]$data.$counter -le [uint64]$before.$counter) {
                    if ([DateTime]::UtcNow -ge $limit) {
                        throw "No observed generated output/presentation evidence: $($data.frameGenerationState)"
                    }
                    Start-Sleep -Milliseconds 100
                    $data = (Invoke-TemporalCommand 'temporal.status').data
                }
            }
        }
        'disable' {
            Set-Temporal 'temporal.fg' @('none') | Out-Null
            $data = Set-Temporal 'temporal.upscale' @('none')
            if ($data.activeUpscaler -ne 'none' -or $data.activeFrameGenerator -ne 'none') { throw 'Temporal feature remains active' }
        }
        'golden-baseline' {
            Set-Temporal 'temporal.fg' @('none') | Out-Null
            $data = Set-Temporal 'temporal.upscale' @('none')
            Require-RealMetadata $data
            if ($data.activeUpscaler -ne 'none' -or $data.activeFrameGenerator -ne 'none' -or
                $data.renderExtent.width -ne $data.displayExtent.width -or
                $data.renderExtent.height -ne $data.displayExtent.height) {
                throw 'Golden baseline must use actual native-resolution real frames with TU/FG disabled'
            }
            $report.scope = 'golden-preflight-only-no-pixels-compared'
        }
        'latency' {
            $data = (Invoke-TemporalCommand 'temporal.latency').data
            $limit = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
            $latencyNames = @{ fsr='anti-lag-2'; dlss='reflex'; xess='xell' }
            $expectedLatency = $latencyNames[[string]$before.selectedFrameGenerator]
            if (!$expectedLatency) { throw 'No selected frame-generation latency provider' }
            while ([uint64]$data.latencyMarkerRealFrameId -le [uint64]$before.latencyMarkerRealFrameId -or
                $data.latencyResult.status -ne 'success' -or $data.latencyProvider -ne $expectedLatency -or
                $data.activeFrameGenerator -ne $before.selectedFrameGenerator) {
                if ($data.requestedGeneration -ne $before.requestedGeneration) { throw 'Latency stage settings were superseded' }
                if ([DateTime]::UtcNow -ge $limit) { throw 'No new successful real-frame latency marker observed' }
                Start-Sleep -Milliseconds 100
                $data = (Invoke-TemporalCommand 'temporal.latency').data
            }
            $report.scope = 'new-marker-only-not-input-to-photon'
        }
        'fallback' {
            $data = (Invoke-TemporalCommand 'temporal.fallback').data
            Require-RealMetadata $data
            $report.scope = 'fallback-diagnostic-only-no-fault-injected'
        }
    }
    $report.snapshot = $data
    $report.passed = $true
} catch {
    $report.error = $_.Exception.Message
    throw
} finally {
    $report | ConvertTo-Json -Depth 40 | Set-Content (Join-Path $OutputDirectory 'stage.json') -Encoding utf8
}
$report | ConvertTo-Json -Depth 40
