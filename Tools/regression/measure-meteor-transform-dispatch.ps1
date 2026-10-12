#Requires -Version 7.0
param(
    [Parameter(Mandatory)][string]$FixtureProject,
    [Parameter(Mandatory)][string]$OutputDirectory,
    [ValidateSet(16,64,256,512)][int[]]$Counts=@(16,64,256,512),
    [ValidateRange(1,4)][int]$Iterations=4,
    [switch]$RequireWarmReuse,
    [string]$Python='python'
)
$ErrorActionPreference='Stop'
. "$PSScriptRoot/ArtifactRetention.ps1"
. "$PSScriptRoot/NativeRealMeasurementGate.ps1"
$repo=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$out=[IO.Path]::GetFullPath($OutputDirectory)
if(Test-Path -LiteralPath $out){throw 'Use a fresh measurement directory.'}
if(Get-Process CreatorEditor -ErrorAction SilentlyContinue){throw 'Close existing Editor before isolated measurement.'}
New-Item -ItemType Directory -Path $out|Out-Null
$exe="$repo/Bin/x64-Release/Editor/CreatorEditor.exe"
$runtime="$repo/Bin/x64-Release/Editor/CreatorEditor.runtime.dll"
$before=(Get-FileHash -LiteralPath $runtime).Hash
function Source-Identity {
    $paths=@('Engine/RenderEngine/MaterialGraphMeshSurface.cpp','Engine/RenderEngine/MaterialGraphMeshSurface.h',
        'Engine/RenderEngine/MaterialGraphSceneHost.cpp','Engine/RenderEngine/RHI/RHIEncoder.h',
        'Engine/RenderEngine/RHI/DX12/DX12Encoder.cpp','Engine/RenderEngine/RHI/DX12/DX12Encoder.h',
        'Editor/EngineEntry/Commands/SceneObjectCommands.cpp')|ForEach-Object {Join-Path $repo $_}
    $paths+=@(Get-ChildItem "$repo/Dynamic_CPP/Assets/Shaders/DefaultPassShader" -Recurse -File -Filter '*.slang'|ForEach-Object FullName)
    @($paths|Sort-Object|ForEach-Object {@{path=[IO.Path]::GetRelativePath($repo,$_);sha256=(Get-FileHash -LiteralPath $_).Hash}})
}
$sourceBefore=Source-Identity
$sourceBefore|ConvertTo-Json -Depth 5|Set-Content "$out/identity-before.json"
$previousValidation=$env:CREATOR_DX12_VALIDATION
$previousAudit=$env:CREATOR_METEOR_TRANSFORM_AUDIT
$previousWorkspace=$env:CREATOR_EDITOR_WORKSPACE_DIR
$previousLegacy=$env:CREATOR_EDITOR_LEGACY_INI
$env:CREATOR_DX12_VALIDATION='off'
$env:CREATOR_METEOR_TRANSFORM_AUDIT='1'
$runs=@()
$nvidia=Get-Command nvidia-smi -ErrorAction SilentlyContinue
function Cmd([string]$Name,[string[]]$Arguments=@()) {
    $r=Invoke-RestMethod "$base/command" -Method Post -Headers $headers -ContentType 'application/json' -Body (@{command=$Name;args=@($Arguments);mode='async'}|ConvertTo-Json -Compress)
    if($Name -eq 'quit'){return}
    $limit=[DateTime]::UtcNow.AddSeconds(180)
    if($r.operationId){do {
        Start-Sleep -Milliseconds 80
        $s=Invoke-RestMethod "$base$($r.poll)" -Headers $headers
        if([DateTime]::UtcNow -gt $limit){throw "$Name timeout"}
    }until($s.state -eq 'completed');$r=if($s.result){$s.result}else{$s}}
    @{command=$Name;arguments=@($Arguments);result=$r}|ConvertTo-Json -Depth 30 -Compress|Add-Content "$case/responses.jsonl"
    if($r.status -ne 'succeeded'){throw "$Name $($r.code) $($r.message)"}
    $r.data
}
function Wait-Recording([string]$Expected) {
    $limit=[DateTime]::UtcNow.AddSeconds(180)
    do {
        $status=Cmd 'profile.stats'
        if($status.recording.state -eq 'failed'){throw "Recording failed: $($status.recording.error)"}
        if([DateTime]::UtcNow -gt $limit){throw "Recording did not reach $Expected"}
        if($status.recording.state -ne $Expected){Start-Sleep -Milliseconds 80}
    }until($status.recording.state -eq $Expected -and ($Expected -ne 'recording' -or $status.state -eq 'recording'))
    $status
}
function Read-Live {
    $limit=[DateTime]::UtcNow.AddSeconds(30)
    do {
        $live=Cmd 'dx12.live'
        if($live.gpu -and $live.materialGeometry){Assert-NativeRealMeasurement $live;return $live}
        if([DateTime]::UtcNow -gt $limit){throw 'Complete live measurement unavailable'}
    }while($true)
}
function Set-Native {
    foreach($command in @('temporal.fg','temporal.nis','temporal.deepdvc','temporal.upscale')) {
        $argument=if($command -in @('temporal.nis','temporal.deepdvc')){'off'}else{'none'}
        $receipt=Cmd $command @($argument)
        $limit=[DateTime]::UtcNow.AddSeconds(180)
        do {
            $status=Cmd 'temporal.status' @([string]$receipt.receiptGeneration)
            if($status.requestState -eq 'superseded'){throw 'Native request superseded'}
            if([DateTime]::UtcNow -gt $limit){throw 'Native request not acknowledged'}
        }until($status.acknowledged)
    }
}
function Save-Recording([string]$Path) {
    # The offscreen host has no closable UI viewport. Save the measured scene,
    # replace it with an empty production scene after the cold interval, and
    # observe completed GPU timing for the newer scene epoch before stopping.
    $recordedScene="$project/Assets/Scenes/LX_TransformRecorded.creator"
    Cmd 'scene.save' @($recordedScene)|Out-Null
    $measuredSnapshot=Get-Content "$case/after.json" -Raw|ConvertFrom-Json
    $measuredEpoch=[uint64]$measuredSnapshot.temporalProvenance.sceneEpoch
    Cmd 'scene.new' @('MeteorRecorderDrain')|Out-Null
    $idleLimit=[DateTime]::UtcNow.AddSeconds(60)
    do {
        $idle=Read-Live
        if([uint64]$idle.gpuTemporalProvenance.sceneEpoch -gt $measuredEpoch -and
           @($idle.gpu.passes|Where-Object {$_.name -like 'METEOR.TransformMeshWorld*'}).Count -eq 0){break}
        if([DateTime]::UtcNow -gt $idleLimit){throw 'Empty scene epoch did not complete on GPU before recorder drain'}
        Start-Sleep -Milliseconds 100
    }while($true)
    $idle|ConvertTo-Json -Depth 30|Set-Content "$case/recording-idle.json"
    Cmd 'profile.pause'|Out-Null
    Wait-Recording 'finalized'|Out-Null
    $save=Cmd 'profile.save' @($Path)
    $limit=[DateTime]::UtcNow.AddSeconds(180)
    while($save.saveState -ne 'saved') {
        if($save.saveState -eq 'failed' -or [DateTime]::UtcNow -gt $limit){throw 'Profile save did not complete'}
        $save=Cmd 'profile.save' @('status')
    }
    if(!$save.complete -or $save.frames -le 0 -or !(Test-Path -LiteralPath $Path)){throw 'Recording incomplete'}
    & $Python "$PSScriptRoot/summarize-material-profile.py" --require-native-real $Path *> "$Path.oracle.log"
    if($LASTEXITCODE -ne 0){throw 'Lossless recorded native-real provenance failed'}
    $restore=Cmd 'scene.switch' @($recordedScene)
    $restoreLimit=[DateTime]::UtcNow.AddSeconds(120)
    do {
        $loaded=Cmd 'scene.load.status' @([string]$restore.requestId)
        if([DateTime]::UtcNow -gt $restoreLimit){throw 'Measured scene did not finish asynchronous restoration'}
        if(!$loaded.complete){Start-Sleep -Milliseconds 100}
    }until($loaded.complete)
    if($loaded.state -ne 'Ready'){throw 'Measured scene restoration failed'}
    Cmd 'camera.editor' @('match')|Out-Null
    $viewLimit=[DateTime]::UtcNow.AddSeconds(60)
    do {
        $restored=Cmd 'dx12.live'
        if([DateTime]::UtcNow -gt $viewLimit){throw 'Scene view did not reopen after finalized recording'}
        Start-Sleep -Milliseconds 100
    }until($restored.display.scene.active -and $restored.display.scene.ready)
}
try {
foreach($count in $Counts){foreach($iteration in 1..$Iterations){
    Assert-ArtifactCapacity $repo
    $case=Join-Path $out "N$count-R$iteration"
    $project=Join-Path $case 'Project'
    New-Item -ItemType Directory -Path $case|Out-Null
    Copy-Item -LiteralPath $FixtureProject -Destination $project -Recurse
    $shaderTarget=[IO.Path]::GetFullPath((Join-Path $project 'Assets/Shaders/DefaultPassShader'))
    if(!$shaderTarget.StartsWith($case+[IO.Path]::DirectorySeparatorChar,[StringComparison]::OrdinalIgnoreCase)){throw 'Shader fixture escaped evidence directory.'}
    if(Test-Path -LiteralPath $shaderTarget){Remove-Item -LiteralPath $shaderTarget -Recurse -Force}
    Copy-Item -LiteralPath "$repo/Dynamic_CPP/Assets/Shaders/DefaultPassShader" -Destination $shaderTarget -Recurse
    $env:CREATOR_EDITOR_WORKSPACE_DIR=Join-Path $case 'workspace'
    $env:CREATOR_EDITOR_LEGACY_INI=Join-Path $case 'workspace/legacy.ini'
    $proc=Start-Process $exe -ArgumentList @('--development-project',$project,'--command-service','--smoke-offscreen') -WorkingDirectory (Split-Path $exe) -WindowStyle Hidden -PassThru -RedirectStandardOutput "$case/editor.stdout.log" -RedirectStandardError "$case/editor.stderr.log"
    $deadline=[DateTime]::UtcNow.AddSeconds(180);$ep=$null
    do {
        Start-Sleep -Milliseconds 100
        if($proc.HasExited){throw "Startup exit $($proc.ExitCode)"}
        if(Test-Path "$project/Library/CommandService/endpoint.json"){$ep=Get-Content "$project/Library/CommandService/endpoint.json" -Raw|ConvertFrom-Json}
        if([DateTime]::UtcNow -gt $deadline){throw 'Startup timeout'}
    }until($ep -and $ep.pid -eq $proc.Id)
    $base="http://127.0.0.1:$($ep.port)";$headers=@{Authorization="Bearer $($ep.token)"}
    Cmd 'play.foreground_override' @('off')|Out-Null
    Cmd 'scene.new' @('MeteorTransformMeasurement')|Out-Null
    Cmd 'object.create' @('ProbeCamera')|Out-Null
    Cmd 'component.add' @('ProbeCamera','CameraComponent')|Out-Null
    $width=[math]::Ceiling([math]::Sqrt($count))
    Cmd 'object.transform' @('ProbeCamera',"$($width*1.5)","$($width*1.7)","$(-$width*2.5)",'20','0','0')|Out-Null
    Cmd 'object.create' @('ProbeLight','Light')|Out-Null
    Cmd 'object.transform' @('ProbeLight','0','20','0','45','-25','0')|Out-Null
    Cmd 'camera.editor' @('match')|Out-Null
    Set-Native
    Cmd 'assets.model.reimport' @("$project/Assets/Models/Prim_Suzanne.glb")|Out-Null
    Cmd 'model.loadcached' @("$project/Assets/Models/Prim_Suzanne.glb")|Out-Null
    Cmd 'model.place' @('Prim_Suzanne')|ConvertTo-Json -Depth 10|Set-Content "$case/placement.json"
    # Warm helper PSO/static source residency. New clones do not exist yet.
    Cmd 'render.live.fence' @('120')|Out-Null
    $warmLimit=[DateTime]::UtcNow.AddSeconds(120);$previous=$null;$stableSince=$null
    do {
        $warm=Read-Live
        if($warm.materialGeometry.entries -gt 0 -and $previous -and
           $warm.framesRendered -gt $previous.framesRendered -and
           $warm.materialGeometry.transforms -eq $previous.materialGeometry.transforms -and
           $warm.materialGeometry.uploads -eq $previous.materialGeometry.uploads){
            if(!$stableSince){$stableSince=[DateTime]::UtcNow}
            if(([DateTime]::UtcNow-$stableSince).TotalSeconds -ge 2){break}
        }else{$stableSince=$null}
        $previous=$warm
        if([DateTime]::UtcNow -gt $warmLimit){throw 'Seed world transform did not become reusable'}
        Start-Sleep -Milliseconds 100
    }while($true)
    $warm|ConvertTo-Json -Depth 30|Set-Content "$case/before.json"
    # Read-only power telemetry. No clock/power policy is changed for the benchmark.
    $powerProcess=$null
    if($nvidia){
        @{utc=[DateTimeOffset]::UtcNow.ToString('o');qpc=[Diagnostics.Stopwatch]::GetTimestamp();frequency=[Diagnostics.Stopwatch]::Frequency}|
            ConvertTo-Json|Set-Content "$case/power-clock-anchor.json"
        $powerProcess=Start-Process $nvidia.Source -ArgumentList @('--query-gpu=timestamp,name,driver_version,pstate,clocks.current.graphics,clocks.current.memory,utilization.gpu,power.draw,temperature.gpu','--format=csv,noheader,nounits','--loop-ms=100') -WindowStyle Hidden -PassThru -RedirectStandardOutput "$case/gpu-power.csv" -RedirectStandardError "$case/gpu-power.stderr.log"
    }
    Cmd 'profile.record'|Out-Null
    Wait-Recording 'recording'|Out-Null
    $spawn=Cmd 'scene.lxtransform.fixture' @('Prim_Suzanne',"$count")
    if($spawn.created -ne $count -or !$spawn.atomicRenderPacket){throw 'Fixture was not created in one packet.'}
    $spawn|ConvertTo-Json|Set-Content "$case/spawn.json"
    Cmd 'render.live.fence' @('120')|Out-Null
    Start-Sleep -Seconds 3
    if($RequireWarmReuse){
        $reuseLimit=[DateTime]::UtcNow.AddSeconds(120);$priorReuse=$null;$reuseStart=$null;$reuseBefore=$null
        do {
            $reuse=Read-Live
            $noTransform=@($reuse.gpu.passes|Where-Object {$_.name -like 'METEOR.TransformMeshWorld*' -or $_.name -like 'METEOR.MeshWorldReady*'}).Count -eq 0
            if($priorReuse -and $reuse.framesRendered -gt $priorReuse.framesRendered -and
               $reuse.gpu.frame -gt $priorReuse.gpu.frame -and $noTransform -and
               $reuse.materialGeometry.transforms -eq $priorReuse.materialGeometry.transforms -and
               $reuse.materialGeometry.uploads -eq $priorReuse.materialGeometry.uploads){
                if(!$reuseStart){$reuseStart=[DateTime]::UtcNow;$reuseBefore=$reuse}
                if(([DateTime]::UtcNow-$reuseStart).TotalSeconds -ge 2){break}
            }else{$reuseStart=$null;$reuseBefore=$null}
            $priorReuse=$reuse
            if([DateTime]::UtcNow -gt $reuseLimit){throw 'Active METEOR scene did not reach zero-transform warm frames'}
            Start-Sleep -Milliseconds 150
        }while($true)
        $reuseBefore|ConvertTo-Json -Depth 30|Set-Content "$case/warm-before.json"
        $reuse|ConvertTo-Json -Depth 30|Set-Content "$case/warm-after.json"
        @{passed=$true;stableSeconds=([DateTime]::UtcNow-$reuseStart).TotalSeconds;
          framesAdvanced=($reuse.framesRendered-$reuseBefore.framesRendered);gpuFramesAdvanced=($reuse.gpu.frame-$reuseBefore.gpu.frame);
          transformCounterDelta=($reuse.materialGeometry.transforms-$reuseBefore.materialGeometry.transforms);
          uploadCounterDelta=($reuse.materialGeometry.uploads-$reuseBefore.materialGeometry.uploads);
          transformPasses=0;readyPasses=0;scope='Production completed GPU frames and live counters; native zero additionally tested by GPU residency oracle'}|
            ConvertTo-Json|Set-Content "$case/warm-reuse-verdict.json"
    }
    Read-Live|ConvertTo-Json -Depth 30|Set-Content "$case/after.json"
    Save-Recording "$case/cold.ceprof"
    Cmd 'render.live.capture' @("$case/live",'editor','controlled')|Out-Null
    Cmd 'dx12.validation'|ConvertTo-Json -Depth 10|Set-Content "$case/validation.json"
    Cmd 'quit'
    if(!$proc.WaitForExit(30000)){throw 'Shutdown timeout'}
    if($powerProcess -and !$powerProcess.HasExited){Stop-Process -Id $powerProcess.Id}
    $sourceAfter=Source-Identity
    $sourceAfter|ConvertTo-Json -Depth 5|Set-Content "$case/identity-after.json"
    $stable=($sourceBefore|ConvertTo-Json -Depth 5 -Compress) -eq ($sourceAfter|ConvertTo-Json -Depth 5 -Compress)
    if($proc.ExitCode -ne 0 -or !$stable -or (Get-FileHash -LiteralPath $runtime).Hash -ne $before){throw 'Measurement identity changed or exit failed.'}
    $run=@{passed=$true;count=$count;iteration=$iteration;validation='off';exitCode=$proc.ExitCode;normalQuit=$true;sourceStable=$stable;runtimeSha256=$before;profile='cold.ceprof';noOldPathComparison=$true}
    $run|ConvertTo-Json|Set-Content "$case/run.json"
    $runs+=@($run+@{directory=$case})
    "METEOR_TRANSFORM_MEASURE_RUN_OK count=$count iteration=$iteration"
}}
$runs|ConvertTo-Json -Depth 8|Set-Content "$out/runs.json"
}finally{
    if($proc -and !$proc.HasExited){Stop-Process -Id $proc.Id}
    if($powerProcess -and !$powerProcess.HasExited){Stop-Process -Id $powerProcess.Id}
    $env:CREATOR_DX12_VALIDATION=$previousValidation
    $env:CREATOR_METEOR_TRANSFORM_AUDIT=$previousAudit
    $env:CREATOR_EDITOR_WORKSPACE_DIR=$previousWorkspace
    $env:CREATOR_EDITOR_LEGACY_INI=$previousLegacy
}
