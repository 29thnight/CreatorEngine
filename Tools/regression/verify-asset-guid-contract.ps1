param(
    [string]$Root = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path,
    [switch]$Strict
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$resolvedRoot = (Resolve-Path -LiteralPath $Root).Path
$excludedSegments = @(
    '.git', 'Artifacts', 'Bin', 'Build', 'output', 'tmp',
    'vcpkg_installed', 'x64'
)

function Test-IsExcluded([string]$Path) {
    $relative = [IO.Path]::GetRelativePath($resolvedRoot, $Path)
    $segments = $relative -split '[\\/]'
    foreach ($segment in $segments) {
        if ($excludedSegments -contains $segment) { return $true }
    }
    return $false
}

$metaFiles = @(
    Get-ChildItem -LiteralPath $resolvedRoot -Recurse -File -Filter '*.meta' |
        Where-Object { -not (Test-IsExcluded $_.FullName) }
)

$records = [System.Collections.Generic.List[object]]::new()
$subassetRecords = [System.Collections.Generic.List[object]]::new()
$invalid = [System.Collections.Generic.List[string]]::new()
$invalidSubassets = [System.Collections.Generic.List[string]]::new()
$missingTargets = [System.Collections.Generic.List[string]]::new()
$uuidPattern = '^[0-9a-fA-F]{8}-[0-9a-fA-F]{4}-([0-9a-fA-F])[0-9a-fA-F]{3}-[0-9a-fA-F]{4}-[0-9a-fA-F]{12}$'
$canonicalV4Pattern = '^[0-9a-f]{8}-[0-9a-f]{4}-4[0-9a-f]{3}-[89ab][0-9a-f]{3}-[0-9a-f]{12}$'

foreach ($meta in $metaFiles) {
    $guidLines = @(Select-String -LiteralPath $meta.FullName -Pattern '^guid:\s*(\S+)\s*$')
    if ($guidLines.Count -ne 1) {
        $invalid.Add($meta.FullName)
        continue
    }

    $guid = $guidLines[0].Matches[0].Groups[1].Value.Trim()
    $guid = $guid.Trim([char[]]@(
        [char]34, [char]39, [char]123, [char]125))
    $uuidMatch = [regex]::Match($guid, $uuidPattern)
    if (-not $uuidMatch.Success) {
        $invalid.Add($meta.FullName)
        continue
    }

    $target = $meta.FullName.Substring(0, $meta.FullName.Length - '.meta'.Length)
    if (-not (Test-Path -LiteralPath $target -PathType Leaf)) {
        $missingTargets.Add($meta.FullName)
    }

    $records.Add([pscustomobject]@{
        Guid = $guid.ToLowerInvariant()
        Version = [int]::Parse(
            $uuidMatch.Groups[1].Value,
            [Globalization.NumberStyles]::HexNumber)
        Meta = $meta.FullName
        Target = $target
    })

    # UUID 판정을 별도 model gate에 복제하지 않는다. subAssets 블록 안의
    # indented guid만 여기서 같은 전역 identity 집합에 합쳐 top-level asset과의
    # 충돌까지 한 번에 잡는다. legacy 표기는 새 schema에서 허용하지 않는다.
    $inSubAssets = $false
    $lineNumber = 0
    foreach ($line in Get-Content -LiteralPath $meta.FullName) {
        ++$lineNumber
        if ($line -match '^subAssets:\s*$') {
            $inSubAssets = $true
            continue
        }
        if ($inSubAssets -and $line -match '^\S') {
            $inSubAssets = $false
        }
        if (-not $inSubAssets -or
            $line -notmatch '^\s+guid:\s*(\S+)\s*$') {
            continue
        }

        $subassetGuid = $Matches[1]
        if ($subassetGuid -notmatch $canonicalV4Pattern) {
            $invalidSubassets.Add(('{0}:{1}' -f $meta.FullName, $lineNumber))
            continue
        }
        $subassetRecords.Add([pscustomobject]@{
            Guid = $subassetGuid
            Version = 4
            Meta = $meta.FullName
            Target = $target
            Line = $lineNumber
        })
    }
}

$allIdentityRecords = @($records) + @($subassetRecords)
$duplicateGroups = @(
    $allIdentityRecords |
        Group-Object Guid |
        Where-Object { $_.Count -gt 1 } |
        Sort-Object @{ Expression = 'Count'; Descending = $true }, Name
)
$version4 = @($records | Where-Object Version -eq 4)
$nonVersion4 = @($records | Where-Object Version -ne 4)
$trackedFiles = @(& git -C $resolvedRoot ls-files)
if ($LASTEXITCODE -ne 0) { throw 'git ls-files failed' }
$trackedSet = [System.Collections.Generic.HashSet[string]]::new(
    [StringComparer]::OrdinalIgnoreCase)
foreach ($tracked in $trackedFiles) {
    $absolute = [IO.Path]::GetFullPath((Join-Path $resolvedRoot $tracked))
    [void]$trackedSet.Add($absolute)
}
$trackedMeta = @($trackedFiles | Where-Object {
    [IO.Path]::GetExtension($_).Equals('.meta',
        [StringComparison]::OrdinalIgnoreCase)
})

function Test-GitIgnored {
    param([Parameter(Mandatory)][string]$RelativePath)

    & git -C $resolvedRoot check-ignore --no-index --quiet -- $RelativePath
    $status = $LASTEXITCODE
    if ($status -eq 0) { return $true }
    if ($status -eq 1) { return $false }
    throw "git check-ignore failed for $RelativePath (exit $status)"
}

# .gitignore의 명시적 negation이 tracked sidecar allowlist의 정본이다.
# ignore된 .meta를 `git add -f`로 강제 추가하면 Strict gate가 거부한다.
$trackedMetaPolicyViolations = @($trackedMeta | Where-Object {
    Test-GitIgnored -RelativePath $_
})
$supportedExtensions = [System.Collections.Generic.HashSet[string]]::new(
    [StringComparer]::OrdinalIgnoreCase)
foreach ($extension in @(
    '.fbx', '.gltf', '.obj', '.glb',
    '.png', '.dds', '.jpg', '.jpeg', '.hdr',
    '.hlsl', '.shadermeta', '.shader', '.cpp', '.cs',
    '.wav', '.mp3', '.ogg', '.spritefont',
    '.terrain', '.bt', '.blackboard', '.prefab', '.volume',
    '.foliage', '.asset')) {
    [void]$supportedExtensions.Add($extension)
}

# EditorAssetDatabase::m_registeredFiles와 같은 자산 범위다. Git이 대상 파일을
# 정본으로 관리할 때만 sidecar도 추적해야 한다. 로컬 전용 게임 콘텐츠와 그
# sidecar가 둘 다 untracked인 것은 저장소 정책 위반이 아니다.
$requiredMetaUntracked = [System.Collections.Generic.List[string]]::new()
foreach ($tracked in $trackedFiles) {
    $absolute = [IO.Path]::GetFullPath((Join-Path $resolvedRoot $tracked))
    if (-not $absolute.StartsWith(
        (Join-Path $resolvedRoot 'Dynamic_CPP\Assets') +
            [IO.Path]::DirectorySeparatorChar,
        [StringComparison]::OrdinalIgnoreCase) -or
        -not $supportedExtensions.Contains([IO.Path]::GetExtension($absolute))) {
        continue
    }
    $metaPath = $absolute + '.meta'
    $relativeMetaPath = [IO.Path]::GetRelativePath($resolvedRoot, $metaPath)
    if (Test-GitIgnored -RelativePath $relativeMetaPath) {
        continue
    }
    if (-not $trackedSet.Contains($metaPath)) {
        $requiredMetaUntracked.Add($metaPath)
    }
}
$orphanTrackedMeta = @(
    $records | Where-Object {
        $trackedSet.Contains($_.Meta) -and -not $trackedSet.Contains($_.Target)
    }
)
$localUntrackedPairs = @(
    $records | Where-Object {
        -not $trackedSet.Contains($_.Meta) -and
        -not $trackedSet.Contains($_.Target)
    }
)

$duplicateFileCount = 0
foreach ($group in $duplicateGroups) { $duplicateFileCount += $group.Count }
$ready = $invalid.Count -eq 0 -and $invalidSubassets.Count -eq 0 -and
    $missingTargets.Count -eq 0 -and
    $duplicateGroups.Count -eq 0 -and $nonVersion4.Count -eq 0 -and
    $trackedMetaPolicyViolations.Count -eq 0 -and
    $requiredMetaUntracked.Count -eq 0 -and $orphanTrackedMeta.Count -eq 0

Write-Output ('asset-guid-contract meta={0} parsed={1} invalid={2} subassetGuids={3} invalidSubasset={4} missingTarget={5} duplicateGroups={6} duplicateFiles={7} uuidV4={8} nonV4={9} trackedMeta={10} trackedMetaPolicyViolations={11} requiredMetaUntracked={12} orphanTrackedMeta={13} localUntrackedPairs={14} d2Ready={15}' -f
    $metaFiles.Count, $records.Count, $invalid.Count,
    $subassetRecords.Count, $invalidSubassets.Count, $missingTargets.Count,
    $duplicateGroups.Count, $duplicateFileCount, $version4.Count,
    $nonVersion4.Count, $trackedMeta.Count, $trackedMetaPolicyViolations.Count,
    $requiredMetaUntracked.Count,
    $orphanTrackedMeta.Count, $localUntrackedPairs.Count,
    $ready.ToString().ToLowerInvariant())

foreach ($group in $duplicateGroups) {
    Write-Output ('duplicate guid={0} count={1}' -f $group.Name, $group.Count)
    foreach ($record in $group.Group) {
        Write-Output ('  {0}' -f
            [IO.Path]::GetRelativePath($resolvedRoot, $record.Meta))
    }
}
foreach ($path in $invalid) {
    Write-Output ('invalid {0}' -f [IO.Path]::GetRelativePath($resolvedRoot, $path))
}
foreach ($path in $invalidSubassets) {
    Write-Output ('invalid-subasset {0}' -f $path)
}
foreach ($path in $missingTargets) {
    Write-Output ('missing-target {0}' -f
        [IO.Path]::GetRelativePath($resolvedRoot, $path))
}
foreach ($path in $requiredMetaUntracked) {
    Write-Output ('required-meta-untracked {0}' -f
        [IO.Path]::GetRelativePath($resolvedRoot, $path))
}
foreach ($path in $trackedMetaPolicyViolations) {
    Write-Output ('tracked-meta-policy-violation {0}' -f $path)
}
foreach ($record in $orphanTrackedMeta) {
    Write-Output ('orphan-tracked-meta {0}' -f
        [IO.Path]::GetRelativePath($resolvedRoot, $record.Meta))
}

if ($Strict -and -not $ready) { exit 1 }
exit 0
