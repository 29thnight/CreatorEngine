[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$EndpointFile,
    [Parameter(Mandatory)][string]$OutputDirectory,
    [Parameter(Mandatory)][string]$CameraTarget,
    [ValidateRange(1,600)][int]$TimeoutSeconds = 60
)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$OutputDirectory = [IO.Path]::GetFullPath($OutputDirectory)
if (Test-Path -LiteralPath $OutputDirectory) { throw 'Use a new evidence directory' }
New-Item -ItemType Directory -Path $OutputDirectory | Out-Null
$endpoint = Get-Content -LiteralPath $EndpointFile -Raw | ConvertFrom-Json
$base = 'http://127.0.0.1:' + [int]$endpoint.port
$headers = @{Authorization='Bearer '+$endpoint.token}
$records = [Collections.Generic.List[object]]::new()
$passed = $false
$failure = $null
$cleanupFailure = $null
$validationAfter = $null
$restoredWindow = $null
function Command([string]$Name, [string[]]$Arguments = @()) {
    @{command=$Name;arguments=@($Arguments)} | ConvertTo-Json -Compress |
        Add-Content (Join-Path $OutputDirectory 'commands.jsonl') -Encoding utf8
    $r = Invoke-RestMethod "$base/command" -Method Post -Headers $headers -ContentType application/json `
        -Body (@{command=$Name;args=@($Arguments);mode='sync'} | ConvertTo-Json -Compress) -TimeoutSec $TimeoutSeconds
    if ($r.status -ne 'succeeded') { throw "$Name failed: $($r.code)" }
    if ($Name -eq 'window.resize' -or $Name -eq 'window.info') {
        @{command=$Name;data=$r.data} | ConvertTo-Json -Compress -Depth 8 |
            Add-Content (Join-Path $OutputDirectory 'window-responses.jsonl') -Encoding utf8
    }
    return $r.data
}
function Wait-Frame($Before, [string]$Provider, [scriptblock]$Predicate) {
    $timer = [Diagnostics.Stopwatch]::StartNew()
    do {
        $d = Command 'temporal.status'
        if ([uint64]$d.requestedGeneration -ne [uint64]$Before.requestedGeneration) { throw 'Request was superseded' }
        $valid = $d.rendererObserved -and $d.aaObserved -and !$d.nativeCaptureExclusionActive -and
            [uint64]$d.observedGeneration -eq [uint64]$d.requestedGeneration -and
            [uint64]$d.lastRealFrameId -gt [uint64]$Before.lastRealFrameId -and
            [uint64]$d.renderSubmittedFrameId -eq [uint64]$d.lastRealFrameId -and
            [uint64]$d.renderGpuCompletedFrameId -ge [uint64]$d.lastHistoryReset.realFrameId -and
            [uint64]$d.viewId -eq [uint64]$Before.viewId -and [uint64]$d.sceneEpoch -eq [uint64]$Before.sceneEpoch -and
            $d.activeUpscaler -eq $Provider -and $d.frameKind -eq 'real' -and
            ($Provider -eq 'none' -or ($d.temporalAaApplied -and !$d.fxaaApplied -and $d.upscaleResult.status -eq 'success'))
        if ($valid -and (& $Predicate $d)) { return $d }
        if ($timer.Elapsed.TotalSeconds -gt $TimeoutSeconds) { throw "No matching submitted $Provider frame after live change" }
        Start-Sleep -Milliseconds 100
    } while ($true)
}
function Require-Reset($Before, $After, [string]$Provider) {
    $r = $After.lastHistoryReset
    if ([uint64]$r.realFrameId -le [uint64]$Before.lastRealFrameId -or
        [uint64]$r.realFrameId -gt [uint64]$After.lastRealFrameId -or
        [uint64]$r.historyRevision -le [uint64]$Before.historyRevision -or
        [uint64]$r.historyRevision -ne [uint64]$After.historyRevision -or
        [uint64]$r.viewId -ne [uint64]$After.viewId -or [uint64]$r.sceneEpoch -ne [uint64]$After.sceneEpoch -or
        $r.provider -ne $Provider -or $r.displayExtent.width -ne $After.displayExtent.width -or
        $r.displayExtent.height -ne $After.displayExtent.height -or $r.renderExtent.width -ne $After.renderExtent.width -or
        $r.renderExtent.height -ne $After.renderExtent.height -or
        [math]::Abs($r.jitterX) -gt 0.000001 -or
        [math]::Abs($r.jitterY - $(if($Provider -eq 'none'){0}else{-1.0/6.0})) -gt 0.000001) {
        throw 'Submitted reset identity, extent or canonical first jitter does not match'
    }
}
function Record([string]$Action, [string]$Provider, $Before, $After, $Receipt) {
    $records.Add([ordered]@{action=$Action;provider=$Provider;before=$Before;after=$After;receipt=$Receipt})
}
$originalWindow = Command 'window.info'
$originalWindow | ConvertTo-Json -Depth 8 | Set-Content (Join-Path $OutputDirectory 'original-window.json') -Encoding utf8
$validationBefore = Command 'dx12.validation'
if (!$originalWindow.hostClientKnown -or $originalWindow.hostClientWidth -lt 320 -or $originalWindow.hostClientHeight -lt 240) {
    throw 'A restorable host client size is required; viewport extent cannot restore a window'
}
try {
    foreach ($provider in @('none','fsr','xess')) {
        Command 'object.transform' @($CameraTarget,'0','0','0') | Out-Null
        & (Join-Path $PSScriptRoot 'Invoke-TemporalStage.ps1') -EndpointFile $EndpointFile -Stage upscale `
            -Provider $provider -Quality quality -TimeoutSeconds $TimeoutSeconds -OutputDirectory "$OutputDirectory/$provider-baseline" | Out-Null
        $before = Command 'temporal.status'
        $before = Wait-Frame $before $provider {param($d) !$d.reset -and $d.cameraValid -and
            [math]::Abs($d.submittedCameraPosition[0]) -lt 0.000001 -and
            [math]::Abs($d.submittedCameraPosition[1]) -lt 0.000001 -and
            [math]::Abs($d.submittedCameraPosition[2]) -lt 0.000001}
        # A zero-position camera is supplied by the caller's disposable fixture.
        # Movement uses the ordinary transform writer; only camera.cut notifies.
        $receipt = Command 'object.transform' @($CameraTarget,'0.1','0','0')
        $after = Wait-Frame $before $provider {param($d) !$d.reset -and $d.cameraValid -and
            [math]::Abs($d.submittedCameraPosition[0]-0.1) -lt 0.000001 -and
            [math]::Abs($d.submittedCameraPosition[1]) -lt 0.000001 -and
            [math]::Abs($d.submittedCameraPosition[2]) -lt 0.000001}
        if ([uint64]$after.historyRevision -ne [uint64]$before.historyRevision) { throw 'Ordinary motion reset history' }
        Record 'continuous-motion' $provider $before $after $receipt
        $before = $after
        $receipt = Command 'camera.cut' @($CameraTarget)
        $cut = [uint64]$receipt.cameraCutRevision
        $after = Wait-Frame $before $provider {param($d) [uint64]$d.lastHistoryReset.cameraCutRevision -eq $cut -and
            [uint64]$d.lastHistoryReset.realFrameId -gt [uint64]$before.lastRealFrameId}
        Require-Reset $before $after $provider
        if ([uint64]$after.historyRevision -ne [uint64]$before.historyRevision+1) { throw 'Single cut counted more than once' }
        Record 'camera-cut' $provider $before $after $receipt
        foreach ($size in @(@(960,640),@(1280,720))) {
            $before = $after
            $receipt = Command 'window.resize' @([string]$size[0],[string]$size[1])
            if ($receipt.clamped -or $receipt.width -ne $size[0] -or $receipt.height -ne $size[1]) {
                throw 'Host client size does not match requested resize dimensions'
            }
            $after = Wait-Frame $before $provider {param($d)
                ($d.displayExtent.width -ne $before.displayExtent.width -or $d.displayExtent.height -ne $before.displayExtent.height) -and
                [uint64]$d.lastHistoryReset.realFrameId -gt [uint64]$before.lastRealFrameId -and !$d.reset}
            Require-Reset $before $after $provider
            if ($after.renderExtent.width -gt $after.displayExtent.width -or $after.renderExtent.height -gt $after.displayExtent.height) { throw 'Render extent exceeds display' }
            if ($provider -eq 'none' -and ($after.renderExtent.width -ne $after.displayExtent.width -or $after.renderExtent.height -ne $after.displayExtent.height)) { throw 'Native resize violates 1:1' }
            if ($provider -ne 'none' -and ($after.renderExtent.width -ge $after.displayExtent.width -or $after.renderExtent.height -ge $after.displayExtent.height)) { throw 'Quality resize did not retain lower render extent' }
            Record 'window-resize' $provider $before $after $receipt
        }
        $before = $after
        $after = Wait-Frame $before $provider {param($d) !$d.reset}
        if ([uint64]$after.historyRevision -ne [uint64]$before.historyRevision) { throw 'History failed to stabilize after resize' }
        Record 'post-resize-continuity' $provider $before $after $null
        # Return the fixture to its initial dimensions before the next provider.
        Command 'window.resize' @([string]$originalWindow.hostClientWidth,[string]$originalWindow.hostClientHeight) | Out-Null
        Start-Sleep -Milliseconds 500
    }
    $validationAfter = Command 'dx12.validation'
    if (!$validationAfter.layerEnabled -or $validationAfter.problems -ne 0 -or $validationAfter.droppedMessages -ne 0) {
        throw 'DX12 validation missing or reported problems during history recovery'
    }
    $passed = $true
} catch {
    $failure = $_.Exception.Message
    throw
} finally {
    try {
        if ($originalWindow.hostClientKnown) {
            Command 'window.resize' @([string]$originalWindow.hostClientWidth,[string]$originalWindow.hostClientHeight) | Out-Null
        }
        Command 'object.transform' @($CameraTarget,'0','0','0') | Out-Null
        $restoredWindow = Command 'window.info'
        if ($restoredWindow.hostClientWidth -ne $originalWindow.hostClientWidth -or
            $restoredWindow.hostClientHeight -ne $originalWindow.hostClientHeight) { throw 'Host window was not restored' }
    } catch { $cleanupFailure = $_.Exception.Message; $passed = $false }
    [ordered]@{passed=$passed;scope='live-submitted-history-and-AA-recovery; no pixels or physical display acceptance';
        originalWindow=$originalWindow;restoredWindow=$restoredWindow;validationBefore=$validationBefore;validationAfter=$validationAfter;
        failure=$failure;cleanupFailure=$cleanupFailure;records=@($records.ToArray())} | ConvertTo-Json -Depth 25 |
        Set-Content (Join-Path $OutputDirectory 'recovery.json') -Encoding utf8
}
if (!$passed) { throw 'History recovery or fixture restoration failed; inspect recovery.json' }
