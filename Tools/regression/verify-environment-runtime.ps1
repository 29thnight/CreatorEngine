param(
    [ValidateSet('Debug','Release')][string]$Configuration='Debug',
    [Parameter(Mandatory=$true)][string]$SessionDirectory
)
Set-StrictMode -Version Latest
$ErrorActionPreference='Stop'
$repo=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$case=[IO.Path]::GetFullPath($SessionDirectory)
$endpoint=Get-Content "$case/Project/Library/CommandService/endpoint.json" -Raw|ConvertFrom-Json
$headers=@{Authorization="Bearer $($endpoint.token)"}
$base="http://127.0.0.1:$($endpoint.port)"
function Cmd($Name,[string[]]$Arguments=@()) {
    $response=Invoke-RestMethod "$base/command" -Method Post -Headers $headers -ContentType 'application/json' -Body (@{command=$Name;args=@($Arguments);mode='async'}|ConvertTo-Json -Compress)
    if($Name -eq 'quit'){return}
    $deadline=[DateTime]::UtcNow.AddSeconds(120)
    if($response.operationId){
        $poll=$response.poll
        do {Start-Sleep -Milliseconds 50; $response=Invoke-RestMethod "$base$poll" -Headers $headers
            if([DateTime]::UtcNow -gt $deadline){throw "$Name timeout"}
        }until($response.state -eq 'completed')
    }
    $response|ConvertTo-Json -Depth 30 -Compress|Add-Content "$case/responses.jsonl"
    if($response.status -ne 'succeeded'){throw "$Name failed: $($response.code) $($response.message)"}
    $response.data
}
function Fence { Cmd 'render.live.fence' @('120')|Out-Null }
function Stage($Name,$Path) {
    $watch=[Diagnostics.Stopwatch]::StartNew()
    if($Path){Cmd 'render.environment' @($Path)|Out-Null}
    Fence
    $watch.Stop()
    $snapshot=Cmd 'dx12.live'
    if(!$snapshot.enabled -or !$snapshot.ready -or $snapshot.framesRendered -le 0){throw "$Name renderer is not ready"}
    @{elapsedMs=$watch.Elapsed.TotalMilliseconds;state=$snapshot}|ConvertTo-Json -Depth 30|Set-Content "$case/$Name.json"
    Write-Output "ENVIRONMENT_RUNTIME_STAGE_OK $Name elapsed_ms=$($watch.Elapsed.TotalMilliseconds)"
}
Stage 'default' ''
$hdr=Join-Path $repo 'Dynamic_CPP/Assets/HDR/autumn_field_puresky_1k.hdr'
Stage 'hdr-cold' $hdr
# Allow fence-completed readbacks and the async atomic disk writer to publish.
Start-Sleep -Seconds 2
$cache=@(Get-ChildItem "$case/Project/Saved/Editor/Cache/Environment" -Filter '*.ceibl')
if($cache.Count -lt 1){throw 'Runtime HDR cooking did not persist a cache'}
$sha=(Get-FileHash $cache[0].FullName).Hash
Stage 'forest-return' (Join-Path $repo 'Resources/Environment/forest.ceibl')
Stage 'hdr-warm' $hdr
Start-Sleep -Seconds 1
if((Get-FileHash $cache[0].FullName).Hash -ne $sha){throw 'Warm selection changed cooked pixels'}
$corrupt=[IO.File]::ReadAllBytes((Join-Path $repo 'Resources/Environment/forest.ceibl'))
$corrupt[$corrupt.Length-1]=$corrupt[$corrupt.Length-1] -bxor 1
[IO.File]::WriteAllBytes("$case/corrupt.ceibl",$corrupt)
$bad=Invoke-RestMethod "$base/command" -Method Post -Headers $headers -ContentType 'application/json' -Body (@{command='render.environment';args=@("$case/corrupt.ceibl");mode='async'}|ConvertTo-Json -Compress)
$poll=$bad.poll
$deadline=[DateTime]::UtcNow.AddSeconds(120)
do {
    Start-Sleep -Milliseconds 50; $bad=Invoke-RestMethod "$base$poll" -Headers $headers
    if([DateTime]::UtcNow -gt $deadline){throw 'Corrupt selection rejection timeout'}
}until($bad.state -eq 'completed')
if($bad.status -eq 'succeeded'){throw 'Corrupt selection accepted'}
Fence
$after=Cmd 'dx12.live'
if(!$after.ready){throw 'Corrupt selection disrupted the previous environment'}
@{configuration=$Configuration;pid=$endpoint.pid;cache=$cache[0].FullName;cacheSha256=$sha;corruptSelection='rejected';previousEnvironment='rendering'}|ConvertTo-Json|Set-Content "$case/result.json"
Cmd 'log.flush'|Out-Null
Cmd 'quit'|Out-Null
Write-Output 'ENVIRONMENT_RUNTIME_OK defaultCooked=true hdrColdPersisted=true warmPixelsStable=true corruptSelectionRejected=true'
