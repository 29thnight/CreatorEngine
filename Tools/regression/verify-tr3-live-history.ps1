#Requires -Version 7.0
param([ValidateSet('Debug','Release')][string]$Configuration='Debug', [Parameter(Mandatory)][string]$OutputDirectory)
$ErrorActionPreference='Stop'
$repo=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$out=[IO.Path]::GetFullPath($OutputDirectory)
if(Test-Path $out){throw 'Fresh output required'}
New-Item -ItemType Directory $out | Out-Null
& "$repo/Tools/regression/new-temporal-base0-fixture.ps1" -OutputDirectory "$out/Project"
Copy-Item "$repo/Dynamic_CPP/Assets/Shaders" "$out/Project/Assets/Shaders" -Recurse
$scenePath="$out/Project/Assets/Scenes/TR0_Native_Workload.creator"
$scene=[IO.File]::ReadAllText($scenePath).Replace('m_name: MainCamera','m_name: Camera')
[IO.File]::WriteAllText($scenePath,$scene)
& python "$repo/Tools/regression/prepare-tr3-live-geometry.py" "$out/Project"
if($LASTEXITCODE){throw "Fixture geometry preparation failed"}
$exe="$repo/Bin/x64-$Configuration/Editor/CreatorEditor.exe"
$savedEnvironment=@{}
foreach($name in @('CREATOR_TEMPORAL_RUNTIME_DIRECTORY','CREATOR_DX12_VALIDATION','CREATOR_EDITOR_WORKSPACE_DIR','CREATOR_EDITOR_LEGACY_INI')) {
    $savedEnvironment[$name]=[Environment]::GetEnvironmentVariable($name,'Process')
}
$env:CREATOR_TEMPORAL_RUNTIME_DIRECTORY="$repo/Bin/x64-$Configuration/Runtime"
$env:CREATOR_DX12_VALIDATION='basic'
$env:CREATOR_EDITOR_WORKSPACE_DIR="$out/workspace"
$env:CREATOR_EDITOR_LEGACY_INI="$out/workspace/legacy.ini"
$arguments=@('--development-project',"$out/Project",'--command-service','--smoke-offscreen')
if(@($arguments | Where-Object { $_.Contains('"') }).Count){throw 'Arguments containing quotes are unsupported.'}
$quoted=@($arguments | ForEach-Object { '"'+$_+'"' })
$p=Start-Process $exe -ArgumentList $quoted -WorkingDirectory (Split-Path $exe) -WindowStyle Hidden -RedirectStandardOutput "$out/stdout.log" -RedirectStandardError "$out/stderr.log" -PassThru
$passed=$false
try {
    $endpoint="$out/Project/Library/CommandService/endpoint.json"
    $limit=[DateTime]::UtcNow.AddSeconds(90)
    do {
        if($p.HasExited){throw 'Editor exited before endpoint'}
        if(Test-Path $endpoint){$e=Get-Content $endpoint -Raw | ConvertFrom-Json;if($e.pid -eq $p.Id){break}}
        if([DateTime]::UtcNow -ge $limit){throw 'Endpoint timeout'}
        Start-Sleep -Milliseconds 250
    }while($true)
    $base='http://127.0.0.1:'+$e.port
    $headers=@{Authorization='Bearer '+$e.token}
    function Command([string]$name,[string[]]$arguments=@()) {
        $r=Invoke-RestMethod "$base/command" -Method Post -Headers $headers -ContentType application/json -Body (@{command=$name;args=@($arguments);mode='async'}|ConvertTo-Json -Compress) -TimeoutSec 180
        if($name -eq 'quit'){return}
        if($r.PSObject.Properties['operationId']) {
            $poll=$r.poll
            $limit=[DateTime]::UtcNow.AddSeconds(180)
            do {
                Start-Sleep -Milliseconds 100
                $r=Invoke-RestMethod "$base$poll" -Headers $headers -TimeoutSec 180
                if([DateTime]::UtcNow -ge $limit){throw "$name timeout"}
            }until($r.state -eq 'completed')
        }
        @{command=$name;result=$r}|ConvertTo-Json -Depth 20 -Compress|Add-Content "$out/setup.jsonl"
        if($r.status -ne 'succeeded'){throw "$name failed: $($r.message)"}
        return $r.data
    }
    $request=Command 'scene.switch' @("$out/Project/Assets/Scenes/TR0_Native_Workload.creator")
    $limit=[DateTime]::UtcNow.AddSeconds(120)
    do {
        $load=Command 'scene.load.status' @([string]$request.requestId)
        if($load.state -in @('failed','cancelled')){throw 'Scene load failed'}
        if($load.complete){break}
        if([DateTime]::UtcNow -ge $limit){throw 'Scene activation timeout'}
        Start-Sleep -Milliseconds 250
    }while($true)
    Command 'editor.viewport' @('game') | Out-Null
    Command 'window.resize' @('1280','720') | Out-Null
    Start-Sleep -Seconds 3
    & "$repo/Tools/temporal-validation/Verify-LiveSSGIRecovery.ps1" -EndpointFile $endpoint -OutputDirectory "$out/recovery" -Provider none -KeepRawCaptures
    $validation=Command 'dx12.validation'
    $validation | ConvertTo-Json -Depth 10 | Set-Content "$out/validation.json"
    $passed=$true
}catch {
    $_ | Out-String | Set-Content "$out/error.txt"
    throw
}finally {
    foreach($name in $savedEnvironment.Keys){[Environment]::SetEnvironmentVariable($name,$savedEnvironment[$name],'Process')}
    if(!$p.HasExited -and $e){Command 'quit' | Out-Null}
    if(!$p.WaitForExit(60000)){throw 'Normal shutdown timed out'}
    @{passed=($passed -and $p.ExitCode -eq 0);configuration=$Configuration;exitCode=$p.ExitCode;binarySha256=(Get-FileHash (Join-Path (Split-Path $exe) 'CreatorEditor.runtime.dll')).Hash} | ConvertTo-Json | Set-Content "$out/result.json"
}
