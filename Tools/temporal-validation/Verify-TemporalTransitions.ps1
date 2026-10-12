param(
    [Parameter(Mandatory)][string]$EndpointFile,
    [Parameter(Mandatory)][string]$OutputDirectory,
    [string]$CameraTarget = 'Camera',
    [ValidateSet('dlss')][string]$Provider = 'dlss',
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
    # Runtime quality controls can affect an older queued scene publication.
    # Wait for RT admission, not just command acknowledgement, before triggering.
    $deadline = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
    do {
        $armed = Command 'render.temporal.history.capture' @('status')
        if ($armed.failed -or $armed.complete -or $armed.directory -ne $directory) { throw 'Capture consumed/superseded before trigger' }
        if ($armed.ready) { break }
        if ([DateTime]::UtcNow -ge $deadline) { throw 'History capture did not reach RT admission' }
        Start-Sleep -Milliseconds 50
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
    $records.Add(@{name=$name;ordinal=$ordinal;action=$action;receipt=$receipt;before=$before;after=$after;fromQuality=$before.activeUpscaleQuality;arming=$armed})
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
    # Each target quality is entered from another quality, then compared with
    # a camera-cut reset of the same final state at accepted ordinal 1 and 8.
    $sourceQuality=if($Quality -eq 'quality'){'balanced'}else{'quality'}
    foreach($ordinal in @(1,8)) {
        & (Join-Path $PSScriptRoot 'Invoke-TemporalStage.ps1') -EndpointFile $EndpointFile -Stage upscale -Provider $Provider -Quality $sourceQuality -OutputDirectory "$out/source-$ordinal" | Out-Null
        Start-Sleep -Milliseconds 500
        Capture "quality-$ordinal" $ordinal 'temporal.upscale' @($Provider,$Quality)
        Capture "quality-$ordinal-reference" $ordinal 'camera.cut' @($CameraTarget)
    }
    foreach ($size in @(@(960,640),@(1280,720))) {
        foreach ($ordinal in @(1,8)) {
            Command 'window.resize' @([string]$original.hostClientWidth,[string]$original.hostClientHeight) | Out-Null
            Start-Sleep -Milliseconds 1000
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
    & python (Join-Path $PSScriptRoot 'Compare-TemporalTransitions.py') --directory $out --provider $Provider --quality $Quality
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

Complete-ArtifactRetention $out "transitions-$Provider-$Quality" -KeepRawCaptures:$KeepRawCaptures

