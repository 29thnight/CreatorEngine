#Requires -Version 7.0
param([ValidateSet('Debug', 'Release')][string]$Configuration = 'Debug')

# Build CreatorEditor and AssetCooker for this configuration before running.
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$repo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$output = Join-Path $repo 'Build/Obj/MaterialProductProbe'
$packageCase = (Get-Content (Join-Path $output "scene-package-$Configuration-root.txt") -Raw).Trim()
$caseRoot = Join-Path $output ("EditorLX-$Configuration-" + [Guid]::NewGuid().ToString('N').Substring(0, 12))
$project = Join-Path $caseRoot 'Project'
New-Item -ItemType Directory -Path $caseRoot | Out-Null
Copy-Item -LiteralPath (Join-Path $packageCase 'Project') -Destination $project -Recurse
$assets = Join-Path $project 'Assets'
Copy-Item -LiteralPath (Join-Path $repo 'Dynamic_CPP/Assets/Shaders/DefaultPassShader') -Destination (Join-Path $assets 'Shaders') -Recurse -Force
$sourceFiles = @(
    Get-ChildItem (Join-Path $repo 'Engine'), (Join-Path $repo 'Editor'), (Join-Path $repo 'Lattice'),
        (Join-Path $repo 'Dynamic_CPP/Assets/Shaders/DefaultPassShader') -Recurse -File |
        Where-Object Extension -In '.cpp', '.h', '.slang', '.vcxproj', '.props', '.targets'
    Get-Item $PSCommandPath
) | Sort-Object FullName -Unique
$snapshot = @($sourceFiles | ForEach-Object {
    [pscustomobject]@{ path = $_.FullName; hash = (Get-FileHash $_.FullName -Algorithm SHA256).Hash }
})
$snapshot | ConvertTo-Json | Set-Content (Join-Path $caseRoot 'source-hashes.json') -Encoding utf8
$cooker = Join-Path $repo "Bin/x64-$Configuration/Tools/AssetCooker/AssetCooker.exe"
$cook = @('--asset-root', $assets, '--generation-root', (Join-Path $project 'Library/ModelAssetGenerations'),
    '--material-shader-root', (Join-Path $assets 'Shaders/DefaultPassShader'), '--output', (Join-Path $caseRoot 'Cooked'))
foreach ($file in Get-ChildItem $assets -Recurse -File | Sort-Object FullName) {
    $flag = switch ($file.Extension) {
        '.glb' { '--model' }; '.png' { '--texture' }; '.hdr' { '--texture' }
        '.shadermeta' { '--shadermeta' }; '.shadergraph' { '--shadergraph' }
        '.asset' { '--material' }; '.creator' { '--scene' }; default { '' }
    }
    if ($flag) { $cook += $flag, $file.FullName }
}
$cookResult = @(& $cooker @cook 2>&1)
$cookExit = $LASTEXITCODE
$cookResult | Set-Content (Join-Path $caseRoot 'cook.log') -Encoding utf8
if ($cookExit -ne 0) { throw "Editor fixture cook failed: $cookExit" }
Copy-Item -LiteralPath (Join-Path $caseRoot 'Cooked/Derived') -Destination $assets -Recurse
$derivedHashes = @(Get-ChildItem (Join-Path $assets 'Derived') -Recurse -File | ForEach-Object {
    [pscustomobject]@{ path = $_.FullName; hash = (Get-FileHash $_.FullName -Algorithm SHA256).Hash }
})
$script:checks = 0
$script:captures = 0
$script:responses = Join-Path $caseRoot 'responses.jsonl'
function Assert([bool]$Condition, [string]$Message) {
    if (!$Condition) { throw $Message }
    $script:checks++
}
function Invoke-Gate([string]$Name, [string[]]$Arguments = @(), [switch]$Reject) {
    $body = @{ command = $Name; args = @($Arguments); mode = 'async' } | ConvertTo-Json -Compress
    $response = Invoke-WebRequest "$script:base/command" -Method Post -Headers $script:headers `
        -ContentType 'application/json' -Body $body -SkipHttpErrorCheck -TimeoutSec 30
    $result = $response.Content | ConvertFrom-Json
    if ($Name -eq 'quit' -and $result.PSObject.Properties['operationId']) {
        $response.Content | Add-Content $script:responses -Encoding utf8
        Assert ($response.StatusCode -eq 202) 'Shutdown request must be accepted'
        return $result
    }
    if ($result.PSObject.Properties['operationId']) {
        $deadline = [DateTime]::UtcNow.AddSeconds(180)
        do {
            Start-Sleep -Milliseconds 100
            $response = Invoke-WebRequest "$script:base$($result.poll)" -Headers $script:headers -SkipHttpErrorCheck -TimeoutSec 30
            $terminal = $response.Content | ConvertFrom-Json
            if ($terminal.state -eq 'completed') { break }
            if ([DateTime]::UtcNow -ge $deadline) { throw "Editor operation timeout: $Name" }
        } while ($true)
        $result = $terminal
    }
    $response.Content | Add-Content $script:responses -Encoding utf8
    Assert (($result.status -eq 'succeeded') -ne [bool]$Reject) "$Name failed expectation: $($response.Content)"
    return $result
}
Add-Type -TypeDefinition @'
using System;
using System.IO;
public static class LXEditorPixels
{
    public static double Difference(string left, string right)
    {
        var a = File.ReadAllBytes(left);
        var b = File.ReadAllBytes(right);
        if (a.Length != b.Length || a.Length % 4 != 0) throw new Exception("Capture dimensions differ");
        double maximum = 0;
        for (int i = 0; i < a.Length; i += 4)
        {
            double x = BitConverter.ToSingle(a, i), y = BitConverter.ToSingle(b, i);
            if (double.IsNaN(x) || double.IsNaN(y) || double.IsInfinity(x) || double.IsInfinity(y))
                throw new Exception("Nonfinite capture");
            maximum = Math.Max(maximum, Math.Abs(x - y) / Math.Max(1, Math.Max(Math.Abs(x), Math.Abs(y))));
        }
        return maximum;
    }
}
'@
function Capture([string]$Label, [string]$Target = 'editor') {
    $expected = State
    Assert ($expected.renderableMesh) 'The fixture must resolve actual model geometry'
    $deadline = [DateTime]::UtcNow.AddSeconds(150)
    $attempt = 0
    do {
        $directory = Join-Path $caseRoot "$Label-$attempt"
        Invoke-Gate 'render.live.fence' @('120') | Out-Null
        Invoke-Gate 'render.live.capture' @($directory, $Target, 'controlled') | Out-Null
        $manifest = Get-Content (Join-Path $directory 'manifest.json') -Raw | ConvertFrom-Json
        $selected = @($manifest.draws | Where-Object route -EQ 'lattice')
        if ($selected.Count -eq 1 -and $selected[0].lattice.graphId -eq $expected.graph -and
            $selected[0].lattice.generation -eq $expected.generation -and
            ($selected[0].lattice.uniformBytes -join ',') -eq ($expected.uniformBytes -join ',')) { break }
        if ([DateTime]::UtcNow -ge $deadline) { throw "Requested material did not reach the actual frame: $Label" }
        $attempt++
    } while ($true)
    Assert ($manifest.source -eq 'product-live' -and $manifest.backend -eq 'dx12') 'Capture must be an actual Editor frame'
    Assert ($manifest.validationCount -eq 0 -and @($manifest.attachments | Where-Object nonfinite -NE 0).Count -eq 0) 'Capture must be finite and validation clean'
    Assert (@($manifest.draws | Where-Object route -EQ 'lattice').Count -eq 1) 'Actual selected LX draw must be captured'
    $baseColor = @($manifest.attachments | Where-Object name -EQ 'baseColor')[0]
    Assert ($baseColor.rgbMax -gt 0.01) 'Actual LX material must populate the GBuffer'
    $script:captures++
    return $directory
}
function SamePixels([string]$Left, [string]$Right) {
    foreach ($attachment in @('baseColor', 'metalRough', 'normal', 'emissive', 'depth', 'preToneHdr')) {
        $difference = [LXEditorPixels]::Difference((Join-Path $Left "$attachment.f32"), (Join-Path $Right "$attachment.f32"))
        Assert ($difference -le 0.003) "Restored $attachment differs: $difference"
    }
}
function State() { return (Invoke-Gate 'material.graph' @('Ground')).data }
function Signature($State) { return @{ graph = $State.graph; parameters = $State.parameters; textures = $State.textures } | ConvertTo-Json -Depth 8 -Compress }
$exe = Join-Path $repo "Bin/x64-$Configuration/Editor/CreatorEditor.exe"
$previousValidation = $env:CREATOR_DX12_VALIDATION
$env:CREATOR_DX12_VALIDATION = 'gpu'
$process = $null
try {
    $process = Start-Process -FilePath $exe -ArgumentList @('--development-project', $project, '--command-service', '--smoke-offscreen') `
        -WorkingDirectory (Split-Path $exe) -WindowStyle Hidden -PassThru `
        -RedirectStandardOutput (Join-Path $caseRoot 'editor.stdout.log') -RedirectStandardError (Join-Path $caseRoot 'editor.stderr.log')
    $endpointPath = Join-Path $project 'Library/CommandService/endpoint.json'
    $deadline = [DateTime]::UtcNow.AddSeconds(180)
    do {
        $process.Refresh()
        if ($process.HasExited) { throw "Editor exited during startup: $($process.ExitCode)" }
        if (Test-Path $endpointPath) {
            $endpoint = Get-Content $endpointPath -Raw | ConvertFrom-Json
            if ($endpoint.pid -eq $process.Id) { break }
        }
        if ([DateTime]::UtcNow -ge $deadline) { throw 'Editor endpoint timed out' }
        Start-Sleep -Milliseconds 100
    } while ($true)
    $script:base = "http://127.0.0.1:$($endpoint.port)"
    $script:headers = @{ Authorization = "Bearer $($endpoint.token)" }
    Invoke-Gate 'play.foreground_override' @('off') | Out-Null
    Invoke-Gate 'play.cursor' @('show') | Out-Null
    Invoke-Gate 'scene.switch' @((Join-Path $assets 'Scenes/LX_CookFixture.creator')) | Out-Null
    Invoke-Gate 'camera.editor' @('follow', 'on') | Out-Null
    $original = State
    Assert ($original.enabled -and $original.graph -eq '11111111-1111-4111-8111-111111111111') 'Core graph must load'
    $baseline = Capture 'baseline'
    Invoke-Gate 'material.graph' @('Ground', 'set', '900', '1.3') | Out-Null
    $changed = State
    Assert ($changed.generation -eq $original.generation) 'Uniform edit must preserve the generation'
    $edited = Capture 'edited'
    Assert ([LXEditorPixels]::Difference((Join-Path $baseline 'preToneHdr.f32'), (Join-Path $edited 'preToneHdr.f32')) -gt 0.003) 'IOR edit must affect actual pixels'
    Invoke-Gate 'undo' | Out-Null
    Assert ((Signature (State)) -eq (Signature $original)) 'Undo must restore graph instance'
    SamePixels $baseline (Capture 'undo')
    Invoke-Gate 'redo' | Out-Null
    SamePixels $edited (Capture 'redo')
    foreach ($arguments in @(@('Ground', 'set', '999999', '1'), @('Ground', 'set', '900', 'NaN'),
        @('Ground', 'set', '900', 'true'), @('Ground', 'set', '900', '1', '2', '3'),
        @('Ground', 'bind', '99999999-9999-4999-8999-999999999999'))) {
        Invoke-Gate 'material.graph' $arguments -Reject | Out-Null
        Assert ((Signature (State)) -eq (Signature $changed)) 'Rejected edit must preserve accepted instance'
    }
    SamePixels $edited (Capture 'rejected')
    $graphSource = Join-Path $assets 'core.shadergraph'
    $graphBytes = [IO.File]::ReadAllBytes($graphSource)
    try {
        [IO.File]::WriteAllText($graphSource, 'invalid graph source')
        Invoke-Gate 'material.graph' @('Ground', 'reload') -Reject | Out-Null
        SamePixels $edited (Capture 'reload-rejected')
    } finally { [IO.File]::WriteAllBytes($graphSource, $graphBytes) }
    Invoke-Gate 'material.graph' @('Ground', 'bind', '33333333-3333-4333-8333-333333333333') | Out-Null
    $layered = State
    Assert ($layered.graph -eq '33333333-3333-4333-8333-333333333333') 'Layered replacement must publish'
    $layeredPixels = Capture 'layered'
    Assert ([LXEditorPixels]::Difference((Join-Path $edited 'preToneHdr.f32'), (Join-Path $layeredPixels 'preToneHdr.f32')) -gt 0.003) 'Graph replacement must affect actual pixels'
    $saved = Join-Path $assets 'Scenes/LX_Saved.creator'
    Invoke-Gate 'scene.save' @($saved) | Out-Null
    Invoke-Gate 'scene.new' @('Empty') | Out-Null
    Invoke-Gate 'scene.switch' @($saved) | Out-Null
    Invoke-Gate 'camera.editor' @('follow', 'on') | Out-Null
    Assert ((Signature (State)) -eq (Signature $layered)) 'Save and reopen must preserve typed graph values'
    SamePixels $layeredPixels (Capture 'reopened')
    Invoke-Gate 'play' | Out-Null
    Invoke-Gate 'material.graph' @('Ground', 'set', '900', '1.4') | Out-Null
    $gamePixels = Capture 'game' 'game'
    Invoke-Gate 'stop' | Out-Null
    Invoke-Gate 'camera.editor' @('follow', 'on') | Out-Null
    Assert ((Signature (State)) -eq (Signature $layered)) 'Stop must restore the authoring Scene snapshot'
    SamePixels $layeredPixels (Capture 'stopped')
    Invoke-Gate 'object.delete' @('Ground') | Out-Null
    Invoke-Gate 'material.graph' @('Ground') -Reject | Out-Null
    Invoke-Gate 'undo' | Out-Null
    Assert ((Signature (State)) -eq (Signature $layered)) 'Delete and Undo must restore the retained material owner'
    SamePixels $layeredPixels (Capture 'delete-undo')
    $validation = (Invoke-Gate 'dx12.validation').data
    Assert ($validation.layerEnabled -and $validation.mode -eq 'gpu' -and $validation.drains -gt 0 -and
        $validation.problems -eq 0 -and $validation.droppedMessages -eq 0) 'GPU validation must be enabled, drained and clean'
    $validation | ConvertTo-Json -Depth 5 | Set-Content (Join-Path $caseRoot 'validation.json') -Encoding utf8
    foreach ($entry in $derivedHashes) {
        Assert ((Test-Path $entry.path) -and (Get-FileHash $entry.path -Algorithm SHA256).Hash -eq $entry.hash) 'Editor must preserve cooked artifact bytes and sidecars'
    }
    Invoke-Gate 'quit' | Out-Null
    Assert ($process.WaitForExit(60000)) 'Editor must finish normal shutdown'
    Assert ($process.ExitCode -eq 0) "Editor shutdown exit $($process.ExitCode)"
    foreach ($entry in $snapshot) {
        Assert ((Get-FileHash $entry.path -Algorithm SHA256).Hash -eq $entry.hash) "Source drift: $($entry.path)"
    }
    $caseRoot | Set-Content (Join-Path $output "editor-integration-$Configuration-root.txt") -Encoding utf8
    "LX_MATERIAL_EDITOR_INTEGRATION_OK configuration=$Configuration checks=$script:checks captures=$script:captures exit=0 sources=$($snapshot.Count) drift=0"
} finally {
    if ($process -and !$process.HasExited) { $process.Kill(); $process.WaitForExit() }
    $env:CREATOR_DX12_VALIDATION = $previousValidation
}
