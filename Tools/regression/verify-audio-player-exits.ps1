param(
    [Parameter(Mandatory)][string]$PackageDirectory,
    [ValidateRange(1,100)][int]$Cycles = 100
)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$repo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$package = [IO.Path]::GetFullPath($PackageDirectory)
$manifest = Get-Content -LiteralPath (Join-Path $package 'package-manifest.json') -Raw | ConvertFrom-Json
if ($manifest.verification -ne 'passed' -or $manifest.config -ne 'Release' -or
    $manifest.startupScene -ne 'Phase22ProductAcceptance.creator')
{
    throw 'Requires the verified Release package of the current project audio acceptance scene.'
}
if ((Get-FileHash -LiteralPath (Join-Path $package 'GameAssets.pak')).Hash -ne $manifest.pakFileSha256)
{
    throw 'Package payload changed after verification.'
}
$exe = Join-Path $package 'Player.exe'
$root = Join-Path $repo ('Build/Validation/Phase22Closure/player-exits-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $root -Force | Out-Null
Copy-Item -LiteralPath (Join-Path $package 'package-manifest.json') -Destination $root
for ($cycle = 1; $cycle -le $Cycles; ++$cycle)
{
    $stem = Join-Path $root ('{0:d3}' -f $cycle)
    $temp = $stem + '.runtime'
    New-Item -ItemType Directory -Path $temp | Out-Null
    $environment = @{ TEMP = $temp; TMP = $temp;
        PATH = "$package;$env:SystemRoot\System32;$env:SystemRoot" }
    $process = Start-Process -FilePath $exe -ArgumentList '--smoke', '600',
        '--smoke-promotions', '120', '--smoke-offscreen' -WorkingDirectory $package `
        -WindowStyle Hidden -Environment $environment -RedirectStandardOutput ($stem + '.out') `
        -RedirectStandardError ($stem + '.err') -PassThru
    if (!$process.WaitForExit(600000))
    {
        $process.Kill()
        throw "Player shutdown timeout cycle=$cycle"
    }
    $stdout = Get-Content -LiteralPath ($stem + '.out') -Raw
    $combined = $stdout + (Get-Content -LiteralPath ($stem + '.err') -Raw)
    $logRoot = Join-Path $temp ("CreatorEngine/Player/$($process.Id)/RuntimeData/Log")
    if (Test-Path -LiteralPath $logRoot)
    {
        foreach ($logFile in Get-ChildItem -LiteralPath $logRoot -Recurse -File |
            Where-Object { $_.Extension -in '.html', '.log', '.txt' })
        {
            $combined += "`n" + (Get-Content -LiteralPath $logFile.FullName -Raw)
        }
    }
    if ($process.ExitCode -ne 0 -or $stdout -notmatch
        '\[AUDIO_CLR_PASS\] cycles=100 completions=100 GC=valueHandle scopes=World,Session attached=pass positioned=pass resident=50 stream=50 mp3=25 flac=25')
    {
        throw "Player exit or managed audio failed cycle=$cycle exit=$($process.ExitCode)"
    }
    $end = [regex]::Match($combined,
        '\[SMOKE\]\s*frame limit reached[^\r\n]*\((\d+)\s+GT frames,\s*display frame\s+(\d+),\s*promotions\s+(\d+)\)')
    $readyRows = @($stdout -split "`n" | Where-Object { $_.StartsWith('[player.smoke] ') })
    if ($readyRows.Count -ne 1)
    {
        throw "Player readiness marker count failed cycle=$cycle"
    }
    $ready = $readyRows[0].Substring(15) | ConvertFrom-Json
    if (!$end.Success -or [int]$end.Groups[1].Value -lt 600 -or [int]$end.Groups[3].Value -lt 120 -or
        !$ready.ready -or $ready.frames -lt 600 -or $ready.displayPromotions -lt 120 -or
        $ready.submittedGameFrames -le 0 -or
        $combined -match '\[CRASH\]|\[SMOKE\].*FAILED|\[CLR\].*실패' -or
        $stdout -notmatch '\[runtime\.text-parser\]\s*calls=0')
    {
        throw "Player runtime validation failed cycle=$cycle"
    }
    if (Get-Process -Id $process.Id -ErrorAction SilentlyContinue)
    {
        throw 'Exited Player process still exists.'
    }
    [pscustomobject]@{ cycle = $cycle; pid = $process.Id; exitCode = $process.ExitCode;
        managedAudioCycles = 100; completions = 100; residentCycles = 50; streamCycles = 50; mp3Cycles = 25; flacCycles = 25; processRetired = $true;
        engineBuildId = $manifest.engineBuildId } | ConvertTo-Json -Compress |
        Add-Content (Join-Path $root 'summary.jsonl')
    if ($cycle % 10 -eq 0)
    {
        "PLAYER_EXIT_PASS cycles=$cycle output=$root"
    }
}
"PLAYER_EXIT_PASS cycles=$Cycles output=$root"
