param([string]$EditorExe='')
$ErrorActionPreference='Stop'
$repo='C:/Users/idene/source/repos/CreatorEngine'
if(!$EditorExe){$EditorExe=Join-Path $repo 'Bin/x64-Debug/Editor/CreatorEditor.exe'}
$exe=Get-Item -LiteralPath $EditorExe
# The launcher stays unchanged when host implementation TUs relink the runtime DLL.
$runtimePath=Join-Path $exe.DirectoryName 'CreatorEditor.runtime.dll'
$implementation=if(Test-Path -LiteralPath $runtimePath){Get-Item -LiteralPath $runtimePath}else{$exe}
$required=@('Editor/EngineEntry/EditorObjectOperations.cpp','Editor/EngineEntry/PhysicsShapeDocument.h','Editor/EngineEntry/Commands/SceneObjectCommands.cpp','Editor/EngineGUIWindow/InspectorWindow.cpp','Engine/SceneRuntime/PhysicsBodyComponent.h') | ForEach-Object {Get-Item (Join-Path $repo $_)}
if($required | Where-Object {$_.LastWriteTimeUtc -gt $implementation.LastWriteTimeUtc}){throw 'Fresh Editor build required; stale executable is not product evidence'}
if(Get-Process CreatorEditor -ErrorAction SilentlyContinue){throw 'An Editor is already running; the gate needs its own session'}
$id=[guid]::NewGuid().ToString('N')
$out=Join-Path $repo "Build/Obj/Phase19T2QueryBench/DynamicAuthor-$id"
New-Item -ItemType Directory -Force $out | Out-Null
$process=Start-Process $exe.FullName -ArgumentList '--command-service','--console','--allow-user-code' -WorkingDirectory $exe.DirectoryName -WindowStyle Hidden -RedirectStandardOutput "$out/editor.out" -RedirectStandardError "$out/editor.err" -PassThru
$script:sequence=0
try {
    $endpoint=Join-Path $repo 'Dynamic_CPP/Library/CommandService/endpoint.json'
    $deadline=(Get-Date).AddSeconds(180)
    $info=$null
    do {
        if($process.HasExited){throw "Editor exited: $($process.ExitCode)"}
        if(Test-Path $endpoint){$candidate=Get-Content $endpoint -Raw|ConvertFrom-Json; if($candidate.pid -eq $process.Id){$info=$candidate;break}}
        Start-Sleep -Milliseconds 200
    } while((Get-Date) -lt $deadline)
    if(!$info){throw 'Owned HTTP endpoint unavailable'}
    $base="http://127.0.0.1:$($info.port)"
    $headers=@{Authorization="Bearer $($info.token)"}
    function Command([string]$name,[string[]]$arguments=@(),[switch]$Reject) {
        $script:sequence++
        $body=@{command=$name;args=@($arguments);mode='sync';correlationId="physics-shapes-$script:sequence"}|ConvertTo-Json -Compress
        try {
            $result=Invoke-RestMethod "$base/command" -Headers $headers -Method Post -ContentType 'application/json' -Body $body -TimeoutSec 120
        } catch {
            if (!$Reject -or !$_.ErrorDetails.Message) { throw }
            $result=$_.ErrorDetails.Message | ConvertFrom-Json
            if ($result.schemaVersion -ne 1 -or $result.command -ne $name -or
                $result.status -notin @('invalid_arguments','preconditions_failed')) { throw }
        }
        if($result.operationId){
            $poll=$result.poll; $until=(Get-Date).AddSeconds(90)
            do {Start-Sleep -Milliseconds 60; $result=Invoke-RestMethod "$base$poll" -Headers $headers; if((Get-Date) -gt $until){throw 'Operation timeout'}} until($result.state -eq 'completed')
        }
        @{command=$name;args=@($arguments);result=$result}|ConvertTo-Json -Depth 30 -Compress|Add-Content "$out/results.jsonl" -Encoding utf8
        if($Reject){if($result.status -notin @('invalid_arguments','preconditions_failed')){throw "Expected argument/precondition rejection: $name"}}
        elseif($result.status -ne 'succeeded'){throw "$name failed: $($result.code) $($result.message)"}
        return $result
    }
    $scene="$repo/Dynamic_CPP/Assets/Scenes/PhysicsQueryDynamic.creator"
    if(Test-Path -LiteralPath $scene){throw 'Dynamic scene already exists; preserve authored content'}

    $null=Command 'scene.switch' @("$repo/Dynamic_CPP/Assets/Scenes/PhysicsQueryStress.creator")
    Start-Sleep -Seconds 2

    for($i=0;$i -lt 128;$i++){
        $name=('DenseQuery{0:d3}' -f $i)
        $null=Command 'object.property' @($name,'PhysicsBodyComponent','m_motion','2')
        $null=Command 'object.property' @($name,'PhysicsBodyComponent','m_gravityEnabled','false')
    }

    $null=Command 'scene.save' @($scene)
    @{scene=$scene;bodies=128;commands=$script:sequence}|ConvertTo-Json|Set-Content "$out/author.json" -Encoding utf8

    $null=Command 'quit'
} finally {
    if(!$process.WaitForExit(30000)){$process.Kill();$process.WaitForExit()}
}
