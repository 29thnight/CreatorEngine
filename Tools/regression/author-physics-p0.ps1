param([string]$Configuration='Release')
$ErrorActionPreference='Stop'
$repo=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$out=Join-Path $repo 'Build/Obj/Phase19P0/http'
New-Item -ItemType Directory -Force $out | Out-Null
if(Get-Process CreatorEditor -ErrorAction SilentlyContinue){throw 'An editor is already running.'}
$exe=Join-Path $repo "Bin/x64-$Configuration/Editor/CreatorEditor.exe"
$process=Start-Process $exe -ArgumentList '--command-service','--console' -WorkingDirectory (Split-Path $exe) -WindowStyle Hidden -RedirectStandardOutput "$out/editor.out" -RedirectStandardError "$out/editor.err" -PassThru
try {
    $endpoint=Join-Path $repo 'Dynamic_CPP/Library/CommandService/endpoint.json'
    $deadline=(Get-Date).AddSeconds(180)
    do {
        if($process.HasExited){throw "Editor exited: $($process.ExitCode)"}
        if(Test-Path $endpoint){$info=Get-Content $endpoint -Raw | ConvertFrom-Json; if($info.pid -eq $process.Id){break}}
        Start-Sleep -Milliseconds 300
    } while((Get-Date) -lt $deadline)
    if(!$info -or $info.pid -ne $process.Id){throw 'No owned HTTP endpoint'}
    $base="http://127.0.0.1:$($info.port)"
    $headers=@{Authorization="Bearer $($info.token)"}
    Invoke-RestMethod "$base/commands" -Headers $headers | ConvertTo-Json -Depth 30 | Set-Content "$out/commands.json" -Encoding utf8
    $script:sequence=0
    function Command([string]$name,[string[]]$arguments=@()) {
        $script:sequence++
        $mode=if($name -eq 'render.live.fence'){'async'}else{'sync'}
        $body=@{command=$name;args=@($arguments);mode=$mode;correlationId="physics-p0-$script:sequence"} | ConvertTo-Json -Compress
        $r=Invoke-RestMethod "$base/command" -Headers $headers -Method Post -ContentType 'application/json' -Body $body -TimeoutSec 120
        if($r.operationId){
            $poll=$r.poll
            $operationDeadline=(Get-Date).AddSeconds(90)
            do {
                Start-Sleep -Milliseconds 60
                $r=Invoke-RestMethod "$base$poll" -Headers $headers
                if((Get-Date) -gt $operationDeadline){throw "$name operation timeout"}
            } until($r.state -eq 'completed')
        }
        $script:lastResult=$r
        @{command=$name;args=@($arguments);result=$r} | ConvertTo-Json -Depth 30 -Compress | Add-Content "$out/results.jsonl" -Encoding utf8
        if($r.status -ne 'succeeded'){throw "$name failed: $($r.code) $($r.message)"}
        Write-Output "$name : succeeded"
    }
    if(Test-Path "$out/results.jsonl"){Remove-Item -LiteralPath "$out/results.jsonl"}
    Command 'scene.new' @('PhysicsP0Baseline')
    Command 'object.create' @('P0Floor')
    Command 'component.add' @('P0Floor','BoxColliderComponent')
    Command 'object.property' @('P0Floor','BoxColliderComponent','m_boxExtent','20 0.5 20')
    Command 'object.transform' @('P0Floor','0','-0.5','0')
    Command 'object.create' @('P0Drop')
    Command 'component.add' @('P0Drop','BoxColliderComponent')
    Command 'object.property' @('P0Drop','BoxColliderComponent','m_boxExtent','0.5 0.5 0.5')
    Command 'component.add' @('P0Drop','RigidBodyComponent')
    Command 'object.transform' @('P0Drop','0','5','0')
    Command 'object.create' @('P0Character')
    Command 'component.add' @('P0Character','CharacterControllerComponent')
    Command 'component.add' @('P0Character','RigidBodyComponent')
    Command 'object.transform' @('P0Character','4','5','0')
    $scene=Join-Path $repo 'Dynamic_CPP/Assets/Scenes/PhysicsP0Baseline.creator'
    Command 'scene.save' @($scene)
    Command 'prefab.create' @('P0Drop','PhysicsP0Drop')
    Command 'scene.transformdigest' @('before-reload')
    Command 'scene.load' @($scene)
    Command 'scene.transformdigest' @('after-reload')
    Command 'object.describe' @('P0Drop')
    Command 'profile.record'
    Command 'play'
    Start-Sleep -Seconds 5
    Command 'play.state'
    Command 'object.describe' @('P0Drop')
    Command 'object.describe' @('P0Character')
    Command 'object.enable' @('P0Drop','off')
    Command 'object.describe' @('P0Drop')
    if($script:lastResult.data.enabled){throw 'Disable did not apply'}
    Command 'object.enable' @('P0Drop','on')
    Command 'object.describe' @('P0Drop')
    if(!$script:lastResult.data.enabled){throw 'Enable did not apply'}
    Command 'render.live.fence' @('60')
    if($script:lastResult.data.completedFrame -le $script:lastResult.data.afterFrame){throw 'Render completion boundary was not reached'}
    Command 'profile.pause'
    Command 'profile.frame'
    $capture=Join-Path $out ((Get-Date -Format 'yyyyMMdd-HHmmss')+'.ceprof')
    Command 'profile.save' @($capture)
    Command 'stop'
    $stopDeadline=(Get-Date).AddSeconds(15)
    do {
        Command 'play.state'
        if(!$script:lastResult.data.pending -and !$script:lastResult.data.committed){break}
        Start-Sleep -Milliseconds 100
    } while((Get-Date) -lt $stopDeadline)
    if($script:lastResult.data.pending -or $script:lastResult.data.committed){throw 'Stop transition did not commit'}
    Command 'scene.transformdigest' @('after-stop')
    $records=@(Get-Content "$out/results.jsonl" | ForEach-Object {$_ | ConvertFrom-Json})
    $digests=@($records | Where-Object command -eq 'scene.transformdigest')
    $poses=@($records | Where-Object command -eq 'object.describe')
    $playState=@(($records | Where-Object command -eq 'play.state').result.data)
    if($digests[0].result.data.hash -ne $digests[1].result.data.hash){throw 'Save/reload Transform digest changed'}
    # Stop recreates entities in a different index order; compare semantic poses by name.
    $before=ConvertTo-Json -InputObject @($digests[0].result.data.worldTransforms | Sort-Object name) -Depth 20 -Compress
    $after=ConvertTo-Json -InputObject @($digests[2].result.data.worldTransforms | Sort-Object name) -Depth 20 -Compress
    if($before -ne $after){throw 'Stop did not restore the authored Transform values'}
    if($poses[1].result.data.position[1] -ge 5 -or $poses[1].result.data.position[1] -lt 0){throw 'Drop did not reach the floor region'}
    if($poses[2].result.data.position[1] -ge 5 -or $poses[2].result.data.position[1] -lt 0){throw 'CCT did not descend to the floor region'}
    if(!(Test-Path $capture) -or (Get-Item $capture).Length -eq 0){throw 'Missing profiling capture'}
    $fixtures=Join-Path $PSScriptRoot 'fixtures/physics-p0'
    New-Item -ItemType Directory -Force $fixtures | Out-Null
    Copy-Item -LiteralPath $scene -Destination (Join-Path $fixtures 'PhysicsP0Baseline.creator')
    Copy-Item -LiteralPath (Join-Path $repo 'Dynamic_CPP/Assets/Prefabs/PhysicsP0Drop.prefab') -Destination (Join-Path $fixtures 'PhysicsP0Drop.prefab')
    @{exe_sha256=(Get-FileHash $exe -Algorithm SHA256).Hash;scene_sha256=(Get-FileHash $scene -Algorithm SHA256).Hash;digests=$digests;poses=$poses;play_state=$playState;capture=$capture} | ConvertTo-Json -Depth 30 | Set-Content "$out/baseline.json" -Encoding utf8
    Command 'prefab.instantiate' @('PhysicsP0Drop','P0Temporary')
    Command 'component.remove' @('P0Temporary','RigidBodyComponent')
    Command 'object.describe' @('P0Temporary')
    if($script:lastResult.data.components.type -contains 'RigidBodyComponent'){throw 'Component removal did not apply'}
    Command 'object.delete' @('P0Temporary')
    Command 'scene.transformdigest' @('after-delete')
    if($script:lastResult.data.objects -ne 4){throw 'Temporary entity was not destroyed'}
    Command 'quit'
    $process.WaitForExit(15000) | Out-Null
} finally {
    if(!$process.HasExited){$process.Kill();$process.WaitForExit(15000)|Out-Null}
}
