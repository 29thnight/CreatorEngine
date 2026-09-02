#Requires -Version 7
param(
    [string]$Root = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path,

    [Parameter(Mandatory)]
    [string]$ManifestPath,

    [switch]$ValidateExisting,

    [switch]$RegenerateNonCanonicalOnly
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

if (-not ('CreatorEngine.AssetGuidMigrationByteCodecV3' -as [type])) {
    Add-Type -TypeDefinition @'
using System;
using System.Collections.Generic;

namespace CreatorEngine
{
    public static class AssetGuidMigrationByteCodecV3
    {
        private static byte[] ParseRaw(string guid)
        {
            string hex = guid.Replace("-", string.Empty);
            byte[] result = new byte[16];
            for (int index = 0; index < result.Length; ++index)
                result[index] = Convert.ToByte(hex.Substring(index * 2, 2), 16);
            return result;
        }

        private static ulong Key(byte[] bytes, int offset)
        {
            ulong value = 0;
            for (int index = 0; index < 8; ++index)
                value |= (ulong)bytes[offset + index] << (index * 8);
            return value;
        }

        private static bool Matches(byte[] bytes, int offset, byte[] pattern)
        {
            if (offset + pattern.Length > bytes.Length) return false;
            for (int index = 0; index < pattern.Length; ++index)
                if (bytes[offset + index] != pattern[index]) return false;
            return true;
        }

        public static string[] CountRaw(byte[] bytes, string[] guids)
        {
            byte[][] patterns = new byte[guids.Length][];
            var buckets = new Dictionary<ulong, List<int>>();
            for (int index = 0; index < guids.Length; ++index)
            {
                patterns[index] = ParseRaw(guids[index]);
                ulong key = Key(patterns[index], 0);
                if (!buckets.TryGetValue(key, out List<int> candidates))
                {
                    candidates = new List<int>();
                    buckets.Add(key, candidates);
                }
                candidates.Add(index);
            }

            int[] counts = new int[guids.Length];
            for (int offset = 0; offset + 16 <= bytes.Length; ++offset)
            {
                if (!buckets.TryGetValue(Key(bytes, offset), out List<int> candidates))
                    continue;
                foreach (int candidate in candidates)
                    if (Matches(bytes, offset, patterns[candidate])) ++counts[candidate];
            }

            var result = new List<string>();
            for (int index = 0; index < counts.Length; ++index)
                if (counts[index] != 0) result.Add(guids[index] + "|" + counts[index]);
            return result.ToArray();
        }
    }
}
'@
}

$resolvedRoot = (Resolve-Path -LiteralPath $Root).Path
$assetRoot = (Resolve-Path -LiteralPath (
    Join-Path $resolvedRoot 'Dynamic_CPP\Assets')).Path
$manifestFullPath = [IO.Path]::GetFullPath(
    $(if ([IO.Path]::IsPathRooted($ManifestPath)) {
        $ManifestPath
    } else {
        Join-Path $resolvedRoot $ManifestPath
    }))

if ($manifestFullPath.StartsWith(
    $assetRoot + [IO.Path]::DirectorySeparatorChar,
    [StringComparison]::OrdinalIgnoreCase)) {
    throw 'migration manifest must be written outside Dynamic_CPP/Assets'
}
$head = (& git -C $resolvedRoot rev-parse HEAD).Trim()
if ($LASTEXITCODE -ne 0) { throw 'git rev-parse HEAD failed' }

$uuidPattern = '^[0-9a-f]{8}-[0-9a-f]{4}-([0-9a-f])[0-9a-f]{3}-[0-9a-f]{4}-[0-9a-f]{12}$'
$canonicalV4Pattern = '^[0-9a-f]{8}-[0-9a-f]{4}-4[0-9a-f]{3}-[89ab][0-9a-f]{3}-[0-9a-f]{12}$'
$uuidSearchPattern = '[{]?[0-9a-fA-F]{8}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{12}[}]?'

function ConvertTo-NormalizedGuid([string]$Value) {
    return $Value.Trim().Trim([char[]]@(
        [char]34, [char]39, [char]123, [char]125)).ToLowerInvariant()
}

function Get-RelativeAssetPath([string]$Path) {
    return [IO.Path]::GetRelativePath($resolvedRoot, $Path).Replace('\', '/')
}

function Get-Sha256([string]$Text) {
    $sha = [Security.Cryptography.SHA256]::Create()
    try {
        $bytes = [Text.Encoding]::UTF8.GetBytes($Text)
        return [Convert]::ToHexString($sha.ComputeHash($bytes)).ToLowerInvariant()
    } finally {
        $sha.Dispose()
    }
}

$metaRecords = [System.Collections.Generic.List[object]]::new()
foreach ($meta in @(Get-ChildItem -LiteralPath $assetRoot -Recurse -File -Filter '*.meta')) {
    $guidLines = @(Select-String -LiteralPath $meta.FullName -Pattern '^guid:\s*(\S+)\s*$')
    if ($guidLines.Count -ne 1) {
        throw "meta must contain exactly one top-level guid: $($meta.FullName)"
    }

    $guid = ConvertTo-NormalizedGuid $guidLines[0].Matches[0].Groups[1].Value
    $match = [regex]::Match($guid, $uuidPattern)
    if (-not $match.Success) {
        throw "invalid asset guid: $guid ($($meta.FullName))"
    }

    $assetPath = $meta.FullName.Substring(0, $meta.FullName.Length - '.meta'.Length)
    if (-not (Test-Path -LiteralPath $assetPath -PathType Leaf)) {
        throw "meta target is missing: $($meta.FullName)"
    }

    $metaRecords.Add([pscustomobject]@{
        MetaPath = Get-RelativeAssetPath $meta.FullName
        AssetPath = Get-RelativeAssetPath $assetPath
        OldGuid = $guid
    })
}
$metaRecords = @($metaRecords | Sort-Object MetaPath)

$oldGuidSet = [System.Collections.Generic.HashSet[string]]::new(
    [StringComparer]::OrdinalIgnoreCase)
foreach ($record in $metaRecords) {
    if (-not $oldGuidSet.Add($record.OldGuid)) {
        throw "duplicate asset guid in source inventory: $($record.OldGuid)"
    }
}

$inventoryLines = @($metaRecords | ForEach-Object {
    '{0}|{1}' -f $_.MetaPath, $_.OldGuid
})
$inventoryHash = Get-Sha256 ($inventoryLines -join "`n")

$referenceCounts = @{}
$referenceFiles = @{}
$referenceKindCounts = @{}
$referenceInventoryLines = [System.Collections.Generic.List[string]]::new()
$textExtensions = [System.Collections.Generic.HashSet[string]]::new(
    [StringComparer]::OrdinalIgnoreCase)
foreach ($extension in @('.creator', '.asset', '.prefab', '.meta')) {
    [void]$textExtensions.Add($extension)
}

function Register-ReferenceOccurrences(
    [string]$Guid,
    [string]$RelativePath,
    [string]$Kind,
    [int]$Count) {
    if ($Count -le 0) { return }
    if (-not $referenceCounts.ContainsKey($Guid)) {
        $referenceCounts[$Guid] = 0
        $referenceFiles[$Guid] = [System.Collections.Generic.HashSet[string]]::new(
            [StringComparer]::OrdinalIgnoreCase)
    }
    $referenceCounts[$Guid] += $Count
    [void]$referenceFiles[$Guid].Add($RelativePath)
    $kindKey = $Guid + '|' + $Kind
    if (-not $referenceKindCounts.ContainsKey($kindKey)) {
        $referenceKindCounts[$kindKey] = 0
    }
    $referenceKindCounts[$kindKey] += $Count
    for ($index = 0; $index -lt $Count; ++$index) {
        $referenceInventoryLines.Add(
            ('{0}|{1}|{2}' -f $RelativePath, $Kind, $Guid))
    }
}

$oldGuidArray = [string[]]@($metaRecords | ForEach-Object OldGuid)
$strictUtf8 = [Text.UTF8Encoding]::new($false, $true)
foreach ($file in @(Get-ChildItem -LiteralPath $assetRoot -Recurse -File |
    Where-Object { $textExtensions.Contains($_.Extension) })) {
    $bytes = [IO.File]::ReadAllBytes($file.FullName)
    $isBinary = $file.Extension.Equals('.asset',
        [StringComparison]::OrdinalIgnoreCase) -and ($bytes -contains 0)
    $content = if ($isBinary) {
        [Text.Encoding]::Latin1.GetString($bytes)
    } else {
        try { $strictUtf8.GetString($bytes) }
        catch { throw "asset reference file is not UTF-8: $($file.FullName)" }
    }
    $relativePath = Get-RelativeAssetPath $file.FullName
    foreach ($match in [regex]::Matches($content, $uuidSearchPattern)) {
        $guid = ConvertTo-NormalizedGuid $match.Value
        if (-not $oldGuidSet.Contains($guid)) { continue }
        Register-ReferenceOccurrences $guid $relativePath $(
            if ($isBinary) { 'binary-ascii' } else { 'text' }) 1
    }
    if ($isBinary) {
        foreach ($rawHit in [CreatorEngine.AssetGuidMigrationByteCodecV3]::CountRaw(
            $bytes, $oldGuidArray)) {
            $separator = $rawHit.LastIndexOf('|')
            $guid = $rawHit.Substring(0, $separator)
            $count = [int]$rawHit.Substring($separator + 1)
            Register-ReferenceOccurrences $guid $relativePath 'binary-raw' $count
        }
    }
}
$referenceInventoryHash = Get-Sha256 (@($referenceInventoryLines | Sort-Object) -join "`n")

$manifest = $null
$incrementalMode = $RegenerateNonCanonicalOnly.IsPresent
if ($ValidateExisting) {
    if (-not (Test-Path -LiteralPath $manifestFullPath -PathType Leaf)) {
        throw "migration manifest does not exist: $manifestFullPath"
    }
    $manifest = Get-Content -LiteralPath $manifestFullPath -Raw | ConvertFrom-Json
    if ($manifest.schemaVersion -eq 3) {
        $incrementalMode = $false
    } elseif ($manifest.schemaVersion -eq 4 -and
        [string]$manifest.migrationMode -eq 'non-canonical-only') {
        $incrementalMode = $true
    } else {
        throw 'unsupported migration manifest schema or mode'
    }
}

$selectedRecords = if ($incrementalMode) {
    @($metaRecords | Where-Object { $_.OldGuid -notmatch $canonicalV4Pattern })
} else {
    @($metaRecords)
}
$selectedGuidSet = [System.Collections.Generic.HashSet[string]]::new(
    [string[]]@($selectedRecords | ForEach-Object OldGuid),
    [StringComparer]::OrdinalIgnoreCase)
$currentRegenerateCount = $selectedRecords.Count
$currentPreserveCount = $metaRecords.Count - $currentRegenerateCount
$currentKnownReferenceOccurrences = 0
$currentBinaryRawReferenceOccurrences = 0
foreach ($record in $selectedRecords) {
    if ($referenceCounts.ContainsKey($record.OldGuid)) {
        $currentKnownReferenceOccurrences += [int]$referenceCounts[$record.OldGuid]
    }
    $rawKindKey = $record.OldGuid + '|binary-raw'
    if ($referenceKindCounts.ContainsKey($rawKindKey)) {
        $currentBinaryRawReferenceOccurrences += [int]$referenceKindCounts[$rawKindKey]
    }
}

if ($ValidateExisting) {
    if ($manifest.sourceHead -ne $head) {
        throw 'repository HEAD changed after manifest creation'
    }
    if ($manifest.sourceInventorySha256 -ne $inventoryHash) {
        throw 'asset meta inventory changed after manifest creation'
    }
    if ($manifest.sourceReferenceInventorySha256 -ne $referenceInventoryHash) {
        throw 'asset reference inventory changed after manifest creation'
    }
    if ([int]$manifest.assetCount -ne $metaRecords.Count -or
        @($manifest.entries).Count -ne $currentRegenerateCount) {
        throw 'manifest asset count does not match current inventory'
    }
    if ($incrementalMode -and
        [int]$manifest.preserveCount -ne $currentPreserveCount) {
        throw 'manifest preserve count does not match current inventory'
    }
    if ([int]$manifest.regenerateCount -ne $currentRegenerateCount -or
        [int]$manifest.knownReferenceOccurrences -ne
            $currentKnownReferenceOccurrences -or
        [int]$manifest.binaryRawReferenceOccurrences -ne
            $currentBinaryRawReferenceOccurrences) {
        throw 'manifest summary does not match current inventory'
    }

    $sourceByMeta = @{}
    foreach ($record in $selectedRecords) { $sourceByMeta[$record.MetaPath] = $record }
    $newGuidSet = [System.Collections.Generic.HashSet[string]]::new(
        [StringComparer]::OrdinalIgnoreCase)
    $entryMetaSet = [System.Collections.Generic.HashSet[string]]::new(
        [StringComparer]::OrdinalIgnoreCase)
    foreach ($entry in @($manifest.entries)) {
        if (-not $sourceByMeta.ContainsKey([string]$entry.metaPath)) {
            throw "manifest contains unknown meta path: $($entry.metaPath)"
        }
        $source = $sourceByMeta[[string]$entry.metaPath]
        $oldGuid = ConvertTo-NormalizedGuid ([string]$entry.oldGuid)
        $newGuid = ConvertTo-NormalizedGuid ([string]$entry.newGuid)
        $expectedReferenceCount = if ($referenceCounts.ContainsKey($oldGuid)) {
            [int]$referenceCounts[$oldGuid]
        } else { 0 }
        $expectedReferenceFiles = if ($referenceFiles.ContainsKey($oldGuid)) {
            @($referenceFiles[$oldGuid] | Sort-Object)
        } else { @() }
        $rawKindKey = $oldGuid + '|binary-raw'
        $expectedBinaryRawReferenceCount = if (
            $referenceKindCounts.ContainsKey($rawKindKey)) {
            [int]$referenceKindCounts[$rawKindKey]
        } else { 0 }
        $manifestReferenceFiles = @($entry.referenceFiles |
            ForEach-Object { [string]$_ } | Sort-Object)
        $newMatch = [regex]::Match($newGuid, $uuidPattern)
        if ($oldGuid -ne $source.OldGuid -or -not $newMatch.Success -or
            [int]::Parse($newMatch.Groups[1].Value,
                [Globalization.NumberStyles]::HexNumber) -ne 4) {
            throw "manifest guid contract mismatch: $($entry.metaPath)"
        }
        if ([string]$entry.assetPath -ne $source.AssetPath -or
            [int]$entry.referenceOccurrenceCount -ne $expectedReferenceCount -or
            [int]$entry.binaryRawReferenceOccurrenceCount -ne
                $expectedBinaryRawReferenceCount -or
            ($manifestReferenceFiles -join "`n") -ne
                ($expectedReferenceFiles -join "`n")) {
            throw "manifest source mapping changed: $($entry.metaPath)"
        }
        if (-not $newGuidSet.Add($newGuid)) {
            throw "manifest new guid is duplicated: $newGuid"
        }
        if (-not $entryMetaSet.Add([string]$entry.metaPath)) {
            throw "manifest meta path is duplicated: $($entry.metaPath)"
        }
        if ($entry.action -ne 'regenerate') {
            throw "unknown migration action: $($entry.action)"
        }
        if ($newGuid -eq $oldGuid -or $oldGuidSet.Contains($newGuid)) {
            throw "invalid regenerated guid: $($entry.metaPath)"
        }
    }

    Write-Output ('asset-guid-migration validate assets={0} regenerate={1} preserve={2} references={3} raw={4} inventory={5} manifest={6}' -f
        $manifest.assetCount, $manifest.regenerateCount, $currentPreserveCount,
        $manifest.knownReferenceOccurrences, $manifest.binaryRawReferenceOccurrences,
        $inventoryHash, $manifestFullPath)
    exit 0
}

if (Test-Path -LiteralPath $manifestFullPath) {
    throw "refusing to overwrite existing migration manifest: $manifestFullPath"
}

$reservedGuidSet = [System.Collections.Generic.HashSet[string]]::new(
    $oldGuidSet, [StringComparer]::OrdinalIgnoreCase)
$entries = [System.Collections.Generic.List[object]]::new()
$regenerateCount = 0
$knownReferenceOccurrences = 0
foreach ($record in $selectedRecords) {
    do {
        $newGuid = [guid]::NewGuid().ToString('D').ToLowerInvariant()
    } while (-not $reservedGuidSet.Add($newGuid))
    $regenerateCount++

    $count = if ($referenceCounts.ContainsKey($record.OldGuid)) {
        [int]$referenceCounts[$record.OldGuid]
    } else { 0 }
    $files = if ($referenceFiles.ContainsKey($record.OldGuid)) {
        @($referenceFiles[$record.OldGuid] | Sort-Object)
    } else { @() }
    $rawKindKey = $record.OldGuid + '|binary-raw'
    $binaryRawCount = if ($referenceKindCounts.ContainsKey($rawKindKey)) {
        [int]$referenceKindCounts[$rawKindKey]
    } else { 0 }
    $knownReferenceOccurrences += $count
    $entries.Add([ordered]@{
        metaPath = $record.MetaPath
        assetPath = $record.AssetPath
        action = 'regenerate'
        oldGuid = $record.OldGuid
        newGuid = $newGuid
        referenceOccurrenceCount = $count
        binaryRawReferenceOccurrenceCount = $binaryRawCount
        referenceFiles = $files
    })
}

$manifest = [ordered]@{
    schemaVersion = $(if ($incrementalMode) { 4 } else { 3 })
    createdUtc = [DateTime]::UtcNow.ToString('o')
    sourceHead = $head
    sourceInventorySha256 = $inventoryHash
    sourceReferenceInventorySha256 = $referenceInventoryHash
    assetCount = $metaRecords.Count
    regenerateCount = $regenerateCount
    knownReferenceOccurrences = $knownReferenceOccurrences
    binaryRawReferenceOccurrences = $currentBinaryRawReferenceOccurrences
    entries = $entries
}
if ($incrementalMode) {
    $manifest['migrationMode'] = 'non-canonical-only'
    $manifest['preserveCount'] = $currentPreserveCount
}

$parent = Split-Path -Parent $manifestFullPath
if ([string]::IsNullOrWhiteSpace($parent)) { throw 'manifest parent is empty' }
[IO.Directory]::CreateDirectory($parent) | Out-Null
[IO.File]::WriteAllText(
    $manifestFullPath,
    ($manifest | ConvertTo-Json -Depth 8),
    [Text.UTF8Encoding]::new($false))

Write-Output ('asset-guid-migration create assets={0} regenerate={1} preserve={2} references={3} raw={4} inventory={5} manifest={6} assetWrites=0' -f
    $metaRecords.Count, $regenerateCount, $currentPreserveCount,
    $knownReferenceOccurrences, $currentBinaryRawReferenceOccurrences,
    $inventoryHash, $manifestFullPath)
exit 0
