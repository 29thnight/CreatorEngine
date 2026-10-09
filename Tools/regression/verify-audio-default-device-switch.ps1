param(
    [Parameter(Mandatory = $true)][string]$AudioDeviceModule,
    [Parameter(Mandatory = $true)][string]$FirstEndpoint,
    [Parameter(Mandatory = $true)][string]$FinalEndpoint
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$repo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$root = Join-Path $repo 'Build/Validation/Phase22DeviceTransition'
$work = Join-Path $root ('switch-' + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $work -Force | Out-Null
Import-Module $AudioDeviceModule
$exe = Join-Path $root 'windows_device_transition_probe.exe'
$process = Start-Process -FilePath $exe -ArgumentList @(('"' + $work + '"'), 140) -WindowStyle Hidden -PassThru `
    -RedirectStandardOutput (Join-Path $work 'device.log') -RedirectStandardError (Join-Path $work 'device.stderr.log')
$changes = @()
try
{
    $deadline = [DateTime]::UtcNow.AddSeconds(20)
    while (!(Test-Path (Join-Path $work 'ready.txt')) -and !$process.HasExited -and [DateTime]::UtcNow -lt $deadline)
    {
        Start-Sleep -Milliseconds 100
    }
    if (!(Test-Path (Join-Path $work 'ready.txt')))
    {
        throw 'Real-device monitor did not become ready.'
    }
    for ($cycle = 0; $cycle -lt 20; ++$cycle)
    {
        $target = if ($cycle % 2 -eq 0) { $FirstEndpoint } else { $FinalEndpoint }
        $null = Set-AudioDevice -ID $target -DefaultOnly
        Start-Sleep -Seconds 4
        $actual = Get-AudioDevice -Playback
        if ($actual.ID -ne $target)
        {
            throw "Default playback endpoint did not match switch $cycle."
        }
        $changes += [pscustomobject]@{ sequence = $cycle + 1; utc = [DateTime]::UtcNow.ToString('o'); id = $actual.ID; name = $actual.Name }
        $changes | ConvertTo-Json -Depth 4 | Set-Content (Join-Path $work 'endpoint-changes.json') -Encoding utf8
    }
    $deadline = [DateTime]::UtcNow.AddSeconds(90)
    while (!$process.HasExited -and [DateTime]::UtcNow -lt $deadline)
    {
        Start-Sleep -Seconds 1
        $process.Refresh()
    }
    if (!$process.HasExited)
    {
        throw 'Real-device monitor timed out.'
    }
    $process.WaitForExit()
    $text = Get-Content (Join-Path $work 'device.log') -Raw
    $metrics = [regex]::Match($text, 'METRICS reroutes=(\d+) restarts=(\d+) unavailable_ticks=(\d+) callbacks=(\d+)')
    if ($process.ExitCode -ne 0 -or !$metrics.Success -or [int]$metrics.Groups[1].Value -lt 20 -or
        $text -match 'alive=0' -or $text -notmatch 'failed=0')
    {
        throw "Real endpoint transition acceptance failed. Inspect $work/device.log"
    }
    "AUDIO_DEFAULT_DEVICE_SWITCH_OK switches=20 reroutes=$($metrics.Groups[1].Value) voiceRetained=true output=$work"
}
finally
{
    $null = Set-AudioDevice -ID $FinalEndpoint -DefaultOnly
    if (!$process.HasExited)
    {
        $process.Kill()
        $process.WaitForExit()
    }
}
