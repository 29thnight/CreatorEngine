param(
    [Parameter(Mandatory)][string]$EndpointFile,
    [Parameter(Mandatory)][string]$OutputDirectory,
    [string]$CameraTarget = 'Camera',
    [ValidateSet('none','fsr','xess')][string]$Provider = 'none',
    [ValidateSet('native-aa','quality','balanced','performance','ultra-performance')][string]$Quality = 'quality',
    [int]$TimeoutSeconds = 90,
    [switch]$KeepRawCaptures
)
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot '../regression/ArtifactRetention.ps1')
Assert-ArtifactCapacity $OutputDirectory
$out = [IO.Path]::GetFullPath($OutputDirectory)
if (Test-Path -LiteralPath $out) { throw 'Use a new evidence directory.' }
New-Item -ItemType Directory -Path $out | Out-Null
$endpoint = Get-Content -LiteralPath $EndpointFile -Raw | ConvertFrom-Json
$base = 'http://127.0.0.1:' + $endpoint.port
$headers = @{Authorization = 'Bearer ' + $endpoint.token}
$records = [Collections.Generic.List[object]]::new()
$passed = $false
function Command([string]$name, [string[]]$arguments = @()) {
    $r = Invoke-RestMethod "$base/command" -Method Post -Headers $headers -ContentType application/json `
        -Body (@{command=$name;args=@($arguments);mode='sync'} | ConvertTo-Json -Compress) -TimeoutSec $TimeoutSeconds
    if ($r.status -ne 'succeeded') { throw "$name failed: $($r.code) $($r.message)" }
    return $r.data
}
function Capture([string]$name, [int]$ordinal, [string]$action, [string[]]$arguments) {
    $directory = Join-Path $out $name
    $before = Command 'temporal.status'
    $captureArgs = @($directory, [string]$ordinal)
    if ($Provider -ne 'none') { $captureArgs += 'fixed-sdk-clock' }
    Command 'render.temporal.history.capture' $captureArgs | Out-Null
    $deadline = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
    do {
        $armed = Command 'render.temporal.history.capture' @('status')
        if ($armed.directory -ne $directory -or $armed.failed -or $armed.complete) { throw 'History capture arming failed.' }
        if ($armed.ready) { break }
        if ([DateTime]::UtcNow -ge $deadline) { throw 'History capture arming timed out.' }
        Start-Sleep -Milliseconds 100
    } while ($true)
    $before = Command 'temporal.status'
    $receipt = Command $action $arguments
    $deadline = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
    do {
        $s = Command 'render.temporal.history.capture' @('status')
        if ($s.directory -ne $directory -or $s.failed) { throw "Capture failed/changed: $($s.error)" }
        if ($s.complete) { break }
        if ([DateTime]::UtcNow -ge $deadline) { throw 'Live history capture timed out.' }
        Start-Sleep -Milliseconds 100
    } while ($true)
    $after = Command 'temporal.status'
    $manifest = Get-Content -LiteralPath (Join-Path $directory 'manifest.json') -Raw | ConvertFrom-Json
    $records.Add(@{name=$name;ordinal=$ordinal;action=$action;receipt=$receipt;before=$before;after=$after})
    # Ensure the next perturbation starts from a warmed accepted history.
    $deadline = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
    do {
        $d = Command 'temporal.status'
        if ([uint64]$d.renderGpuCompletedFrameId -ge [uint64]$manifest.sourceRealFrameId -and
            [uint64]$d.lastRealFrameId -gt [uint64]$after.lastRealFrameId + 16 -and !$d.reset) { break }
        if ([DateTime]::UtcNow -ge $deadline) { throw 'History did not resume after capture.' }
        Start-Sleep -Milliseconds 100
    } while ($true)
}
$original = Command 'window.info'
try {
    & (Join-Path $PSScriptRoot 'Invoke-TemporalStage.ps1') -EndpointFile $EndpointFile -Stage disable `
        -OutputDirectory (Join-Path $out 'native') -TimeoutSeconds $TimeoutSeconds | Out-Null
    if ($Provider -ne 'none') {
        & (Join-Path $PSScriptRoot 'Invoke-TemporalStage.ps1') -EndpointFile $EndpointFile -Stage upscale `
            -Provider $Provider -Quality $Quality -OutputDirectory (Join-Path $out 'upscale') -TimeoutSeconds $TimeoutSeconds | Out-Null
    }
    Command 'object.transform' @($CameraTarget,'0','0','0') | Out-Null
    Capture 'old-pose' 1 'camera.cut' @($CameraTarget)
    Command 'object.transform' @($CameraTarget,'0.2','0','0') | Out-Null
    # Polling the actual submitted pose avoids accepting a cut of the old pose.
    $deadline = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
    do {
        $s = Command 'temporal.status'
        if ($s.cameraValid -and [Math]::Abs($s.submittedCameraPosition[0]-0.2) -lt 0.000001) { break }
        if ([DateTime]::UtcNow -ge $deadline) { throw 'Moved camera pose did not submit.' }
        Start-Sleep -Milliseconds 100
    } while ($true)
    foreach ($ordinal in @(1,8)) {
        Capture "cut-$ordinal" $ordinal 'camera.cut' @($CameraTarget)
        Capture "cut-reference-$ordinal" $ordinal 'camera.cut' @($CameraTarget)
    }
    foreach ($size in @(@(960,640),@(1280,720))) {
        foreach ($ordinal in @(1,8)) {
            # The starting host can already equal this target. Establish a
            # different extent so every resize capture has a real reset trigger.
            $baselineWidth = [int]$original.hostClientWidth
            $baselineHeight = [int]$original.hostClientHeight
            if ($baselineWidth -eq $size[0] -and $baselineHeight -eq $size[1]) {
                $baselineWidth += 64
                $baselineHeight += 64
            }
            $baselineBefore = Command 'temporal.status'
            $hostBefore = Command 'window.info'
            $expectChange = $hostBefore.hostClientWidth -ne $baselineWidth -or $hostBefore.hostClientHeight -ne $baselineHeight
            Command 'window.resize' @([string]$baselineWidth,[string]$baselineHeight) | Out-Null
            $deadline = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
            do {
                $baseline = Command 'temporal.status'
                $extentChanged = $baseline.displayExtent.width -ne $baselineBefore.displayExtent.width -or
                    $baseline.displayExtent.height -ne $baselineBefore.displayExtent.height
                if ([uint64]$baseline.lastRealFrameId -gt [uint64]$baselineBefore.lastRealFrameId + 2 -and
                    !$baseline.reset -and (!$expectChange -or $extentChanged)) { break }
                if ([DateTime]::UtcNow -ge $deadline) { throw 'Baseline resize did not reach a submitted view.' }
                Start-Sleep -Milliseconds 100
            } while ($true)
            $label = "resize-$($size[0])-$ordinal"
            Capture $label $ordinal 'window.resize' @([string]$size[0],[string]$size[1])
            Capture "$label-reference" $ordinal 'camera.cut' @($CameraTarget)
        }
    }
    $validation = Command 'dx12.validation'
    if (!$validation.layerEnabled -or $validation.messages -ne 0 -or $validation.problems -ne 0 -or $validation.droppedMessages -ne 0) {
        throw 'DX12 validation was missing or reported problems.'
    }
    $records | ConvertTo-Json -Depth 25 | Set-Content (Join-Path $out 'records.json') -Encoding utf8
    & python (Join-Path $PSScriptRoot 'Compare-LiveSSGIRecovery.py') --directory $out --provider $Provider --quality $Quality
    if ($LASTEXITCODE -ne 0) { throw 'Live recovery pixel oracle rejected the captures.' }
    $passed = $true
}
finally {
    Command 'render.temporal.history.capture' @('cancel') | Out-Null
    Command 'window.resize' @([string]$original.hostClientWidth,[string]$original.hostClientHeight) | Out-Null
    Command 'object.transform' @($CameraTarget,'0','0','0') | Out-Null
    $restored = Command 'window.info'
    if ($restored.hostClientWidth -ne $original.hostClientWidth -or $restored.hostClientHeight -ne $original.hostClientHeight) { $passed=$false }
    @{passed=$passed;provider=$Provider;quality=$Quality;records=@($records.ToArray());originalWindow=$original;restoredWindow=$restored;
        validation=$validation;scope='live cut/window resize SSGI and reconstructed/final GPU recovery pixels; no physical display or general image-quality rating'} |
        ConvertTo-Json -Depth 25 | Set-Content (Join-Path $out 'recovery.json') -Encoding utf8
}
if (!$passed) { throw 'Live SSGI recovery or window restoration failed.' }

Complete-ArtifactRetention $out "ssgi-$Provider" -KeepRawCaptures:$KeepRawCaptures
