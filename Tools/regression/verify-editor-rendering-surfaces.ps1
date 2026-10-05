param(
    [ValidateSet('Debug','Release')][string]$Configuration='Debug',
    [ValidateSet('dx12','vulkan')][string]$Backend='dx12',
    [Parameter(Mandatory=$true)][string]$FixtureProject,
    [Parameter(Mandatory=$true)][string]$OutputDirectory
)
Set-StrictMode -Version Latest
$ErrorActionPreference='Stop'
# Reject before creating artifacts or changing settings; never label a DX12 run Vulkan.
if ($Backend -contains 'vulkan') {
    throw 'CreatorEditor supports DX12 only; Vulkan Editor runs are unsupported. Use native Vulkan RHI probes or Player validation instead.'
}
$repo=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$case=[IO.Path]::GetFullPath($OutputDirectory)
if(Test-Path -LiteralPath $case){throw 'Use a new output directory'}
$project=Join-Path $case 'Project'
New-Item -ItemType Directory -Path $project -Force|Out-Null
foreach($folder in @('Assets','ProjectSetting','Saved')){
    $source=Join-Path $FixtureProject $folder
    if(Test-Path $source){Copy-Item -LiteralPath $source -Destination $project -Recurse}
}
Copy-Item -LiteralPath "$repo/Dynamic_CPP/Assets/Shaders/DefaultPassShader" -Destination "$project/Assets/Shaders" -Recurse -Force
$draftFolder="$project/Saved/Editor/MaterialDrafts"
New-Item -ItemType Directory -Path $draftFolder -Force|Out-Null
$existingDraft="$draftFolder/Scene_2_Document_1.shadergraph"
[IO.File]::WriteAllText($existingDraft,'previous-session-recovery')
$exe="$repo/Bin/x64-$Configuration/Editor/CreatorEditor.exe"
$proc=$null
$checks=0
function Assert([bool]$Condition,[string]$Message){
    $script:checks++
    if(!$Condition){throw $Message}
}
try {
    $proc=Start-Process $exe -ArgumentList @('--development-project',$project,'--command-service','--smoke-offscreen') -WorkingDirectory (Split-Path $exe) -WindowStyle Hidden -PassThru -RedirectStandardOutput "$case/editor.stdout.log" -RedirectStandardError "$case/editor.stderr.log"
    $endpoint=$null
    $deadline=[DateTime]::UtcNow.AddSeconds(180)
    do {
        Start-Sleep -Milliseconds 100
        if($proc.HasExited){throw "Editor exited $($proc.ExitCode)"}
        if(Test-Path "$project/Library/CommandService/endpoint.json"){$endpoint=Get-Content "$project/Library/CommandService/endpoint.json" -Raw|ConvertFrom-Json}
        if([DateTime]::UtcNow -gt $deadline){throw 'Editor startup timeout'}
    }until($endpoint -and $endpoint.pid -eq $proc.Id)
    $base="http://127.0.0.1:$($endpoint.port)"
    $headers=@{Authorization="Bearer $($endpoint.token)"}
    function Cmd([string]$Name,[string[]]$Arguments=@(),[switch]$AllowFailure){
        $r=Invoke-RestMethod "$base/command" -Method Post -Headers $headers -ContentType 'application/json' -Body (@{command=$Name;args=@($Arguments);mode='async'}|ConvertTo-Json -Compress)
        if($Name -eq 'quit'){return}
        $deadline=[DateTime]::UtcNow.AddSeconds(120)
        if($r.operationId){
            $poll=$r.poll
            do {Start-Sleep -Milliseconds 60;$r=Invoke-RestMethod "$base$poll" -Headers $headers
                if([DateTime]::UtcNow -gt $deadline){throw "$Name timeout"}
            }until($r.state -eq 'completed')
        }
        $r|ConvertTo-Json -Depth 40 -Compress|Add-Content "$case/responses.jsonl"
        if($AllowFailure){return $r}
        if($r.status -ne 'succeeded'){throw "$Name failed: $($r.code) $($r.message)"}
        $r.data
    }
    function Fence {Cmd 'render.live.fence' @('120')|Out-Null}
    function UiFence([long]$After){
        $deadline=[DateTime]::UtcNow.AddSeconds(10)
        do {Start-Sleep -Milliseconds 100;$state=Cmd 'editor.state'
            if([DateTime]::UtcNow -gt $deadline){throw 'UI did not advance after window focus'}
        }until($state.frames -gt $After+1)
    }
    function Capture([string]$Name,[string]$View='editor'){
        Fence
        Cmd 'render.live.capture' @("$case/$Name",$View,'controlled')|Out-Null
    }
    function Overlay([string]$Name,[string]$Value){
        Cmd 'editor.sceneview' @($Name,$Value)|Out-Null
        $deadline=[DateTime]::UtcNow.AddSeconds(10)
        do {Start-Sleep -Milliseconds 100;$view=Cmd 'editor.sceneview'
            if([DateTime]::UtcNow -gt $deadline){throw "Overlay $Name did not apply"}
        }until($view.$Name -eq ($Value -eq 'on'))
        Assert $true "Overlay $Name applied"
        Fence
    }
    Cmd 'play.foreground_override' @('off')|Out-Null
    Cmd 'scene.switch' @("$project/Assets/Scenes/LX_CookFixture.creator")|Out-Null
    Cmd 'render.environment' @('background','on')|Out-Null
    Fence
    $initial=Cmd 'render.environment' @('status')
    $initial|ConvertTo-Json|Set-Content "$case/environment-before.json"
    $windows=Cmd 'editor.windows'
    Assert ($windows.orphanBodies -eq 0 -and $windows.bodylessWindows -eq 0) 'Window registry has orphan/missing bodies'
    foreach($window in @('###Editor.Preferences','###Editor.ProjectSettings','###Editor.RenderPass','###Editor.RenderPassDebug')){
        $uiBefore=Cmd 'editor.state'
        Cmd 'editor.window' @($window,'focus')|Out-Null
        # A tool window can occlude the viewport. Observe UI frames, not Scene GPU demand.
        UiFence $uiBefore.frames
        Cmd 'editor.window' @($window,'close')|Out-Null
    }
    Cmd 'profile.pause'|Out-Null
    $deadline=[DateTime]::UtcNow.AddSeconds(15)
    do {$recorder=Cmd 'profile.stats'; Start-Sleep -Milliseconds 100
        if([DateTime]::UtcNow -gt $deadline){throw 'Recorder did not stop'}
    }until($recorder.state -in @('frozen','stopped'))
    $recorder|ConvertTo-Json -Depth 20|Set-Content "$case/recorder-off.json"
    Assert ($recorder.state -ne 'recording') 'Rendering Live test requires Record off'
    $liveBefore=Cmd 'dx12.live'
    $uiBefore=Cmd 'editor.state'
    Cmd 'editor.window' @('###Editor.FrameProfiler','rendering-live')|Out-Null
    UiFence $uiBefore.frames
    $live=Cmd 'dx12.live'
    Assert ($live.ready -and $live.framesRendered -gt 0) 'Rendering Live ready'
    if($Backend -eq 'dx12'){
        $deadline=[DateTime]::UtcNow.AddSeconds(10)
        while($live.gpu.collects -le $liveBefore.gpu.collects){
            Fence; $live=Cmd 'dx12.live'
            if([DateTime]::UtcNow -gt $deadline){throw 'GPU timing stopped when Record was off'}
        }
        Assert ($live.gpu.collects -gt $liveBefore.gpu.collects) 'GPU timing must advance without Record'
    }
    Cmd 'editor.window' @('###Editor.FrameProfiler','close')|Out-Null
    Cmd 'editor.window' @('###Editor.GamePreview','open')|Out-Null
    Fence
    Overlay 'statistics' 'on'
    Capture 'scene-on'
    Capture 'game-on' 'game'
    Overlay 'skybox' 'off'
    Capture 'scene-hidden'
    Capture 'game-scene-hidden' 'game'
    Overlay 'skybox' 'on'
    Cmd 'render.environment' @('background','off')|Out-Null
    Capture 'scene-off'
    Cmd 'render.environment' @('background','on')|Out-Null
    Capture 'scene-restored'
    $after=Cmd 'render.environment' @('status')
    Assert ($after.iblGenerations -eq $initial.iblGenerations) 'Background visibility regenerated IBL'
    Overlay 'statistics' 'off'
    Overlay 'fps' 'off'
    Overlay 'fps' 'on'
    $opened=Cmd 'material.editor' @('open','Ground')
    Fence
    # First presentation can fit the canvas and advance the document revision.
    $edited=$null
    for($attempt=0;$attempt -lt 4;$attempt++){
        $current=Cmd 'material.editor' @('state')
        Assert ($current.document -eq $opened.document) 'Opened document changed before edit'
        $response=Cmd 'material.editor' @('add',"$($current.document)","$($current.revision)",'ShaderNodeValue') -AllowFailure
        if($response.status -eq 'succeeded'){$edited=$response.data;break}
        if($response.code -ne 'material.editor.revision'){
            throw "Draft edit failed: $($response.code) $($response.message)"
        }
        Fence
    }
    Assert ($null -ne $edited) 'Draft edit revision did not stabilize'
    Assert $edited.dirty 'Draft edit did not become dirty'
    $failed=Cmd 'scene.switch' @("$project/Assets/Scenes/missing.creator") -AllowFailure
    Assert ($failed.status -ne 'succeeded') 'Missing Scene must fail'
    $same=Cmd 'material.editor' @('state')
    Assert ($same.document -eq $edited.document) 'Failed load changed the active document'
    Cmd 'scene.new' @('SurfaceContextB')|Out-Null
    Fence
    $sessions=Cmd 'material.editor' @('sessions')
    Assert (!$sessions.open -and @($sessions.visible).Count -eq 0) 'Previous Scene document is still visible'
    Assert (@($sessions.retained).Count -ge 1) 'Unsaved draft lost'
    $closed=Cmd 'material.editor' @('apply',"$($edited.document)","$($edited.revision)") -AllowFailure
    Assert ($closed.status -ne 'succeeded') 'Old draft applied in a new Scene'
    $drafts=@(Get-ChildItem "$project/Saved/Editor/MaterialDrafts" -Filter '*.shadergraph')
    Assert ($drafts.Count -ge 2) 'Draft recovery copy not persisted'
    Assert ([IO.File]::ReadAllText($existingDraft) -eq 'previous-session-recovery') 'Previous Editor recovery copy was overwritten'
    $sessions|ConvertTo-Json -Depth 25|Set-Content "$case/sessions-after.json"
    Cmd 'log.flush'|Out-Null
    Cmd 'quit'|Out-Null
    if(!$proc.WaitForExit(30000)){throw 'Editor shutdown timeout'}
    Assert ($proc.ExitCode -eq 0) 'Editor shutdown failed'
    @{configuration=$Configuration;backend=$Backend;checks=$checks;pid=$proc.Id;iblGenerations=$after.iblGenerations;draftCopies=$drafts.Count}|ConvertTo-Json|Set-Content "$case/result.json"
    Write-Output "EDITOR_RENDERING_SURFACES_OK $Configuration $Backend checks=$checks $case"
}finally{
    if($proc -and !$proc.HasExited){Stop-Process -Id $proc.Id}
}
