Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
Import-Module (Join-Path $PSScriptRoot '../runtime/RuntimeLayout.psm1')
Import-Module (Join-Path $PSScriptRoot 'EngineVersion.psm1')

function Get-EngineMetadataText($Manifest) {
    $lines = foreach ($key in @('productName','featureRelease','version','channel','localDevelopment','buildId','payloadDigest','configuration','shipping')) {
        $value = [string]$Manifest.$key
        if ($Manifest.$key -is [bool]) { $value = $value.ToLowerInvariant() }
        if ($value.Contains("`r") -or $value.Contains("`n")) { throw 'Invalid native metadata value.' }
        "$key=$value"
    }
    ($lines -join "`n") + "`n"
}

function Write-EngineMetadataFile([string]$Path, $Manifest) {
    [IO.File]::WriteAllText($Path, (Get-EngineMetadataText $Manifest), [Text.UTF8Encoding]::new($false))
}

function Read-EngineDistribution([string]$Root) {
    $Root = [IO.Path]::GetFullPath($Root)
    $manifest = Get-Content -LiteralPath (Join-Path $Root 'engine.manifest.json') -Raw | ConvertFrom-Json
    if ($manifest.schemaVersion -ne 1 -or $manifest.platform -ne 'win-x64') { throw 'Unsupported engine distribution.' }
    Assert-EngineVersionMetadata $manifest
    $id = [Guid]::Empty
    if (-not [Guid]::TryParseExact($manifest.buildId, 'D', [ref]$id) -or $id -eq [Guid]::Empty -or
        (Get-EngineDigest @($manifest.files)) -ne $manifest.payloadDigest) {
        throw 'Engine distribution identity is invalid.'
    }
    if ($manifest.configuration -notin @('Debug','Release') -or $manifest.binaryRoot -cne "Bin/x64-$($manifest.configuration)") { throw 'Invalid engine binary root.' }
    Test-EngineEntries $Root @($manifest.files)
    if ([IO.File]::ReadAllText((Join-Path $Root 'engine.info')) -cne (Get-EngineMetadataText $manifest)) { throw 'Native engine metadata differs from the manifest.' }
    $expected = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
    foreach ($entry in $manifest.files) { [void]$expected.Add($entry.path.Replace('\','/')) }
    foreach ($file in Get-ChildItem -LiteralPath $Root -Recurse -File -Force) {
        $relative = [IO.Path]::GetRelativePath($Root,$file.FullName).Replace('\','/')
        if ($relative -notin @('engine.manifest.json','engine.info') -and -not $expected.Contains($relative)) {
            throw "Unlisted file in immutable engine distribution: $relative"
        }
    }
    $manifest
}

function Resolve-EngineDistribution([string]$Repository, [string]$Configuration, [string]$ExplicitRoot) {
    if ($ExplicitRoot) { return [IO.Path]::GetFullPath($ExplicitRoot) }
    if (Test-Path -LiteralPath (Join-Path $Repository 'engine.manifest.json')) { return $Repository }
    $pointer = Join-Path $Repository "Bin\x64-$Configuration\engine.distribution.json"
    if (-not (Test-Path -LiteralPath $pointer)) {
        throw "No prebuilt $Configuration engine. Run Tools/distribution/publish-engine.ps1 after building the engine, or pass -EngineDistribution."
    }
    $record = Get-Content -LiteralPath $pointer -Raw | ConvertFrom-Json
    $record.path
}

function Assert-EngineProjectPin([string]$Project, $Manifest, [switch]$Require) {
    $pinPath = Join-Path $Project "ProjectSetting\Engine.$($Manifest.configuration).lock.json"
    if (-not (Test-Path -LiteralPath $pinPath)) {
        if ($Require) { throw "Project engine pin is missing: $pinPath (use select-engine.ps1)." }
        return
    }
    $pin = Get-Content -LiteralPath $pinPath -Raw | ConvertFrom-Json
    if ($pin.schemaVersion -ne 1 -or $pin.configuration -ne $Manifest.configuration) { throw 'Unsupported project engine pin.' }
    if ($pin.version -ne $Manifest.version -or $pin.buildId -ne $Manifest.buildId) {
        throw "Project requires engine $($pin.version) / $($pin.buildId). Selected build is $($Manifest.buildId)."
    }
}

Export-ModuleMember -Function Read-EngineDistribution,Resolve-EngineDistribution,Assert-EngineProjectPin,Get-EngineMetadataText,Write-EngineMetadataFile
