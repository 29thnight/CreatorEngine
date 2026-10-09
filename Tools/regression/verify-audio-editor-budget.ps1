param([ValidateRange(30,3600)][int]$Seconds = 1800, [switch]$RequireStreamObservation,
    [string]$ProbeType = 'Phase22ProductAudioProbe')

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$repo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$output = Join-Path $repo ('Build/Validation/Phase22Closure/budget-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $output -Force | Out-Null
$endpoint = Get-Content (Join-Path $repo 'Dynamic_CPP/Library/CommandService/endpoint.json') -Raw | ConvertFrom-Json
$ownedPid = [int](Get-Content (Join-Path $repo 'Build/Validation/Phase22Editor/pid.txt'))
if ($endpoint.pid -ne $ownedPid) { throw 'Editor endpoint ownership mismatch.' }

function Call([string]$command, [string[]]$arguments = @())
{
    $watch = [Diagnostics.Stopwatch]::StartNew()
    $response = Invoke-RestMethod -Uri ('http://127.0.0.1:' + $endpoint.port + '/command') -Method Post `
        -Headers @{Authorization = 'Bearer ' + $endpoint.token} -ContentType 'application/json' `
        -Body (@{command = $command; args = $arguments} | ConvertTo-Json -Compress) -TimeoutSec 30
    if ($response.status -eq 'accepted')
    {
        $poll = $response.poll
        $deadline = [DateTime]::UtcNow.AddSeconds(60)
        do
        {
            Start-Sleep -Milliseconds 100
            $response = Invoke-RestMethod -Uri ('http://127.0.0.1:' + $endpoint.port + $poll) `
                -Headers @{Authorization = 'Bearer ' + $endpoint.token} -TimeoutSec 10
        } while ($response.state -ne 'completed' -and [DateTime]::UtcNow -lt $deadline)
    }
    $watch.Stop()
    $response | Add-Member -NotePropertyName clientElapsedMs -NotePropertyValue $watch.Elapsed.TotalMilliseconds
    $response | ConvertTo-Json -Depth 15 -Compress | Add-Content (Join-Path $output 'commands.jsonl')
    if ($response.status -ne 'succeeded') { throw "$command failed: $($response.code)" }
    return $response
}

$playState = (Call 'play.state').data
if (!$playState.committed)
{
    $null = Call 'play'
    $playDeadline = [DateTime]::UtcNow.AddSeconds(30)
    do
    {
        Start-Sleep -Milliseconds 100
        $playState = (Call 'play.state').data
    } while (!$playState.committed -and [DateTime]::UtcNow -lt $playDeadline)
}
if (!$playState.committed -or !$playState.gameStart)
{
    throw 'Budget workload requires a committed Editor Play session.'
}
$started = [DateTime]::UtcNow
$baseline = (Get-Process -Id $ownedPid).PrivateMemorySize64
$peak = $baseline
$latency = 0.0
$samples = 0
$lastLoad = -1
$completed = $false
try
{
    while (([DateTime]::UtcNow - $started).TotalSeconds -lt $Seconds)
    {
        $elapsed = ([DateTime]::UtcNow - $started).TotalSeconds
        $fraction = $elapsed / $Seconds
        $voices = if ($fraction -lt 0.1) { 0 } elseif ($fraction -lt 0.25) { 1 } elseif ($fraction -lt 0.55) { 32 } else { 128 }
        if ($voices -ne $lastLoad)
        {
            $reverb = if ($voices -eq 0 -or $voices -eq 32) { 'false' } else { 'true' }
            $result = Call 'script.invoke' @($ProbeType, 'SetLoad', [string]$voices, $reverb)
            if ($voices -eq 1) { $latency = $result.clientElapsedMs }
            $lastLoad = $voices
            Start-Sleep -Milliseconds 500
            "WORKLOAD voices=$voices elapsed=$([int]$elapsed) output=$output"
        }
        $snapshot = (Call 'audio.status').data
        if ($RequireStreamObservation -and
            ($snapshot.host.PSObject.Properties.Name -notcontains 'streamStarvationReads' -or
             $snapshot.host.streamStarvationReads -ne 0))
        {
            throw 'Stream observation unavailable or decoded stream starvation detected.'
        }
        if ($snapshot.activeVoices -ne $voices -or $snapshot.physicalVoices -ne $voices -or
            $snapshot.playbackInstances -ne $voices -or $snapshot.backendFailures -ne 0 -or
            !$snapshot.host.available -or $snapshot.host.outputMode -ne 1 -or $snapshot.host.streamReadFailures -ne 0)
        {
            throw 'Actual Editor workload or hardware output disagrees with the expected load.'
        }
        $process = Get-Process -Id $ownedPid
        if ($voices -eq 0)
        {
            # Startup/GC can lower private bytes during the zero-voice segment.
            # Use its minimum so those releases cannot conceal the audio increase.
            $baseline = [Math]::Min($baseline, $process.PrivateMemorySize64)
        }
        $peak = [Math]::Max($peak, $process.PrivateMemorySize64)
        [pscustomobject]@{ utc = [DateTime]::UtcNow.ToString('o'); elapsedSeconds = $elapsed; voices = $voices; privateBytes = $process.PrivateMemorySize64;
            handles = $process.HandleCount; threads = $process.Threads.Count; audio = $snapshot } |
            ConvertTo-Json -Depth 12 -Compress | Add-Content (Join-Path $output 'samples.jsonl')
        ++$samples
        Start-Sleep -Seconds 1
    }
    $last = (Call 'audio.status').data
    $summary = [ordered]@{
        startedUtc = $started.ToString('o')
        endedUtc = [DateTime]::UtcNow.ToString('o')
        seconds = ([DateTime]::UtcNow - $started).TotalSeconds
        sampleCount = $samples
        runtimeUpdateSamples = $last.host.runtimeUpdateSamples
        runtimeUpdateP99UpperNs = $last.host.runtimeUpdateP99UpperNs
        runtime128UpdateSamples = $last.host.runtime128UpdateSamples
        runtime128UpdatesOverOneMillisecond = $last.host.runtime128UpdatesOverOneMillisecond
        runtime128P99WithinOneMillisecond = ($last.host.runtime128UpdateSamples -ge 10000 -and
            $last.host.runtime128UpdatesOverOneMillisecond -le [Math]::Floor($last.host.runtime128UpdateSamples / 100.0))
        callbackCount = $last.host.callbackCount
        callbackP99Ns = $last.host.callbackP99Ns
        callbackMaxNs = $last.host.callbackMaxNs
        callbackOverHalfPeriod = $last.host.callbackOverHalfPeriod
        callbackP99UnderHalfPeriod = ($last.host.callbackCount -ge 10000 -and
            $last.host.callbackOverHalfPeriod -le [Math]::Floor($last.host.callbackCount / 100.0))
        streamBytesRead = $last.host.streamBytesRead
        streamReadFailures = $last.host.streamReadFailures
        streamPcmReads = $(if ($RequireStreamObservation) { $last.host.streamPcmReads } else { $null })
        streamStarvationReads = $(if ($RequireStreamObservation) { $last.host.streamStarvationReads } else { $null })
        privateBytesBaseline = $baseline
        privateBytesPeak = $peak
        privateBytesIncrease = $peak - $baseline
        firstPlaybackApiResponseMs = $latency
        underrun = 'UNMEASURED; requires an independently validated OS trace'
        budgetPass = ($last.host.runtime128UpdateSamples -ge 10000 -and
            $last.host.runtime128UpdatesOverOneMillisecond -le [Math]::Floor($last.host.runtime128UpdateSamples / 100.0) -and
            ($peak - $baseline) -le 268435456 -and $latency -gt 0 -and $latency -le 500)
    }
    $summary | ConvertTo-Json -Depth 8 | Set-Content (Join-Path $output 'summary.json')
    if (!$summary.budgetPass) { throw 'One or more user-approved product budgets failed.' }
    if ($RequireStreamObservation -and $summary.streamPcmReads -le 0)
    {
        throw 'Stream observer did not see actual mixer PCM reads.'
    }
    if ($RequireStreamObservation -and !$summary.callbackP99UnderHalfPeriod)
    {
        throw 'Callback p99 exceeded half of its buffer period.'
    }
    $completed = $true
}
finally
{
    $null = Call 'script.invoke' @($ProbeType, 'SetLoad', '0', 'false')
    $null = Call 'stop'
    $final = (Call 'audio.status').data
    $final | ConvertTo-Json -Depth 10 | Set-Content (Join-Path $output 'stopped.json')
    if ($final.activeVoices -ne 0 -or $final.playbackInstances -ne 0) { throw 'Budget workload did not clean up.' }
}
if ($completed) { "EDITOR_AUDIO_BUDGET_PASS output=$output" }
