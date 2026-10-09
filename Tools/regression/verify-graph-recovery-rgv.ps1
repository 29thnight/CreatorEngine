#Requires -Version 7.0
param(
    [ValidateSet('Debug','Release')][string]$Configuration = 'Release',
    [Parameter(Mandatory)][string]$FixtureProject,
    [Parameter(Mandatory)][string]$OutputDirectory,
    [string]$CompilerFixtureDirectory = '',
    [switch]$ShowEditor,
    [ValidateRange(0,32)][int]$PreparationSamples = 0,
    [switch]$MemorySampling
)
$ErrorActionPreference = 'Stop'
$repo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$out = [IO.Path]::GetFullPath($OutputDirectory)
if (Test-Path -LiteralPath $out) { throw 'Use a fresh output directory.' }
if (Get-Process CreatorEditor -ErrorAction SilentlyContinue) { throw 'An Editor is already running.' }
$project = Join-Path $out 'Project'
New-Item -ItemType Directory -Path $project | Out-Null
foreach ($folder in @('Assets','ProjectSetting','Saved')) {
    Copy-Item -LiteralPath (Join-Path $FixtureProject $folder) -Destination $project -Recurse
}
$modelCache = Join-Path $FixtureProject 'Library/ModelAssetGenerations'
if (Test-Path -LiteralPath $modelCache) {
    New-Item -ItemType Directory -Path "$project/Library" | Out-Null
    Copy-Item -LiteralPath $modelCache -Destination "$project/Library" -Recurse
}
# Faults exist only inside this disposable project. Preserve all failed source bytes.
$faults = @(Get-ChildItem "$project/Assets" -Recurse -Filter *.shadergraph | ForEach-Object {
    $clean = [IO.File]::ReadAllBytes($_.FullName)
    [IO.File]::WriteAllBytes($_.FullName + '.original', $clean)
    [IO.File]::WriteAllText($_.FullName, 'invalid graph fixture')
    @{path=$_.FullName; hash=(Get-FileHash -LiteralPath $_.FullName).Hash}
})
$faults | ConvertTo-Json | Set-Content "$out/faults.json" -Encoding utf8
$exe = Join-Path $repo "Bin/x64-$Configuration/Editor/CreatorEditor.exe"
$previousValidation = $env:CREATOR_DX12_VALIDATION
$env:CREATOR_DX12_VALIDATION = 'gpu'
# Compiler fixtures are commandlets; live HTTP deliberately excludes these probes.
if ($CompilerFixtureDirectory) {
    $binary = Get-Content "$CompilerFixtureDirectory/compiler-binary-hashes.json" -Raw | ConvertFrom-Json
    if ($binary.exe -ne (Get-FileHash $exe).Hash -or $binary.runtime -ne (Get-FileHash (Join-Path (Split-Path $exe) 'CreatorEditor.runtime.dll')).Hash) {
        throw 'Compiler fixture binary hash mismatch'
    }
    $fixtures = @(Get-Content "$CompilerFixtureDirectory/compiler-fixtures.jsonl" | ForEach-Object { $_ | ConvertFrom-Json })
    if ($fixtures[0].status -ne 'succeeded' -or $fixtures[0].data.log -notmatch 'RGV_READER_OK[^\r\n]*stale-scene' -or $fixtures[-1].status -ne 'succeeded') {
        throw 'Referenced compiler fixtures did not pass'
    }
    Copy-Item -LiteralPath "$CompilerFixtureDirectory/compiler-fixtures.jsonl" -Destination "$out/compiler-fixtures.jsonl"
    Copy-Item -LiteralPath "$CompilerFixtureDirectory/compiler-binary-hashes.json" -Destination "$out/compiler-binary-hashes.json"
} else {
@{exe=(Get-FileHash $exe).Hash;runtime=(Get-FileHash (Join-Path (Split-Path $exe) 'CreatorEditor.runtime.dll')).Hash} |
    ConvertTo-Json | Set-Content "$out/compiler-binary-hashes.json" -Encoding utf8
[IO.File]::WriteAllText("$out/compiler-fixtures.txt", "dx12.rendergraph`nquit`n")
$fixtureProcess = Start-Process $exe -ArgumentList @('--development-project',$project,
    '--commandlet-script',"$out/compiler-fixtures.txt",'--result-file',"$out/compiler-fixtures.jsonl") `
    -WorkingDirectory (Split-Path $exe) -WindowStyle Hidden -PassThru `
    -RedirectStandardOutput "$out/compiler-fixtures.stdout.log" -RedirectStandardError "$out/compiler-fixtures.stderr.log"
if (!$fixtureProcess.WaitForExit(600000)) {
    $fixtureProcess.Kill()
    $fixtureProcess.WaitForExit()
    $env:CREATOR_DX12_VALIDATION = $previousValidation
    throw 'Compiler fixture timeout'
}
$fixtures = @(Get-Content "$out/compiler-fixtures.jsonl" | ForEach-Object { $_ | ConvertFrom-Json })
if ($fixtureProcess.ExitCode -ne 0 -or $fixtures.Count -ne 2 -or $fixtures[0].status -ne 'succeeded' -or $fixtures[0].data.log -notmatch 'RGV_READER_OK[^\r\n]*stale-scene') {
    $env:CREATOR_DX12_VALIDATION = $previousValidation
    throw 'RG-V compiler fixtures failed'
}
}
$start = [Diagnostics.ProcessStartInfo]::new($exe)
$liveArguments = @('--development-project',$project,'--command-service')
if (!$ShowEditor) { $liveArguments += '--smoke-offscreen' }
foreach ($arg in $liveArguments) {
    $start.ArgumentList.Add($arg)
}
$start.WorkingDirectory = Split-Path $exe
$start.UseShellExecute = $false
$start.CreateNoWindow = $true
$start.RedirectStandardOutput = $true
$start.RedirectStandardError = $true
if ($MemorySampling)
{
    $start.Environment['CREATOR_GPU_MEMORY_SAMPLES'] = Join-Path $out 'memory-continuous.jsonl'
}
$process = [Diagnostics.Process]::Start($start)
$stdout = $process.StandardOutput.ReadToEndAsync()
$stderr = $process.StandardError.ReadToEndAsync()
$report = [ordered]@{configuration=$Configuration; compilerFixtures=$true; complete=$false; failures=@(); graphSamples=@(); captures=@()}
try {
    $deadline = [DateTime]::UtcNow.AddSeconds(180)
    do {
        if ($process.HasExited) { throw "Startup exited: $($process.ExitCode)" }
        $endpoint = if (Test-Path "$project/Library/CommandService/endpoint.json") {
            Get-Content "$project/Library/CommandService/endpoint.json" -Raw | ConvertFrom-Json
        } else { $null }
        if ([DateTime]::UtcNow -gt $deadline) { throw 'Endpoint timeout' }
        Start-Sleep -Milliseconds 100
    } until ($endpoint -and $endpoint.pid -eq $process.Id)
    $base = "http://127.0.0.1:$($endpoint.port)"
    $headers = @{Authorization="Bearer $($endpoint.token)"}
    $session = [Microsoft.PowerShell.Commands.WebRequestSession]::new()
    function Cmd([string]$name, [string[]]$arguments=@(), [bool]$allowPreparing=$false) {
        if ($MemorySampling)
        {
            @{command=$name; args=@($arguments); utcMs=[DateTimeOffset]::UtcNow.ToUnixTimeMilliseconds()} |
                ConvertTo-Json -Compress | Add-Content "$out/memory-stages.jsonl" -Encoding utf8
        }
        $body = @{command=$name;args=@($arguments);mode='async'} | ConvertTo-Json -Compress
        $result = Invoke-RestMethod "$base/command" -Headers $headers -WebSession $session -Method Post -ContentType 'application/json' -Body $body -TimeoutSec 650
        if ($name -eq 'quit' -and $result.operationId) {
            @{command=$name;args=@($arguments);result=$result} | ConvertTo-Json -Depth 50 -Compress | Add-Content "$out/results.jsonl" -Encoding utf8
            return $result
        }
        if ($result.operationId) {
            $poll = $result.poll
            $until = [DateTime]::UtcNow.AddSeconds(650)
            do {
                Start-Sleep -Milliseconds 100
                $result = Invoke-RestMethod "$base$poll" -Headers $headers -WebSession $session
                if ([DateTime]::UtcNow -gt $until) { throw "$name timeout" }
            } until ($result.state -eq 'completed')
        }
        @{command=$name;args=@($arguments);result=$result} | ConvertTo-Json -Depth 50 -Compress | Add-Content "$out/results.jsonl" -Encoding utf8
        if ($allowPreparing -and $result.code -eq 'render.pbr.capture.failed' -and $result.message -eq 'LX Scene requested material program is still preparing.') { return $null }
        if ($result.status -ne 'succeeded') { throw "$name failed: $($result.code) $($result.message)" }
        return $result.data
    }
    function Graph([string]$target) {
        $until = [DateTime]::UtcNow.AddSeconds(180)
        do {
            $graph = Cmd 'render.graph' @($target)
            if ($graph.ready) { return $graph }
            Cmd 'render.live.fence' @('180') | Out-Null
            if ([DateTime]::UtcNow -gt $until) { throw "$target compiled graph timeout" }
            Start-Sleep -Milliseconds 250
        } while ($true)
    }
    Cmd 'scene.switch' @("$project/Assets/Scenes/LX_CookFixture.creator") | Out-Null
    Cmd 'render.live.fence' @('600') | Out-Null
    Cmd 'camera.editor' @('follow','on') | Out-Null
    Cmd 'render.environment' @('background','off') | Out-Null
    Cmd 'profile.pause' | Out-Null
    $material = Cmd 'material.graph' @('Ground')
    if (!$material.enabled -or !$material.renderableMesh -or $material.graph -eq '11111111-1111-4111-8111-111111111111') {
        throw 'Failed scene graph did not retain mesh with a new graph owner'
    }
    $report.recoveredSceneGraph = $material.graph
    $modelLoaded = Cmd 'model.loadcached' @("$project/Assets/Models/CreatorRobot.glb")
    $placed = Cmd 'model.place' @('CreatorRobot')
    $report.modelPlaced = $placed
    # Save/reload must resolve the published recovery graph, without creating another replacement.
    $saved = "$project/Assets/Scenes/Recovered.creator"
    Cmd 'scene.save' @($saved) | Out-Null
    $before = @(Get-ChildItem "$project/Assets/Materials" -Filter Recovered_*.shadergraph).Count
    Cmd 'scene.switch' @($saved) | Out-Null
    Cmd 'render.live.fence' @('600') | Out-Null
    $reloaded = Cmd 'material.graph' @('Ground')
    $after = @(Get-ChildItem "$project/Assets/Materials" -Filter Recovered_*.shadergraph).Count
    if ($reloaded.graph -ne $material.graph -or $before -ne $after) { throw 'Recovery graph failed scene save/reload' }
    $report.recoveryGraphs = $after
    $document = Cmd 'material.editor' @('open','Ground')
    if (!$document.editable -or $document.nodes.Count -ne 2 -or $document.links.Count -ne 1) { throw 'Default graph is not an editable Principled/Output graph' }
    # The first UI draw fits the canvas and advances the document revision.
    Start-Sleep -Milliseconds 500
    $document = Cmd 'material.editor' @('state')
    $roughness = ($document.nodes | Where-Object type -eq 'ShaderNodeBsdfPrincipled').pins | Where-Object name -eq 'Roughness'
    Cmd 'material.editor' @('value',"$($document.document)","$($document.revision)","$($roughness.id)",'0.25') | Out-Null
    $document = Cmd 'material.editor' @('state')
    Cmd 'material.editor' @('save',"$($document.document)","$($document.revision)") | Out-Null
    $document = Cmd 'material.editor' @('state')
    Cmd 'material.editor' @('apply',"$($document.document)","$($document.revision)") | Out-Null
    $report.editedGraph = Cmd 'material.graph' @('Ground')
    if ($report.editedGraph.generation -le $reloaded.generation) { throw 'Authored graph edit did not publish a new runtime generation' }
    Cmd 'render.live.fence' @('600') | Out-Null
    $captureAttempt = 0
    $captureDeadline = [DateTime]::UtcNow.AddSeconds(600)
    do {
        $capturePath = "$out/capture-$captureAttempt"
        $captureReady = Cmd 'render.live.capture' @($capturePath,'editor','controlled') $true
        $capture = if ($captureReady) { Get-Content "$capturePath/manifest.json" -Raw | ConvertFrom-Json } else { $null }
        if ($capture.draws.Count -ge 5) { break }
        if ([DateTime]::UtcNow -gt $captureDeadline) { throw 'Recovered graph GPU preparation timeout' }
        Start-Sleep -Seconds 1
        Cmd 'render.live.fence' @('600') | Out-Null
        ++$captureAttempt
    } while ($true)
    if ($capture.draws.Count -lt 5 -or @($capture.draws | Where-Object route -ne 'lattice').Count) {
        throw 'Imported mesh or graph-only material draws are missing'
    }
    $editedDraws = @($capture.draws | Where-Object { $_.lattice.graphId -eq $report.editedGraph.graph -and $_.lattice.generation -eq $report.editedGraph.generation })
    if ($editedDraws.Count -ne 1) { throw 'Edited graph generation was not consumed by the captured Ground draw' }
    $modelDraws = @($capture.draws | Where-Object modelId -eq $modelLoaded.modelId)
    if ($modelDraws.Count -ne 4 -or @($modelDraws.meshId | Select-Object -Unique).Count -ne 4) { throw 'CreatorRobot mesh capture is incomplete' }
    $report.captures += @{path=$capturePath;draws=$capture.draws.Count;modelDraws=$modelDraws.Count}
    $report.preparationSamples = @()
    for ($sample = 0; $sample -lt $PreparationSamples; ++$sample)
    {
        Cmd 'render.live.fence' @('600') | Out-Null
        $started = [DateTime]::UtcNow.ToString('o')
        $samplePath = "$out/preparation-sample-$sample"
        Cmd 'render.live.capture' @($samplePath,'editor','controlled') | Out-Null
        $measured = Get-Content "$samplePath/manifest.json" -Raw | ConvertFrom-Json
        if (!($measured.measurement.cpuTransientPrepareMs -gt 0) -or
            [double]::IsInfinity([double]$measured.measurement.cpuTransientPrepareMs))
        {
            throw 'Capture is missing a finite positive transient preparation measurement.'
        }
        $report.preparationSamples += @{index=$sample; path=$samplePath; startedUtc=$started;
            completedUtc=[DateTime]::UtcNow.ToString('o'); measurement=$measured.measurement;
            graph=$measured.graph; width=$measured.width; height=$measured.height}
    }
    Cmd 'editor.panelcost' @('reset') | Out-Null
    Cmd 'editor.window' @('###Editor.RenderPass','open') | Out-Null
    foreach ($target in @('scene','game','preview')) {
        if ($target -eq 'game') { Cmd 'editor.window' @('###Editor.GamePreview','open') | Out-Null }
        if ($target -eq 'preview') {
            Cmd 'material.editor' @('preview','on') | Out-Null
            $previewDeadline = [DateTime]::UtcNow.AddSeconds(180)
            do {
                $previewState = Cmd 'material.editor' @('state')
                if ($previewState.preview.ready) { break }
                if ([DateTime]::UtcNow -gt $previewDeadline) { throw 'Material preview preparation timeout' }
                Cmd 'render.live.fence' @('180') | Out-Null
            } while ($true)
            # Request the graph after the cached image is complete, to test late reader admission.
        }
        $matchingBefore = (Cmd 'render.graph' @($target)).uiMatchingFrames
        Cmd 'render.graph' @('view',$target) | Out-Null
        $until = [DateTime]::UtcNow.AddSeconds(180)
        do {
            $graph = Graph $target
            $expected = @('scene','game','preview').IndexOf($target)
            if ($graph.uiTarget -eq $expected -and $graph.uiMatchingFrames -gt $matchingBefore) { break }
            if ([DateTime]::UtcNow -gt $until) { throw "$target UI did not draw its graph" }
            Start-Sleep -Milliseconds 250
        } while ($true)
        if (!$graph.edges.Count -or !$graph.resources.Count -or !$graph.passes.Count) { throw 'Viewer lacks compiler structure' }
        if ($graph.storageBytes -gt 4194304 -or $graph.copyMs -gt 10) { throw 'Viewer copy exceeds 4 MiB / 10 ms bound' }
        $graph | ConvertTo-Json -Depth 50 | Set-Content "$out/graph-$target.json" -Encoding utf8
        $report.graphSamples += @{target=$target;view=$graph.view;frame=$graph.frame;bytes=$graph.storageBytes;copyMs=$graph.copyMs;uiFrames=$graph.uiFrames}
    }
    Cmd 'render.graph' @('view','scene') | Out-Null
    Cmd 'editor.window' @('###Editor.Viewport','focus') | Out-Null
    Cmd 'editor.renderscale' @('off') | Out-Null
    Cmd 'render.live.fence' @('600') | Out-Null
    $previousSceneGraph = Graph 'scene'
    Cmd 'scene.switch' @($saved) | Out-Null
    Cmd 'render.live.fence' @('600') | Out-Null
    $sceneDeadline = [DateTime]::UtcNow.AddSeconds(180)
    do {
        $original = Graph 'scene'
        if ($original.sceneEpoch -ne $previousSceneGraph.sceneEpoch) { break }
        if ([DateTime]::UtcNow -gt $sceneDeadline) { throw 'Viewer retained a graph from the previous scene' }
        Start-Sleep -Milliseconds 250
    } while ($true)
    $report.sceneChange = @{before=$previousSceneGraph.sceneEpoch;after=$original.sceneEpoch}
    $original = Graph 'scene'
    Cmd 'editor.renderscale' @('0.5') | Out-Null
    $until = [DateTime]::UtcNow.AddSeconds(120)
    do {
        $resized = Graph 'scene'
        if ($resized.width -ne $original.width -or $resized.height -ne $original.height) { break }
        if ([DateTime]::UtcNow -gt $until) { throw 'Viewer failed to replace resized graph' }
        Start-Sleep -Milliseconds 250
    } while ($true)
    $report.resize = @{before=@($original.width,$original.height);after=@($resized.width,$resized.height);view=$resized.view}
    Start-Sleep -Seconds 1
    $report.viewerOnCosts = Cmd 'editor.panelcost'
    $windowCosts = @($report.viewerOnCosts.windows | Where-Object { $_.id -match 'RenderPass' })
    if (!$windowCosts.Count -or @($windowCosts | Where-Object { $_.samples -gt 0 -and $_.p95Ms -le 10 }).Count -ne $windowCosts.Count) {
        throw 'Viewer UI cost exceeds 10 ms or has no real UI samples'
    }
    Cmd 'editor.window' @('###Editor.RenderPass','close') | Out-Null
    Start-Sleep -Seconds 1
    $closed = Cmd 'render.graph' @('scene')
    Start-Sleep -Seconds 1
    $closedAgain = Cmd 'render.graph' @('scene')
    if ($closed.uiFrames -ne $closedAgain.uiFrames) { throw 'Closed viewer still draws UI' }
    Cmd 'editor.panelcost' @('reset') | Out-Null
    Start-Sleep -Seconds 1
    $report.viewerOffCosts = Cmd 'editor.panelcost'
    foreach ($fault in $faults) {
        if ((Get-FileHash -LiteralPath $fault.path).Hash -ne $fault.hash) { throw 'Recovery overwrote failed source' }
    }
    $validation = Cmd 'dx12.validation'
    if (!$validation.layerEnabled -or $validation.mode -ne 'gpu' -or $validation.problems -ne 0 -or $validation.droppedMessages -ne 0) {
        throw 'GPU validation is missing or reported a problem'
    }
    $report.validation = $validation
    try { Cmd 'quit' | Out-Null } catch { $report.quitTransportError = $_.Exception.Message }
    if (!$process.WaitForExit(600000) -or $process.ExitCode -ne 0) { throw 'Editor did not exit cleanly' }
    $report.exitCode = $process.ExitCode
    $report.complete = $true
} catch {
    $report.failures += $_.Exception.Message
    throw
} finally {
    $env:CREATOR_DX12_VALIDATION = $previousValidation
    if (!$process.HasExited) { $process.Kill(); $process.WaitForExit(15000) | Out-Null }
    [IO.File]::WriteAllText("$out/editor.stdout.log", $stdout.GetAwaiter().GetResult())
    [IO.File]::WriteAllText("$out/editor.stderr.log", $stderr.GetAwaiter().GetResult())
    $report | ConvertTo-Json -Depth 50 | Set-Content "$out/result.json" -Encoding utf8
}
