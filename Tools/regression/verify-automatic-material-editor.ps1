#Requires -Version 7.0
param([ValidateSet('Debug', 'Release')][string]$Configuration = 'Debug')
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$repo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$output = Join-Path $repo 'Build/Obj/MaterialProductProbe'
$fixture = (Get-Content (Join-Path $output "editor-integration-$Configuration-root.txt") -Raw).Trim()
# 이 경로의 이전 자동 적용 회귀를 명시적 요청과 비자동 동작 회귀로 대체한다.
$caseRoot = Join-Path $output ("ManualEditor-$Configuration-" + [Guid]::NewGuid().ToString('N').Substring(0, 12))
$project = Join-Path $caseRoot 'Project'
New-Item -ItemType Directory -Path $caseRoot | Out-Null
Copy-Item -LiteralPath (Join-Path $fixture 'Project') -Destination $project -Recurse
& (Join-Path $PSScriptRoot 'sync-material-editor-scale.ps1') -Project $project
$assets = Join-Path $project 'Assets'
# The archived fixture can contain an older Scene host. Exercise the same
# shader includes as the current Editor/cooker, not the fixture's old copy.
Copy-Item -LiteralPath (Join-Path $repo 'Dynamic_CPP/Assets/Shaders/DefaultPassShader') -Destination (Join-Path $assets 'Shaders') -Recurse -Force
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
    $expectedScale = [float]::Parse(([regex]::Match(
        [IO.File]::ReadAllText((Join-Path $project 'ProjectSetting/EngineSettings.asset')),
        '(?m)^imguiScale:\s*([^\r\n]+)')).Groups[1].Value,
        [Globalization.CultureInfo]::InvariantCulture)
    $theme = (Invoke-Gate 'editor.theme').data
    Assert ($theme.scaleMatches -and $theme.preferenceScale -eq $expectedScale -and
        $theme.fontScaleMain -eq $expectedScale) 'Editor must apply the inherited UI scale on startup and restart'
    Invoke-Gate 'play.foreground_override' @('off') | Out-Null
    Invoke-Gate 'play.cursor' @('show') | Out-Null
}

function Doc { (Invoke-Gate 'material.editor' @('state')).data }
function Edit([string]$Operation, [string[]]$Arguments = @(), [switch]$Reject) {
    $d = Doc
    $response = Invoke-Gate 'material.editor' (@($Operation, [string]$d.document, [string]$d.revision) + $Arguments)
    if ($Operation -in @('save', 'apply')) {
        Assert ($response.data.requestSerial -gt $d.requestSerial) 'Save and Apply must issue an explicit request'
        return Wait-MaterialRequest $response.data.requestSerial -Reject:$Reject
    }
    return $response
}
function Wait-MaterialRequest([UInt64]$Serial, [switch]$Reject) {
    $deadline = [DateTime]::UtcNow.AddSeconds(150)
    do {
        $d = Doc
        if ($d.requestSerial -ne $Serial) { throw 'A different request must not satisfy this compilation wait' }
        if (!$d.requestPending -and $d.completedRequestSerial -eq $Serial) {
            Assert ($d.requestSucceeded -ne [bool]$Reject) "Material request failed expectation: $($d.requestError)"
            return $d
        }
        if ([DateTime]::UtcNow -gt $deadline) { throw "Explicit material request timed out: $($d | ConvertTo-Json -Depth 4 -Compress)" }
        Start-Sleep -Milliseconds 100
    } while ($true)
}
function MaterialFiles($Document) {
    $files = @{}
    foreach ($path in @($Document.path, "$($Document.path).meta", $Document.materialPath, "$($Document.materialPath).meta")) {
        $files[$path] = [Convert]::ToBase64String([IO.File]::ReadAllBytes($path))
    }
    return $files
}
function Assert-MaterialFiles($Expected, [string]$Message) {
    foreach ($path in $Expected.Keys) {
        Assert ([Convert]::ToBase64String([IO.File]::ReadAllBytes($path)) -ceq $Expected[$path]) "$Message : $path"
    }
}
try {
    Start-Editor 'manual'
    Invoke-Gate 'scene.new' @('ManualResourceGate') | Out-Null
    Invoke-Gate 'model.loadcached' @((Join-Path $assets 'Models/Prim_Plane.glb')) | Out-Null
    $placed = Invoke-Gate 'model.place' @('Prim_Plane')
    function Find-Mesh([string]$Id) {
        $object = (Invoke-Gate 'object.describe' @($Id)).data
        if (@($object.components | Where-Object type -EQ 'MeshRenderer').Count -gt 0) { return $Id }
        foreach ($child in $object.children) {
            $found = Find-Mesh $child
            if ($found) { return $found }
        }
        return $null
    }
    $meshObject = Find-Mesh $placed.data.id
    Assert (![string]::IsNullOrEmpty($meshObject)) 'Native model placement must create a MeshRenderer'
    Invoke-Gate 'object.rename' @($meshObject, 'Ground') | Out-Null
    Invoke-Gate 'material.editor' @('new', 'Ground') | Out-Null
    $newDraft = Doc
    Assert ($newDraft.dirty -and !$newDraft.automaticPreview -and $newDraft.requestSerial -eq 0) 'New drafts must start in manual mode'
    Start-Sleep -Milliseconds 1500
    Assert ((Doc).requestSerial -eq 0 -and !(Test-Path $newDraft.path)) 'An untouched draft must not compile, apply or save itself'
    $initial = Edit 'save'
    Assert (!$initial.dirty -and $initial.applied -and $initial.catalogRegistered) 'Explicit Save must persist and apply the prepared material'
    Edit 'assign' @('Ground') | Out-Null
    $original = [IO.File]::ReadAllBytes($initial.path)
    $accepted = (Invoke-Gate 'material.graph' @('Ground')).data
    $bsdf = @($initial.nodes | Where-Object type -EQ 'ShaderNodeBsdfPrincipled')[0]
    $roughness = @($bsdf.pins | Where-Object name -EQ 'Roughness')[0]
    Edit 'value' @([string]$roughness.id, '0.23') | Out-Null
    $unrequested = Doc
    Invoke-Gate 'editor.window' @('###Editor.MaterialGraph', 'close') | Out-Null
    Start-Sleep -Milliseconds 1500
    $hidden = Doc
    $hiddenLive = (Invoke-Gate 'material.graph' @('Ground')).data
    Assert ($hidden.dirty -and !$hidden.requestPending -and $hidden.requestSerial -eq $unrequested.requestSerial) 'Closing the window must not issue a compile or save request'
    Assert ([Convert]::ToBase64String([IO.File]::ReadAllBytes($initial.path)) -eq [Convert]::ToBase64String($original)) 'Draft edits must preserve the saved graph until Save'
    Assert ($hiddenLive.generation -eq $accepted.generation -and ($hiddenLive.uniformBytes -join ',') -eq ($accepted.uniformBytes -join ',')) 'Draft edits must preserve the accepted Scene material until Compile or Save'
    $compiled = Edit 'apply'
    Assert ($compiled.applied -and $compiled.dirty) 'Explicit Compile must apply the draft while leaving it unsaved'
    Assert ([Convert]::ToBase64String([IO.File]::ReadAllBytes($initial.path)) -eq [Convert]::ToBase64String($original)) 'Compile without Save must preserve disk bytes'
    $compiledLive = (Invoke-Gate 'material.graph' @('Ground')).data
    Assert ($compiledLive.generation -ne $accepted.generation) 'The compiled node constant must replace the Scene program generation'
    $changed = Edit 'save'
    $changedBytes = [IO.File]::ReadAllBytes($changed.path)
    Assert ([Convert]::ToBase64String($original) -ne [Convert]::ToBase64String($changedBytes)) 'Explicit Save must persist the edited graph'
    $live = (Invoke-Gate 'material.graph' @('Ground')).data
    Assert ($changed.lastPreparationReused -and $live.generation -eq $compiledLive.generation) 'Saving an unchanged compiled draft must reuse its generation'
    Assert ($live.graph -eq $changed.graph) 'Explicit publication must reach the existing scene renderer'
    $sidecar = [IO.File]::ReadAllText("$($changed.path).meta")
    Assert ($changed.preparedSourceKey -match '^[0-9a-f]{64}$' -and $sidecar.Contains($changed.preparedSourceKey)) 'Save must record its verified source key in the graph sidecar'
    Edit 'undo' | Out-Null
    Start-Sleep -Milliseconds 800
    Assert ([Convert]::ToBase64String([IO.File]::ReadAllBytes($changed.path)) -eq [Convert]::ToBase64String($changedBytes)) 'Undo must remain a draft edit until Save'
    $undone = Edit 'save'
    Assert ([Convert]::ToBase64String([IO.File]::ReadAllBytes($undone.path)) -eq [Convert]::ToBase64String($original)) 'Explicit Save after Undo must retain history and persist the restored values'
    Edit 'redo' | Out-Null
    $redone = Edit 'save'
    Assert ([Convert]::ToBase64String([IO.File]::ReadAllBytes($redone.path)) -eq [Convert]::ToBase64String($changedBytes)) 'Explicit Save after Redo must persist the edited values'

    $beforeBlocked = (Invoke-Gate 'material.graph' @('Ground')).data
    $beforeBlockedFiles = MaterialFiles $redone
    Edit 'value' @([string]$roughness.id, '0.37') | Out-Null
    # 읽기와 백업은 허용하되 마지막 material 파일 교체를 막아 앞선 graph/sidecar 원복을 확인한다.
    $lockedMaterial = [IO.File]::Open($redone.materialPath, [IO.FileMode]::Open, [IO.FileAccess]::Read, [IO.FileShare]::Read)
    try {
        $failed = Edit 'save' -Reject
        Assert ($failed.dirty -and !$failed.requestSucceeded) 'A failed file transaction must leave the draft unsaved'
        Assert ($failed.requestError.StartsWith('Material/graph save failed;')) 'The locked destination must fail at file publication after successful preparation'
    } finally { $lockedMaterial.Dispose() }
    Assert-MaterialFiles $beforeBlockedFiles 'Failed Save must restore all four source and sidecar files'
    $afterBlocked = (Invoke-Gate 'material.graph' @('Ground')).data
    Assert ($afterBlocked.generation -eq $beforeBlocked.generation -and ($afterBlocked.uniformBytes -join ',') -eq ($beforeBlocked.uniformBytes -join ',')) 'File failure must precede runtime publication'
    Edit 'undo' | Out-Null
    Edit 'save' | Out-Null

    $modelBefore = (Invoke-Gate 'material.graph' @('Ground')).data
    $modelMeta = Join-Path $assets 'Models/Prim_Plane.glb.meta'
    $metaText = [IO.File]::ReadAllText($modelMeta)
    $updatedMeta = [regex]::Replace($metaText, '(?m)^(\s*lodLevels:\s*)\d+', '${1}2')
    if ($updatedMeta -eq $metaText) { $updatedMeta = [regex]::Replace($metaText, '(?m)^(\s*lodLevels:\s*)\d+', '${1}1') }
    Assert ($updatedMeta -ne $metaText) 'Model import setting fixture must change'
    # A positive LOD count requires meshlets in the canonical importer contract.
    $updatedMeta = [regex]::Replace($updatedMeta, '(?m)^(\s*buildMeshlets:\s*)false', '${1}true')
    [IO.File]::WriteAllText($modelMeta, $updatedMeta)
    $deadline = [DateTime]::UtcNow.AddSeconds(120)
    do {
        $modelAfter = (Invoke-Gate 'material.graph' @('Ground')).data
        if ($modelAfter.modelGeneration -ne $modelBefore.modelGeneration) { break }
        if ([DateTime]::UtcNow -ge $deadline) { throw 'Model metadata watcher did not refresh the existing renderer' }
        Start-Sleep -Milliseconds 200
    } while ($true)
    Assert ($modelAfter.modelId -eq $modelBefore.modelId -and $modelAfter.meshId -eq $modelBefore.meshId) 'Model refresh must preserve logical identity'
    Assert (($modelAfter.uniformBytes -join ',') -eq ($modelBefore.uniformBytes -join ',')) 'Model refresh must preserve material edits'
    $shader = Join-Path $assets 'Shaders/DefaultPassShader/Includes/MaterialGraphSurfacePoint.slang'
    $shaderBytes = [IO.File]::ReadAllBytes($shader)
    $shaderText = [IO.File]::ReadAllText($shader)
    $shaderChanged = $shaderText.Replace('<= 1e6', '<= 9e5')
    Assert ($shaderChanged -ne $shaderText) 'Shader include fixture must change a compiled expression'
    [IO.File]::WriteAllText($shader, $shaderChanged)
    $beforeShader = $modelAfter.generation
    $deadline = [DateTime]::UtcNow.AddSeconds(150)
    do {
        $afterShader = (Invoke-Gate 'material.graph' @('Ground')).data
        if ($afterShader.generation -ne $beforeShader) { break }
        if ([DateTime]::UtcNow -ge $deadline) { throw 'Shader include watcher did not publish the replacement generation' }
        Start-Sleep -Milliseconds 200
    } while ($true)
    Assert (($afterShader.uniformBytes -join ',') -eq ($modelAfter.uniformBytes -join ',')) 'Shader reload must preserve material values'
    [IO.File]::WriteAllBytes($shader, $shaderBytes)
    $deadline = [DateTime]::UtcNow.AddSeconds(150)
    do {
        $restoredShader = (Invoke-Gate 'material.graph' @('Ground')).data
        if ($restoredShader.generation -ne $afterShader.generation) { break }
        if ([DateTime]::UtcNow -ge $deadline) { throw 'Restored shader include did not reload' }
        Start-Sleep -Milliseconds 200
    } while ($true)
    $shutdownFiles = MaterialFiles (Doc)
    $beforeShutdown = Doc
    Edit 'value' @([string]$roughness.id, '0.41') | Out-Null
    Assert ((Doc).dirty -and (Doc).requestSerial -eq $beforeShutdown.requestSerial) 'A final draft edit must not implicitly request compilation'
    Invoke-Gate 'quit' | Out-Null
    Assert ($process.WaitForExit(60000)) 'Editor shutdown timed out'
    Assert ($process.ExitCode -eq 0) 'Editor shutdown failed'
    Assert-MaterialFiles $shutdownFiles 'Shutdown must preserve saved files and discard unrequested draft edits'
    [ordered]@{passed=$true;configuration=$Configuration;checks=$script:checks;root=$caseRoot;
        scope='Manual Compile/Save, hidden and idle drafts, file rollback, dirty shutdown, Undo/Redo, model metadata and shader include reload';
        runtimeSha256=(Get-FileHash (Join-Path (Split-Path $exe) 'CreatorEditor.runtime.dll')).Hash} |
        ConvertTo-Json -Depth 5 | Set-Content (Join-Path $caseRoot 'result.json') -Encoding utf8
    Set-Content (Join-Path $output "automatic-$Configuration-root.txt") $caseRoot -Encoding utf8
    "MANUAL_MATERIAL_EDITOR_OK checks=$script:checks root=$caseRoot"
}
finally {
    if ($process -and !$process.HasExited) { $process.Kill(); $process.WaitForExit() }
    $env:CREATOR_DX12_VALIDATION = $previousValidation
}
