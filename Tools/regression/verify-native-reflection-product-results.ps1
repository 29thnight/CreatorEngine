[CmdletBinding()]
param()
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'NativeReflectionProductResults.ps1')

# These are result-consumer rejection tests, never evidence of a product run.
$checks = @{}
foreach ($name in @('catalogUniqueIds', 'catalogScriptComponents', 'characterExists', 'characterDefinition',
    'lightScalarAndRecord', 'nestedProfileWrite', 'decalFields', 'unicodeField', 'borrowedNullResult',
    'soundValueResult', 'typedAudioPlayback', 'audioSurvivesGc', 'fixedStep', 'simulationOrder',
    'nativePhysicsRead', 'nativePhysicsWrite', 'typedAudioPause', 'typedAudioResume', 'typedAudioStop',
    'physicsMoved', 'audioCompletion')) { $checks[$name] = $true }
$baseline = @{
    schemaVersion = 1; processId = 123; protocol = 'creator-native-reflection-v1'; nativeApi = 48
    catalogComponents = @('Transform', 'ScriptComponent', 'InvalidScriptComponent')
    catalogTypes = 20; fixedTicks = 30; moved = 0.5; checks = $checks
}
function Read-Fixture($Value, [string]$Suffix = '') {
    $text = '[native.reflection.acceptance] ' + ($Value | ConvertTo-Json -Compress -Depth 6) + $Suffix
    Read-NativeReflectionAcceptance $text 123 $baseline.catalogComponents 48 | Out-Null
}
Read-Fixture $baseline
$count = 1
foreach ($case in @(
    @{ name = 'wrong process'; change = { param($x) $x.processId = 456 } },
    @{ name = 'old API'; change = { param($x) $x.nativeApi = 47 } },
    @{ name = 'wrong schema'; change = { param($x) $x.schemaVersion = 2 } },
    @{ name = 'wrong protocol'; change = { param($x) $x.protocol = 'fixture' } },
    @{ name = 'missing component'; change = { param($x) $x.catalogComponents = @('Transform', 'ScriptComponent') } },
    @{ name = 'duplicate component'; change = { param($x) $x.catalogComponents = @('Transform', 'ScriptComponent', 'ScriptComponent') } },
    @{ name = 'extra component'; change = { param($x) $x.catalogComponents += 'Unknown' } },
    @{ name = 'missing reachable types'; change = { param($x) $x.catalogTypes = 3 } },
    @{ name = 'no simulation ticks'; change = { param($x) $x.fixedTicks = 0 } },
    @{ name = 'timed out'; change = { param($x) $x.fixedTicks = 181 } },
    @{ name = 'no native motion'; change = { param($x) $x.moved = 0.0 } },
    @{ name = 'string motion'; change = { param($x) $x.moved = '0.5' } },
    @{ name = 'audio completion missing'; change = { param($x) $x.checks.PSObject.Properties.Remove('audioCompletion') } },
    @{ name = 'audio failed'; change = { param($x) $x.checks.typedAudioPlayback = $false } },
    @{ name = 'boolean string'; change = { param($x) $x.checks.physicsMoved = 'true' } }
)) {
    $value = $baseline | ConvertTo-Json -Depth 6 | ConvertFrom-Json
    & $case.change $value
    $rejected = $false
    try { Read-Fixture $value } catch { $rejected = $true }
    if (!$rejected) { throw "Accepted invalid product result: $($case.name)" }
    $count++
}
foreach ($suffix in @("`n[native.reflection.failure] injected failure",
    ("`n[native.reflection.acceptance] " + ($baseline | ConvertTo-Json -Compress -Depth 6)))) {
    $rejected = $false
    try { Read-Fixture $baseline $suffix } catch { $rejected = $true }
    if (!$rejected) { throw 'Accepted duplicate success or a later managed failure.' }
    $count++
}
$audio = @{ event = 'audio.host'; data = @{ outputMode = 1; backendCountersAvailable = $true; callbackCount = 12; backendFailures = 0 } }
$null = Assert-NativeReflectionAudioDevice @($audio)
$count++
foreach ($case in @(
    @{ name = 'null output'; change = { param($x) $x.data.outputMode = 2 } },
    @{ name = 'degraded output'; change = { param($x) $x.data.outputMode = 3 } },
    @{ name = 'offline output'; change = { param($x) $x.data.outputMode = 4 } },
    @{ name = 'no device callbacks'; change = { param($x) $x.data.callbackCount = 0 } },
    @{ name = 'backend failure'; change = { param($x) $x.data.backendFailures = 1 } },
    @{ name = 'no backend counters'; change = { param($x) $x.data.backendCountersAvailable = $false } },
    @{ name = 'string device mode'; change = { param($x) $x.data.outputMode = '1' } },
    @{ name = 'string callback count'; change = { param($x) $x.data.callbackCount = '12' } }
)) {
    $value = $audio | ConvertTo-Json -Depth 6 | ConvertFrom-Json
    & $case.change $value
    $rejected = $false
    try { $null = Assert-NativeReflectionAudioDevice @($value) } catch { $rejected = $true }
    if (!$rejected) { throw "Accepted invalid audio device result: $($case.name)" }
    $count++
}
foreach ($rows in @(@(), @($audio, $audio))) {
    $rejected = $false
    try { $null = Assert-NativeReflectionAudioDevice $rows } catch { $rejected = $true }
    if (!$rejected) { throw 'Accepted absent or duplicate audio device results.' }
    $count++
}

# Hash comparison only: these synthetic files are never executed as products.
$closureFixture = Join-Path ([IO.Path]::GetTempPath()) ('NativeReflectionClosure-' + [guid]::NewGuid().ToString('N'))
$published = Join-Path $closureFixture 'Published'
$repository = Join-Path $closureFixture 'Repository'
$paths = @('Editor/CreatorEditor.exe', 'Editor/CreatorEditor.runtime.dll', 'Runtime/Core/shared.dll', 'Runtime/layout.version')
try {
    foreach ($root in @($published, $repository)) {
        foreach ($path in $paths) {
            $full = Join-Path $root $path
            [IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($full)) | Out-Null
            [IO.File]::WriteAllText($full, 'current-' + $path)
        }
    }
    $entries = @(Get-EngineEntries $published $paths)
    $record = @{
        schemaVersion = 1; host = 'CreatorEditor'; configuration = 'Release'
        abi = @{ scriptApi = 49 }; entries = $entries; digest = Get-EngineDigest $entries
    }
    $recordPath = Join-Path $published 'Runtime/Manifests/CreatorEditor.json'
    Write-EngineJson $recordPath $record
    $receipt = Assert-NativeReflectionEditorClosure $repository $published 'Release' 49
    if ($receipt.files -ne $paths.Count -or $receipt.runtimeSha256 -cne (Get-EngineFileHash (Join-Path $repository 'Editor/CreatorEditor.runtime.dll'))) {
        throw 'Editor closure receipt does not identify the runtime DLL and full file set.'
    }
    $count++
    foreach ($path in @('Editor/CreatorEditor.runtime.dll', 'Runtime/Core/shared.dll')) {
        $file = Join-Path $repository $path
        # Preserve file length and the launcher hash: a size/exe-only check must fail this test.
        [IO.File]::WriteAllText($file, 'retired-' + $path)
        if ((Get-EngineFileHash (Join-Path $repository 'Editor/CreatorEditor.exe')) -cne
            (Get-EngineFileHash (Join-Path $published 'Editor/CreatorEditor.exe'))) { throw 'Fixture launcher unexpectedly changed.' }
        $rejected = $false
        try { $null = Assert-NativeReflectionEditorClosure $repository $published 'Release' 49 } catch { $rejected = $true }
        if (!$rejected) { throw "Accepted stale native closure member with an unchanged launcher: $path" }
        $count++
        Copy-Item -LiteralPath (Join-Path $published $path) -Destination $file -Force
    }
    Remove-Item -LiteralPath (Join-Path $repository 'Runtime/Core/shared.dll')
    $rejected = $false
    try { $null = Assert-NativeReflectionEditorClosure $repository $published 'Release' 49 } catch { $rejected = $true }
    if (!$rejected) { throw 'Accepted a missing shared native dependency.' }
    $count++
    Copy-Item -LiteralPath (Join-Path $published 'Runtime/Core/shared.dll') -Destination (Join-Path $repository 'Runtime/Core/shared.dll')
    $record.entries = @($entries | Where-Object path -CNE 'Editor/CreatorEditor.runtime.dll')
    $record.digest = Get-EngineDigest $record.entries
    Write-EngineJson $recordPath $record
    $rejected = $false
    try { $null = Assert-NativeReflectionEditorClosure $repository $published 'Release' 49 } catch { $rejected = $true }
    if (!$rejected) { throw 'Accepted a deployment record that omits the actual Editor runtime DLL.' }
    $count++
} finally {
    if (Test-Path -LiteralPath $closureFixture) { Remove-Item -LiteralPath $closureFixture -Recurse -Force }
}

# Parse the actual runner as part of the portable consumer gate. Do not execute
# Windows process orchestration or award product credit on this path.
$tokens = $null
$errors = $null
$null = [System.Management.Automation.Language.Parser]::ParseFile(
    (Join-Path $PSScriptRoot 'verify-fsharp-native-product.ps1'), [ref]$tokens, [ref]$errors)
if ($errors.Count) { throw ($errors | Out-String) }
$count++
"NATIVE_REFLECTION_PRODUCT_READERS_OK checks=$count scope=result-consumer-only"
