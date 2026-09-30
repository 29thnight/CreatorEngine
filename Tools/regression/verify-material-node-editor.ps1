#Requires -Version 7.0
param([ValidateSet('Debug', 'Release')][string]$Configuration = 'Debug')
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$repo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$output = Join-Path $repo 'Build/Obj/MaterialProductProbe'
$fixture = (Get-Content (Join-Path $output "editor-integration-$Configuration-root.txt") -Raw).Trim()
$caseRoot = Join-Path $output ("NodeEditor-$Configuration-" + [Guid]::NewGuid().ToString('N').Substring(0, 12))
$project = Join-Path $caseRoot 'Project'
New-Item -ItemType Directory -Path $caseRoot | Out-Null
Copy-Item -LiteralPath (Join-Path $fixture 'Project') -Destination $project -Recurse
$assets = Join-Path $project 'Assets'
$sourceFiles = @(Get-ChildItem (Join-Path $repo 'Engine'), (Join-Path $repo 'Editor'), (Join-Path $repo 'Lattice') -Recurse -File |
    Where-Object Extension -In '.cpp', '.h', '.vcxproj'; Get-Item $PSCommandPath) | Sort-Object FullName -Unique
$snapshot = @($sourceFiles | ForEach-Object { [pscustomobject]@{ path = $_.FullName; hash = (Get-FileHash $_.FullName -Algorithm SHA256).Hash } })
$snapshot | ConvertTo-Json | Set-Content (Join-Path $caseRoot 'source-hashes.json') -Encoding utf8
$script:checks = 0
$script:captures = 0
$script:responses = Join-Path $caseRoot 'responses.jsonl'
function Assert([bool]$Condition, [string]$Message) {
    if (!$Condition) { throw $Message }
    $script:checks++
}
function Invoke-Gate([string]$Name, [string[]]$Arguments = @(), [switch]$Reject, [switch]$AllowCameraPending) {
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
    if ($AllowCameraPending -and $result.code -eq 'camera.unavailable') {
        Assert ($result.status -eq 'preconditions_failed') 'Pending camera must be an explicit precondition failure'
        return $result
    }
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
    Invoke-Gate 'editor.window' @('###Editor.MaterialGraph', 'close') | Out-Null
    Invoke-Gate 'editor.window' @('###Editor.Viewport', 'focus') | Out-Null
    $expected = State
    Assert ($expected.renderableMesh) 'The fixture must resolve actual model geometry'
    $deadline = [DateTime]::UtcNow.AddSeconds(150)
    $attempt = 0
    $expectedDraws = if ($Target -eq 'material') { 3 } else { 1 }
    do {
        $directory = Join-Path $caseRoot "$Label-$attempt"
        Invoke-Gate 'render.live.fence' @('120') | Out-Null
        Invoke-Gate 'render.live.capture' @($directory, $Target, 'controlled') | Out-Null
        $manifest = Get-Content (Join-Path $directory 'manifest.json') -Raw | ConvertFrom-Json
        $selected = @($manifest.draws | Where-Object { $_.route -eq 'lattice' -and $_.lattice.graphId -eq $expected.graph })
        if ($selected.Count -eq 1 -and $selected[0].lattice.graphId -eq $expected.graph -and
            $selected[0].lattice.generation -eq $expected.generation -and
            ($selected[0].lattice.uniformBytes -join ',') -eq ($expected.uniformBytes -join ',')) { break }
        if ([DateTime]::UtcNow -ge $deadline) { throw "Requested material did not reach the actual frame: $Label" }
        $attempt++
    } while ($true)
    Assert ($manifest.source -eq 'product-live' -and $manifest.backend -eq 'dx12') 'Capture must be an actual Editor frame'
    Assert ($manifest.validationCount -eq 0 -and @($manifest.attachments | Where-Object nonfinite -NE 0).Count -eq 0) 'Capture must be finite and validation clean'
    Assert (@($manifest.draws | Where-Object route -EQ 'lattice').Count -eq $expectedDraws) 'Capture must contain the selected surface and, for preview, both checker floor meshes'
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
function Start-Editor([string]$Label) {
    $script:process = Start-Process -FilePath $exe -ArgumentList @('--development-project', $project, '--command-service', '--smoke-offscreen') `
        -WorkingDirectory (Split-Path $exe) -WindowStyle Hidden -PassThru `
        -RedirectStandardOutput (Join-Path $caseRoot "$Label.stdout.log") -RedirectStandardError (Join-Path $caseRoot "$Label.stderr.log")
    $endpointPath = Join-Path $project 'Library/CommandService/endpoint.json'
    $deadline = [DateTime]::UtcNow.AddSeconds(180)
    do {
        $process.Refresh()
        if ($process.HasExited) { throw "Editor startup failed: $($process.ExitCode)" }
        if (Test-Path $endpointPath) {
            $endpoint = Get-Content $endpointPath -Raw | ConvertFrom-Json
            if ($endpoint.pid -eq $process.Id) { break }
        }
        if ([DateTime]::UtcNow -ge $deadline) { throw 'Editor endpoint timeout' }
        Start-Sleep -Milliseconds 100
    } while ($true)
    $script:base = "http://127.0.0.1:$($endpoint.port)"
    $script:headers = @{ Authorization = "Bearer $($endpoint.token)" }
    Invoke-Gate 'play.foreground_override' @('off') | Out-Null
    Invoke-Gate 'play.cursor' @('show') | Out-Null
}
function Wait-Canvas {
    $deadline = [DateTime]::UtcNow.AddSeconds(30)
    while ((Doc).uiFrames -eq 0) {
        if ([DateTime]::UtcNow -ge $deadline) { throw 'Material ImGui window did not render' }
        Start-Sleep -Milliseconds 100
    }
}
function Follow-Camera {
    $deadline = [DateTime]::UtcNow.AddSeconds(30)
    do {
        # The camera/editor rig can still be unavailable immediately after Scene activation.
        $result = Invoke-Gate 'camera.editor' @('follow', 'on') -AllowCameraPending
        if ($result.status -eq 'succeeded') { return }
        if ([DateTime]::UtcNow -ge $deadline) { throw 'Scene game camera did not become ready' }
        Start-Sleep -Milliseconds 100
    } while ($true)
}
function Doc { (Invoke-Gate 'material.editor' @('state')).data }
function Wait-Preview {
    $deadline = [DateTime]::UtcNow.AddSeconds(150)
    do {
        $preview = (Doc).preview
        if ($preview.ready) { return $preview }
        if ([DateTime]::UtcNow -gt $deadline) { throw 'Material sphere preview did not complete' }
        Start-Sleep -Milliseconds 200
    } while ($true)
}
function Edit([string]$Operation, [string[]]$Arguments = @(), [switch]$Reject) {
    $document = Doc
    Invoke-Gate 'material.editor' (@($Operation, [string]$document.document, [string]$document.revision) + $Arguments) -Reject:$Reject
}
function Stop-Editor {
    Invoke-Gate 'quit' | Out-Null
    Assert ($process.WaitForExit(60000)) 'Normal Editor shutdown timed out'
    Assert ($process.ExitCode -eq 0) "Editor exit: $($process.ExitCode)"
}
try {
    Start-Editor 'editor'
    Invoke-Gate 'scene.switch' @((Join-Path $assets 'Scenes/LX_CookFixture.creator')) | Out-Null
    Follow-Camera
    Invoke-Gate 'material.editor' @('open', 'Ground') | Out-Null
    Wait-Canvas
    Assert ((Doc).uiFrames -gt 0) 'Registered Material window must render actual ImGui frames'
    Invoke-Gate 'material.editor' @('new', 'Ground') | Out-Null
    Wait-Canvas
    $initial = Doc
    Assert ($initial.nodes.Count -eq 2 -and $initial.links.Count -eq 1 -and $initial.dirty) 'New Blender material must contain Principled linked to Output'
    $bsdf = @($initial.nodes | Where-Object type -EQ 'ShaderNodeBsdfPrincipled')[0]
    $color = @($bsdf.pins | Where-Object name -EQ 'Base Color')[0]
    $roughness = @($bsdf.pins | Where-Object name -EQ 'Roughness')[0]
    Assert ($bsdf.height -lt 500 -and $bsdf.width -eq 330) 'Collapsed advanced sections must keep Principled compact'
    Assert ($roughness.y -gt $color.y) 'Socket rows must align in shader input order'
    $revision = (Doc).revision
    Invoke-Gate 'material.editor' @('value', [string]($initial.document + 99), [string]$revision, [string]$color.id, '0.2', '0.3', '0.4', '1') -Reject | Out-Null
    Assert ((Doc).revision -eq $revision) 'Another document identity must not edit the active material'
    Invoke-Gate 'material.editor' @('value', [string]$initial.document, [string]($revision + 99), [string]$color.id, '0.2', '0.3', '0.4', '1') -Reject | Out-Null
    Assert ((Doc).revision -eq $revision) 'Stale revision must not mutate document'
    Edit 'value' @([string]$color.id, '0.2', '0.3', '0.4', '1') | Out-Null
    Edit 'value' @([string]$roughness.id, 'NaN') -Reject | Out-Null
    Edit 'save' | Out-Null
    $savedDoc = Doc
    $graphSource = $savedDoc.path
    $graphBytes = [IO.File]::ReadAllBytes($graphSource)
    $graphJson = Get-Content $graphSource -Raw | ConvertFrom-Json
    Assert ($graphJson.kind -eq 'LatticeMaterial' -and $graphJson.domain -eq 'material') 'Product save must use canonical shadergraph archive'
    Edit 'undo' | Out-Null
    Assert ((Doc).dirty) 'Undo after Save must preserve history and mark changed values dirty'
    Edit 'redo' | Out-Null
    Assert (!(Doc).dirty) 'Redo to saved snapshot must clear dirty'
    Edit 'reload' | Out-Null
    Edit 'save' | Out-Null
    Assert ([Convert]::ToBase64String([IO.File]::ReadAllBytes($graphSource)) -eq [Convert]::ToBase64String($graphBytes)) 'Unchanged save and reload must preserve exact bytes'
    Edit 'apply' | Out-Null
    $baseline = Capture 'applied'
    $accepted = State
    Assert ($accepted.graph -eq $savedDoc.graph) 'Actual MeshRenderer must bind authored graph identity'
    Invoke-Gate 'material.editor' @('preview', 'on') | Out-Null
    $previewBefore = Wait-Preview
    $previewCapture = Capture 'sphere-applied' 'material'
    $previewManifest = Get-Content (Join-Path $previewCapture 'manifest.json') -Raw | ConvertFrom-Json
    Assert ($previewManifest.viewId -eq 3) 'Sphere preview must use an independent live view'
    Assert ($previewManifest.draws[0].modelId -eq 'e590a6f7-075f-8f70-83cb-3f427aa960bc') 'Preview must render the reserved sphere geometry instead of a Scene mesh'
    $cached = Wait-Preview
    Start-Sleep -Milliseconds 800
    Assert ((Doc).preview.completedFrame -eq $cached.completedFrame) 'Unchanged sphere preview must stop submitting render frames'
    Invoke-Gate 'material.editor' @('preview', 'off') | Out-Null
    Start-Sleep -Milliseconds 400
    Invoke-Gate 'material.editor' @('preview', 'on') | Out-Null
    Assert ((Wait-Preview).completedFrame -eq $cached.completedFrame) 'Reopening unchanged preview must republish the cached image'
    Edit 'add' @('ShaderNodeRGB') | Out-Null
    $rgb = @((Doc).nodes | Where-Object type -EQ 'ShaderNodeRGB')[0]
    $rgbPin = $rgb.pins[0]
    Edit 'value' @([string]$rgbPin.id, '0.8', '0.1', '0.15', '1') | Out-Null
    Edit 'connect' @([string]$rgbPin.id, [string]$color.id) | Out-Null
    $linked = Doc
    Assert ($linked.links.Count -eq 2) 'Compatible typed socket connection must be created'
    $beforeRejected = $linked.revision
    Edit 'connect' @([string]$rgbPin.id, [string]$linked.nodes[1].pins[0].id) -Reject | Out-Null
    Assert ((Doc).revision -eq $beforeRejected) 'Color to Closure must fail without mutation'
    Edit 'save' | Out-Null
    Edit 'apply' | Out-Null
    $changed = Capture 'linked-color'
    Assert ([LXEditorPixels]::Difference((Join-Path $baseline 'baseColor.f32'), (Join-Path $changed 'baseColor.f32')) -gt 0.01) 'Node socket editing must change actual GBuffer pixels'
    $previewAfter = Wait-Preview
    Assert ($previewAfter.revision -gt $previewBefore.revision) 'Applied instance replacement must invalidate sphere preview'
    $previewChanged = Capture 'sphere-linked-color' 'material'
    Assert ([LXEditorPixels]::Difference((Join-Path $previewCapture 'baseColor.f32'), (Join-Path $previewChanged 'baseColor.f32')) -gt 0.01) 'Same Scene graph instance must change actual sphere pixels'
    Edit 'collapse' @([string]$rgb.id, 'true') | Out-Null
    Edit 'save' | Out-Null
    Edit 'reload' | Out-Null
    Assert (@((Doc).nodes | Where-Object id -EQ $rgb.id)[0].collapsed) 'Node collapse state must round-trip'
    $currentBytes = [IO.File]::ReadAllBytes($graphSource)
    [IO.File]::WriteAllText($graphSource, [IO.File]::ReadAllText($graphSource) + "`n ")
    $externalBytes = [IO.File]::ReadAllBytes($graphSource)
    Edit 'save' -Reject | Out-Null
    Assert ([Convert]::ToBase64String([IO.File]::ReadAllBytes($graphSource)) -eq [Convert]::ToBase64String($externalBytes)) 'Conflicting external edits must be preserved'
    Edit 'reload' | Out-Null
    Edit 'save' | Out-Null
    $beforeBroken = State
    $baseLink = @((Doc).links | Where-Object input -EQ $color.id)[0]
    Edit 'disconnect' @([string]$baseLink.id) | Out-Null
    Edit 'add' @('ShaderNodeTexImage') | Out-Null
    $image = @((Doc).nodes | Where-Object type -EQ 'ShaderNodeTexImage')[0]
    $imageColor = @($image.pins | Where-Object { !$_.input -and $_.name -eq 'Color' })[0]
    Edit 'connect' @([string]$imageColor.id, [string]$color.id) | Out-Null
    Edit 'reload' -Reject | Out-Null
    Edit 'save' | Out-Null
    Edit 'apply' -Reject | Out-Null
    Assert ((State).generation -eq $beforeBroken.generation) 'Failed graph compilation must retain accepted Scene generation'
    Assert ((Wait-Preview).revision -eq $previewAfter.revision) 'Rejected Apply must retain the accepted sphere material'
    Invoke-Gate 'material.editor' @('preview', 'off') | Out-Null
    Edit 'undo' | Out-Null
    Edit 'undo' | Out-Null
    Edit 'undo' | Out-Null
    Edit 'save' | Out-Null
    Edit 'apply' | Out-Null
    $scene = Join-Path $assets 'Scenes/MaterialNodeEditor.creator'
    Invoke-Gate 'scene.save' @($scene) | Out-Null
    $finalState = State
    $validation = (Invoke-Gate 'dx12.validation').data
    Assert ($validation.layerEnabled -and $validation.problems -eq 0 -and $validation.droppedMessages -eq 0) 'GPU validation must stay clean'
    $validation | ConvertTo-Json -Depth 5 | Set-Content (Join-Path $caseRoot 'validation.json') -Encoding utf8
    Stop-Editor
    Start-Editor 'reopen'
    Invoke-Gate 'scene.switch' @($scene) | Out-Null
    Follow-Camera
    Assert ((State).graph -eq $finalState.graph) 'Fresh Editor must load newly authored graph without a package recook'
    Invoke-Gate 'material.editor' @('open', 'Ground') | Out-Null
    Wait-Canvas
    Assert ((Doc).uiFrames -gt 0) 'Reopened product editor must render'
    SamePixels $changed (Capture 'reopened')
    Stop-Editor
    foreach ($entry in $snapshot) {
        Assert ((Get-FileHash $entry.path -Algorithm SHA256).Hash -eq $entry.hash) "Source drift: $($entry.path)"
    }
    $caseRoot | Set-Content (Join-Path $output "node-editor-$Configuration-root.txt") -Encoding utf8
    "LX_MATERIAL_NODE_EDITOR_OK configuration=$Configuration checks=$script:checks captures=$script:captures sources=$($snapshot.Count) drift=0 exit=0"
} finally {
    if ($process -and !$process.HasExited) { $process.Kill(); $process.WaitForExit() }
    $env:CREATOR_DX12_VALIDATION = $previousValidation
}
