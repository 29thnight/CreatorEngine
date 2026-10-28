[CmdletBinding()]
param([string]$OutputDirectory = '', [int[]]$Counts = @(10000,100000),
    [ValidateRange(32,1024)][int]$SampleFrames = 128)
$ErrorActionPreference = 'Stop'
$repo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
if (!$OutputDirectory) { $OutputDirectory = Join-Path $repo ('Build/Verification/FoliageEngine/Run-' + [guid]::NewGuid().ToString('N')) }
$out = [IO.Path]::GetFullPath($OutputDirectory)
if (Test-Path $out) { throw 'Use a fresh output directory' }
if (Get-Process CreatorEditor -ErrorAction SilentlyContinue) { throw 'An Editor is already running; leave it untouched' }
$project = Join-Path $out 'Project'
New-Item -ItemType Directory -Path "$project/Assets/Models" -Force | Out-Null
Copy-Item "$repo/Dynamic_CPP/ProjectSetting" $project -Recurse
Copy-Item "$repo/Dynamic_CPP/Assets/Shaders" "$project/Assets/Shaders" -Recurse
& python "$PSScriptRoot/new-n3-pacing-fixture.py" "$project/Assets/Models"
if ($LASTEXITCODE) { throw 'GLB fixture creation failed' }
$model = "$project/Assets/Models/N3Green.glb"
& python "$PSScriptRoot/scale-foliage-probe-glb.py" $model "$out/named.glb" 1
if ($LASTEXITCODE) { throw 'Stable named mesh fixture creation failed' }
Copy-Item "$out/named.glb" $model -Force
Copy-Item $model "$out/original.glb"
$exe = "$repo/Bin/x64-Release/Editor/CreatorEditor.exe"
$priorValidation = $env:CREATOR_DX12_VALIDATION
$priorWorkspace = $env:CREATOR_EDITOR_WORKSPACE_DIR
$priorLegacy = $env:CREATOR_EDITOR_LEGACY_INI
$env:CREATOR_DX12_VALIDATION = 'off'
$env:CREATOR_EDITOR_WORKSPACE_DIR = "$out/workspace"
$env:CREATOR_EDITOR_LEGACY_INI = "$out/workspace/legacy.ini"
$proc = Start-Process $exe -ArgumentList @('--development-project',('"'+$project+'"'),'--command-service') -WorkingDirectory (Split-Path $exe) -WindowStyle Normal -PassThru -RedirectStandardOutput "$out/stdout.log" -RedirectStandardError "$out/stderr.log"
$hardware = @{cpu=@(Get-CimInstance Win32_Processor | ForEach-Object Name);gpu=@(Get-CimInstance Win32_VideoController | Select-Object Name,DriverVersion);head=(git -C $repo rev-parse HEAD);runtime=(Get-FileHash "$repo/Bin/x64-Release/Editor/CreatorEditor.runtime.dll").Hash;mode='Release DX12 visible Editor; validation off; game-loop frame intervals'}
$hardware | ConvertTo-Json -Depth 8 | Set-Content "$out/environment.json"
function Cmd([string]$Name,[string[]]$Arguments=@()) {
    $reply = Invoke-RestMethod "$base/command" -DisableKeepAlive -Method Post -Headers $headers -ContentType application/json -Body (@{command=$Name;args=@($Arguments);mode='async'}|ConvertTo-Json -Compress) -TimeoutSec 600
    if ($Name -eq 'quit') { return }
    $limit = [DateTime]::UtcNow.AddSeconds(600)
    if ($reply.operationId) {
        $poll = $reply.poll
        do {
            Start-Sleep -Milliseconds 100
            $reply = Invoke-RestMethod "$base$poll" -Headers $headers -TimeoutSec 600
            if ([DateTime]::UtcNow -gt $limit) { throw "Command timeout: $Name" }
        } until ($reply.state -eq 'completed')
        if ($reply.result) { $reply = $reply.result }
    }
    @{command=$Name;arguments=$Arguments;result=$reply} | ConvertTo-Json -Depth 25 -Compress | Add-Content "$out/commands.jsonl"
    if ($reply.status -ne 'succeeded') { throw "$Name failed: $($reply.code) $($reply.message)" }
    return $reply.data
}
function Fence { Cmd 'render.live.fence' @('30') | Out-Null }
function Probe([string[]]$Arguments=@('status')) { Cmd 'foliage.cache.probe' $Arguments }
function Ready {
    $limit = [DateTime]::UtcNow.AddSeconds(180)
    do {
        $value = Probe
        if ($value.binding -eq 1) { return $value }
        if ($value.binding -gt 1 -or [DateTime]::UtcNow -gt $limit) { throw "Binding failed: $($value|ConvertTo-Json -Compress)" }
        Start-Sleep -Milliseconds 200
    } while ($true)
}
function Snapshot([string]$Name) {
    $gpuBefore = Cmd 'dx12.live'
    Fence
    $v = Ready
    Fence
    $v = Probe
    $v | ConvertTo-Json -Depth 12 | Set-Content "$out/$Name.json"
    if ($v.visibilityMismatches -ne 0) { throw "Visibility oracle mismatch: $Name" }
    if ($v.playing) {
        $deadline = [DateTime]::UtcNow.AddSeconds(180)
        do {
            $completed = Cmd 'dx12.live'
            if ([DateTime]::UtcNow -gt $deadline) { throw "No fresh real geometry renders: $Name" }
            if ($completed.framesRendered -le ($gpuBefore.framesRendered+8)) { Start-Sleep -Milliseconds 200 }
        } until ($completed.framesRendered -gt ($gpuBefore.framesRendered+8) -and $completed.materialGeometry.entries -gt 0 -and $completed.gpu.clockValid)
        $completed | ConvertTo-Json -Depth 30 | Set-Content "$out/$Name-live.json"
    }
    return $v
}
function Play([bool]$Playing) {
    $command = if ($Playing) { 'play' } else { 'stop' }
    Cmd $command | Out-Null
    $limit = [DateTime]::UtcNow.AddSeconds(180)
    do {
        Start-Sleep -Milliseconds 200
        $v = Cmd 'play.state'
        if ([DateTime]::UtcNow -gt $limit) { throw "$command transition timeout" }
    } until ($v.gameStart -eq $Playing -and $v.committed -eq $Playing -and !$v.pending)
    Fence
}
function NewScene([string]$Name,[int]$Count) {
    Cmd 'scene.new' @($Name) | Out-Null
    Fence
    Cmd 'object.create' @('ProbeCamera') | Out-Null
    Cmd 'component.add' @('ProbeCamera','CameraComponent') | Out-Null
    $width = [math]::Ceiling([math]::Sqrt($Count)) * 2
    Cmd 'object.transform' @('ProbeCamera','10','15','-20','30','0','0') | Out-Null
    Cmd 'camera.lens.probe' @('ProbeCamera','60','0.1','60') | Out-Null
    Cmd 'camera.editor' @('match') | Out-Null
    Fence
}
try {
    $endpoint = "$project/Library/CommandService/endpoint.json"
    $limit = [DateTime]::UtcNow.AddSeconds(180)
    do {
        Start-Sleep -Milliseconds 200
        if ($proc.HasExited) { throw "Editor startup exited: $($proc.ExitCode)" }
        if (Test-Path $endpoint) { $ep = Get-Content $endpoint -Raw | ConvertFrom-Json }
        if ([DateTime]::UtcNow -gt $limit) { throw 'Command service startup timeout' }
    } until ($ep -and $ep.pid -eq $proc.Id)
    $base = "http://127.0.0.1:$($ep.port)"
    $headers = @{Authorization='Bearer '+$ep.token}
    Cmd 'window.resize' @('1280','720') | Out-Null
    Cmd 'editor.workspace' @('preset','legacy_unity') | Out-Null
    Cmd 'play.foreground_override' @('on') | Out-Null
    Cmd 'render.pacing' @('unlimited') | Out-Null
    Cmd 'render.pacing' @('play','off') | Out-Null
    foreach ($request in @(@('temporal.fg','none'),@('temporal.nis','off'),@('temporal.deepdvc','off'),@('temporal.upscale','none'))) { Cmd $request[0] @($request[1]) | Out-Null }
    NewScene 'FoliageAcceptance' 32
    Cmd 'assets.model.reimport' @($model) | Out-Null
    Probe @('create',$model,'32') | Out-Null
    $editor = Snapshot 'accept-editor'
    Probe @('save','CacheAcceptance') | Out-Null
    $saved = Snapshot 'accept-saved'
    if (!$saved.savedAsset) { throw 'Foliage asset was not published for play restoration' }
    Play $true
    $before = Snapshot 'accept-play'
    if (!$before.hasFrustum -or $before.cacheBytes -le 0) { throw 'Actual play culling/cache is inactive' }
    Probe @('measure','cached') | Out-Null
    Probe @('edit') | Out-Null
    $edited = Snapshot 'accept-edit'
    if ($edited.firstX -ne 100000 -or $edited.firstIdentity -ne $before.firstIdentity -or $edited.transforms -ne 1) { throw 'Transform edit/identity/cache invalidation contract failed' }
    Probe @('measure','cached') | Out-Null
    & python "$PSScriptRoot/scale-foliage-probe-glb.py" "$out/original.glb" $model 4
    if ($LASTEXITCODE) { throw 'Asset replacement fixture failed' }
    Cmd 'assets.model.reimport' @($model) | Out-Null
    Probe @('rebind') | Out-Null
    $replaced = Snapshot 'accept-replace'
    if ([math]::Abs($replaced.localExtentX - 4*$before.localExtentX) -gt .001 -or $replaced.firstIdentity -ne $edited.firstIdentity -or $replaced.transforms -ne 32) { throw 'Actual reimport/rebind did not replace/recompute bounds coherently' }
    Probe @('measure','cached') | Out-Null
    Probe @('remove') | Out-Null
    $removed = Snapshot 'accept-remove'
    if ($removed.count -ne 31 -or $removed.firstIdentity -eq $edited.firstIdentity -or $removed.transforms -ne 31) { throw 'Instance deletion/cache compaction failed' }
    Play $false
    $restored = Snapshot 'accept-stop'
    if ($restored.count -ne 32 -or $restored.firstX -ne $editor.firstX) { throw 'Play stop did not restore editor data' }
    Play $true
    $second = Snapshot 'accept-replay'
    if ($second.count -ne 32 -or [math]::Abs($second.localExtentX-$replaced.localExtentX) -gt .001) { throw 'Play reload lost replacement bounds' }
    Play $false
    @{passed=$true;checks=@('editor','play','transform edit','source GLB replacement/reimport/rebind','delete','stop restoration','play reload')} | ConvertTo-Json | Set-Content "$out/acceptance.json"
    Copy-Item "$out/original.glb" $model -Force
    Cmd 'assets.model.reimport' @($model) | Out-Null
    $runs = @()
    foreach ($count in $Counts) {
        NewScene "Foliage$count" $count
        Play $true
        Probe @('create',$model,"$count") | Out-Null
        $seed = Snapshot "N$count-seed"
        if ($seed.count -ne $count -or $seed.visible -le 0 -or $seed.visible -ge 4096 -or !$seed.hasFrustum -or $seed.cacheBytes -le 0) { throw 'Population/visibility/frustum/cache activation mismatch' }
        $renderDeadline = [DateTime]::UtcNow.AddSeconds(180)
        do {
            $render = Cmd 'dx12.live'
            if ([DateTime]::UtcNow -gt $renderDeadline) {
                $render | ConvertTo-Json -Depth 30 | Set-Content "$out/N$count-render-failure.json"
                throw 'Real foliage geometry did not become GPU resident'
            }
            if ($render.materialGeometry.entries -le 0) { Start-Sleep -Milliseconds 300 }
        } until ($render.materialGeometry.entries -gt 0 -and $render.gpu.clockValid)
        # Fixed ABBA order, 64 warm frames followed by SampleFrames samples.
        $trial = 0
        foreach ($mode in @('baseline','cached','cached','baseline')) {
            $trial++
            Probe @('measure',$mode) | Out-Null
            $limit = [DateTime]::UtcNow.AddSeconds(900)
            $frameClock = $null
            do {
                Start-Sleep -Milliseconds 500
                $samples = Probe @('samples')
                if (!$frameClock -and $samples.cullingMs.Count -ge 64) {
                    $liveBegin = Cmd 'dx12.live'
                    $frameClock = [Diagnostics.Stopwatch]::StartNew()
                }
                if ([DateTime]::UtcNow -gt $limit) { throw "N$count $mode measurement timeout" }
            } until ($samples.cullingMs.Count -ge (64+$SampleFrames))
            $samples | ConvertTo-Json -Depth 10 | Set-Content "$out/N$count-$trial-$mode.json"
            $proc.Refresh()
            $liveEnd = Cmd 'dx12.live'
            $frameClock.Stop()
            $liveEnd | ConvertTo-Json -Depth 30 | Set-Content "$out/N$count-$trial-$mode-live.json"
            Cmd 'profile.stats' | ConvertTo-Json -Depth 20 | Set-Content "$out/N$count-$trial-$mode-profile.json"
            $rendered = [long]$liveEnd.framesRendered - [long]$liveBegin.framesRendered
            if ($rendered -le 0 -or $liveEnd.materialGeometry.entries -le 0) { throw 'Renderer did not produce real geometry frames during measurement' }
            $renderInterval = if ($rendered -gt 0) { $frameClock.Elapsed.TotalMilliseconds/$rendered } else { $null }
            $runs += @{count=$count;trial=$trial;mode=$mode;visible=$seed.visible;cacheBytes=$samples.cacheBytes;processPrivateBytes=$proc.PrivateMemorySize64;processWorkingSetBytes=$proc.WorkingSet64;observedRenderedFrames=$rendered;observedWallMs=$frameClock.Elapsed.TotalMilliseconds;renderedFrameIntervalMs=$renderInterval;cullingMs=@($samples.cullingMs[64..(63+$SampleFrames)]);gameFrameIntervalMs=@($samples.gameFrameIntervalMs[64..(63+$SampleFrames)])}
        }
        Play $false
    }
    $runs | ConvertTo-Json -Depth 12 | Set-Content "$out/measurements.json"
    Cmd 'quit'
    $proc.WaitForExit(30000) | Out-Null
    "FOLIAGE_ENGINE_ACCEPTANCE_MEASUREMENT_OK output=$out"
} finally {
    $env:CREATOR_DX12_VALIDATION = $priorValidation
    $env:CREATOR_EDITOR_WORKSPACE_DIR = $priorWorkspace
    $env:CREATOR_EDITOR_LEGACY_INI = $priorLegacy
    if (!$proc.HasExited -and $base) {
        try { Cmd 'quit' } catch { Write-Warning "Owned Editor still running: pid=$($proc.Id) output=$out" }
    }
}
