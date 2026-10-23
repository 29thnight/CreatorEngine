param(
    [Parameter(Mandatory)][string]$PackageDirectory,
    [ValidateRange(1,100)][int]$Cycles = 100
)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'RuntimeResults.ps1')
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
    $resultsPath = $stem + '.runtime.jsonl'
    $process = Start-ResultProcess -Exe $exe -WorkingDirectory $package `
        -Arguments @('--smoke', '600', '--smoke-promotions', '120', '--smoke-offscreen') `
        -Environment $environment -Stdout ($stem + '.out') -Stderr ($stem + '.err') -Results $resultsPath
    try {
        if (!$process.WaitForExit(600000)) { throw "Player shutdown timeout cycle=$cycle" }
        $process.Refresh()
        if ($null -eq $process.ExitCode -or $process.ExitCode -ne 0 -or (Get-Item ($stem + '.err')).Length -ne 0) {
            throw "Player exit failed cycle=$cycle exit=$($process.ExitCode)"
        }
        $stdout = Get-Content -LiteralPath ($stem + '.out') -Raw -Encoding UTF8
        $audio = Read-AudioAcceptance $stdout $process.Id
        $ready = Read-PlayerSmoke $stdout $process.Id 600 120
        Assert-PlayerRuntime (Read-RuntimeResults $resultsPath $process.Id)
    }
    finally {
        if (!$process.HasExited) { $process.Kill(); $process.WaitForExit() }
    }
    if (Get-Process -Id $process.Id -ErrorAction SilentlyContinue)
    {
        throw 'Exited Player process still exists.'
    }
    [pscustomobject]@{ cycle = $cycle; pid = $process.Id; exitCode = $process.ExitCode;
        managedAudioCycles = $audio.cycles; completions = $audio.completions;
        residentCycles = $audio.resident; streamCycles = $audio.stream; mp3Cycles = $audio.mp3; flacCycles = $audio.flac;
        processRetired = $true; runtimeResults = $resultsPath;
        engineBuildId = $manifest.engineBuildId } | ConvertTo-Json -Compress |
        Add-Content (Join-Path $root 'summary.jsonl')
    if ($cycle % 10 -eq 0)
    {
        "PLAYER_EXIT_PASS cycles=$cycle output=$root"
    }
}
"PLAYER_EXIT_PASS cycles=$Cycles output=$root"
