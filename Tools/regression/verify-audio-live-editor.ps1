param([ValidateRange(1,100)][int]$Cycles = 100, [string]$OutputDirectory = '')

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$repo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$rootBase = Join-Path $repo 'Build/Validation/Phase22Editor'
$root = if ($OutputDirectory) { [IO.Path]::GetFullPath($OutputDirectory) } else { Join-Path $rootBase ('run-' + [guid]::NewGuid().ToString('N')) }
New-Item -ItemType Directory -Path $root -Force | Out-Null
$endpoint = Get-Content (Join-Path $repo 'Dynamic_CPP/Library/CommandService/endpoint.json') -Raw | ConvertFrom-Json
$ownedPid = [int](Get-Content (Join-Path $rootBase 'pid.txt'))
if ($endpoint.pid -ne $ownedPid)
{
    throw 'The current endpoint does not belong to the Editor opened for this acceptance.'
}
$sequence = 0
function Call([string]$command, [string[]]$arguments = @())
{
    $script:sequence++
    $result = Invoke-RestMethod -Uri ('http://127.0.0.1:'+$endpoint.port+'/command') -Method Post `
        -Headers @{Authorization='Bearer '+$endpoint.token} -ContentType 'application/json' `
        -Body (@{command=$command;args=$arguments} | ConvertTo-Json -Compress) -TimeoutSec 30
    if ($result.status -eq 'accepted')
    {
        $poll = $result.poll
        $deadline = [DateTime]::UtcNow.AddSeconds(60)
        do
        {
            Start-Sleep -Milliseconds 100
            $result = Invoke-RestMethod -Uri ('http://127.0.0.1:'+$endpoint.port+$poll) -Headers @{Authorization='Bearer '+$endpoint.token} -TimeoutSec 5
        } while ($result.state -ne 'completed' -and [DateTime]::UtcNow -lt $deadline)
    }
    $result | ConvertTo-Json -Depth 20 | Set-Content (Join-Path $root ('{0:d4}-{1}.json' -f $script:sequence,$command)) -Encoding utf8
    if ($result.status -ne 'succeeded' -or $result.code -ne 'ok')
    {
        throw "$command failed: $($result.code) $($result.message)"
    }
    return $result.data
}

function AssertAudio([double]$Expected, [long]$AfterFrame)
{
    $deadline = [DateTime]::UtcNow.AddSeconds(10)
    do
    {
        $snapshot = Call 'audio.status' @('Phase22AudioAcceptance')
        if ($snapshot.activeVoices -eq $Expected -and $snapshot.playbackInstances -eq $Expected)
        {
            if ($snapshot.backendFailures -ne 0 -or $snapshot.playing -ne ($Expected -eq 1) -or
                $snapshot.physicalVoices -ne $Expected -or $snapshot.childVoices -ne $Expected -or
                ($Expected -eq 0 -and $snapshot.playbackHandle -ne '0'))
            {
                throw 'Actual Editor audio state disagrees with the playback expectation.'
            }
            return $AfterFrame + 1
        }
        Start-Sleep -Milliseconds 100
    } while ([DateTime]::UtcNow -lt $deadline)
    throw "Editor audio did not converge: voices=$($snapshot.activeVoices) instances=$($snapshot.playbackInstances) expected=$Expected"
}
$target = 'Phase22AudioAcceptance'
$object = Call 'object.describe' @($target)
if (!($object.components | Where-Object type -eq 'SoundComponent'))
{
    $null = Call 'component.add' @($target,'SoundComponent')
}
$before = Call 'object.properties' @($target,'SoundComponent')
$before | ConvertTo-Json -Depth 15 | Set-Content (Join-Path $root 'sound-before.json')
foreach ($setting in @(
    @('clipKey','41ec131c-cf46-402f-b5f3-b0484468ca5d'),
    @('volume','0.02'), @('loop','true'), @('playOnStart','true'), @('spatial','false')))
{
    $null = Call 'object.property' @($target,'SoundComponent',$setting[0],$setting[1])
}
$after = Call 'object.properties' @($target,'SoundComponent')
$after | ConvertTo-Json -Depth 15 | Set-Content (Join-Path $root 'sound-configured.json')
$audioFrame = AssertAudio 0 -1
for ($cycle = 0; $cycle -lt $Cycles; ++$cycle)
{
    $null = Call 'play'
    $deadline = [DateTime]::UtcNow.AddSeconds(15)
    do
    {
        Start-Sleep -Milliseconds 100
        $playing = Call 'play.state'
    } while (!$playing.committed -and [DateTime]::UtcNow -lt $deadline)
    if (!$playing.committed -or !$playing.gameStart)
    {
        throw "Actual Editor Play did not commit in cycle $cycle."
    }
    $audioFrame = AssertAudio 1 $audioFrame
    $null = Call 'stop'
    $deadline = [DateTime]::UtcNow.AddSeconds(15)
    do
    {
        Start-Sleep -Milliseconds 100
        $stopped = Call 'play.state'
    } while ($stopped.committed -and [DateTime]::UtcNow -lt $deadline)
    if ($stopped.committed -or $stopped.gameStart)
    {
        throw "Actual Editor Stop did not commit in cycle $cycle."
    }
    $audioFrame = AssertAudio 0 $audioFrame
    $process = Get-Process -Id $ownedPid
    [pscustomobject]@{
        cycle = $cycle + 1
        utc = [DateTime]::UtcNow.ToString('o')
        threads = $process.Threads.Count
        handles = $process.HandleCount
        privateBytes = $process.PrivateMemorySize64
    } | ConvertTo-Json -Compress | Add-Content (Join-Path $root 'process-samples.jsonl')
    if (($cycle + 1) % 10 -eq 0)
    {
        "EDITOR_PLAY_STOP cycles=$($cycle + 1) committed=true restored=true"
    }
}
"AUDIO_LIVE_EDITOR_PLAYBACK_OK cycles=$Cycles sceneScope=actual component=SoundComponent output=$root"
"EXCLUSIONS physical-loss/UI-preview; audible-output requires user observation"
