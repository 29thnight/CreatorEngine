[CmdletBinding()]
param([string]$Work = '')

$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'RuntimeResults.ps1')
. (Join-Path $PSScriptRoot 'CommandResults.ps1')

if (!$Work) {
    $Work = Join-Path $PSScriptRoot ('../../Build/Verification/CliConsumers/Readers-' + [guid]::NewGuid().ToString('N'))
}
$Work = [IO.Path]::GetFullPath($Work)
New-Item -ItemType Directory -Path $Work -Force | Out-Null
$path = Join-Path $Work 'fixture.jsonl'
$checks = [Collections.Generic.List[object]]::new()

function Check([string]$Name, [scriptblock]$Action, [bool]$Reject = $false) {
    $rejected = $false
    try { & $Action | Out-Null } catch { $rejected = $true }
    if ($rejected -ne $Reject) { throw "Result-reader regression failed: $Name (rejected=$rejected)" }
    $checks.Add([pscustomobject]@{ name = $Name; rejected = $rejected })
}

function New-Events {
    $specs = @(
        @('session.started', @{}),
        @('editor.quit_requested', @{ source = 'native' }),
        @('autosave.flush', @{ path = 'C:\fixture\한글.png.meta'; ageMs = 20 }),
        @('autosave.completed', @{ path = 'C:\fixture\한글.png.meta'; succeeded = $true; conflict = $false }),
        @('rhi.backend_shutdown', @{ backend = 'DX12'; clean = $true; pendingTasks = 0; pendingBatches = 0; pendingRetirements = 0 }),
        @('editor.finalized', @{}),
        @('jobs.shutdown', @{}),
        @('runtime.finalized', @{})
    )
    $sequence = 0
    foreach ($spec in $specs) {
        $sequence++
        [pscustomobject]@{ schemaVersion = 1; processId = 123; sequence = $sequence; event = $spec[0]; data = [pscustomobject]$spec[1] }
    }
}

function Write-Events($Rows) {
    @($Rows | ForEach-Object { $_ | ConvertTo-Json -Depth 8 -Compress }) |
        Set-Content -LiteralPath $path -Encoding UTF8
}

function Read-Save {
    $events = Read-RuntimeResults $path 123
    Get-AutomaticSaveShutdown $events 'C:\fixture\한글.png.meta' $false 'native'
}

$commandScript = Join-Path $Work 'commands.txt'
$commandLines = @('wait 2', 'editor.browser', 'quit')
Write-CommandScript $commandScript $commandLines
Check 'command input has no UTF8 BOM in either shell' {
    $bytes = [IO.File]::ReadAllBytes($commandScript)
    if ($bytes[0] -ne [byte][char]'w') { throw 'Command input starts with a BOM' }
}
$commandRows = @($commandLines | ForEach-Object { [pscustomobject]@{command=$_.Split(' ')[0]} })
Check 'command result sequence' { Assert-CommandSequence $commandRows $commandLines }
$commandRows[0].command = 'wrong'
Check 'wrong command result rejected' { Assert-CommandSequence $commandRows $commandLines } $true

Write-Events (New-Events)
Check 'valid ordered native close / Unicode path' { Read-Save }
Check 'Windows path case and separator normalization' {
    $events = Read-RuntimeResults $path 123
    Get-AutomaticSaveShutdown $events 'c:/FIXTURE/한글.png.meta' $false 'native'
}
Check 'wrong process' { Read-RuntimeResults $path 999 } $true
Check 'missing file' { Read-RuntimeResults (Join-Path $Work 'absent.jsonl') 123 } $true

foreach ($event in @('session.started', 'editor.quit_requested', 'autosave.flush', 'autosave.completed',
    'rhi.backend_shutdown', 'editor.finalized', 'jobs.shutdown', 'runtime.finalized')) {
    $events = @(New-Events | Where-Object event -NE $event)
    $n = 0
    foreach ($row in $events) { $row.sequence = ++$n }
    Write-Events $events
    Check "missing $event" { Read-Save } $true
}

foreach ($mutate in @(
    @{ name = 'schema'; apply = { param($r) $r[0].schemaVersion = 2 } },
    @{ name = 'reused journal'; apply = { param($r) $r[1].processId = 456 } },
    @{ name = 'sequence gap'; apply = { param($r) $r[3].sequence = 99 } },
    @{ name = 'string sequence'; apply = { param($r) $r[3].sequence = '4' } },
    @{ name = 'wrong close source'; apply = { param($r) $r[1].data.source = 'cli' } },
    @{ name = 'save error'; apply = { param($r) $r[3].data.succeeded = $false } },
    @{ name = 'unexpected conflict'; apply = { param($r) $r[3].data.conflict = $true } },
    @{ name = 'string boolean'; apply = { param($r) $r[3].data.succeeded = 'true' } },
    @{ name = 'late flush'; apply = { param($r) $r[2].data.ageMs = 400 } },
    @{ name = 'negative age'; apply = { param($r) $r[2].data.ageMs = -1 } },
    @{ name = 'wrong sidecar'; apply = { param($r) $r[2].data.path = 'C:\other.meta' } },
    @{ name = 'dirty queue'; apply = { param($r) $r[4].data.pendingTasks = 1 } },
    @{ name = 'negative queue'; apply = { param($r) $r[4].data.pendingTasks = -1 } },
    @{ name = 'unclean backend'; apply = { param($r) $r[4].data.clean = $false } },
    @{ name = 'unknown backend'; apply = { param($r) $r[4].data.backend = 'Unknown' } },
    @{ name = 'premature editor finalization'; apply = { param($r) $r[5].event = 'jobs.shutdown'; $r[6].event = 'editor.finalized' } },
    @{ name = 'missing queue field'; apply = { param($r) $r[4].data.PSObject.Properties.Remove('pendingBatches') } },
    @{ name = 'flush before request'; apply = { param($r) $r[1].event = 'autosave.flush'; $r[1].data = $r[2].data; $r[2].event = 'editor.quit_requested'; $r[2].data = [pscustomobject]@{source='native'} } },
    @{ name = 'premature runtime finalization'; apply = { param($r) $r[6].event = 'runtime.finalized'; $r[7].event = 'jobs.shutdown' } }
)) {
    $events = @(New-Events)
    & $mutate.apply $events
    Write-Events $events
    Check $mutate.name { Read-Save } $true
}

$events = @(New-Events)
$events += $events[-1]
Write-Events $events
Check 'duplicate event' { Read-Save } $true
Set-Content -LiteralPath $path -Value '' -Encoding UTF8
Check 'empty journal' { Read-RuntimeResults $path 123 } $true
Set-Content -LiteralPath $path -Value '{bad json' -Encoding UTF8
Check 'malformed journal' { Read-RuntimeResults $path 123 } $true

$smoke = @{
    schemaVersion = 1; processId = 123; ready = $true; scriptsLoaded = $true
    frames = 600; displayPromotions = 120; submittedGameFrames = 20
    modelGenerationFailures = 0; meshResolveFailures = 0
}
function Smoke-Text($Value) { '[player.smoke] ' + ($Value | ConvertTo-Json -Compress) }
Check 'valid smoke' { Read-PlayerSmoke (Smoke-Text $smoke) 123 600 120 }
Check 'duplicate smoke' { Read-PlayerSmoke ((Smoke-Text $smoke) + "`n" + (Smoke-Text $smoke)) 123 600 120 } $true
Check 'human success cannot replace smoke' { Read-PlayerSmoke '[SMOKE] clean exit' 123 600 120 } $true
foreach ($field in @('schemaVersion', 'processId', 'ready', 'scriptsLoaded', 'frames', 'displayPromotions',
    'submittedGameFrames', 'modelGenerationFailures', 'meshResolveFailures')) {
    $changed = @{} + $smoke
    $changed.Remove($field)
    Check "missing smoke $field" { Read-PlayerSmoke (Smoke-Text $changed) 123 600 120 } $true
}
foreach ($field in @('modelGenerationFailures', 'meshResolveFailures')) {
    $changed = @{} + $smoke
    $changed[$field] = 1
    Check "smoke $field" { Read-PlayerSmoke (Smoke-Text $changed) 123 600 120 } $true
}
$changed = @{} + $smoke
$changed.scriptsLoaded = $false
Check 'managed load failure' { Read-PlayerSmoke (Smoke-Text $changed) 123 600 120 } $true

$audio = @{
    schemaVersion = 1; processId = 123; cycles = 100; completions = 100
    resident = 50; stream = 50; mp3 = 25; flac = 25; gcValueHandle = $true
    worldScope = $true; sessionScope = $true; attached = $true; positioned = $true
}
function Audio-Text($Value) { '[audio.acceptance] ' + ($Value | ConvertTo-Json -Compress) }
Check 'valid audio' { Read-AudioAcceptance (Audio-Text $audio) 123 }
foreach ($field in @($audio.Keys)) {
    $changed = @{} + $audio
    $changed.Remove($field)
    Check "missing audio $field" { Read-AudioAcceptance (Audio-Text $changed) 123 } $true
}
$changed = @{} + $audio
$changed.completions = 99
Check 'incomplete audio' { Read-AudioAcceptance (Audio-Text $changed) 123 } $true
Check 'human audio marker cannot replace result' { Read-AudioAcceptance '[AUDIO_CLR_PASS] cycles=100' 123 } $true

# Exercise the three receipt consumers themselves in an isolated, synthetic
# repository. This verifies their wiring, not Inspector/GPU product behavior.
# No editor.out file exists: a consumer cannot silently recover from human logs.
$fixtureRepo = Join-Path $Work 'ReceiptFixture'
$fixtureTools = Join-Path $fixtureRepo 'Tools/regression'
$fixtureBin = Join-Path $fixtureRepo 'Bin/x64-Release/Editor'
$fixtureImages = Join-Path $fixtureRepo 'Tools/regression/fixtures/browser-thumbnails'
New-Item -ItemType Directory -Force $fixtureTools, $fixtureBin, $fixtureImages | Out-Null
foreach ($name in @('RuntimeResults.ps1', 'CommandResults.ps1', 'verify-texture-auto-ui-shutdown.ps1',
    'verify-texture-pending-native-close.ps1', 'verify-texture-auto-ui-soak.ps1')) {
    Copy-Item -LiteralPath (Join-Path $PSScriptRoot $name) -Destination $fixtureTools
}
$fixtureDll = Join-Path $fixtureBin 'CreatorEditor.runtime.dll'
Set-Content -LiteralPath $fixtureDll -Value 'Synthetic identity input, not a native executable'
$fixtureHash = (Get-FileHash -LiteralPath $fixtureDll).Hash
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'fixtures/browser-thumbnails/Tiny4.png') -Destination $fixtureImages
$sceneFixture = Join-Path $fixtureRepo 'SceneFixture'
New-Item -ItemType Directory -Path "$sceneFixture/Assets/Scenes" -Force | Out-Null
[IO.File]::WriteAllText("$sceneFixture/Assets/Scenes/SavedFixture.creator", 'synthetic scene')

foreach ($mode in @('deleteshutdown', 'corruptshutdown', 'conflictshutdown', 'nativeclose', 'conflictnativeclose', 'soak')) {
    $root = Join-Path $fixtureRepo $mode
    $project = Join-Path $root 'Project'
    New-Item -ItemType Directory -Force "$project/Assets/Textures", "$project/Assets/Scenes" | Out-Null
    Set-Content -LiteralPath "$project/.texture-auto-ui-race" -Value 'synthetic receipt fixture'
    [IO.File]::WriteAllText("$project/Assets/Scenes/SavedFixture.creator", 'synthetic scene')
    $source = Join-Path $project 'Assets/Textures/fixture.png'
    Copy-Item -LiteralPath (Join-Path $fixtureImages 'Tiny4.png') -Destination $source
    $sidecar = $source + '.meta'
    $native = $mode -in @('nativeclose', 'conflictnativeclose')
    $conflict = $mode -notin @('nativeclose', 'soak')
    $dimension = if ($conflict) { 1 } else { 2 }
    $metadata = "guid: 33333333-3333-4333-8333-333333333333`nmaxDimension: $dimension`n"
    if ($mode -eq 'corruptshutdown') { $metadata = 'invalid metadata [' }
    if ($mode -ne 'deleteshutdown') { [IO.File]::WriteAllText($sidecar, $metadata) }

    @{ configuration = 'Release'; mode = $mode; processId = 123; runtimeSha256 = $fixtureHash } |
        ConvertTo-Json | Set-Content -LiteralPath (Join-Path $root 'launch.json') -Encoding UTF8
    @{ exited = $true; processId = 123; exitCode = 0 } |
        ConvertTo-Json | Set-Content -LiteralPath (Join-Path $root 'exit.json') -Encoding UTF8
    $data = @{
        diskUnchangedAtRequest = $true; requestAgeMs = 10; nativeMessage = 'WM_CLOSE'; externalBytes = $metadata
        elapsedMs = 300000; initialUiAgeMs = 10; finalMaximumDimension = 2; sourceAndSavedScenePreserved = $true
        normal = 20; conflicts = 20; deleted = 20; corrupt = 20; forcedFlushes = 40; cycles = 80
    }
    $commands = @(@{ schemaVersion = 1; command = 'assets.texture.pendingprobe'; status = 'succeeded'; data = $data;
        message = 'Localized diagnostic text may change' })
    if ($mode -eq 'soak') {
        $commands += @{ schemaVersion = 1; command = 'object.describe'; status = 'succeeded'; data = @{} }
        ConvertTo-Json -InputObject @(@{ path = $fixtureDll; sha256 = $fixtureHash }) |
            Set-Content -LiteralPath (Join-Path $root 'inputs.json') -Encoding UTF8
    }
    $commands | ForEach-Object { $_ | ConvertTo-Json -Depth 6 -Compress } |
        Set-Content -LiteralPath (Join-Path $root 'commands.jsonl') -Encoding UTF8
    $events = @(New-Events)
    $events[1].data.source = if ($native) { 'native' } else { 'cli' }
    $events[2].data.path = $sidecar
    $events[3].data.path = $sidecar
    $events[3].data.conflict = $conflict
    $events[3].data.succeeded = !$conflict
    $journal = Join-Path $root 'runtime-results.jsonl'
    $events | ForEach-Object { $_ | ConvertTo-Json -Depth 6 -Compress } | Set-Content -LiteralPath $journal -Encoding UTF8
    $consumer = if ($mode -eq 'soak') { 'verify-texture-auto-ui-soak.ps1' }
        elseif ($native) { 'verify-texture-pending-native-close.ps1' } else { 'verify-texture-auto-ui-shutdown.ps1' }
    $arguments = @{ FixtureProject = $sceneFixture; ResultFile = (Join-Path $root 'verification.json') }
    if ($mode -eq 'soak') { $arguments.Root = $root } else { $arguments.Roots = @($root) }
    Check "receipt consumer $mode" { & (Join-Path $fixtureTools $consumer) @arguments }
    Remove-Item -LiteralPath $journal
    Check "receipt consumer rejects missing journal $mode" { & (Join-Path $fixtureTools $consumer) @arguments } $true
}

# Validate process result behavior on both supported shells, including nonzero exit.
$shell = (Get-Process -Id $PID).Path
foreach ($exitCode in @(0, 23)) {
    $stem = Join-Path $Work "exit-$exitCode"
    $process = Start-ResultProcess -Exe $shell -WorkingDirectory $Work `
        -Arguments @('-NoProfile', '-Command', "exit $exitCode") `
        -Stdout ($stem + '.out') -Stderr ($stem + '.err') -Results ($stem + '.jsonl')
    if (!$process.WaitForExit(10000)) { $process.Kill(); throw 'Exit probe timed out' }
    Check "cached process exit $exitCode" {
        if ($null -eq $process.ExitCode -or $process.ExitCode -ne $exitCode) { throw 'Exit code mismatch' }
    }
}

[ordered]@{ schemaVersion = 1; checks = $checks.Count; cases = @($checks.ToArray()); shell = $PSVersionTable.PSVersion.ToString() } |
    ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $Work 'result.json') -Encoding UTF8
"RUNTIME_RESULT_CONSUMERS_OK checks=$($checks.Count) evidence=$Work"
