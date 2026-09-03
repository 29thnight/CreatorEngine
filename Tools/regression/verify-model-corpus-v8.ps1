param(
    [string]$Root = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path,
    [string]$Baseline = (Join-Path $PSScriptRoot 'mbc0_corpus_baseline.json'),
    [string]$GenerationRoot,
    [int]$ExpectedSubAssets = 0,
    [switch]$RequireFirstGeneration
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$resolvedRoot = (Resolve-Path -LiteralPath $Root).Path
$assets = Join-Path $resolvedRoot 'Dynamic_CPP\Assets'
$headerPath = Join-Path $resolvedRoot 'Dynamic_CPP\ProjectSetting\AssetIdentity.asset'
if ([string]::IsNullOrWhiteSpace($GenerationRoot)) {
    $GenerationRoot = Join-Path $resolvedRoot 'Dynamic_CPP\Library\ModelAssetGenerations'
}
$baselineDoc = Get-Content -LiteralPath $Baseline -Raw | ConvertFrom-Json
$uuidV8 = '^[0-9a-f]{8}-[0-9a-f]{4}-8[0-9a-f]{3}-[89ab][0-9a-f]{3}-[0-9a-f]{12}$'
$failures = [Collections.Generic.List[string]]::new()
$allIds = [Collections.Generic.HashSet[string]]::new([StringComparer]::Ordinal)
$modelIds = [Collections.Generic.HashSet[string]]::new([StringComparer]::Ordinal)
$oldIds = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
$modelCount = 0
$subassetCount = 0
$generationCount = 0
$headerEpoch = $null

function Add-Failure([string]$Message) { $failures.Add($Message) }

function Get-TopScalar([string]$Text, [string]$Key) {
    $match = [regex]::Match($Text, '(?m)^' + [regex]::Escape($Key) + ':\s*(\S.*?)\s*$')
    if ($match.Success) { return $match.Groups[1].Value }
    return $null
}

function Get-SubAssetRecords([string]$Text) {
    $records = [Collections.Generic.List[object]]::new()
    $current = $null
    foreach ($line in ($Text -split "`r?`n")) {
        if ($line -match '^  - kind:\s*(\S+)\s*$') {
            if ($null -ne $current) { $records.Add([pscustomobject]$current) }
            $current = [ordered]@{ Kind = $Matches[1]; StableKey = ''; AssetId = ''; Binding = '' }
            continue
        }
        if ($null -eq $current) { continue }
        if ($line -match '^    stableKey:\s*(.*?)\s*$') { $current.StableKey = $Matches[1]; continue }
        if ($line -match '^    assetId:\s*(\S+)\s*$') { $current.AssetId = $Matches[1]; continue }
        if ($line -match '^    binding:\s*(.*?)\s*$') { $current.Binding = $Matches[1]; continue }
    }
    if ($null -ne $current) { $records.Add([pscustomobject]$current) }
    return @($records)
}

if (-not (Test-Path -LiteralPath $assets -PathType Container)) {
    throw "Assets root가 없다: $assets"
}
if (-not (Test-Path -LiteralPath $headerPath -PathType Leaf)) {
    Add-Failure "identity epoch header가 없다: $headerPath"
} else {
    $headerText = Get-Content -LiteralPath $headerPath -Raw
    $headerSchema = Get-TopScalar $headerText 'schemaVersion'
    $headerProfile = Get-TopScalar $headerText 'identityProfile'
    $headerEpoch = Get-TopScalar $headerText 'identityEpoch'
    $headerSeed = Get-TopScalar $headerText 'identityEpochSeed'
    if ($headerSchema -ne '1' -or $headerProfile -ne 'ce.uuidv8.sha256.v1' -or
        [string]::IsNullOrWhiteSpace($headerEpoch) -or
        $headerSeed -cnotmatch '^[0-9a-f]{64}$' -or $headerSeed -match '^0{64}$') {
        Add-Failure 'identity epoch header 계약이 맞지 않는다.'
    }
}

foreach ($model in @($baselineDoc.models)) {
    ++$modelCount
    $source = Join-Path $resolvedRoot ([string]$model.source)
    $meta = $source + '.meta'
    if ($model.guid) { [void]$oldIds.Add(([string]$model.guid).ToLowerInvariant()) }
    foreach ($item in @($model.materials) + @($model.embeddedTextures)) {
        if ($item.guid) { [void]$oldIds.Add(([string]$item.guid).ToLowerInvariant()) }
    }
    if (-not (Test-Path -LiteralPath $source -PathType Leaf) -or
        -not (Test-Path -LiteralPath $meta -PathType Leaf)) {
        Add-Failure "model/source sidecar가 없다: $($model.source)"
        continue
    }
    $sourceHash = (Get-FileHash -LiteralPath $source -Algorithm SHA256).Hash.ToLowerInvariant()
    if ($sourceHash -ne ([string]$model.sha256).ToLowerInvariant()) {
        Add-Failure "MBC0 이후 model source가 변했다: $($model.source)"
    }

    $text = Get-Content -LiteralPath $meta -Raw
    $schema = Get-TopScalar $text 'schemaVersion'
    $profile = Get-TopScalar $text 'identityProfile'
    $epoch = Get-TopScalar $text 'identityEpoch'
    $modelId = Get-TopScalar $text 'assetId'
    $generationText = Get-TopScalar $text 'generation'
    $sourceFingerprint = Get-TopScalar $text 'sourceFingerprint'
    [uint64]$generation = 0
    if ($schema -ne '2' -or $profile -ne 'ce.uuidv8.sha256.v1' -or
        $epoch -ne $headerEpoch -or $modelId -cnotmatch $uuidV8 -or
        -not [uint64]::TryParse($generationText, [ref]$generation) -or $generation -lt 1 -or
        $sourceFingerprint -ne ('sha256:' + $sourceHash) -or
        $text -match '(?m)^\s*guid:' -or
        $text -match '(?m)^\s*schemaVersion:\s*1\s*$') {
        Add-Failure "schema v2 model sidecar 계약이 맞지 않는다: $($model.source)"
        continue
    }
    if ($RequireFirstGeneration -and $generation -ne 1) {
        Add-Failure "MBC4 최초 generation이 1이 아니다: $($model.source)=$generation"
    }
    if (-not $allIds.Add($modelId) -or -not $modelIds.Add($modelId)) {
        Add-Failure "model UUIDv8이 corpus에서 중복됐다: $modelId"
    }

    $records = @(Get-SubAssetRecords $text)
    foreach ($record in $records) {
        ++$subassetCount
        if ($record.AssetId -cnotmatch $uuidV8 -or -not $allIds.Add($record.AssetId)) {
            Add-Failure "subasset UUIDv8이 유효하지 않거나 중복됐다: $($model.source)::$($record.AssetId)"
        }
        if ([string]::IsNullOrWhiteSpace($record.StableKey) -or
            $record.StableKey -match '^(gltf|fbx)/(material|image|mesh|skin|animation)/\d+$') {
            Add-Failure "ordinal/empty stable key가 남았다: $($model.source)::$($record.StableKey)"
        }
    }
    foreach ($legacy in @($model.materials)) {
        if (-not ($records | Where-Object {
            $_.Kind -eq 'material' -and $_.Binding -eq [string]$legacy.key })) {
            Add-Failure "legacy material binding을 새 closure에서 찾지 못했다: $($model.source)::$($legacy.key)"
        }
    }
    foreach ($legacy in @($model.embeddedTextures)) {
        if (-not ($records | Where-Object {
            $_.Kind -eq 'texture' -and $_.Binding -eq [string]$legacy.key })) {
            Add-Failure "legacy embedded texture binding을 새 closure에서 찾지 못했다: $($model.source)::$($legacy.key)"
        }
    }

    $generationPath = Join-Path $GenerationRoot (Join-Path $modelId ([string]$generation))
    foreach ($name in @('sidecar.meta', 'model.cemc', 'generation.asset')) {
        if (-not (Test-Path -LiteralPath (Join-Path $generationPath $name) -PathType Leaf)) {
            Add-Failure "generation 산출물이 없다: $($model.source)::$name"
        }
    }
    $generationSidecar = Join-Path $generationPath 'sidecar.meta'
    if ((Test-Path -LiteralPath $generationSidecar -PathType Leaf) -and
        (Get-FileHash -LiteralPath $generationSidecar -Algorithm SHA256).Hash -ne
        (Get-FileHash -LiteralPath $meta -Algorithm SHA256).Hash) {
        Add-Failure "generation/canonical sidecar가 다르다: $($model.source)"
    }
    $generationRecord = Join-Path $generationPath 'generation.asset'
    if (Test-Path -LiteralPath $generationRecord -PathType Leaf) {
        $recordText = Get-Content -LiteralPath $generationRecord -Raw
        if ((Get-TopScalar $recordText 'assetId') -ne $modelId -or
            (Get-TopScalar $recordText 'generation') -ne ([string]$generation)) {
            Add-Failure "generation record identity가 sidecar와 다르다: $($model.source)"
        }
    }
    ++$generationCount
}

if ($ExpectedSubAssets -gt 0 -and $subassetCount -ne $ExpectedSubAssets) {
    Add-Failure "subasset closure 수가 기준과 다르다: $subassetCount != $ExpectedSubAssets"
}

$persisted = @(Get-ChildItem -LiteralPath $assets -Recurse -File | Where-Object {
    $_.Extension.ToLowerInvariant() -in @('.creator', '.prefab', '.asset')
})
$oldReferenceCount = 0
$newReferenceCount = 0
foreach ($file in $persisted) {
    $stream = [IO.File]::OpenRead($file.FullName)
    try {
        $magic = [byte[]]::new(4)
        [void]$stream.Read($magic, 0, 4)
    } finally { $stream.Dispose() }
    if ([Text.Encoding]::ASCII.GetString($magic) -eq 'CEMA') { continue }
    $text = Get-Content -LiteralPath $file.FullName -Raw
    foreach ($old in $oldIds) {
        $oldReferenceCount += [regex]::Matches($text,
            [regex]::Escape($old), [Text.RegularExpressions.RegexOptions]::IgnoreCase).Count
    }
    foreach ($match in [regex]::Matches($text,
        '[0-9a-f]{8}-[0-9a-f]{4}-8[0-9a-f]{3}-[89ab][0-9a-f]{3}-[0-9a-f]{12}')) {
        if ($allIds.Contains($match.Value)) { ++$newReferenceCount }
    }
}
$expectedReferences = [int]$baselineDoc.summary.modelReferences +
    [int]$baselineDoc.summary.subassetReferences
if ($oldReferenceCount -ne 0) {
    Add-Failure "persisted document에 legacy model GUID가 남았다: $oldReferenceCount"
}
if ($newReferenceCount -ne $expectedReferences) {
    Add-Failure "새 UUIDv8 저장 참조 수가 기준과 다르다: $newReferenceCount != $expectedReferences"
}

"model-corpus-v8 models=$modelCount generations=$generationCount subassets=$subassetCount identities=$($allIds.Count) oldIds=$($oldIds.Count) oldRefs=$oldReferenceCount newRefs=$newReferenceCount failures=$($failures.Count)"
if ($failures.Count -gt 0) {
    $failures | ForEach-Object { "  $_" }
    exit 1
}
'통과 — 전체 모델 corpus가 단일 UUIDv8 epoch/schema v2/generation closure이고 저장 참조에 legacy model GUID가 없다'
exit 0
