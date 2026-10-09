[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$EndpointFile,
    [ValidateSet('discover','support','metadata','motion','motion-fixture','reset','upscale','fg','nis','deepdvc','reflex','disable','latency','fallback','fault-fallback','fg-fault-fallback','golden-baseline')]
    [string]$Stage = 'support',
    [ValidateSet('none','fsr','dlss','xess')][string]$Provider = 'fsr',
    [uint32]$InterpolatedFrameCount = 1,
    [ValidateSet('capability','dispatch','fg-capability','fg-configure','fg-prepare','fg-evaluate','fg-present','fg-final-consumption')][string]$FaultMode = 'dispatch',
    [ValidateSet('none','fsr')][string]$ExpectedFallback = 'none',
    [ValidateSet('native-aa','quality','balanced','performance','ultra-performance')][string]$Quality = 'quality',
    [ValidateSet('off','scale','sharpen')][string]$NisMode = 'scale',
    [ValidateRange(0.5,1.0)][float]$NisRenderScale = 0.77,
    [ValidateRange(0.0,1.0)][float]$NisSharpness = 0.5,
    [ValidateSet('off','on')][string]$DeepDvcMode = 'on',
    [ValidateRange(0.0,1.0)][float]$DeepDvcIntensity = 0.5,
    [ValidateRange(0.0,1.0)][float]$DeepDvcSaturationBoost = 0.25,
    [ValidateSet('off','on','on-boost')][string]$ReflexMode = 'on',
    [string]$RuntimeDirectory,
    [string]$DlssProjectId,
    [string]$PythonExecutable = 'python',
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

function Wait-SpatialFrame([string]$Generation, [uint64]$PreviousFrameId) {
    $data = Wait-TemporalGeneration $Generation
    $limit = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
    do {
        if ($data.requestState -eq 'superseded' -or [uint64]$data.requestedGeneration -ne [uint64]$Generation) {
            throw "Spatial generation $Generation was superseded before a fresh submitted frame"
        }
        if ($data.acknowledged -and $data.spatialPost.observed -and !$data.spatialPost.pending -and
            [uint64]$data.observedGeneration -eq [uint64]$Generation -and
            [uint64]$data.spatialPost.observedGeneration -eq [uint64]$Generation -and
            [uint64]$data.spatialPost.realFrameId -gt $PreviousFrameId -and
            [uint64]$data.spatialPost.realFrameId -eq [uint64]$data.lastRealFrameId -and
            [uint64]$data.renderSubmittedFrameId -eq [uint64]$data.lastRealFrameId) {
            Require-RealMetadata $data
            if ([uint64]$data.viewId -eq 0 -or [uint64]$data.sceneEpoch -eq 0) {
                throw 'Spatial observation is missing its production view/scene identity'
            }
            return $data
        }
        if ([DateTime]::UtcNow -ge $limit) { throw 'No fresh submitted spatial observation at the exact requested generation' }
        Start-Sleep -Milliseconds 100
        $data = (Invoke-TemporalCommand 'temporal.status' @($Generation)).data
    } while ($true)
}

function Format-Setting([float]$Value) {
    return $Value.ToString('R', [Globalization.CultureInfo]::InvariantCulture)
}

function Require-ExactGeneration($Data, [string]$Generation) {
    if ($Data.requestState -eq 'superseded' -or !$Data.acknowledged -or
        [uint64]$Data.requestedGeneration -ne [uint64]$Generation -or
        [uint64]$Data.observedGeneration -ne [uint64]$Generation -or
        ($Data.target -eq 'player_swapchain' -and [uint64]$Data.playerObservedGeneration -ne [uint64]$Generation)) {
        throw 'Temporal acceptance requires the exact acknowledged renderer/presenter generation'
    }
}

function Wait-SubmittedTemporal([string]$Generation, [uint64]$PreviousFrameId) {
    $data = Wait-TemporalGeneration $Generation
    $limit = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
    while (!$data.aaObserved -or [uint64]$data.lastRealFrameId -le $PreviousFrameId -or
        [uint64]$data.renderSubmittedFrameId -ne [uint64]$data.lastRealFrameId) {
        if ($data.requestState -eq 'superseded' -or [uint64]$data.requestedGeneration -ne [uint64]$Generation) {
            throw 'Temporal request was superseded before a fresh submitted observation'
        }
        if ([DateTime]::UtcNow -ge $limit) { throw 'No fresh submitted temporal frame' }
        Start-Sleep -Milliseconds 100
        $data = (Invoke-TemporalCommand 'temporal.status' @($Generation)).data
    }
    Require-ExactGeneration $data $Generation
    Require-RealMetadata $data
    return $data
}

$report = [ordered]@{ stage=$Stage; passed=$false; scope='live-diagnostic-only'; pixelAcceptance=$false;
    performanceAcceptance=$false; latencyAcceptance=$false; timestampUtc=[DateTime]::UtcNow.ToString('o') }
$ownedFaultRevision = $null
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
            foreach ($command in @('support','metadata','motion','latency','fallback','status','nis','deepdvc','reflex')) {
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
        'motion-fixture' {
            if ($TimeoutSeconds -lt 300) {
                throw 'Use -TimeoutSeconds 300 or more for the explicit shader/asset preparation and seven-route capture session'
            }
            $fixtureDirectory = [IO.Path]::GetFullPath((Join-Path $OutputDirectory 'motion-fixture'))
            $recording = Invoke-TemporalCommand 'render.temporal.motion.fixture' @($fixtureDirectory)
            $oraclePath = Join-Path $OutputDirectory 'motion-oracle.json'
            & $PythonExecutable (Join-Path $PSScriptRoot 'Compare-TemporalMotion.py') `
                --capture $fixtureDirectory --fixture (Join-Path $fixtureDirectory 'fixture.json') --output $oraclePath
            $oracleExit = $LASTEXITCODE
            if (!(Test-Path -LiteralPath $oraclePath)) { throw 'Motion oracle did not produce a report' }
            $oracle = Get-Content -LiteralPath $oraclePath -Raw | ConvertFrom-Json
            $report.motionFixture = $recording.data
            $report.motionOracle = $oracle
            $report.scope = 'native-real-submission-seven-route-motion-and-ownership-pixels'
            if ($oracleExit -ne 0 -or !$oracle.routeCoverageValidated -or $oracle.acceptanceStatus -ne 'PASS') {
                throw "Motion fixture acceptance is $($oracle.acceptanceStatus); recording completion is not PASS"
            }
            $report.pixelAcceptance = $true
            $data = $recording.data
        }
        'reset' {
            $data = Set-Temporal 'temporal.reset'
            Require-RealMetadata $data
            if ([uint64]$data.historyRevision -le [uint64]$before.historyRevision) {
                throw 'Acknowledged request did not advance production history revision'
            }
        }
        'upscale' {
            $receipt = Invoke-TemporalCommand 'temporal.upscale' @($Provider,$Quality)
            $generation = [string]$receipt.data.receiptGeneration
            $report.receiptGeneration = $generation
            $data = Wait-TemporalGeneration $generation
            $limit = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
            # Acknowledgement alone cannot reuse an older frame's provider or AA quality.
            while (!$data.aaObserved -or [uint64]$data.lastRealFrameId -le [uint64]$before.lastRealFrameId -or
                [uint64]$data.renderSubmittedFrameId -ne [uint64]$data.lastRealFrameId) {
                if ($data.requestState -eq 'superseded' -or [uint64]$data.requestedGeneration -ne [uint64]$generation) {
                    throw "Upscale generation $generation was superseded before fresh AA evidence"
                }
                if ([DateTime]::UtcNow -ge $limit) {
                    throw 'No fresh submitted real-frame AA observation for the upscale request'
                }
                Start-Sleep -Milliseconds 100
                $data = (Invoke-TemporalCommand 'temporal.status' @($generation)).data
            }
            Require-RealMetadata $data
            if (!$data.acknowledged -or $data.requestState -ne 'acknowledged' -or
                [uint64]$data.requestedGeneration -ne [uint64]$generation -or
                [uint64]$data.observedGeneration -ne [uint64]$generation -or
                [uint64]$data.viewId -eq 0 -or [uint64]$data.sceneEpoch -eq 0 -or
                $data.requestedUpscaler -ne $Provider -or $data.effectiveRequestedUpscaler -ne $Provider -or
                $data.requestedUpscaleQuality -ne $Quality -or $data.effectiveRequestedUpscaleQuality -ne $Quality) {
                throw 'Submitted AA evidence does not match the exact requested generation/provider/quality'
            }
            if ($Provider -ne 'none') {
                $nativeAaNames = @{ fsr='fsr_native_aa'; dlss='dlaa'; xess='xess_aa' }
                $expectedAa = if ($Quality -eq 'native-aa') { $nativeAaNames[$Provider] } else { $Provider + '_upscale' }
                if ($data.upscaleState -ne 'active' -or $data.activeUpscaler -ne $Provider -or
                    $data.upscaleResult.status -ne 'success' -or !$data.temporalAaApplied -or $data.fxaaApplied -or
                    $data.activeUpscaleQuality -ne $Quality -or $data.activeAaMethod -ne $expectedAa) {
                    throw "Requested temporal AA/quality was not observed: state=$($data.upscaleState), provider=$($data.activeUpscaler), AA=$($data.activeAaMethod), quality=$($data.activeUpscaleQuality)"
                }
                if ($Quality -eq 'native-aa' -and ($data.renderExtent.width -ne $data.displayExtent.width -or
                    $data.renderExtent.height -ne $data.displayExtent.height)) {
                    throw 'Native AA requires matching observed render/display extents'
                }
            } else {
                $expectedAa = if ($data.fxaaRequested) { 'fxaa' } else { 'none' }
                if ($data.activeUpscaler -ne 'none' -or $data.temporalAaApplied -or $null -ne $data.activeUpscaleQuality -or
                    $data.fxaaApplied -ne $data.fxaaRequested -or $data.activeAaMethod -ne $expectedAa) {
                    throw 'Native rendering did not resume its saved FXAA preference'
                }
            }
        }
        'fg' {
            if ($before.target -ne 'player_swapchain') { throw 'FG stage requires development Player; Editor is forbidden' }
            if ($InterpolatedFrameCount -eq 0) { throw 'InterpolatedFrameCount must be positive' }
            $data = Set-Temporal 'temporal.fg' @($Provider,[string]$InterpolatedFrameCount)
            $generation = [string]$data.requestedGeneration
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
                    $data.configuredInterpolatedFrameCount -ne $InterpolatedFrameCount -or
                    $data.activeInterpolatedFrameCount -ne $InterpolatedFrameCount -or
                    [uint64]$data.$counter -le [uint64]$before.$counter) {
                    if ([string]$data.requestedGeneration -ne $generation -or $data.requestState -eq 'superseded') {
                        throw 'Frame-generation request was superseded'
                    }
                    if ([DateTime]::UtcNow -ge $limit) {
                        throw "No observed generated output/presentation evidence: $($data.frameGenerationState)"
                    }
                    Start-Sleep -Milliseconds 100
                    $data = (Invoke-TemporalCommand 'temporal.status' @($generation)).data
                }
                Require-ExactGeneration $data $generation
                if ($data.requestedInterpolatedFrameCount -ne $InterpolatedFrameCount -or
                    $data.effectiveRequestedInterpolatedFrameCount -ne $InterpolatedFrameCount) {
                    throw 'Observed MFG count does not match the requested count'
                }
                $report.interpolatedFrameCount = $InterpolatedFrameCount
            }
        }
        'nis' {
            $receipt = Invoke-TemporalCommand 'temporal.nis' @($NisMode,(Format-Setting $NisRenderScale),(Format-Setting $NisSharpness))
            $generation = [string]$receipt.data.receiptGeneration
            $report.receiptGeneration = $generation
            $data = Wait-SpatialFrame $generation ([uint64]$before.lastRealFrameId)
            $spatial = $data.spatialPost
            if ($spatial.requestedNisMode -ne $NisMode -or $spatial.effectiveRequestedNisMode -ne $NisMode -or
                [Math]::Abs([double]$spatial.nisRenderScale - $NisRenderScale) -gt 0.00001 -or
                [Math]::Abs([double]$spatial.nisSharpness - $NisSharpness) -gt 0.00001 -or
                [Math]::Abs([double]$spatial.submittedNisRenderScale - $NisRenderScale) -gt 0.00001 -or
                [Math]::Abs([double]$spatial.submittedNisSharpness - $NisSharpness) -gt 0.00001) {
                throw 'Submitted NIS tuning does not match the staged request'
            }
            $expectedMode = if ($NisMode -eq 'scale' -and ($data.selectedUpscaler -ne 'none' -or $NisRenderScale -eq 1.0)) { 'sharpen' } else { $NisMode }
            if ($spatial.submittedNisMode -ne $expectedMode -or $spatial.selectedNisMode -ne $expectedMode -or
                $spatial.activeNisMode -ne $expectedMode) {
                throw "NIS did not execute the expected mode without duplicate scaling: expected=$expectedMode, selected=$($spatial.selectedNisMode), active=$($spatial.activeNisMode)"
            }
            if ($NisMode -ne 'off') {
                if ($spatial.nisCapability.status -ne 'success' -or $spatial.nisResult.status -ne 'success' -or
                    $spatial.outputExtent.width -ne $data.displayExtent.width -or
                    $spatial.outputExtent.height -ne $data.displayExtent.height) {
                    throw 'NIS support/evaluation/final-resolution output was not observed'
                }
                if ($expectedMode -eq 'sharpen' -and ($spatial.inputExtent.width -ne $spatial.outputExtent.width -or
                    $spatial.inputExtent.height -ne $spatial.outputExtent.height)) {
                    throw 'NIS sharpen-only must preserve its input extent'
                }
                if ($expectedMode -eq 'scale' -and ($spatial.inputExtent.width -gt $spatial.outputExtent.width -or
                    $spatial.inputExtent.height -gt $spatial.outputExtent.height -or
                    ($NisRenderScale -lt 1.0 -and $spatial.inputExtent.width -eq $spatial.outputExtent.width -and
                        $spatial.inputExtent.height -eq $spatial.outputExtent.height))) {
                    throw 'NIS scaling did not observe the expected lower-resolution input'
                }
            }
            $report.scope = 'submitted-spatial-accepted-dispatch-only-no-pixels-compared'
        }
        'deepdvc' {
            $receipt = Invoke-TemporalCommand 'temporal.deepdvc' @($DeepDvcMode,(Format-Setting $DeepDvcIntensity),(Format-Setting $DeepDvcSaturationBoost))
            $generation = [string]$receipt.data.receiptGeneration
            $report.receiptGeneration = $generation
            $data = Wait-SpatialFrame $generation ([uint64]$before.lastRealFrameId)
            $spatial = $data.spatialPost
            $enabled = $DeepDvcMode -eq 'on'
            if ($spatial.deepDvcRequested -ne $enabled -or $spatial.deepDvcEffectiveRequested -ne $enabled -or
                $spatial.submittedDeepDvcEnabled -ne $enabled -or $spatial.deepDvcApplied -ne $enabled -or
                [Math]::Abs([double]$spatial.deepDvcIntensity - $DeepDvcIntensity) -gt 0.00001 -or
                [Math]::Abs([double]$spatial.deepDvcSaturationBoost - $DeepDvcSaturationBoost) -gt 0.00001 -or
                [Math]::Abs([double]$spatial.submittedDeepDvcIntensity - $DeepDvcIntensity) -gt 0.00001 -or
                [Math]::Abs([double]$spatial.submittedDeepDvcSaturationBoost - $DeepDvcSaturationBoost) -gt 0.00001) {
                throw 'DeepDVC did not observe the exact requested state/tuning'
            }
            if ($enabled -and (!$spatial.deepDvcSelected -or !$spatial.sdrEligible -or
                $spatial.deepDvcCapability.status -ne 'success' -or $spatial.deepDvcResult.status -ne 'success' -or
                $spatial.outputExtent.width -ne $data.displayExtent.width -or
                $spatial.outputExtent.height -ne $data.displayExtent.height)) {
                throw 'DeepDVC requires an accepted SDR-only final-resolution pre-UI SDK dispatch'
            }
            $report.scope = 'submitted-sdr-accepted-dispatch-only-no-internal-evaluation-or-pixel-proof'
        }
        'reflex' {
            if ($before.target -ne 'player_swapchain') { throw 'Reflex execution stage requires development DX12 Player' }
            $receipt = Invoke-TemporalCommand 'temporal.reflex' @($ReflexMode)
            $generation = [string]$receipt.data.receiptGeneration
            $report.receiptGeneration = $generation
            $data = Wait-TemporalGeneration $generation
            # A final frame from the prior mode can complete after the pre-request snapshot.
            # Require API evidence newer than the acknowledged mode's own baseline.
            $reflexBaseline = $data.reflex
            $limit = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
            do {
                if ($data.requestState -eq 'superseded' -or [uint64]$data.requestedGeneration -ne [uint64]$generation) {
                    throw "Reflex generation $generation was superseded"
                }
                $reflex = $data.reflex
                $expectedMode = if ($ReflexMode -eq 'off' -and $reflex.requiredByFrameGeneration) { 'on' } else { $ReflexMode }
                $observedMode = $data.acknowledged -and !$reflex.pending -and
                    [uint64]$data.playerObservedGeneration -eq [uint64]$generation -and
                    [uint64]$reflex.observedGeneration -eq [uint64]$generation -and
                    $data.requestedReflexMode -eq $ReflexMode -and $reflex.requestedMode -eq $ReflexMode -and
                    $reflex.effectiveMode -eq $expectedMode
                if ($observedMode -and $expectedMode -eq 'off') { break }
                if ($observedMode -and $reflex.configured -and $reflex.support.status -eq 'success' -and
                    $reflex.sleepSupport.status -eq 'success' -and $reflex.markerSupport.status -eq 'success' -and
                    $reflex.optionsResult.status -eq 'success' -and
                    [uint64]$reflex.markerRealFrameId -gt [uint64]$reflexBaseline.markerRealFrameId -and
                    [uint64]$reflex.sleepRealFrameId -gt [uint64]$reflexBaseline.sleepRealFrameId -and
                    [uint64]$reflex.presentedRealFrameId -gt [uint64]$reflexBaseline.presentedRealFrameId) { break }
                if ([DateTime]::UtcNow -ge $limit) { throw 'No fresh successful Reflex sleep/marker/presentation observations for the requested mode' }
                Start-Sleep -Milliseconds 100
                $data = (Invoke-TemporalCommand 'temporal.status' @($generation)).data
            } while ($true)
            $report.scope = 'reflex-api-observations-only-not-input-to-photon'
            $report.fgRequiredModeOverride = [bool]$data.reflex.requiredByFrameGeneration
        }
        'disable' {
            Set-Temporal 'temporal.fg' @('none') | Out-Null
            Set-Temporal 'temporal.nis' @('off') | Out-Null
            Set-Temporal 'temporal.deepdvc' @('off') | Out-Null
            $data = Set-Temporal 'temporal.upscale' @('none')
            if ($data.activeUpscaler -ne 'none' -or $data.activeFrameGenerator -ne 'none' -or
                $data.spatialPost.activeNisMode -ne 'off' -or $data.spatialPost.deepDvcApplied) { throw 'Image feature remains active' }
        }
        'golden-baseline' {
            if ($before.faultInjection.mode -ne 'none') { throw 'Golden baseline cannot run while a developer fault is armed' }
            Set-Temporal 'temporal.fg' @('none') | Out-Null
            Set-Temporal 'temporal.nis' @('off') | Out-Null
            Set-Temporal 'temporal.deepdvc' @('off') | Out-Null
            $receipt = Invoke-TemporalCommand 'temporal.upscale' @('none')
            $data = Wait-SpatialFrame ([string]$receipt.data.receiptGeneration) ([uint64]$before.lastRealFrameId)
            Require-RealMetadata $data
            if ($data.faultInjection.mode -ne 'none' -or $data.activeUpscaler -ne 'none' -or $data.activeFrameGenerator -ne 'none' -or
                $data.spatialPost.activeNisMode -ne 'off' -or $data.spatialPost.deepDvcApplied -or
                $data.renderExtent.width -ne $data.displayExtent.width -or
                $data.renderExtent.height -ne $data.displayExtent.height) {
                throw 'Golden baseline must use actual native-resolution real frames with TU/FG/NIS/DeepDVC disabled'
            }
            $report.scope = 'golden-preflight-only-no-pixels-compared'
        }
        'latency' {
            $data = (Invoke-TemporalCommand 'temporal.latency').data
            $limit = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
            $latencyNames = @{ fsr='anti-lag-2'; dlss='reflex'; xess='xell' }
            $independentReflex = $before.reflex.configured -and $before.reflex.effectiveMode -ne 'off'
            $expectedLatency = if ($independentReflex) { 'reflex' } else { $latencyNames[[string]$before.selectedFrameGenerator] }
            if (!$expectedLatency) { throw 'No configured independent Reflex or selected frame-generation latency provider' }
            while ([uint64]$data.latencyMarkerRealFrameId -le [uint64]$before.latencyMarkerRealFrameId -or
                $data.latencyResult.status -ne 'success' -or $data.latencyProvider -ne $expectedLatency -or
                (!$independentReflex -and $data.activeFrameGenerator -ne $before.selectedFrameGenerator) -or
                ($independentReflex -and (!$data.reflex.configured -or $data.reflex.effectiveMode -ne $before.reflex.effectiveMode))) {
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
        'fg-fault-fallback' {
            if (!$before.faultInjection.available -or $before.faultInjection.mode -ne 'none' -or
                !$before.playerObserved -or $before.observedPresentationTarget -ne 'player_swapchain') {
                throw 'An unfaulted development DX12 Player with live presenter observations is required'
            }
            $codes = @{'fg-capability'=-45003; 'fg-configure'=-45004; 'fg-prepare'=-45005;
                'fg-evaluate'=-45006; 'fg-present'=-45007; 'fg-final-consumption'=-45008}
            if (!$codes.ContainsKey($FaultMode) -or $Provider -eq 'none' -or $ExpectedFallback -eq $Provider -or
                ($FaultMode -eq 'fg-evaluate' -and $Provider -ne 'fsr')) {
                throw 'Choose a supported FG fault stage and a different expected fallback; fg-evaluate is FSR-only'
            }
            $receipt = Invoke-TemporalCommand 'temporal.fg' @($Provider,[string]$InterpolatedFrameCount)
            $baselineGeneration = [string]$receipt.data.receiptGeneration
            $counter = if ($Provider -eq 'fsr') { 'generatedSubmissionCount' } else { 'generatedPresentationCount' }
            $limit = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
            do {
                $baseline = (Invoke-TemporalCommand 'temporal.status' @($baselineGeneration)).data
                if ($baseline.requestState -eq 'superseded') { throw 'FG baseline was superseded' }
                $present = $baseline.presenterObservation
                $ready = $baseline.acknowledged -and $baseline.activeFrameGenerator -eq $Provider -and
                    $baseline.frameGenerationResult.status -eq 'success' -and $present.valid -and
                    $present.result.status -eq 'success' -and $present.provider -eq $Provider -and
                    [string]$present.requestGeneration -eq $baselineGeneration -and
                    [string]$present.playerObservedGeneration -eq $baselineGeneration -and
                    $baseline.activeInterpolatedFrameCount -eq $InterpolatedFrameCount -and
                    [uint64]$baseline.$counter -gt [uint64]$before.$counter
                if ($ready) { break }
                if ([DateTime]::UtcNow -ge $limit) { throw 'No real generated-output baseline; SDK absence is not a fault pass' }
                Start-Sleep -Milliseconds 100
            } while ($true)
            $receipt = Invoke-TemporalCommand 'temporal.fault' @($FaultMode,$Provider,[string]$present.viewId)
            $generation = [string]$receipt.data.receiptGeneration
            $ownedFaultRevision = [string]$receipt.data.faultInjection.revision
            $limit = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
            do {
                $data = (Invoke-TemporalCommand 'temporal.status' @($generation)).data
                if ($data.requestState -eq 'superseded') { throw 'FG fault request was superseded' }
                $fault = $data.faultInjection
                $fallback = $data.presenterObservation
                $ready = $data.acknowledged -and [string]$fault.consumedRevision -eq $ownedFaultRevision -and
                    $fault.consumedMode -eq $FaultMode -and [uint64]$fault.consumedCount -gt [uint64]$baseline.faultInjection.consumedCount -and
                    [string]$fault.viewId -eq [string]$present.viewId -and [string]$fault.sceneEpoch -eq [string]$present.sceneEpoch -and
                    $fault.injectedResult.status -eq 'setup_or_execution_failed' -and $fault.injectedResult.nativeCode -eq $codes[$FaultMode] -and
                    $data.requestedFrameGenerator -eq $Provider -and $data.requestedFrameGenerationResult.nativeCode -eq $codes[$FaultMode] -and
                    $fallback.valid -and $fallback.result.status -eq 'success' -and $fallback.provider -eq $ExpectedFallback -and
                    [string]$fallback.requestGeneration -eq $generation -and [string]$fallback.playerObservedGeneration -eq $generation -and
                    [string]$fallback.viewId -eq [string]$present.viewId -and [string]$fallback.sceneEpoch -eq [string]$present.sceneEpoch -and
                    [uint64]$fallback.realFrameId -gt [uint64]$present.realFrameId -and
                    [uint64]$fallback.realFrameId -gt [uint64]$fault.realFrameId -and
                    $fallback.faultMode -eq $FaultMode -and [string]$fallback.faultRevision -eq $ownedFaultRevision
                if ($ready -and $ExpectedFallback -eq 'fsr') {
                    $ready = $data.activeFrameGenerator -eq 'fsr' -and
                        [uint64]$data.sdkFinalConsumedFrameId -ge [uint64]$fallback.realFrameId -and
                        [uint64]$data.generatedSubmissionCount -gt [uint64]$baseline.generatedSubmissionCount
                }
                if ($ready) { break }
                if ([DateTime]::UtcNow -ge $limit) { throw 'No exact consumed-stage failure followed by a fresh successful fallback Present' }
                Start-Sleep -Milliseconds 100
            } while ($true)
            if ($FaultMode -notin @('fg-capability','fg-configure') -and [uint64]$fault.realFrameId -eq 0) {
                throw 'Frame-stage fault did not identify its actual source real frame'
            }
            $report.scope = 'live-FG-host-failure-recovery-only-not-device-loss-pixel-display-or-lifetime-race-acceptance'
            $report.faultEvidence = $fault
            $report.baseline = $baseline
            $report.finalConsumptionSemantics = 'post-genuine-drain acknowledgement rejection; no manufactured fence completion'
        }
        'fault-fallback' {
            if ($FaultMode -notin @('capability','dispatch')) { throw 'TU fault stage accepts only capability or dispatch' }
            if (!$before.faultInjection.available -or $before.faultInjection.mode -ne 'none') {
                throw 'A development build with no already-active test injection is required'
            }
            if ($Provider -eq 'none' -or $ExpectedFallback -eq $Provider) {
                throw 'Fault stage requires a concrete provider and a different expected fallback'
            }
            $receipt = Invoke-TemporalCommand 'temporal.upscale' @($Provider,$Quality)
            $baseline = Wait-SubmittedTemporal ([string]$receipt.data.receiptGeneration) ([uint64]$before.lastRealFrameId)
            if ($baseline.activeUpscaler -ne $Provider -or !$baseline.temporalAaApplied -or
                $baseline.upscaleResult.status -ne 'success') {
                throw 'Requested provider must actually reconstruct before injection; unavailable SDK is not a fault pass'
            }
            $receipt = Invoke-TemporalCommand 'temporal.fault' @($FaultMode,$Provider,[string]$baseline.viewId)
            $generation = [string]$receipt.data.receiptGeneration
            $ownedFaultRevision = [string]$receipt.data.faultInjection.revision
            $data = Wait-SubmittedTemporal $generation ([uint64]$baseline.lastRealFrameId)
            $limit = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
            while ([string]$data.faultInjection.consumedRevision -ne $ownedFaultRevision -or
                [uint64]$data.faultInjection.consumedCount -le [uint64]$baseline.faultInjection.consumedCount -or
                [string]$data.viewId -ne [string]$baseline.viewId -or
                [string]$data.sceneEpoch -ne [string]$baseline.sceneEpoch -or
                $data.activeUpscaler -ne $ExpectedFallback) {
                if ($data.requestState -eq 'superseded' -or [string]$data.requestedGeneration -ne $generation) {
                    throw 'Fault stage request was superseded'
                }
                if ([DateTime]::UtcNow -ge $limit) { throw 'Injected failure was not consumed followed by the expected live fallback' }
                Start-Sleep -Milliseconds 100
                $data = (Invoke-TemporalCommand 'temporal.status' @($generation)).data
            }
            Require-ExactGeneration $data $generation
            $expectedCode = if ($FaultMode -eq 'capability') { -45001 } else { -45002 }
            if ($data.requestedUpscaler -ne $Provider -or $data.requestedUpscaleResult.nativeCode -ne $expectedCode -or
                [string]$data.faultInjection.viewId -ne [string]$baseline.viewId -or
                [string]$data.faultInjection.sceneEpoch -ne [string]$baseline.sceneEpoch -or
                [uint64]$data.lastRealFrameId -le [uint64]$baseline.lastRealFrameId -or
                [uint64]$data.renderSubmittedFrameId -ne [uint64]$data.lastRealFrameId -or
                ($FaultMode -eq 'dispatch' -and ([uint64]$data.faultInjection.realFrameId -eq 0 -or
                    [uint64]$data.lastRealFrameId -le [uint64]$data.faultInjection.realFrameId))) {
                throw 'Fallback lacks the injected failure code or a later successfully submitted real frame'
            }
            if ($ExpectedFallback -eq 'none') {
                if ($data.temporalAaApplied -or $data.fxaaApplied -ne $data.fxaaRequested -or
                    $data.renderExtent.width -ne $data.displayExtent.width -or
                    $data.renderExtent.height -ne $data.displayExtent.height) {
                    throw 'Native fault fallback did not restore display extents and the saved FXAA preference'
                }
            } elseif (!$data.temporalAaApplied -or $data.fxaaApplied -or $data.upscaleResult.status -ne 'success') {
                throw 'FSR fallback was selected but did not successfully reconstruct the submitted frame'
            }
            $report.scope = 'live-injected-TU-fallback-submission-only-no-pixel-or-FG-fault-acceptance'
            $report.faultEvidence = $data.faultInjection
            $report.requestedFailureCode = $expectedCode
        }
    }
    $report.snapshot = $data
    $report.passed = $true
} catch {
    $report.error = $_.Exception.Message
    throw
} finally {
    if ($null -ne $ownedFaultRevision) {
        try {
            $current = (Invoke-TemporalCommand 'temporal.status').data
            if ([string]$current.faultInjection.revision -eq $ownedFaultRevision) {
                $report.faultCleanup = Set-Temporal 'temporal.fault' @('clear')
                if ($report.faultCleanup.faultInjection.mode -ne 'none') { throw 'Fault injection remained active after clear' }
            } else {
                $report.faultCleanup = 'not-cleared-because-another-request-replaced-the-owned-fault'
                $report.passed = $false
            }
        } catch {
            $report.passed = $false
            $report.faultCleanupError = $_.Exception.Message
            $report | ConvertTo-Json -Depth 40 | Set-Content (Join-Path $OutputDirectory 'stage.json') -Encoding utf8
            throw
        }
    }
    $report | ConvertTo-Json -Depth 40 | Set-Content (Join-Path $OutputDirectory 'stage.json') -Encoding utf8
}
$report | ConvertTo-Json -Depth 40
if (!$report.passed) { throw 'Temporal stage did not satisfy its declared scope' }
