[CmdletBinding()]
param(
    [string]$Exe = (Join-Path $PSScriptRoot '../../Bin/x64-Debug/Editor/CreatorEditor.exe'),
    [string]$Work = (Join-Path $env:TEMP 'creator-command-surface'),
    [switch]$SkipCommandlets,
    [string]$MaterialScene = "",
    [string]$MaterialTarget = ""
)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
if ([string]::IsNullOrWhiteSpace($MaterialScene) -ne [string]::IsNullOrWhiteSpace($MaterialTarget)) { throw 'Provide both MaterialScene and MaterialTarget for the optional real-material Undo leg.' }
$Exe = [IO.Path]::GetFullPath($Exe)
$Work = [IO.Path]::GetFullPath($Work)
$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$endpointPath = Join-Path $repoRoot 'Dynamic_CPP/Library/CommandService/endpoint.json'
New-Item -ItemType Directory -Force -Path $Work | Out-Null
# The project has one endpoint file. Never stop an editor owned by the developer.
if (Get-Process CreatorEditor -ErrorAction SilentlyContinue) { throw 'Close the existing editor before running this isolated gate.' }
$prefabName = "__CommandSurface_$PID"
$prefabDirectory = Join-Path $repoRoot 'Dynamic_CPP/Assets/Prefabs'
$prefabFiles = @('.prefab','.prefab.meta') | ForEach-Object { Join-Path $prefabDirectory ($prefabName+$_) }
foreach ($file in $prefabFiles) { if (Test-Path -LiteralPath $file) { throw "Gate artifact already exists: $file" } }
$responsePath = Join-Path $Work 'responses.jsonl'
if (Test-Path -LiteralPath $responsePath) { Remove-Item -LiteralPath $responsePath }
$script:checks = 0
function Assert([bool]$Condition, [string]$Message) {
    if (-not $Condition) { throw $Message }
    $script:checks++
}
function Start-GateEditor([string[]]$Arguments, [string]$Label) {
    Start-Process -FilePath $Exe -ArgumentList $Arguments -WorkingDirectory (Split-Path $Exe) -WindowStyle Hidden `
        -RedirectStandardOutput (Join-Path $Work "$Label.out") -RedirectStandardError (Join-Path $Work "$Label.err") -PassThru
}
if (-not $SkipCommandlets) {
    $cases = @(
        @{ Name='list'; Exit=0 },
        @{ Name='experiment.matresolve'; Exit=0 },
        @{ Name='experiment.matmigrate'; Exit=0 },
        @{ Name='experiment.matscript'; Exit=0 },
        @{ Name='dx12.bench11'; Exit=2 },
        @{ Name='dx12.encoderbench'; Exit=2 },
        @{ Name='dx12.ssaoscale'; Exit=2 },
        @{ Name='dx12.postscale'; Exit=2 },
        @{ Name='dx12.forwardscale'; Exit=2 },
        @{ Name='perf.reflect'; Exit=2 },
        @{ Name='experiment.animlive'; Exit=2 },
        @{ Name='tag.authoring.probe'; Exit=2 },
        @{ Name='dx12.selftest'; Exit=2 },
        @{ Name='vk.selftest'; Exit=2 },
        @{ Name='rhi.uploadsegments'; Exit=2 },
        @{ Name='scene.transformbulk'; Exit=2 },
        @{ Name='experiment.foliage'; Exit=2 },
        @{ Name='missing'; Exit=2 },
        @{ Name='experiment.matparity'; Exit=2 },
        @{ Name='list'; Exit=2; Conflict=$true }
    )
    $i = 0
    foreach ($case in $cases) {
        $label = "commandlet-$i"
        $resultPath = Join-Path $Work "$label.jsonl"
        if (Test-Path -LiteralPath $resultPath) { Remove-Item -LiteralPath $resultPath }
        $arguments = @('--commandlet', $case.Name, '--', '--result-file', ('"'+$resultPath+'"'))
        if ($case.ContainsKey('Conflict')) { $arguments += '--command-service' }
        $proc = Start-GateEditor $arguments $label
        try {
            if (-not $proc.WaitForExit(180000)) { throw "Commandlet timed out: $($case.Name)" }
            $proc.Refresh()
            Assert ($proc.ExitCode -eq $case.Exit) "Commandlet $($case.Name) exit $($proc.ExitCode), expected $($case.Exit)"
            $results = @(Get-Content -LiteralPath $resultPath | ForEach-Object { $_ | ConvertFrom-Json })
            Assert ($results.Count -eq 1) 'Commandlet must emit exactly one terminal result'
            if ($case.Name -eq 'list' -and $case.Exit -eq 0) {
                Assert ($results[0].data.names -contains 'experiment.matresolve' -and $results[0].data.names -contains 'scene.hierarchymutation' -and $results[0].data.names -contains 'dx12.selftest') 'Commandlet discovery is missing a domain'
                Assert (-not ($results[0].data.names -contains 'object.rename')) 'Product editing leaked into Commandlet discovery'
                foreach ($name in @('model.async', 'render.pbr.capture', 'render.pbr.parity', 'render.pbr.coverage', 'render.pbr.occlusion', 'render.pbr.emission', 'render.pbr.transform', 'render.pbr.uv')) {
                    Assert ($results[0].data.names -contains $name) "Integrated Commandlet missing: $name"
                }
            }
            if ($case.Exit -eq 0 -and $case.Name -ne 'list') {
                Assert ($results[0].data.synthetic -and $results[0].data.real) 'Both commandlet legs must execute and pass'
            }
        } finally { if (-not $proc.HasExited) { $proc.Kill(); $proc.WaitForExit() } }
        $i++
    }
}
if (-not $SkipCommandlets) {
    $scenario = Join-Path $Work 'commandlet-scenario.txt'
    $output = Join-Path $Work 'commandlet-scenario.jsonl'
    @('scene.new CommandletScenario', 'object.create ScenarioObject', 'scene.hierarchycheck', 'scene.transformwritestats.check probe') | Set-Content -LiteralPath $scenario -Encoding utf8
    if (Test-Path -LiteralPath $output) { Remove-Item -LiteralPath $output }
    $proc = Start-GateEditor @('--commandlet-script', ('"'+$scenario+'"'), '--result-file', ('"'+$output+'"')) 'scenario'
    try {
        Assert ($proc.WaitForExit(180000)) 'Commandlet scenario must terminate without quit'
        Assert ($proc.ExitCode -eq 0) "Commandlet scenario failed: $($proc.ExitCode)"
        $results = @(Get-Content -LiteralPath $output | ConvertFrom-Json)
        Assert ($results.Count -eq 4 -and @($results | Where-Object status -ne succeeded).Count -eq 0) 'Scenario did not execute all preparation and verification steps'
    } finally { if (-not $proc.HasExited) { $proc.Kill(); $proc.WaitForExit() } }
    $ordinary = Join-Path $Work 'ordinary-batch.txt'
    $ordinaryResults = Join-Path $Work 'ordinary-batch.jsonl'
    @('scene.hierarchymutation probe','model.async status','render.pbr.parity','render.pbr.capture','quit') | Set-Content -LiteralPath $ordinary -Encoding utf8
    if (Test-Path -LiteralPath $ordinaryResults) { Remove-Item -LiteralPath $ordinaryResults }
    $proc = Start-GateEditor @('--script', ('"'+$ordinary+'"'), '--result-format', 'jsonl', '--result-file', ('"'+$ordinaryResults+'"')) 'ordinary-batch'
    try {
        Assert ($proc.WaitForExit(180000)) 'Ordinary batch did not terminate'
        Assert ($proc.ExitCode -eq 2) 'Ordinary batch must reject a Commandlet name'
        $results = @(Get-Content -LiteralPath $ordinaryResults | ConvertFrom-Json)
        Assert ($results[0].code -eq 'command.unknown') 'Commandlet leaked into ordinary batch execution'
        Assert ($results.Count -eq 5 -and @($results[0..3] | Where-Object code -ne 'command.unknown').Count -eq 0) 'Integrated Commandlets leaked into ordinary batch execution'
    } finally { if (-not $proc.HasExited) { $proc.Kill(); $proc.WaitForExit() } }
    $output = Join-Path $Work 'commandlet-missing-script.jsonl'
    if (Test-Path -LiteralPath $output) { Remove-Item -LiteralPath $output }
    $proc = Start-GateEditor @('--commandlet-script', ('"'+(Join-Path $Work 'does-not-exist.txt')+'"'), '--result-file', ('"'+$output+'"')) 'missing-script'
    try {
        Assert ($proc.WaitForExit(180000)) 'Missing script must terminate'
        Assert ($proc.ExitCode -eq 2) 'Missing Commandlet script must fail with exit 2'
        $result = Get-Content -LiteralPath $output | ConvertFrom-Json
        Assert ($result.code -eq 'commandlet.script_missing') 'Missing script needs a terminal result'
    } finally { if (-not $proc.HasExited) { $proc.Kill(); $proc.WaitForExit() } }
}
$proc = Start-GateEditor @('--command-service') 'http'
try {
    $info = $null
    $deadline = (Get-Date).AddSeconds(150)
    while ((Get-Date) -lt $deadline -and -not $proc.HasExited) {
        if (Test-Path -LiteralPath $endpointPath) {
            try {
                $candidate = Get-Content -LiteralPath $endpointPath -Raw | ConvertFrom-Json
                if ($candidate.pid -eq $proc.Id) { $info = $candidate; break }
            } catch {}
        }
        Start-Sleep -Milliseconds 200
    }
    if (-not $info) { throw 'HTTP endpoint did not become ready' }
    $base = "http://127.0.0.1:$($info.port)"
    $headers = @{ Authorization="Bearer $($info.token)" }
    # The endpoint is published before Editor startup finishes. Product commands
    # need an advancing game loop; discovery and health do not.
    $ready = $false
    $deadline = (Get-Date).AddSeconds(150)
    $firstHealth = Invoke-RestMethod "$base/health" -Headers $headers -TimeoutSec 5
    do {
        $health = Invoke-RestMethod "$base/health" -Headers $headers -TimeoutSec 5
        if ($health.frame -ge 2 -and $health.frame -gt $firstHealth.frame -and $health.state -eq 'idle') {
            $ready = $true
            break
        }
        Start-Sleep -Milliseconds 100
    } while ((Get-Date) -lt $deadline -and -not $proc.HasExited)
    Assert $ready 'HTTP endpoint was available, but the Editor game loop did not become ready'
    Write-Output "HTTP ready: firstFrame=$($firstHealth.frame) readyFrame=$($health.frame)"
    function Invoke-Body([hashtable]$Body) {
        $Body.mode = 'sync'
        $response = Invoke-WebRequest "$base/command" -Method Post -Headers $headers -ContentType 'application/json' `
            -Body ($Body | ConvertTo-Json -Depth 10 -Compress) -SkipHttpErrorCheck -TimeoutSec 45
        Add-Content -LiteralPath (Join-Path $Work "responses.jsonl") -Value $response.Content
        return ($response.Content | ConvertFrom-Json)
    }
    function Invoke-Cmd([string]$Name, [string[]]$Arguments = @()) { Invoke-Body @{command=$Name;args=$Arguments} }
    function Expect-Ok($Result) { Assert ($Result.status -eq 'succeeded') "Unexpected result: $($Result | ConvertTo-Json -Compress -Depth 8)" }
    function Read-Object([string]$Target) { Invoke-Body @{ command='object.describe'; parameters=@{target=$Target} } }
    $schema = Invoke-RestMethod "$base/commands/object.rename" -Headers $headers
    Assert ($schema.undoable -and $schema.inputSchema.properties.name.type -eq 'string') 'Rename discovery schema missing'
    $discovery = Invoke-RestMethod "$base/commands" -Headers $headers
    Assert (-not ($discovery.commands.name -contains 'selftest')) 'selftest leaked into live discovery'
    $light = Invoke-Body @{command='light.proxy';parameters=@{}}
    Expect-Ok $light
    Assert ($light.data.count -ge 0 -and $null -ne $light.data.lights) 'Light proxy must return an owned snapshot'
    Assert ($null -ne $light.data.publish -and $null -ne $light.data.queued) 'Light publication metrics are missing'
    Assert ((Invoke-Cmd 'light.proxy' @('extra')).status -eq 'invalid_arguments') 'Light proxy accepted unused arguments'
    Expect-Ok (Invoke-Cmd 'scene.new' @('CommandSurfaceGate'))
    Expect-Ok (Invoke-Cmd 'object.create' @('Before name'))
    $initial = Read-Object 'Before name'; Expect-Ok $initial
    $id = $initial.data.id
    $depth = (Invoke-Cmd 'undo.state').data.editUndo
    $rename = Invoke-Body @{command='object.rename';parameters=@{target=$id;name='After name'}}
    Expect-Ok $rename
    Assert ($rename.data.id -eq $id -and $rename.data.changed) 'Rename lost identity or did not mutate'
    Assert ((Invoke-Cmd 'undo.state').data.editUndo -eq $depth+1) 'Rename must create one undo record'
    Expect-Ok (Invoke-Cmd 'undo')
    Assert ((Read-Object $id).data.name -eq 'Before name') 'Undo did not restore the name'
    Expect-Ok (Invoke-Cmd 'redo')
    Assert ((Read-Object $id).data.name -eq 'After name') 'Redo did not restore the edit'
    $noop = Invoke-Body @{command='object.rename';parameters=@{target=$id;name='After name'}}
    Expect-Ok $noop
    Assert (-not $noop.data.changed) 'Same-name edit must be a no-op'
    Assert ((Invoke-Cmd 'undo.state').data.editUndo -eq $depth+1) 'No-op polluted history'
    foreach ($bad in @(
        @{command='object.rename';parameters=@{target=$id;name=42}},
        @{command='object.rename';parameters=@{target=$id;name=''}},
        @{command='object.rename';parameters=@{target=$id;name='Bad';extra='x'}},
        @{command='object.rename';parameters=@{target=$id}},
        @{command='object.rename';parameters=@{target=$id;name='Bad'};args=@()},
        @{command='object.rename';args=@('@4294967296:1:1','Bad')}
    )) {
        $result = Invoke-Body $bad
        Assert ($result.code -in @('request.parameters_invalid','request.parameters_conflict','object.id_invalid')) 'Invalid input must fail before mutation'
    }
    Assert ((Read-Object $id).data.name -eq 'After name') 'Invalid input changed the object'
    Assert ((Invoke-Cmd 'undo.state').data.editUndo -eq $depth+1) 'Invalid input polluted history'
    Expect-Ok (Invoke-Cmd 'undo')
    Expect-Ok (Invoke-Body @{command='object.rename';parameters=@{target=$id;name='Branch'}})
    Assert ((Invoke-Cmd 'undo.state').data.editRedo -eq 0) 'A new edit must invalidate redo'
    Expect-Ok (Invoke-Cmd 'object.create' @('Duplicate'))
    $second = (Read-Object 'Duplicate').data.id
    Expect-Ok (Invoke-Body @{command='object.rename';parameters=@{target=$second;name='Branch'}})
    Assert ((Read-Object 'Branch').code -eq 'object.ambiguous') 'Duplicate names must not silently choose an object'
    Assert ((Read-Object $id).data.name -eq 'Branch') 'Stable id must resolve ambiguous names'
    foreach ($name in @('selftest','experiment.matparity','experiment.matresolve','dx12.bench11','dx12.encoderbench','dx12.ssaoscale','dx12.postscale','dx12.forwardscale','perf.reflect','experiment.animlive','tag.authoring.probe','assets.decodeab','assets.decodeabhdr','assets.texturebench','vk.texturecodec')) {
        Assert ((Invoke-Cmd $name).code -eq 'command.unknown') "Commandlet/retired harness leaked into HTTP: $name"
    }
    # Verify state restoration, not only the existence of undo records.
    Expect-Ok (Invoke-Cmd 'scene.new' @('EditHistory'))
    Expect-Ok (Invoke-Body @{command='object.create';parameters=@{name='History'}})
    Expect-Ok (Invoke-Body @{command='object.rename';parameters=@{target='History';name='History edited'}})
    Expect-Ok (Invoke-Cmd 'undo'); Expect-Ok (Invoke-Cmd 'undo')
    Assert ((Read-Object 'History').code -eq 'object.not_found') 'Undo create left the object alive'
    Expect-Ok (Invoke-Cmd 'redo'); Expect-Ok (Invoke-Cmd 'redo')
    Expect-Ok (Read-Object 'History edited')
    $root = (Read-Object 'History edited').data.id
    Expect-Ok (Invoke-Body @{command='object.create';parameters=@{name='Child';type='Light'}})
    $child = (Read-Object 'Child').data.id
    Expect-Ok (Invoke-Body @{command='object.parent';parameters=@{target=$child;parent=$root}})
    Assert ((Read-Object $child).data.parent -eq $root) 'Parent operation did not update hierarchy'
    Expect-Ok (Invoke-Cmd 'undo')
    Assert ((Read-Object $child).data.parent -ne $root) 'Undo parent did not restore hierarchy'
    Expect-Ok (Invoke-Cmd 'redo')
    Assert ((Read-Object $child).data.parent -eq $root) 'Redo parent did not restore hierarchy'
    $depth = (Invoke-Cmd 'undo.state').data.editUndo
    Assert ((Invoke-Body @{command='object.parent';parameters=@{target=$root;parent=$child}}).code -eq 'object.parent.cycle') 'Cycle was accepted'
    Assert ((Invoke-Cmd 'undo.state').data.editUndo -eq $depth) 'Failed parent operation polluted history'
    Expect-Ok (Invoke-Body @{command='object.transform';parameters=@{target=$child;position=@(1,2,3);rotation=@(10,20,30);scale=@(2,3,4)}})
    Assert (((Read-Object $child).data.position -join ',') -eq '1,2,3') 'Typed position was not applied'
    Expect-Ok (Invoke-Cmd 'undo')
    Assert (((Read-Object $child).data.position -join ',') -eq '0,0,0') 'Undo transform failed'
    Expect-Ok (Invoke-Cmd 'redo')
    Assert (((Read-Object $child).data.scale -join ',') -eq '2,3,4') 'Redo transform failed'
    $depth = (Invoke-Cmd 'undo.state').data.editUndo
    Assert ((Invoke-Cmd 'object.transform' @($child,'garbage','0','0')).status -eq 'invalid_arguments') 'Invalid positional number was accepted'
    Assert ((Invoke-Body @{command='object.transform';parameters=@{target=$child;position=@(1,2)}}).code -eq 'request.parameters_invalid') 'Malformed vector was accepted'
    Assert ((Invoke-Body @{command='object.property';parameters=@{target=$child;component='LightComponent';field='m_intencity';value='garbage'}}).status -eq 'invalid_arguments') 'Invalid property number was accepted'
    Assert ((Invoke-Cmd 'undo.state').data.editUndo -eq $depth) 'Invalid values polluted history'
    Expect-Ok (Invoke-Body @{command='object.property';parameters=@{target=$child;component='LightComponent';field='m_intencity';value=4.5}})
    $property = Invoke-Body @{command='object.properties';parameters=@{target=$child;component='LightComponent'}}
    Expect-Ok $property
    Assert ([double]$property.data.values.m_intencity -eq 4.5) 'Property setter did not apply the value'
    Expect-Ok (Invoke-Cmd 'undo')
    $before = Invoke-Body @{command='object.properties';parameters=@{target=$child;component='LightComponent'}}
    Assert ([double]$before.data.values.m_intencity -ne 4.5) 'Undo property failed'
    Expect-Ok (Invoke-Cmd 'redo')
    $after = Invoke-Body @{command='object.properties';parameters=@{target=$child;component='LightComponent'}}
    Assert ([double]$after.data.values.m_intencity -eq 4.5) 'Redo property failed'
    Expect-Ok (Invoke-Body @{command='object.delete';parameters=@{target=$root}})
    Assert ((Read-Object $root).code -eq 'object.stale' -and (Read-Object $child).code -eq 'object.stale') 'Delete did not remove the subtree'
    Expect-Ok (Invoke-Cmd 'undo')
    $restoredRoot = (Read-Object 'History edited').data.id
    $restoredChild = Read-Object 'Child'; Expect-Ok $restoredChild
    Assert ($restoredChild.data.parent -eq $restoredRoot) 'Delete undo lost the child hierarchy'
    Assert (($restoredChild.data.position -join ',') -eq '1,2,3') 'Delete undo lost transform state'
    $property = Invoke-Body @{command='object.properties';parameters=@{target='Child';component='LightComponent'}}
    Assert ([double]$property.data.values.m_intencity -eq 4.5) 'Delete undo lost component state'
    Expect-Ok (Invoke-Cmd 'undo') # the property edit preceding deletion must still address the restored component
    $property = Invoke-Body @{command='object.properties';parameters=@{target='Child';component='LightComponent'}}
    Assert ([double]$property.data.values.m_intencity -ne 4.5) 'Earlier history lost the restored component identity'
    Expect-Ok (Invoke-Cmd 'redo')
    Expect-Ok (Invoke-Body @{command='object.duplicate';parameters=@{target=$restoredRoot;name='Clone'}})
    $clone = Read-Object 'Clone'; Expect-Ok $clone
    Assert ($clone.data.children.Count -eq 1) 'Duplicate lost the subtree'
    $cloneChild = $clone.data.children[0]
    Assert (((Read-Object $cloneChild).data.position -join ',') -eq '1,2,3') 'Duplicate lost child transform'
    Expect-Ok (Invoke-Cmd 'undo')
    Assert ((Read-Object 'Clone').code -eq 'object.not_found') 'Undo duplicate failed'
    Expect-Ok (Invoke-Cmd 'redo')
    Assert ((Read-Object 'Clone').data.children.Count -eq 1) 'Redo duplicate lost the subtree'
    Expect-Ok (Invoke-Body @{command='component.add';parameters=@{target='Clone';type='LightComponent'}})
    $clone = Read-Object 'Clone'
    $lightId = @($clone.data.components | Where-Object type -eq 'LightComponent')[0].id
    Expect-Ok (Invoke-Cmd 'undo')
    Assert (@((Read-Object 'Clone').data.components | Where-Object id -eq $lightId).Count -eq 0) 'Undo component add failed'
    Expect-Ok (Invoke-Cmd 'redo')
    Assert (@((Read-Object 'Clone').data.components | Where-Object id -eq $lightId).Count -eq 1) 'Redo component add lost identity'
    Expect-Ok (Invoke-Body @{command='component.remove';parameters=@{target='Clone';component=$lightId}})
    Assert (@((Read-Object 'Clone').data.components | Where-Object id -eq $lightId).Count -eq 0) 'Remove component failed'
    Expect-Ok (Invoke-Cmd 'undo')
    Assert (@((Read-Object 'Clone').data.components | Where-Object id -eq $lightId).Count -eq 1) 'Undo component remove failed'
    Expect-Ok (Invoke-Cmd 'scene.select' @('-'))
    Expect-Ok (Invoke-Body @{command='scene.select';parameters=@{target='Clone'}})
    $selected = Invoke-Cmd 'scene.selection' @('selected')
    Assert ($selected.data.primary -eq 'Clone' -and $selected.data.multi -eq 1) 'Single and multi selection disagree'
    Expect-Ok (Invoke-Cmd 'undo')
    $selected = Invoke-Cmd 'scene.selection' @('unselected')
    Assert ($selected.data.multi -eq 0) 'Undo selection failed'
    Expect-Ok (Invoke-Cmd 'redo')
    $selected = Invoke-Cmd 'scene.selection' @('selected-again')
    Assert ($selected.data.primary -eq 'Clone') 'Redo selection failed'
    Expect-Ok (Invoke-Cmd 'prefab.create' @('History edited',$prefabName))
    Expect-Ok (Invoke-Body @{command='prefab.instantiate';parameters=@{prefab=$prefabName;name='Prefab instance'}})
    $instance = Read-Object 'Prefab instance'; Expect-Ok $instance
    Assert ($instance.data.children.Count -eq 1) 'Prefab instantiation lost the subtree'
    Expect-Ok (Invoke-Cmd 'undo')
    Assert ((Read-Object 'Prefab instance').code -eq 'object.not_found') 'Undo prefab instantiation failed'
    Expect-Ok (Invoke-Cmd 'redo')
    Assert ((Read-Object 'Prefab instance').data.children.Count -eq 1) 'Redo prefab instantiation failed'
    Expect-Ok (Invoke-Body @{command='object.create';parameters=@{name='UI edit';type='UI'}})
    function Read-Rect { (Invoke-Body @{command='object.properties';parameters=@{target='UI edit';component='RectTransformComponent'}}).data.values }
    function Equal-Vector2($A,$B) { [Math]::Abs([double]$A.x-[double]$B.x) -lt 0.00001 -and [Math]::Abs([double]$A.y-[double]$B.y) -lt 0.00001 }
    $beforeRect = (Read-Rect).m_anchoredPosition
    Expect-Ok (Invoke-Body @{command='ui.pos';parameters=@{target='UI edit';x=35;y=-20}})
    Assert (Equal-Vector2 (Read-Rect).m_anchoredPosition @{x=35;y=-20}) 'UI position not changed'
    Expect-Ok (Invoke-Cmd 'undo')
    Assert (Equal-Vector2 (Read-Rect).m_anchoredPosition $beforeRect) 'Undo UI position failed'
    Expect-Ok (Invoke-Cmd 'redo')
    Assert (Equal-Vector2 (Read-Rect).m_anchoredPosition @{x=35;y=-20}) 'Redo UI position failed'
    $beforeRect = Read-Rect
    Expect-Ok (Invoke-Body @{command='ui.anchor';parameters=@{target='UI edit';minX=0.2;minY=0.3;maxX=0.7;maxY=0.8}})
    $afterRect = Read-Rect
    Assert ((Equal-Vector2 $afterRect.m_anchorMin @{x=0.2;y=0.3}) -and (Equal-Vector2 $afterRect.m_anchorMax @{x=0.7;y=0.8})) 'UI anchor pair not changed'
    Expect-Ok (Invoke-Cmd 'undo')
    $restoredRect = Read-Rect
    Assert ((Equal-Vector2 $restoredRect.m_anchorMin $beforeRect.m_anchorMin) -and (Equal-Vector2 $restoredRect.m_anchorMax $beforeRect.m_anchorMax)) 'Undo did not restore both anchors in one step'
    $depth = (Invoke-Cmd 'undo.state').data.editUndo
    Assert ((Invoke-Cmd 'ui.pos' @('UI edit','invalid','1')).status -eq 'invalid_arguments') 'Malformed UI number accepted'
    Assert ((Invoke-Cmd 'undo.state').data.editUndo -eq $depth) 'Rejected UI edit changed history'
    Assert ((Invoke-RestMethod "$base/commands/model.place" -Headers $headers).undoable) 'Model placement undo metadata missing'
    Assert (-not (Invoke-RestMethod "$base/commands/prefab.create" -Headers $headers).undoable) 'File-writing prefab creation incorrectly advertises scene Undo'
    Assert ((Invoke-Body @{command='model.place';parameters=@{model='__missing_model__'}}).code -eq 'model.not_found') 'Missing model must report a precondition failure'
    foreach ($name in @('scene.hierarchymutation','object.rootref','dx12.selftest','serialize.nodeequal','crash.test','commands.dump','cli.probe.timing','scene.transformpull.check','assets.scenemodel','animator.state','animator.exit')) {
        Assert (-not ($discovery.commands.name -contains $name)) "Harness leaked into live discovery: $name"
        Assert ((Invoke-Cmd $name).code -eq 'command.unknown') "Harness reachable through HTTP: $name"
    }
    Assert ((Invoke-Cmd 'scene.transformpull' @('probe')).code -eq 'commandlet.required') 'Mixed diagnostic still runs a fixture through HTTP'
    Expect-Ok (Invoke-Cmd 'scene.new' @('OtherScene'))
    Assert ((Read-Object $id).code -eq 'object.stale') 'Old scene id resolved into a new scene'
    Expect-Ok (Invoke-Body @{command='scene.hierarchycheck';parameters=@{}})
    $animatorStatus = Invoke-Body @{command='animator.status';parameters=@{}}
    Expect-Ok $animatorStatus
    Assert ($animatorStatus.data.count -eq 0 -and @($animatorStatus.data.animators).Count -eq 0) 'Empty scene Animator diagnostics must return an empty collection'
    $tagAsset = Join-Path $repoRoot 'Dynamic_CPP/ProjectSetting/TagManager.asset'
    $tagBackup = [IO.File]::ReadAllBytes($tagAsset)
    $tagName = "__CommandSurfaceTag_$PID"
    $tagBefore = (Invoke-Body @{command='tag.list';parameters=@{}}).data
    Assert (@($tagBefore.tags) -notcontains $tagName) 'Test tag already exists'
    try {
        Expect-Ok (Invoke-Body @{command='tag.add';parameters=@{name=$tagName}})
        Assert ((Invoke-Body @{command='tag.has';parameters=@{name=$tagName}}).data.exists) 'Tag add not visible'
        Assert ([IO.File]::ReadAllText($tagAsset).Contains($tagName)) 'Tag add not persisted'
        $tagDepth = (Invoke-Cmd 'undo.state').data.editUndo
        Assert (-not (Invoke-Body @{command='tag.add';parameters=@{name=$tagName}}).data.changed) 'Duplicate tag add must be a no-op'
        Assert ((Invoke-Cmd 'undo.state').data.editUndo -eq $tagDepth) 'No-op tag edit added history'
        Expect-Ok (Invoke-Cmd 'undo')
        Assert (-not (Invoke-Body @{command='tag.has';parameters=@{name=$tagName}}).data.exists) 'Tag add undo did not restore definitions'
        Assert (-not [IO.File]::ReadAllText($tagAsset).Contains($tagName)) 'Tag undo not persisted'
        Expect-Ok (Invoke-Cmd 'redo')
        Assert ((Invoke-Body @{command='tag.has';parameters=@{name=$tagName}}).data.exists) 'Tag add redo failed'
        Expect-Ok (Invoke-Body @{command='tag.remove';parameters=@{name=$tagName}})
        Assert (-not (Invoke-Body @{command='tag.has';parameters=@{name=$tagName}}).data.exists) 'Tag remove failed'
        Expect-Ok (Invoke-Cmd 'undo')
        Assert ((Invoke-Body @{command='tag.has';parameters=@{name=$tagName}}).data.exists) 'Tag remove undo failed'
        Expect-Ok (Invoke-Cmd 'redo')
        $tagAfter = (Invoke-Body @{command='tag.list';parameters=@{}}).data
        Assert (($tagBefore.tags | ConvertTo-Json -Compress) -eq ($tagAfter.tags | ConvertTo-Json -Compress)) 'Tag round trip changed definition order'
        Assert (($tagBefore.layers | ConvertTo-Json -Compress) -eq ($tagAfter.layers | ConvertTo-Json -Compress)) 'Tag edit changed layers'
    } finally {
        # Restore the in-memory state before the outer finally terminates the host.
        Invoke-Body @{command='tag.remove';parameters=@{name=$tagName}} | Out-Null
        [IO.File]::WriteAllBytes($tagAsset, $tagBackup)
    }
    # All retained HTTP commands must report an explicit terminal result.
    Assert (@($discovery.commands | Where-Object { -not $_.resultBearing }).Count -eq 0) 'Legacy product handler remains in HTTP discovery'
    foreach ($case in @(
        @{Name='window.resize'; Args=@('640junk','480')},
        @{Name='lifecycle.trace'; Args=@('on','-1')},
        @{Name='lifecycle.trace'; Args=@('unknown')},
        @{Name='lifecycle.registry'; Args=@('on')},
        @{Name='render.backend'; Args=@('dx11')},
        @{Name='dx12.live'; Args=@('off')},
        @{Name='camera.editor'; Args=@('follow','nonsense')},
        @{Name='script.fields'; Args=@('0junk')},
        @{Name='script.set'; Args=@('0junk','0','1')},
        @{Name='dump.list'; Args=@('-1')},
        @{Name='pix.capture'; Args=@('unknown')}
    )) { Assert ((Invoke-Cmd $case.Name $case.Args).status -eq 'invalid_arguments') "Malformed arguments accepted: $($case.Name)" }
    Expect-Ok (Invoke-Cmd 'lifecycle.trace' @('clear'))
    Assert ((Invoke-Cmd 'lifecycle.dump').code -eq 'lifecycle.empty') 'Empty trace dump did not fail'
    Assert ((Invoke-Cmd 'script.fields' @('2147483647')).code -eq 'script.instance_not_found') 'Missing managed instance was reported as an empty successful query'
    Assert ((Invoke-Cmd 'model.loadcached' @('__missing_model__.glb')).status -eq 'failed') 'Missing cached generation must report failure'
    $windowSchema = Invoke-RestMethod "$base/commands/window.resize" -Headers $headers
    Assert ($windowSchema.inputSchema.properties.width.type -eq 'integer') 'Window size schema must advertise integers'
    Assert ((Invoke-Body @{command='window.resize';parameters=@{width=640.5;height=480}}).code -eq 'request.parameters_invalid') 'Fractional named size accepted'
    Assert ((Invoke-Body @{command='script.fields';parameters=@{instance=2147483647}}).code -eq 'script.instance_not_found') 'Named integer parameters are not dispatched correctly'
    $components = Invoke-Cmd 'component.list' @('Animator'); Expect-Ok $components
    Assert ($components.data.types -contains 'Animator' -and $components.data.count -gt 0) 'Component discovery has no owned types'
    $memory = Invoke-Cmd 'mem.stats'
    if ($Exe -match 'Debug') { Expect-Ok $memory; Assert ($memory.data.bytes -gt 0) 'Debug heap metrics missing' }
    else { Assert ($memory.code -eq 'memory.debug_crt_required') 'Release heap metrics must be unavailable, not zero' }
    $profile = Invoke-Cmd 'profile.stats'; Expect-Ok $profile
    Assert ($profile.data.ticksPerSecond -gt 0) 'Profiler result omitted clock frequency'
    $pipeline = Invoke-Cmd 'pipeline.nodes'; Expect-Ok $pipeline
    Assert ($pipeline.data.nodes.Count -gt 0 -and $pipeline.data.count -eq $pipeline.data.nodes.Count) 'Pipeline node snapshot is empty'
    $render = Invoke-Cmd 'render.rtinfo'; Expect-Ok $render
    Assert ($render.data.width -gt 0 -and $render.data.height -gt 0) 'Render target dimensions absent'
    Expect-Ok (Invoke-Cmd 'gpu.baseline')
    $gpu = Invoke-Cmd 'gpu.delta'; Expect-Ok $gpu
    Assert ($null -ne $gpu.data.vramDeltaMB -and $null -ne $gpu.data.resourceDelta) 'GPU baseline delta omitted'
    Expect-Ok (Invoke-Cmd 'object.create' @('Animator edit'))
    Expect-Ok (Invoke-Cmd 'component.add' @('Animator edit','Animator'))
    function Read-AnimatorParameters {
        $parameters = (Invoke-Cmd 'object.properties' @('Animator edit','Animator')).data.values.Parameters
        if ($parameters -is [string]) { Assert ($parameters -eq 'null') 'Unexpected serialized Animator parameter value'; return }
        return $parameters
    }
    Expect-Ok (Invoke-Body @{command='animator.param';parameters=@{target='Animator edit';name='ContractSpeed';type='float'}})
    Assert (@(Read-AnimatorParameters | Where-Object name -eq 'ContractSpeed').Count -eq 1) 'Animator parameter was not created'
    $depth = (Invoke-Cmd 'undo.state').data.editUndo
    Assert (-not (Invoke-Cmd 'animator.param' @('Animator edit','ContractSpeed','float')).data.changed) 'Repeated parameter create must be a no-op'
    Assert ((Invoke-Cmd 'animator.param' @('Animator edit','ContractSpeed','int')).status -eq 'invalid_arguments') 'Conflicting parameter type accepted'
    Assert ((Invoke-Cmd 'undo.state').data.editUndo -eq $depth) 'Rejected or repeated parameter create changed history'
    Expect-Ok (Invoke-Cmd 'undo')
    Assert (@(Read-AnimatorParameters | Where-Object name -eq 'ContractSpeed').Count -eq 0) 'Animator parameter undo failed'
    Expect-Ok (Invoke-Cmd 'redo')
    Assert (@(Read-AnimatorParameters | Where-Object name -eq 'ContractSpeed').Count -eq 1) 'Animator parameter redo failed'
    Assert ((Invoke-Cmd 'render.matmode' @('Animator edit','opaque')).code -eq 'material.not_found') 'Material edit with no target silently succeeded'
    if (-not [string]::IsNullOrWhiteSpace($MaterialScene)) {
        Expect-Ok (Invoke-Cmd 'scene.switch' @([IO.Path]::GetFullPath($MaterialScene)))
        Expect-Ok (Invoke-Cmd 'render.matmode' @($MaterialTarget,'opaque'))
        $materialEdit = Invoke-Cmd 'render.matmode' @($MaterialTarget,'transparent'); Expect-Ok $materialEdit
        Assert ($materialEdit.data.materials -gt 0 -and $materialEdit.data.changed -gt 0) 'Real material edit had no coverage'
        Expect-Ok (Invoke-Cmd 'undo')
        $afterUndo = Invoke-Cmd 'render.matmode' @($MaterialTarget,'opaque'); Expect-Ok $afterUndo
        Assert ($afterUndo.data.changed -eq 0) 'Material Undo did not restore opaque state'
        Expect-Ok (Invoke-Cmd 'redo')
        $afterRedo = Invoke-Cmd 'render.matmode' @($MaterialTarget,'transparent'); Expect-Ok $afterRedo
        Assert ($afterRedo.data.changed -eq 0) 'Material Redo did not restore transparent state'
    }
    # Capture the actual current registry without updating the golden implicitly.
    Expect-Ok (Invoke-Cmd 'commands.list' @((Join-Path $Work 'registry.tsv')))
    Expect-Ok (Invoke-Cmd 'commands.selftest')
    Invoke-Cmd 'quit' | Out-Null
    if (-not $proc.WaitForExit(60000)) { throw 'Editor did not shut down' }
} finally {
    if (-not $proc.HasExited) { $proc.Kill(); $proc.WaitForExit() }
    foreach ($file in $prefabFiles) { if (Test-Path -LiteralPath $file) { Remove-Item -LiteralPath $file } }
}
"EDITOR_COMMAND_SURFACE_OK checks=$script:checks"
