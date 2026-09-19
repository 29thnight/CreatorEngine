param(
    [string]$Editor = (Join-Path $PSScriptRoot '../../Bin/x64-Debug/Editor/CreatorEditor.exe'),
    [string]$Work = $env:TEMP,
    [int]$TimeoutSeconds = 180
)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'CommandResults.ps1')
$root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$project = Join-Path $root 'Dynamic_CPP'
$run = Join-Path ([IO.Path]::GetFullPath($Work)) ('creator-model-recovery-' + [guid]::NewGuid().ToString('N'))
$utf8 = [Text.UTF8Encoding]::new($false)
$probes = @{}
$process = $null
$assertions = 0
if (Get-Process CreatorEditor -ErrorAction SilentlyContinue) { throw 'Close the Editor before this isolated gate.' }
New-Item -ItemType Directory -Path $run -Force | Out-Null
function Assert([bool]$condition, [string]$message) {
    if (-not $condition) { throw $message }
    $script:assertions++
}
function Start-Editor([string[]]$arguments, [string]$label) {
    $script:process = Start-Process -FilePath $Editor -ArgumentList $arguments -WorkingDirectory $root -WindowStyle Hidden `
        -RedirectStandardOutput (Join-Path $run "$label.out") -RedirectStandardError (Join-Path $run "$label.err") -PassThru
}
function Stop-Editor {
    if ($process -and -not $process.HasExited) { $process.Kill(); $process.WaitForExit(15000) | Out-Null }
}
function Read-Probe($p) {
    $text = [IO.File]::ReadAllText($p.meta)
    $p.id = [regex]::Match($text, '(?m)^assetId: (.+)\r?$').Groups[1].Value.Trim()
    $p.generation = [long][regex]::Match($text, '(?m)^generation: (\d+)').Groups[1].Value
    Assert ($p.id -match '^[0-9a-f-]{36}$' -and $p.generation -gt 0) 'Invalid owned probe identity'
    $p.directory = Join-Path $project "Library/ModelAssetGenerations/$($p.id)/$($p.generation)"
}
function Invoke-Cmd([string]$name, [string[]]$arguments = @()) {
    $body = @{command=$name;args=$arguments;mode='sync'} | ConvertTo-Json -Compress
    $response = Invoke-WebRequest "$base/command" -Method Post -Headers $headers -ContentType 'application/json' `
        -Body $body -SkipHttpErrorCheck -TimeoutSec 60
    Add-Content -LiteralPath (Join-Path $run 'responses.jsonl') -Value $response.Content -Encoding utf8
    return $response.Content | ConvertFrom-Json
}
function Load-Probe([string]$name) { Invoke-Cmd 'model.loadcached' @($probes[$name].path) }
try {
    $prefix = 'AutoRecovery_' + [guid]::NewGuid().ToString('N').Substring(0,8)
    $sourceBytes = [IO.File]::ReadAllBytes((Join-Path $project 'Assets/Models/Prim_Cube.glb'))
    $commands = @()
    foreach ($name in @('missing','oldformat','record','sidecar','badsource','watch','identity','async')) {
        $path = Join-Path $project "Assets/Models/${prefix}_${name}.glb"
        $probes[$name] = @{path=$path;meta="$path.meta";id='';generation=0;directory=''}
        [IO.File]::WriteAllBytes($path, $sourceBytes)
        $commands += "model.load `"$path`""
    }
    $commands += 'quit'
    $scriptPath = Join-Path $run 'prepare.txt'
    $resultsPath = Join-Path $run 'prepare.jsonl'
    [IO.File]::WriteAllText($scriptPath, ($commands -join "`n"), $utf8)
    Start-Editor @('--commandlet-script', ('"'+$scriptPath+'"'), '--result-file', ('"'+$resultsPath+'"')) 'prepare'
    Assert ($process.WaitForExit($TimeoutSeconds*1000)) 'Preparation timed out'
    Assert ($process.ExitCode -eq 0) 'Preparation failed'
    $rows = @(Read-CommandResults $resultsPath)
    Assert (@($rows | Where-Object status -ne succeeded).Count -eq 0) 'Preparation command failed'
    foreach ($p in $probes.Values) { Read-Probe $p }

    # Only this run's generated artifacts are damaged. Canonical identity stays valid.
    foreach ($name in @('missing','badsource','async')) {
        Remove-Item -LiteralPath (Join-Path $probes[$name].directory 'model.cemc')
    }
    Remove-Item -LiteralPath (Join-Path $probes.sidecar.directory 'sidecar.meta')
    $recordPath = Join-Path $probes.record.directory 'generation.asset'
    $record = [IO.File]::ReadAllText($recordPath) -replace '(?m)^generation: \d+', 'generation: 999999'
    [IO.File]::WriteAllText($recordPath, $record, $utf8)
    $oldPath = Join-Path $probes.oldformat.directory 'model.cemc'
    $bytes = [IO.File]::ReadAllBytes($oldPath)
    [BitConverter]::GetBytes([uint32]0).CopyTo($bytes, 4)
    [IO.File]::WriteAllBytes($oldPath, $bytes)
    # Update the checksum too: this must reach the format reader, not merely SHA rejection.
    $digest = [Convert]::ToHexString([Security.Cryptography.SHA256]::HashData($bytes)).ToLowerInvariant()
    $recordPath = Join-Path $probes.oldformat.directory 'generation.asset'
    $record = [IO.File]::ReadAllText($recordPath) -replace '(?m)^  fingerprint: sha256:[0-9a-f]+', "  fingerprint: sha256:$digest"
    [IO.File]::WriteAllText($recordPath, $record, $utf8)
    [IO.File]::WriteAllText($probes.badsource.path, 'invalid model source', $utf8)

    Start-Editor @('--command-service') 'live'
    $endpointPath = Join-Path $project 'Library/CommandService/endpoint.json'
    $deadline = (Get-Date).AddSeconds($TimeoutSeconds)
    $info = $null
    while ((Get-Date) -lt $deadline -and -not $process.HasExited) {
        if (Test-Path -LiteralPath $endpointPath) {
            try {
                $candidate = Get-Content -LiteralPath $endpointPath -Raw | ConvertFrom-Json
                if ($candidate.pid -eq $process.Id) { $info = $candidate; break }
            } catch {}
        }
        Start-Sleep -Milliseconds 200
    }
    Assert ($null -ne $info) 'No owned HTTP endpoint'
    $base = "http://127.0.0.1:$($info.port)"
    $headers = @{Authorization="Bearer $($info.token)"}
    $first = Invoke-RestMethod "$base/health" -Headers $headers
    $ready = $false
    while ((Get-Date) -lt $deadline) {
        $health = Invoke-RestMethod "$base/health" -Headers $headers
        if ($health.state -eq 'idle' -and $health.frame -gt $first.frame -and $health.frame -ge 2) { $ready=$true; break }
        Start-Sleep -Milliseconds 200
    }
    Assert $ready 'Editor frames did not advance'

    $expectedAttempts = 0
    foreach ($name in @('missing','oldformat','record','sidecar')) {
        $row = Load-Probe $name
        $expectedAttempts++
        Assert ($row.status -eq 'succeeded') "$name did not automatically recover"
        Assert ($row.data.modelId -eq $probes[$name].id -and $row.data.generation -gt $probes[$name].generation) "$name changed identity or did not replace generation"
        Assert ($row.data.recoveryAttempts -eq $expectedAttempts -and $row.data.recoverySucceeded -eq $expectedAttempts) "$name did not perform exactly one repair"
        $again = Load-Probe $name
        Assert ($again.data.generation -eq $row.data.generation -and $again.data.recoveryAttempts -eq $expectedAttempts) "$name repeated a successful repair"
    }
    $failed = Load-Probe 'badsource'
    $again = Load-Probe 'badsource'
    Assert ($failed.status -eq 'failed' -and $again.status -eq 'failed') 'Invalid source was accepted'
    Assert ($failed.data.recoveryAttempts -eq 5 -and $again.data.recoveryAttempts -eq 5 -and $again.data.recoverySuppressed -ge 1) 'Unchanged failed input was reimported repeatedly'
    $originalMeta = [IO.File]::ReadAllText($probes.badsource.meta)
    Assert ([long][regex]::Match($originalMeta,'(?m)^generation: (\d+)').Groups[1].Value -eq $probes.badsource.generation) 'Failed recovery changed canonical generation'

    # Actual OS watcher -> authoring -> queued runtime replacement, no manual import.
    $watch = Load-Probe 'watch'
    Assert ($watch.status -eq 'succeeded') 'Watcher setup load failed'
    $watchBefore = $watch.data.generation
    [IO.File]::SetLastWriteTimeUtc($probes.watch.path, [DateTime]::UtcNow.AddSeconds(1))
    [IO.File]::WriteAllBytes($probes.badsource.path, $sourceBytes)
    $deadline = (Get-Date).AddSeconds(30)
    do {
        Start-Sleep -Milliseconds 250
        $watch = Load-Probe 'watch'
        $repaired = Load-Probe 'badsource'
    } while ((Get-Date) -lt $deadline -and ($watch.data.generation -le $watchBefore -or $repaired.status -ne 'succeeded'))
    Assert ($watch.data.generation -gt $watchBefore -and $watch.data.modelId -eq $probes.watch.id) 'Source watcher left the resident cache stale'
    Assert ($repaired.status -eq 'succeeded' -and $repaired.data.modelId -eq $probes.badsource.id) 'Changed failed source did not become loadable'
    Assert ($watch.data.sourceReloads -ge 1) 'Watcher replacement was not observed'

    $watchBefore = $watch.data.generation
    $replacement = $probes.watch.path + '.tmp'
    [IO.File]::WriteAllBytes($replacement, $sourceBytes)
    [IO.File]::Move($replacement, $probes.watch.path, $true)
    $deadline = (Get-Date).AddSeconds(30)
    do {
        Start-Sleep -Milliseconds 250
        $watch = Load-Probe 'watch'
    } while ((Get-Date) -lt $deadline -and $watch.data.generation -le $watchBefore)
    Assert ($watch.data.generation -gt $watchBefore -and $watch.data.modelId -eq $probes.watch.id) 'Atomic source replacement left the resident cache stale'

    # Restoring a deleted source may preserve its original size and mtime.
    $watchBefore = $watch.data.generation
    $savedMeta = [IO.File]::ReadAllBytes($probes.watch.meta)
    $savedTime = [IO.File]::GetLastWriteTimeUtc($probes.watch.path)
    [IO.File]::Delete($probes.watch.path)
    $deadline = (Get-Date).AddSeconds(15)
    do { Start-Sleep -Milliseconds 100 } while ((Get-Date) -lt $deadline -and (Test-Path -LiteralPath $probes.watch.meta))
    Assert (-not (Test-Path -LiteralPath $probes.watch.meta)) 'Watcher did not observe actual source deletion'
    $removed = Load-Probe 'watch'
    Assert ($removed.status -eq 'failed') 'Deleted model remained in current lookup'
    [IO.File]::WriteAllBytes($probes.watch.meta, $savedMeta)
    [IO.File]::WriteAllBytes($replacement, $sourceBytes)
    [IO.File]::SetLastWriteTimeUtc($replacement, $savedTime)
    [IO.File]::Move($replacement, $probes.watch.path)
    $deadline = (Get-Date).AddSeconds(30)
    do {
        Start-Sleep -Milliseconds 250
        $watch = Load-Probe 'watch'
    } while ((Get-Date) -lt $deadline -and ($watch.status -ne 'succeeded' -or $watch.data.generation -le $watchBefore))
    Assert ($watch.status -eq 'succeeded' -and $watch.data.generation -gt $watchBefore -and $watch.data.modelId -eq $probes.watch.id) 'Restored source was suppressed as a duplicate'

    # Canonical identity corruption must remain an error, even though source is valid.
    $metaPath = $probes.identity.meta
    $identityText = [IO.File]::ReadAllText($metaPath)
    try {
        $corrupt = $identityText -replace '(?m)^authoringKey: .+', 'authoringKey: authoring:0000000000000000000000000000000000000000000000000000000000000000'
        [IO.File]::WriteAllText($metaPath, $corrupt, $utf8)
        $identity = Load-Probe 'identity'
        Assert ($identity.status -eq 'failed' -and $identity.data.recoveryDeclined -ge 1) 'Corrupt identity was automatically replaced'
        Assert ([IO.File]::ReadAllText($metaPath) -eq $corrupt) 'Automatic recovery rewrote corrupt identity'
    } finally { [IO.File]::WriteAllText($metaPath, $identityText, $utf8) }

    # Worker preparation uses the same runtime loader; do not call model.load first.
    $script:asyncPath = Join-Path $run 'async.txt'
    $script:asyncResults = Join-Path $run 'async.jsonl'
    $null = Invoke-Cmd 'quit'
    Assert ($process.WaitForExit(30000)) 'Live Editor did not stop'
    $commands = @('scene.new AutoRecovery', "model.async `"$($probes.async.path)`"", 'model.async wait', "model.loadcached `"$($probes.async.path)`"", 'quit')
    [IO.File]::WriteAllText($asyncPath, ($commands -join "`n"), $utf8)
    Start-Editor @('--commandlet-script', ('"'+$asyncPath+'"'), '--result-file', ('"'+$asyncResults+'"')) 'async'
    Assert ($process.WaitForExit($TimeoutSeconds*1000)) 'Async repair timed out'
    Assert ($process.ExitCode -eq 0) 'Async repair failed'
    $rows = @(Read-CommandResults $asyncResults)
    $loaded = Get-CommandResult $rows 'model.loadcached'
    Assert ($loaded.status -eq 'succeeded' -and $loaded.data.recoverySucceeded -eq 1 -and $loaded.data.modelId -eq $probes.async.id) 'Worker load did not repair exactly once'
    Assert ([IO.File]::ReadAllText((Join-Path $run 'async.out')) -match '\[model\.async\] ready path=') 'Worker recovery never produced a prepared instance'
    @{passed=$true;assertions=$assertions;automaticCases=4;failedRetrySuppressed=$true;sourceWatcherReplaced=$true;identityPreserved=$true;asyncRecovery=$true} |
        ConvertTo-Json | Set-Content -LiteralPath (Join-Path $run 'summary.json') -Encoding utf8
    "Model automatic recovery PASS ($assertions assertions): $run"
} finally {
    Stop-Editor
    foreach ($p in $probes.Values) {
        if (-not $p.id -and (Test-Path -LiteralPath $p.meta)) { try { Read-Probe $p } catch {} }
        foreach ($path in @($p.path,$p.meta,($p.path+'.tmp'))) { if (Test-Path -LiteralPath $path) { Remove-Item -LiteralPath $path -Force } }
        if ($p.id -match '^[0-9a-f-]{36}$') {
            $generationRoot = [IO.Path]::GetFullPath((Join-Path $project 'Library/ModelAssetGenerations')) + [IO.Path]::DirectorySeparatorChar
            $target = [IO.Path]::GetFullPath((Join-Path $generationRoot $p.id))
            if (-not $target.StartsWith($generationRoot,[StringComparison]::OrdinalIgnoreCase)) { throw 'Unsafe probe cleanup path' }
            if (Test-Path -LiteralPath $target) { Remove-Item -LiteralPath $target -Recurse -Force }
        }
    }
}
