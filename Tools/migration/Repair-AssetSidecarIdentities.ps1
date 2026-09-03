#Requires -Version 7
<#
.SYNOPSIS
    `.meta` sidecar 표기를 정규화하고 누락된 sidecar 를 발급한다.

.DESCRIPTION
    비모델 legacy 자산의 저작 경계 도구다. model은 PHASE 3.75 MBC3의
    `AssetCooker --author-model-asset` transaction만 쓸 수 있으므로 이 도구가
    정규화하거나 발급하지 않는다.

    두 가지 일을 한다.

    -Normalize
        최상위 `guid:` 가 `{...}`·따옴표·대문자 같은 legacy 표기면 canonical
        소문자 8-4-4-4-12 로 다시 쓴다. **값은 보존한다** — 표기만 바꾼다.
        AssetIdentity.h 가 legacy 표기 호환을 두지 않으므로 producer 들이
        이것들을 fail-closed 로 거부해 왔다.

    -Issue
        등록 확장자인데 `.meta` 가 없는 파일에 새 UUIDv4 를 발급한다.
        corpus 전체(최상위 + subasset)와 충돌을 먼저 확인한다.

    기본은 dry-run 이다. -Apply 를 줘야 쓴다.

.NOTES
    UUIDv4 판정은 AssetIdentity.h::TryParseCanonicalAssetId 와 같은 규칙이다.
#>
param(
    [string]$Root = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path,
    [switch]$Normalize,
    [switch]$Issue,
    [switch]$Apply
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

if (-not $Normalize -and -not $Issue) {
    "적어도 하나는 필요하다: -Normalize 또는 -Issue"
    exit 2
}

$assetsRoot = Join-Path $Root 'Dynamic_CPP\Assets'
if (-not (Test-Path -LiteralPath $assetsRoot -PathType Container)) {
    "asset root가 없다: $assetsRoot"
    exit 2
}

# EditorAssetDatabase::m_registeredFiles 와 같은 범위여야 한다. 여기만 늘리면
# 에디터가 새 자산에 sidecar 를 안 만들고, 저기만 늘리면 이 도구가 백필을 못 한다.
$registeredExtensions = @(
    '.fbx', '.gltf', '.obj', '.glb',
    '.png', '.dds', '.jpg', '.jpeg', '.hdr',
    '.hlsl', '.shadermeta', '.shader', '.cpp', '.cs',
    '.wav', '.mp3', '.ogg', '.spritefont',
    '.terrain', '.bt', '.blackboard', '.prefab', '.volume',
    '.foliage', '.asset', '.creator'
)
$registered = [System.Collections.Generic.HashSet[string]]::new(
    [string[]]$registeredExtensions, [StringComparer]::OrdinalIgnoreCase)
$modelExtensions = [System.Collections.Generic.HashSet[string]]::new(
    [string[]]@('.fbx', '.gltf', '.obj', '.glb'),
    [StringComparer]::OrdinalIgnoreCase)

function Test-IsModelMeta([string]$Path) {
    $assetPath = $Path.Substring(0, $Path.Length - '.meta'.Length)
    return $modelExtensions.Contains([IO.Path]::GetExtension($assetPath))
}

$canonical = '^[0-9a-f]{8}-[0-9a-f]{4}-4[0-9a-f]{3}-[89ab][0-9a-f]{3}-[0-9a-f]{12}$'
$anyUuid = '^[0-9a-fA-F]{8}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{12}$'

# ── corpus 의 모든 GUID 를 먼저 예약한다 ─────────────────────────────────
# 최상위와 subasset 을 함께 본다. 새 GUID 는 이 집합과 충돌하면 안 된다.
$taken = [System.Collections.Generic.HashSet[string]]::new(
    [StringComparer]::OrdinalIgnoreCase)
$metaFiles = @(Get-ChildItem -LiteralPath $assetsRoot -Recurse -File -Filter '*.meta')
foreach ($meta in $metaFiles) {
    foreach ($line in [IO.File]::ReadAllLines($meta.FullName)) {
        $match = [regex]::Match($line, '^\s*guid:\s*(\S+)\s*$')
        if (-not $match.Success) { continue }
        $value = $match.Groups[1].Value.Trim([char[]]@(
            [char]34, [char]39, [char]123, [char]125))
        if ($value -match $anyUuid) { [void]$taken.Add($value.ToLowerInvariant()) }
    }
}
"corpus GUID 예약: $($taken.Count)"

$normalized = [System.Collections.Generic.List[string]]::new()
$issued = [System.Collections.Generic.List[string]]::new()
$refused = [System.Collections.Generic.List[string]]::new()

# ── 1. 표기 정규화 ───────────────────────────────────────────────────────
if ($Normalize) {
    foreach ($meta in $metaFiles) {
        if (Test-IsModelMeta $meta.FullName) { continue }
        $lines = [IO.File]::ReadAllLines($meta.FullName)
        $changed = $false
        for ($index = 0; $index -lt $lines.Length; ++$index) {
            # 최상위 guid 만 본다. subasset guid 는 들여쓰기가 있다.
            $match = [regex]::Match($lines[$index], '^guid:\s*(\S+)\s*$')
            if (-not $match.Success) { continue }
            $raw = $match.Groups[1].Value
            if ($raw -match $canonical) { continue }

            $value = $raw.Trim([char[]]@(
                [char]34, [char]39, [char]123, [char]125)).ToLowerInvariant()
            if ($value -notmatch $canonical) {
                # 표기를 벗겨도 UUIDv4 가 아니면 손대지 않는다. 값을 지어내는
                # 것은 정규화가 아니라 재발급이고, 그건 사람이 결정할 일이다.
                $refused.Add(('{0} -> {1}' -f
                    [IO.Path]::GetRelativePath($Root, $meta.FullName), $raw))
                continue
            }
            $lines[$index] = 'guid: ' + $value
            $changed = $true
        }
        if (-not $changed) { continue }
        $normalized.Add([IO.Path]::GetRelativePath($Root, $meta.FullName))
        if ($Apply) {
            # 원본 줄바꿈 관습을 지킨다. 이진 모드 원자 게시가 CRLF 를 LF 로
            # 뒤집어 저장할 때마다 git status 를 더럽힌 전례가 있다.
            $text = [IO.File]::ReadAllText($meta.FullName)
            $newline = if ($text.Contains("`r`n")) { "`r`n" } else { "`n" }
            $trailing = if ($text.EndsWith("`n")) { $newline } else { '' }
            [IO.File]::WriteAllText($meta.FullName,
                ($lines -join $newline) + $trailing, [Text.UTF8Encoding]::new($false))
        }
    }
}

# ── 2. 누락 sidecar 발급 ─────────────────────────────────────────────────
if ($Issue) {
    $targets = @(Get-ChildItem -LiteralPath $assetsRoot -Recurse -File |
        Where-Object { $registered.Contains($_.Extension) } |
        Where-Object { -not $modelExtensions.Contains($_.Extension) } |
        Where-Object { -not (Test-Path -LiteralPath ($_.FullName + '.meta') -PathType Leaf) })

    foreach ($target in $targets) {
        # 충돌 없는 UUIDv4 를 뽑는다. New-Guid 는 v4 를 만든다.
        $guid = $null
        for ($attempt = 0; $attempt -lt 64; ++$attempt) {
            $candidate = [guid]::NewGuid().ToString('D').ToLowerInvariant()
            if ($candidate -notmatch $canonical) { continue }
            if ($taken.Contains($candidate)) { continue }
            $guid = $candidate
            break
        }
        if ($null -eq $guid) {
            "충돌 없는 UUIDv4를 뽑지 못했다: $($target.FullName)"
            exit 3
        }
        [void]$taken.Add($guid)

        $ticks = $target.LastWriteTimeUtc.ToFileTimeUtc()
        $body = "guid: $guid`nimportSettings:`n  extension: $($target.Extension)`n  timestamp: $ticks`n"
        $issued.Add(('{0} -> {1}' -f
            [IO.Path]::GetRelativePath($Root, $target.FullName), $guid))
        if ($Apply) {
            [IO.File]::WriteAllText($target.FullName + '.meta', $body,
                [Text.UTF8Encoding]::new($false))
        }
    }
}

$mode = if ($Apply) { 'APPLY' } else { 'DRY-RUN' }
"sidecar-repair mode=$mode normalized=$($normalized.Count) issued=$($issued.Count) refused=$($refused.Count)"
foreach ($path in $normalized) { "  normalized $path" }
foreach ($path in $issued) { "  issued     $path" }
foreach ($path in $refused) { "  refused    $path" }
if ($refused.Count -gt 0) { exit 1 }
exit 0
