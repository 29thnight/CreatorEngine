#Requires -Version 7.0
param([ValidateSet('Debug', 'Release')][string]$Configuration = 'Debug')
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$repo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$output = Join-Path $repo 'Build/Obj/MaterialProductProbe'
$fixture = (Get-Content (Join-Path $output "editor-integration-$Configuration-root.txt") -Raw).Trim()
$case = Join-Path $output ("ModelSoT-$Configuration-" + [Guid]::NewGuid().ToString('N').Substring(0, 10))
$project = Join-Path $case 'Project'
New-Item -ItemType Directory $case | Out-Null
Copy-Item (Join-Path $fixture 'Project') $project -Recurse
& (Join-Path $PSScriptRoot 'sync-material-editor-scale.ps1') -Project $project
$assets = Join-Path $project 'Assets'
# The saved integration fixture can predate a mesh host shader fix.
Copy-Item (Join-Path $repo 'Dynamic_CPP/Assets/Shaders/DefaultPassShader') (Join-Path $assets 'Shaders') -Recurse -Force
$exe = Join-Path $repo "Bin/x64-$Configuration/Editor/CreatorEditor.exe"
$cooker = Join-Path $repo "Bin/x64-$Configuration/Tools/AssetCooker/AssetCooker.exe"
$robot = Join-Path $assets 'Models/CreatorRobot.glb'
Copy-Item (Join-Path $repo 'Dynamic_CPP/Assets/Models/CreatorRobot.glb') $robot
Copy-Item (Join-Path $repo 'Dynamic_CPP/Assets/Models/CreatorRobot.glb.meta') ($robot + '.meta')
$script:checks = 0
$process = $null
$previousValidation = $env:CREATOR_DX12_VALIDATION
$env:CREATOR_DX12_VALIDATION = 'gpu'
function Assert([bool]$Condition, [string]$Message) {
    if (!$Condition) { throw $Message }
    $script:checks++
}
function Command([string]$Name, [string[]]$Arguments = @()) {
    $body = @{ command = $Name; args = @($Arguments); mode = 'async' } | ConvertTo-Json -Compress
    $result = Invoke-RestMethod "$script:base/command" -Method Post -Headers $script:headers -ContentType 'application/json' -Body $body
    if ($Name -eq 'quit') { return }
    if ($result.PSObject.Properties['operationId']) {
        $deadline = [DateTime]::UtcNow.AddSeconds(180)
        do {
            Start-Sleep -Milliseconds 100
            try {
                $terminal = Invoke-RestMethod "$script:base$($result.poll)" -Headers $script:headers
            } catch {
                # A transient local socket bind failure is safe to retry for
                # this read-only operation status request.
                if ($_.Exception.Message -notmatch 'socket|소켓|address|주소' -or
                    [DateTime]::UtcNow -ge $deadline) { throw }
                Start-Sleep -Milliseconds 250
                continue
            }
            if ($terminal.state -eq 'completed') { $result = $terminal; break }
            if ([DateTime]::UtcNow -ge $deadline) { throw "Command timeout: $Name" }
        } while ($true)
    }
    $result | ConvertTo-Json -Depth 20 -Compress | Add-Content (Join-Path $case 'responses.jsonl')
    Assert ($result.status -eq 'succeeded') "$Name failed: $($result | ConvertTo-Json -Depth 5 -Compress)"
    return $result.data
}
function Start-Editor([string]$Label) {
    $script:process = Start-Process $exe -ArgumentList @('--development-project', $project, '--command-service', '--smoke-offscreen') `
        -WorkingDirectory (Split-Path $exe) -WindowStyle Hidden -PassThru `
        -RedirectStandardOutput (Join-Path $case "$Label.stdout.log") -RedirectStandardError (Join-Path $case "$Label.stderr.log")
    $deadline = [DateTime]::UtcNow.AddSeconds(180)
    $endpointPath = Join-Path $project 'Library/CommandService/endpoint.json'
    do {
        $process.Refresh()
        if ($process.HasExited) { throw "Editor startup failed: $($process.ExitCode)" }
        if (Test-Path $endpointPath) {
            $endpoint = Get-Content $endpointPath -Raw | ConvertFrom-Json
            if ($endpoint.pid -eq $process.Id) { break }
        }
        if ([DateTime]::UtcNow -ge $deadline) { throw 'Editor startup timeout' }
        Start-Sleep -Milliseconds 100
    } while ($true)
    $script:base = "http://127.0.0.1:$($endpoint.port)"
    $script:headers = @{ Authorization = "Bearer $($endpoint.token)" }
    Command 'play.foreground_override' @('off') | Out-Null
    Command 'play.cursor' @('show') | Out-Null
}
function Stop-Editor {
    Command 'quit'
    Assert ($process.WaitForExit(60000)) 'Editor shutdown timeout'
    Assert ($process.ExitCode -eq 0) "Editor exit: $($process.ExitCode)"
}
function Follow-Camera {
    $deadline = [DateTime]::UtcNow.AddSeconds(30)
    do {
        $camera = Command 'camera.editor'
        if ($null -ne $camera.game) { break }
        if ([DateTime]::UtcNow -ge $deadline) { throw 'Primary game camera did not become ready after Scene activation' }
        Start-Sleep -Milliseconds 100
    } while ($true)
    Command 'camera.editor' @('follow', 'on') | Out-Null
}
function Doc { Command 'material.editor' @('state') }
function Edit([string]$Name, [string[]]$Arguments = @()) {
    $doc = Doc
    Command 'material.editor' (@($Name, [string]$doc.document, [string]$doc.revision) + $Arguments)
}
function Capture([string]$Label) {
    Command 'editor.window' @('###Editor.MaterialGraph', 'close') | Out-Null
    Command 'editor.window' @('###Editor.Viewport', 'focus') | Out-Null
    $expected = Command 'material.graph' @($target)
    $deadline = [DateTime]::UtcNow.AddSeconds(150)
    $attempt = 0
    do {
        Command 'render.live.fence' @('120') | Out-Null
        $directory = Join-Path $case "$Label-$attempt"
        Command 'render.live.capture' @($directory, 'editor', 'controlled') | Out-Null
        $manifest = Get-Content (Join-Path $directory 'manifest.json') -Raw | ConvertFrom-Json
        $selected = @($manifest.draws | Where-Object route -EQ 'lattice')
        $pendingFallback = @($manifest.draws | Where-Object route -EQ 'gbuffer-pending-lattice')
        if ($pendingFallback.Count -gt 0) {
            Assert ($pendingFallback.Count -eq 1 -and $selected.Count -eq 0) 'Cold model must show one complete legacy draw before graph promotion'
            $fallbackDepth = @($manifest.attachments | Where-Object name -EQ 'depth')[0]
            Assert ($fallbackDepth.min -lt 0.999) 'Cold model fallback must write visible depth'
        }
        if ($selected.Count -eq 1 -and $selected[0].lattice.graphId -eq $expected.graph -and
            $selected[0].lattice.generation -eq $expected.generation -and
            ($selected[0].lattice.uniformBytes -join ',') -eq ($expected.uniformBytes -join ',')) { break }
        if ([DateTime]::UtcNow -ge $deadline) { throw "Requested model material did not reach the Scene frame: $Label" }
        $attempt++
    } while ($true)
    Assert ($manifest.source -eq 'product-live' -and $manifest.validationCount -eq 0) 'Actual Scene capture must be validation clean'
    Assert (@($manifest.attachments | Where-Object nonfinite -NE 0).Count -eq 0) 'Actual Scene capture must be finite'
    Assert (@($manifest.draws | Where-Object route -EQ 'lattice').Count -gt 0) 'Default model material must draw through LX'
    return $manifest
}
function Capture-Robot([string]$Label) {
    Command 'editor.window' @('###Editor.MaterialGraph', 'close') | Out-Null
    Command 'editor.window' @('###Editor.Viewport', 'focus') | Out-Null
    $before = Command 'dx12.live'
    $deadline = [DateTime]::UtcNow.AddSeconds(150)
    $attempt = 0
    do {
        Command 'render.live.fence' @('120') | Out-Null
        $directory = Join-Path $case "$Label-$attempt"
        Command 'render.live.capture' @($directory, 'editor', 'controlled') | Out-Null
        $manifest = Get-Content (Join-Path $directory 'manifest.json') -Raw | ConvertFrom-Json
        $selected = @($manifest.draws | Where-Object { $_.route -eq 'lattice' -and $_.modelId -eq $robotModel })
        $pendingFallback = @($manifest.draws | Where-Object { $_.route -eq 'gbuffer-pending-lattice' -and $_.modelId -eq $robotModel })
        if ($pendingFallback.Count -gt 0) {
            Assert ($pendingFallback.Count -eq $expectedRobotGraphs.Count -and $selected.Count -eq 0) 'Cold Robot must show every mesh together before graph promotion'
            $fallbackDepth = @($manifest.attachments | Where-Object name -EQ 'depth')[0]
            Assert ($fallbackDepth.min -lt 0.999) 'Cold Robot fallback must write visible depth'
        }
        $actualGraphs = @($selected | ForEach-Object { $_.lattice.graphId } | Sort-Object -Unique)
        if ($selected.Count -eq $expectedRobotGraphs.Count -and
            ($actualGraphs -join ',') -eq ($expectedRobotGraphs -join ',')) { break }
        if ([DateTime]::UtcNow -ge $deadline) { throw "Robot material draws did not reach a completed Scene frame: $Label" }
        $attempt++
    } while ($true)
    Assert ($manifest.source -eq 'product-live' -and $manifest.validationCount -eq 0) 'Robot actual Scene capture must be validation clean'
    Assert (@($manifest.attachments | Where-Object nonfinite -NE 0).Count -eq 0) 'Robot actual Scene capture must be finite'
    Assert (@($manifest.draws | Where-Object modelId -NE $robotModel).Count -eq 0) 'Robot capture must contain only Robot geometry'
    $depth = @($manifest.attachments | Where-Object name -EQ 'depth')[0]
    Assert ($depth.min -lt 0.999) 'Robot geometry must write visible Scene depth'
    $completed = Command 'dx12.live'
    Assert ($completed.framesRendered -gt $before.framesRendered -and
        $completed.display.scene.completedFrame -gt $before.display.scene.completedFrame) 'Robot placement must advance completed Scene frames'
    Command 'render.live.fence' @('120') | Out-Null
    $next = Command 'dx12.live'
    Assert ($next.framesRendered -gt $completed.framesRendered -and
        $next.display.scene.completedFrame -gt $completed.display.scene.completedFrame) 'Robot rendering must continue after capture'
    Assert ((Command 'dx12.validation').problems -eq 0) 'Robot skin transforms must be GPU validation clean'
    return $manifest
}
try {
    # Native model authoring must generate all of the real Robot material graphs.
    $author = Start-Process $cooker -ArgumentList @('--author-model-asset', '--asset-root', $assets, '--output',
        (Join-Path $project 'Library/ModelAssetGenerations'), '--model', $robot) -WindowStyle Hidden -Wait -PassThru `
        -RedirectStandardOutput (Join-Path $case 'author.stdout.log') -RedirectStandardError (Join-Path $case 'author.stderr.log')
    Assert ($author.ExitCode -eq 0) "Real Robot authoring failed: $(Get-Content (Join-Path $case 'author.stderr.log') -Raw)"
    $robotGraphs = @(Get-ChildItem (Join-Path $assets 'Materials/Models') -Recurse -Filter '*.shadergraph')
    Assert ($robotGraphs.Count -ge 3) 'Robot materials must have source graphs'
    foreach ($file in $robotGraphs) {
        $graph = Get-Content $file.FullName -Raw | ConvertFrom-Json
        $types = @($graph.graph.nodes.definition)
        Assert ($types -contains 'ShaderNodeBsdfPrincipled' -and $types -contains 'ShaderNodeOutputMaterial') 'Imported graph must contain connected surface and output'
        if ($graph.blackboard.identifier -contains 'ormMap') {
            Assert ($types -contains 'ShaderNodeSeparateColor') 'ORM must separate R/G/B channels'
        }
        if ($graph.blackboard.identifier -contains 'normalMap') {
            Assert ($types -contains 'ShaderNodeNormalMap') 'Normal texture must pass through Normal Map'
        }
    }
    $robotModel = (Select-String -Path ($robot + '.meta') -Pattern '^assetId: (.+)$')[0].Matches[0].Groups[1].Value
    $expectedRobotGraphs = @($robotGraphs | Where-Object { $_.Directory.Name -eq $robotModel } | ForEach-Object {
        (Select-String -Path ($_.FullName + '.meta') -Pattern '^guid: (.+)$')[0].Matches[0].Groups[1].Value
    } | Sort-Object -Unique)
    Assert ($expectedRobotGraphs.Count -eq 4) 'Robot regression must exercise all four imported materials'
    Start-Editor 'editor'
    Command 'scene.switch' @((Join-Path $assets 'Scenes/LX_CookFixture.creator')) | Out-Null
    Command 'object.delete' @('Ground') | Out-Null
    Command 'model.loadcached' @((Join-Path $assets 'Models/Prim_Plane.glb')) | Out-Null
    $placed = Command 'model.place' @('Prim_Plane')
    Follow-Camera
    $target = $placed.name
    $state = Command 'material.graph' @($target)
    Assert ($state.enabled -and $state.renderableMesh -and $state.parameters.Count -eq 0) 'New model placement must use source graph defaults without legacy overrides'
    Assert ($state.textures.Count -gt 0) 'Embedded model texture must resolve on the first placement'
    Command 'scene.select' @($target) | Out-Null
    Command 'editor.inspector' @('expand', 'on') | Out-Null
    Command 'material.editor' @('open', $target) | Out-Null
    $deadline = [DateTime]::UtcNow.AddSeconds(30)
    do {
        $doc = Doc
        if ($doc.uiFrames -ge 3) { break }
        if ([DateTime]::UtcNow -ge $deadline) { throw 'Material canvas did not render' }
        Start-Sleep -Milliseconds 100
    } while ($true)
    Assert ($doc.graph -eq $state.graph -and $doc.nodes.Count -gt 2) 'Node Editor must open the actual imported graph'
    $inspector = Command 'editor.inspector'
    Assert ($inspector.frames -gt 0 -and $inspector.entity -eq $target) 'Real Inspector must render the graph material target'
    $source = Get-Content $doc.path -Raw | ConvertFrom-Json
    $roughness = @($source.blackboard | Where-Object identifier -EQ 'roughness')[0]
    $parameterNode = @($source.graph.nodes | Where-Object { $_.properties.PSObject.Properties['parameter'] -and $_.properties.parameter -eq [string]$roughness.id })[0]
    # The first Save persists the initially fitted canvas view.
    Edit 'save' | Out-Null
    $initialHash = (Get-FileHash $doc.path).Hash
    Edit 'reload' | Out-Null
    Edit 'save' | Out-Null
    Assert ((Get-FileHash $doc.path).Hash -eq $initialHash) 'Unchanged imported graph save/reload must preserve exact source bytes'
    Edit 'value' @([string]$parameterNode.sockets[0].id, '0.27') | Out-Null
    Edit 'save' | Out-Null
    Edit 'apply' | Out-Null
    Command 'material.graph' @($target, 'set', [string]$roughness.id, '0.62') | Out-Null
    $override = Command 'material.graph' @($target)
    Assert (@($override.parameters | Where-Object id -EQ $roughness.id)[0].value -eq 0.62) 'Instance parameter override must take effect'
    Command 'undo' | Out-Null
    Assert ((Command 'material.graph' @($target)).parameters.Count -eq 0) 'Undo must restore graph source defaults'
    Command 'redo' | Out-Null
    $named = Command 'material.override' @($target, '0', 'roughness', '0.44')
    Assert ([Math]::Abs($named.resolvedValue - 0.44) -lt 0.00001) 'Existing script property binding must read/write graph parameters by identifier'
    Command 'undo' | Out-Null
    $scene = Join-Path $assets 'Scenes/ModelMaterialSoT.creator'
    Command 'scene.save' @($scene) | Out-Null
    $accepted = Command 'material.graph' @($target)
    $capture = Capture 'before-reopen'
    Assert (@($capture.draws | Where-Object { $_.route -eq 'lattice' -and $_.lattice.graphId -eq $accepted.graph }).Count -eq 1) 'Scene draw must bind the model graph identity'
    $validation = Command 'dx12.validation'
    Assert ($validation.problems -eq 0) 'Native GPU validation must remain clean'
    Stop-Editor
    $graphHash = (Get-FileHash $doc.path).Hash
    $plane = Join-Path $assets 'Models/Prim_Plane.glb'
    $reimport = Start-Process $cooker -ArgumentList @('--author-model-asset', '--asset-root', $assets, '--output',
        (Join-Path $project 'Library/ModelAssetGenerations'), '--model', $plane) -WindowStyle Hidden -Wait -PassThru `
        -RedirectStandardOutput (Join-Path $case 'reimport.stdout.log') -RedirectStandardError (Join-Path $case 'reimport.stderr.log')
    Assert ($reimport.ExitCode -eq 0 -and (Get-FileHash $doc.path).Hash -eq $graphHash) 'Reimport must preserve edited source graph exactly'
    Start-Editor 'reopen'
    Command 'scene.switch' @($scene) | Out-Null
    Follow-Camera
    $restored = Command 'material.graph' @($target)
    Assert ($restored.graph -eq $accepted.graph -and ($restored.uniformBytes -join ',') -eq ($accepted.uniformBytes -join ',')) 'Fresh Editor must restore graph binding and effective instance values'
    Assert (($restored.textures.asset -join ',') -eq ($accepted.textures.asset -join ',')) 'Embedded texture identity must survive Scene reopen'
    Capture 'after-reopen' | Out-Null
    Command 'object.delete' @($target) | Out-Null
    Command 'model.loadcached' @($robot) | Out-Null
    $robotPlacement = Command 'model.place' @('CreatorRobot')
    Assert ($robotPlacement.changed -and $robotPlacement.children.Count -gt 0) 'Real Robot placement must configure every model material graph without a fallback'
    Capture-Robot 'robot-placed' | Out-Null
    $robotScene = Join-Path $assets 'Scenes/ModelMaterialRobot.creator'
    Command 'scene.save' @($robotScene) | Out-Null
    Stop-Editor
    Start-Editor 'robot-reopen'
    Command 'scene.switch' @($robotScene) | Out-Null
    Follow-Camera
    Capture-Robot 'robot-reopened' | Out-Null
    Stop-Editor
    $case | Set-Content (Join-Path $output "model-sot-$Configuration-root.txt")
    "LX_MODEL_MATERIAL_SOT_OK configuration=$Configuration checks=$script:checks robotGraphs=$($robotGraphs.Count) robotPlaced=true robotRendered=true robotReopened=true captures=4 exit=0 root=$case"
} finally {
    $env:CREATOR_DX12_VALIDATION = $previousValidation
    if ($process -and !$process.HasExited) { Stop-Process -Id $process.Id -Force }
}
