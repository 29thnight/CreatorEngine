param([ValidateRange(1,100)][int]$Cycles = 100)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$repo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$exe = Join-Path $repo 'Bin/x64-Release/Editor/CreatorEditor.exe'
$root = Join-Path $repo ('Build/Validation/Phase22Closure/editor-exits-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $root -Force | Out-Null
$scenario = Join-Path $root 'scenario.txt'
@'
wait 30
scene.new Phase22Exit
object.create Phase22ExitAudio
component.add Phase22ExitAudio SoundComponent
object.property Phase22ExitAudio SoundComponent clipKey 41ec131c-cf46-402f-b5f3-b0484468ca5d
object.property Phase22ExitAudio SoundComponent volume 0.001
object.property Phase22ExitAudio SoundComponent loop true
object.property Phase22ExitAudio SoundComponent playOnStart true
play
wait 60
audio.status Phase22ExitAudio
stop
wait 60
audio.status Phase22ExitAudio
quit
'@ | Set-Content -LiteralPath $scenario -Encoding utf8
for ($cycle = 1; $cycle -le $Cycles; ++$cycle)
{
    $stem = Join-Path $root ('{0:d3}' -f $cycle)
    $process = Start-Process -FilePath $exe -ArgumentList '--smoke-offscreen', '--script', $scenario,
        '--result-format', 'jsonl', '--result-file', ($stem + '.jsonl') -WindowStyle Hidden `
        -WorkingDirectory (Split-Path $exe) -RedirectStandardOutput ($stem + '.out') `
        -RedirectStandardError ($stem + '.err') -PassThru
    if (!$process.WaitForExit(180000))
    {
        $process.Kill()
        throw "Editor shutdown timeout cycle=$cycle"
    }
    if ($process.ExitCode -ne 0) { throw "Editor exit failed cycle=$cycle code=$($process.ExitCode)" }
    $rows = @(Get-Content ($stem + '.jsonl') | ForEach-Object { $_ | ConvertFrom-Json })
    if (@($rows | Where-Object status -ne 'succeeded').Count -ne 0) { throw "Editor command failure cycle=$cycle" }
    $audio = @($rows | Where-Object command -eq 'audio.status')
    if ($audio.Count -ne 2 -or $audio[0].data.physicalVoices -ne 1 -or
        $audio[1].data.activeVoices -ne 0 -or $audio[1].data.playbackInstances -ne 0 -or
        $audio[0].data.backendFailures -ne 0 -or $audio[1].data.backendFailures -ne 0)
    {
        throw "Editor audio teardown failure cycle=$cycle"
    }
    if (Get-Process -Id $process.Id -ErrorAction SilentlyContinue) { throw 'Exited Editor process still exists' }
    [pscustomobject]@{ cycle = $cycle; pid = $process.Id; exitCode = $process.ExitCode;
        playingVoices = 1; stoppedVoices = 0; processRetired = $true } | ConvertTo-Json -Compress |
        Add-Content (Join-Path $root 'summary.jsonl')
    if ($cycle % 10 -eq 0) { "EDITOR_EXIT_PASS cycles=$cycle output=$root" }
}
"EDITOR_EXIT_PASS cycles=$Cycles output=$root"
