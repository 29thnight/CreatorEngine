#Requires -Version 7
param(
    [string]$Root = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path,

    [Parameter(Mandatory)]
    [string]$ManifestPath,

    [Parameter(Mandatory)]
    [string]$BackupPath,

    [switch]$Apply
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

if (-not ('CreatorEngine.AssetGuidMigrationApplyByteCodecV3' -as [type])) {
    Add-Type -TypeDefinition @'
using System;
using System.Collections.Generic;
using System.Text;

namespace CreatorEngine
{
    public static class AssetGuidMigrationApplyByteCodecV3
    {
        private static byte[] Encode(string guid, bool ascii)
        {
            if (ascii) return Encoding.ASCII.GetBytes(guid);
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

        private static string[] Process(byte[] bytes, string[] oldGuids,
            string[] newGuids, bool ascii, bool replace)
        {
            if (oldGuids.Length != newGuids.Length)
                throw new ArgumentException("guid mapping length mismatch");

            byte[][] oldPatterns = new byte[oldGuids.Length][];
            byte[][] newPatterns = new byte[newGuids.Length][];
            var buckets = new Dictionary<ulong, List<int>>();
            for (int index = 0; index < oldGuids.Length; ++index)
            {
                oldPatterns[index] = Encode(oldGuids[index], ascii);
                newPatterns[index] = Encode(newGuids[index], ascii);
                if (oldPatterns[index].Length != newPatterns[index].Length)
                    throw new InvalidOperationException("guid replacement changed byte length");
                ulong key = Key(oldPatterns[index], 0);
                if (!buckets.TryGetValue(key, out List<int> candidates))
                {
                    candidates = new List<int>();
                    buckets.Add(key, candidates);
                }
                candidates.Add(index);
            }

            int[] counts = new int[oldGuids.Length];
            int minimumLength = ascii ? 36 : 16;
            for (int offset = 0; offset + minimumLength <= bytes.Length; ++offset)
            {
                if (!buckets.TryGetValue(Key(bytes, offset), out List<int> candidates))
                    continue;
                foreach (int candidate in candidates)
                {
                    if (!Matches(bytes, offset, oldPatterns[candidate])) continue;
                    ++counts[candidate];
                    if (replace)
                        Buffer.BlockCopy(newPatterns[candidate], 0, bytes, offset,
                            newPatterns[candidate].Length);
                    offset += oldPatterns[candidate].Length - 1;
                    break;
                }
            }

            var result = new List<string>();
            for (int index = 0; index < counts.Length; ++index)
                if (counts[index] != 0)
                    result.Add(oldGuids[index] + "|" + counts[index]);
            return result.ToArray();
        }

        public static string[] ReplaceAscii(byte[] bytes, string[] oldGuids,
            string[] newGuids) => Process(bytes, oldGuids, newGuids, true, true);

        public static string[] ReplaceRaw(byte[] bytes, string[] oldGuids,
            string[] newGuids) => Process(bytes, oldGuids, newGuids, false, true);

        public static string[] CountAscii(byte[] bytes, string[] guids) =>
            Process(bytes, guids, guids, true, false);

        public static string[] CountRaw(byte[] bytes, string[] guids) =>
            Process(bytes, guids, guids, false, false);
    }
}
'@
}

function Get-NormalizedFullPath([string]$Path, [string]$Base) {
    $candidate = if ([IO.Path]::IsPathRooted($Path)) {
        $Path
    } else {
        Join-Path $Base $Path
    }
    return [IO.Path]::GetFullPath($candidate)
}

function Add-ReplacementCounts([hashtable]$Counts, [string[]]$Hits) {
    foreach ($hit in $Hits) {
        $separator = $hit.LastIndexOf('|')
        $guid = $hit.Substring(0, $separator)
        $count = [int]$hit.Substring($separator + 1)
        if (-not $Counts.ContainsKey($guid)) { $Counts[$guid] = 0 }
        $Counts[$guid] += $count
    }
}

$resolvedRoot = (Resolve-Path -LiteralPath $Root).Path
$assetRoot = (Resolve-Path -LiteralPath (
    Join-Path $resolvedRoot 'Dynamic_CPP\Assets')).Path
$assetPrefix = $assetRoot + [IO.Path]::DirectorySeparatorChar
$manifestFullPath = Get-NormalizedFullPath $ManifestPath $resolvedRoot
$backupFullPath = Get-NormalizedFullPath $BackupPath $resolvedRoot
$rootPrefix = $resolvedRoot + [IO.Path]::DirectorySeparatorChar

if ($backupFullPath.Equals($resolvedRoot, [StringComparison]::OrdinalIgnoreCase) -or
    $backupFullPath.StartsWith($rootPrefix, [StringComparison]::OrdinalIgnoreCase)) {
    throw 'backup path must be outside the repository'
}
if (Test-Path -LiteralPath $backupFullPath) {
    throw "backup path already exists: $backupFullPath"
}
if (-not (Test-Path -LiteralPath $manifestFullPath -PathType Leaf)) {
    throw "migration manifest does not exist: $manifestFullPath"
}

$validator = Join-Path $PSScriptRoot 'New-AssetGuidMigrationManifest.ps1'
$validationOutput = & pwsh -NoProfile -File $validator -Root $resolvedRoot `
    -ManifestPath $manifestFullPath -ValidateExisting 2>&1
if ($LASTEXITCODE -ne 0) {
    throw "migration manifest validation failed:`n$($validationOutput -join "`n")"
}

$manifest = Get-Content -LiteralPath $manifestFullPath -Raw | ConvertFrom-Json
if ([int]$manifest.schemaVersion -ne 3) { throw 'apply requires manifest schemaVersion 3' }
$entries = @($manifest.entries)
$oldGuids = [string[]]@($entries | ForEach-Object { [string]$_.oldGuid })
$newGuids = [string[]]@($entries | ForEach-Object { [string]$_.newGuid })

$entriesByFile = @{}
foreach ($entry in $entries) {
    foreach ($relativePath in @($entry.referenceFiles)) {
        $key = [string]$relativePath
        if (-not $entriesByFile.ContainsKey($key)) {
            $entriesByFile[$key] = [System.Collections.Generic.List[object]]::new()
        }
        $entriesByFile[$key].Add($entry)
    }
}
$affectedFiles = @($entriesByFile.Keys | Sort-Object)
if ($affectedFiles.Count -eq 0) { throw 'manifest has no affected files' }

$originalRoot = Join-Path $backupFullPath 'original'
$stagedRoot = Join-Path $backupFullPath 'staged'
[IO.Directory]::CreateDirectory($originalRoot) | Out-Null
[IO.Directory]::CreateDirectory($stagedRoot) | Out-Null
[IO.File]::Copy($manifestFullPath,
    (Join-Path $backupFullPath 'migration-manifest.json'), $false)

$strictUtf8 = [Text.UTF8Encoding]::new($false, $true)
$replacementCounts = @{}
$fileRecords = [System.Collections.Generic.List[object]]::new()
foreach ($relativePath in $affectedFiles) {
    $sourcePath = Get-NormalizedFullPath ($relativePath -replace '/', '\') $resolvedRoot
    if (-not $sourcePath.StartsWith($assetPrefix,
        [StringComparison]::OrdinalIgnoreCase)) {
        throw "manifest path escapes asset root: $relativePath"
    }
    if (-not (Test-Path -LiteralPath $sourcePath -PathType Leaf)) {
        throw "manifest source file is missing: $relativePath"
    }

    $originalPath = Join-Path $originalRoot ($relativePath -replace '/', '\')
    $stagedPath = Join-Path $stagedRoot ($relativePath -replace '/', '\')
    [IO.Directory]::CreateDirectory((Split-Path -Parent $originalPath)) | Out-Null
    [IO.Directory]::CreateDirectory((Split-Path -Parent $stagedPath)) | Out-Null
    [IO.File]::Copy($sourcePath, $originalPath, $false)

    $bytes = [IO.File]::ReadAllBytes($sourcePath)
    $sourceHash = (Get-FileHash -LiteralPath $sourcePath -Algorithm SHA256).Hash
    $isBinary = [IO.Path]::GetExtension($sourcePath).Equals('.asset',
        [StringComparison]::OrdinalIgnoreCase) -and ($bytes -contains 0)
    $fileReplacementCount = 0
    if ($isBinary) {
        $asciiHits = [CreatorEngine.AssetGuidMigrationApplyByteCodecV3]::ReplaceAscii(
            $bytes, $oldGuids, $newGuids)
        $rawHits = [CreatorEngine.AssetGuidMigrationApplyByteCodecV3]::ReplaceRaw(
            $bytes, $oldGuids, $newGuids)
        Add-ReplacementCounts $replacementCounts $asciiHits
        Add-ReplacementCounts $replacementCounts $rawHits
        foreach ($hit in @($asciiHits) + @($rawHits)) {
            $fileReplacementCount += [int]$hit.Substring($hit.LastIndexOf('|') + 1)
        }
        if ([CreatorEngine.AssetGuidMigrationApplyByteCodecV3]::CountAscii(
                $bytes, $oldGuids).Count -ne 0 -or
            [CreatorEngine.AssetGuidMigrationApplyByteCodecV3]::CountRaw(
                $bytes, $oldGuids).Count -ne 0) {
            throw "old guid remained in staged binary file: $relativePath"
        }
    } else {
        try { $content = $strictUtf8.GetString($bytes) }
        catch { throw "asset reference file is not UTF-8: $relativePath" }
        foreach ($entry in @($entriesByFile[$relativePath])) {
            $oldGuid = [string]$entry.oldGuid
            $newGuid = [string]$entry.newGuid
            $pattern = [regex]::Escape($oldGuid)
            $count = [regex]::Matches($content, $pattern,
                [Text.RegularExpressions.RegexOptions]::IgnoreCase).Count
            if ($count -eq 0) { continue }
            $content = [regex]::Replace($content, $pattern, $newGuid,
                [Text.RegularExpressions.RegexOptions]::IgnoreCase)
            if (-not $replacementCounts.ContainsKey($oldGuid)) {
                $replacementCounts[$oldGuid] = 0
            }
            $replacementCounts[$oldGuid] += $count
            $fileReplacementCount += $count
        }
        $bytes = [Text.Encoding]::UTF8.GetBytes($content)
        foreach ($entry in @($entriesByFile[$relativePath])) {
            if ([regex]::IsMatch($content, [regex]::Escape([string]$entry.oldGuid),
                [Text.RegularExpressions.RegexOptions]::IgnoreCase)) {
                throw "old guid remained in staged text file: $relativePath"
            }
        }
    }

    if ($fileReplacementCount -eq 0) {
        throw "manifest affected file had no replacements: $relativePath"
    }
    if ($bytes.Length -ne (Get-Item -LiteralPath $sourcePath).Length) {
        throw "guid migration changed file byte length: $relativePath"
    }
    [IO.File]::WriteAllBytes($stagedPath, $bytes)
    $stagedHash = (Get-FileHash -LiteralPath $stagedPath -Algorithm SHA256).Hash
    $fileRecords.Add([pscustomobject]@{
        RelativePath = $relativePath
        SourcePath = $sourcePath
        OriginalPath = $originalPath
        StagedPath = $stagedPath
        SourceHash = $sourceHash
        StagedHash = $stagedHash
        ReplacementCount = $fileReplacementCount
        Binary = $isBinary
    })
}

$totalReplacements = 0
foreach ($entry in $entries) {
    $oldGuid = [string]$entry.oldGuid
    $actual = if ($replacementCounts.ContainsKey($oldGuid)) {
        [int]$replacementCounts[$oldGuid]
    } else { 0 }
    if ($actual -ne [int]$entry.referenceOccurrenceCount) {
        throw "replacement count mismatch for $oldGuid expected=$($entry.referenceOccurrenceCount) actual=$actual"
    }
    $totalReplacements += $actual
}
if ($totalReplacements -ne [int]$manifest.knownReferenceOccurrences) {
    throw "total replacement count mismatch expected=$($manifest.knownReferenceOccurrences) actual=$totalReplacements"
}

if (-not $Apply) {
    Write-Output ('asset-guid-migration stage-only files={0} binary={1} replacements={2} backup={3} assetWrites=0' -f
        $fileRecords.Count, @($fileRecords | Where-Object Binary).Count,
        $totalReplacements, $backupFullPath)
    exit 0
}

foreach ($record in $fileRecords) {
    $currentHash = (Get-FileHash -LiteralPath $record.SourcePath -Algorithm SHA256).Hash
    if ($currentHash -ne $record.SourceHash) {
        throw "source file changed during migration staging: $($record.RelativePath)"
    }
}

$committed = [System.Collections.Generic.List[object]]::new()
try {
    foreach ($record in $fileRecords) {
        [IO.File]::Copy($record.StagedPath, $record.SourcePath, $true)
        $committed.Add($record)
    }
} catch {
    for ($index = $committed.Count - 1; $index -ge 0; --$index) {
        $record = $committed[$index]
        [IO.File]::Copy($record.OriginalPath, $record.SourcePath, $true)
    }
    throw
}

foreach ($record in $fileRecords) {
    $liveHash = (Get-FileHash -LiteralPath $record.SourcePath -Algorithm SHA256).Hash
    if ($liveHash -ne $record.StagedHash) {
        throw "post-apply hash mismatch: $($record.RelativePath)"
    }
}

Write-Output ('asset-guid-migration applied files={0} binary={1} replacements={2} backup={3}' -f
    $fileRecords.Count, @($fileRecords | Where-Object Binary).Count,
    $totalReplacements, $backupFullPath)
exit 0
