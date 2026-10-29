[CmdletBinding()]
param([ValidateSet('Debug','Release')][string]$Configuration='Release', [string]$EditorExe='')
$ErrorActionPreference='Stop'
$repo=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
if(Get-Process CreatorEditor -ErrorAction SilentlyContinue){throw 'Close the existing Editor; this gate owns its session.'}
$out=Join-Path $repo ('Build/Verification/N3Exchange/Editor-'+[guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Force $out|Out-Null
$exe=if($EditorExe){(Resolve-Path -LiteralPath $EditorExe).Path}else{Join-Path $repo "Bin/x64-$Configuration/Editor/CreatorEditor.exe"}
$project=Join-Path $out 'Project'
New-Item -ItemType Directory -Force "$project/Assets/Script","$project/Assets/Scenes"|Out-Null
Copy-Item "$repo/Dynamic_CPP/ProjectSetting" $project -Recurse
Copy-Item "$repo/Dynamic_CPP/Assets/Shaders" "$project/Assets/Shaders" -Recurse
Copy-Item "$repo/GameScripts/SimulationExchangeContractProbe.fs" "$project/Assets/Script"
Set-Content "$project/Assets/Script/compile.order" 'SimulationExchangeContractProbe.fs' -Encoding utf8
& dotnet build "$repo/Engine/Scripting/GameScripts/ScriptNative.GameScripts.fsproj" -c $Configuration "-p:CreatorScriptSourceRoot=$project/Assets/Script" -v:q *> "$out/managed-build.log"
if($LASTEXITCODE){throw "F# exchange fixture build failed: $out/managed-build.log"}
$started=Get-Date
$process=Start-Process $exe -ArgumentList @('--development-project',('"'+$project+'"'),'--command-service','--allow-user-code','--smoke-offscreen') -WorkingDirectory (Split-Path $exe) -WindowStyle Hidden -RedirectStandardOutput "$out/editor.out" -RedirectStandardError "$out/editor.err" -PassThru
$script:sequence=0
try {
    $until=(Get-Date).AddSeconds(180)
    $endpoint=Join-Path $project 'Library/CommandService/endpoint.json'
    do {
        if($process.HasExited){throw "Editor exited $($process.ExitCode)"}
        if(Test-Path $endpoint){$candidate=Get-Content $endpoint -Raw|ConvertFrom-Json;if($candidate.pid -eq $process.Id){$info=$candidate;break}}
        Start-Sleep -Milliseconds 200
    }while((Get-Date) -lt $until)
    if(!$info){throw 'Owned endpoint unavailable'}
    $headers=@{Authorization="Bearer $($info.token)"};$base="http://127.0.0.1:$($info.port)"
    function Command([string]$name,[string[]]$arguments=@()) {
        $script:sequence++
        $body=@{command=$name;args=@($arguments);mode='sync';correlationId="n3-exchange-$script:sequence"}|ConvertTo-Json -Compress
        $result=Invoke-RestMethod "$base/command" -Headers $headers -Method Post -ContentType 'application/json' -Body $body -TimeoutSec 120
        if($result.operationId){$poll=$result.poll;$deadline=(Get-Date).AddSeconds(120);do {Start-Sleep -Milliseconds 100;$result=Invoke-RestMethod "$base$poll" -Headers $headers;if((Get-Date)-gt $deadline){throw 'Command timeout'}}until($result.state -eq 'completed')}
        @{command=$name;args=$arguments;result=$result}|ConvertTo-Json -Depth 25 -Compress|Add-Content "$out/results.jsonl" -Encoding utf8
        if($result.status -ne 'succeeded'){throw "$name failed: $($result.code) $($result.message)"}
        return $result
    }
    $null=Command 'scene.new' @('N3Exchange')
    $null=Command 'object.create' @('N3Driver')
    $null=Command 'script.add' @('N3Driver','SimulationExchangeContractProbe')
    $null=Command 'object.create' @('N3Camera')
    $null=Command 'component.add' @('N3Camera','CameraComponent')
    $null=Command 'object.property' @('N3Camera','CameraComponent','m_isPrimary','true')
    $scene=Join-Path $project ('Assets/Scenes/N3Exchange-'+(Split-Path $out -Leaf)+'.creator')
    $null=Command 'scene.save' @($scene)
    $sceneHash=(Get-FileHash $scene).Hash
    $null=Command 'play.foreground_override' @('on')
    $cycles=@()
    foreach($cycle in 1..2) {
        $null=Command 'play'
        $until=(Get-Date).AddSeconds(60)
        do {
            Start-Sleep -Milliseconds 150
            $state=(Command 'script.invoke' @('SimulationExchangeContractProbe','State')).data.returnValue|ConvertFrom-Json
            if($state.failures -gt 0){throw 'N3 product assertion failed'}
            if($process.HasExited -or (Get-Date)-gt $until){throw 'N3 product timeout'}
        }until($state.completed -eq $cycle)
        $null=Command 'play.pause'
        $paused=(Command 'script.invoke' @('SimulationExchangeContractProbe','State')).data.returnValue|ConvertFrom-Json
        $null=Command 'script.invoke' @('SimulationExchangeContractProbe','QueuePaused')
        Start-Sleep -Milliseconds 300
        $still=(Command 'script.invoke' @('SimulationExchangeContractProbe','State')).data.returnValue|ConvertFrom-Json
        if($paused.committed -ne $still.committed -or $still.queued -ne 1){throw 'Paused commands were lost or applied'}
        $null=Command 'play.resume'
        $until=(Get-Date).AddSeconds(10)
        do{Start-Sleep -Milliseconds 100;$resumed=(Command 'script.invoke' @('SimulationExchangeContractProbe','State')).data.returnValue|ConvertFrom-Json;if((Get-Date)-gt $until){throw 'Resume timeout'}}until($resumed.committed -gt $still.committed -and $resumed.queued -eq 0)
        if($resumed.failures){throw 'Resumed command failed'}
        $null=Command 'stop'
        $until=(Get-Date).AddSeconds(30)
        do{Start-Sleep -Milliseconds 100;$play=(Command 'play.state').data;if((Get-Date)-gt $until){throw 'Stop timeout'}}while($play.pending -or $play.committed)
        $null=Command 'script.invoke' @('SimulationExchangeContractProbe','CheckStopped')
        $state=(Command 'script.invoke' @('SimulationExchangeContractProbe','State')).data.returnValue|ConvertFrom-Json
        if($state.failures -or $state.ended -ne $cycle){throw 'Stop epoch/teardown failed'}
        $cycles+=@{cycle=$cycle;state=$state;paused=$still;resumed=$resumed}
    }
    $beforeReload=$state.epoch
    $null=Command 'script.reload'
    $reloadState=(Command 'script.invoke' @('SimulationExchangeContractProbe','State')).data.returnValue|ConvertFrom-Json
    if($reloadState.epoch -le $beforeReload -or $reloadState.committed -ne 0 -or $reloadState.queued -ne 0){throw 'Reload did not invalidate exchange'}
    $null=Command 'scene.new' @('N3EpochAfter')
    $sceneState=(Command 'script.invoke' @('SimulationExchangeContractProbe','State')).data.returnValue|ConvertFrom-Json
    if($sceneState.epoch -le $reloadState.epoch -or $sceneState.active -ne 0 -or $sceneState.queued -ne 0){throw 'Scene change did not invalidate exchange'}
    $null=Command 'log.flush'
    $null=Command 'quit'
    if(!$process.WaitForExit(30000)){throw 'Editor did not close'}
    $log=Get-ChildItem (Join-Path $project 'Saved/Editor/Log') -File -Filter 'Editor_*.html' | Where-Object LastWriteTime -ge $started | Sort-Object LastWriteTime -Descending | Select-Object -First 1
    if(!$log){throw 'Fresh log missing'}
    Copy-Item $log.FullName "$out/editor.html"
    $text=((Get-Content "$out/editor.html" -Raw) -replace '<[^>]+>','') + (Get-Content "$out/editor.out" -Raw)
    if([regex]::Matches($text,'\[N3_PRODUCT_OK\]').Count -ne 2 -or $text -match '\[N3_FAIL\]'){throw 'Product log assertions failed'}
    if((Get-FileHash $scene).Hash -ne $sceneHash){throw 'Authored scene mutated'}
    @{result='N3_EXCHANGE_EDITOR_OK';scene=$scene;sceneSha256=$sceneHash;cycles=$cycles;reload=$reloadState;sceneChange=$sceneState;hostHash=(Get-FileHash (Join-Path (Split-Path $exe) 'CreatorEditor.runtime.dll')).Hash;nativeBindingHash=(Get-FileHash (Join-Path (Split-Path (Split-Path $exe)) 'Managed/CreatorEngine.NativeBindings.dll')).Hash}|ConvertTo-Json -Depth 20|Set-Content "$out/result.json" -Encoding utf8
    "N3_EXCHANGE_EDITOR_OK evidence=$out"
} finally {if(!$process.WaitForExit(30000)){$process.Kill();$process.WaitForExit()}}
