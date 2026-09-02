# PHASE 3.75 MBC0 — 모델 corpus·sidecar·저장 참조 기준선 export
#
# cutover 전 상태를 기계가 읽을 수 있는 한 파일로 굳힌다. MBC4(전 corpus 재발급·참조
# rewrite)와 MBC11(완료 판정)이 "무엇이 있었고 무엇이 무엇을 가리켰나"를 이 파일과
# 대조한다. 기존 GUID를 **입력 위치 찾기**에 쓰는 유일하게 허용된 용도(§5.2)가 이것이다.
#
# 기록하는 것:
#   models[]      source 경로·tracked 여부·크기·SHA-256·sidecar guid·schema·subasset 목록
#   references[]  .creator/.prefab/.asset 마다 등장한 GUID와 그 분류
#                 (model / model-subasset / other-meta / nil / unresolved)
#   summary       건수·미해석 GUID 목록
#
# ★ Dynamic_CPP/Assets의 대부분은 gitignore라(Gunner·SU sidecar, 씬 13개) 이 파일은
#   **이 기계의 로컬 상태**를 적는다. 그래서 기준선을 한 번 떠서 커밋하고 다시 뜨지 않는다.
param(
    [string]$Out = (Join-Path $PSScriptRoot 'mbc0_corpus_baseline.json')
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$root = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$assets = Join-Path $root 'Dynamic_CPP'
$tracked = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
foreach ($p in @(& git -C $root ls-files -- 'Dynamic_CPP')) { [void]$tracked.Add(($p -replace '/', '\')) }
$head = (& git -C $root rev-parse --short HEAD).Trim()

$uuidRegex = '[0-9a-fA-F]{8}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{12}'
# GUID 앞의 YAML 키까지 잡는다. 키가 분류를 가른다 — m_typeUUID/m_FileID는 타입·인스턴스
# 식별자라 자산 참조가 아니고, 그것을 "미해석"으로 세면 진짜 고아 참조(예: Test1의
# m_textureGuid 1건)가 400여 건의 소음에 묻힌다(첫 실행에서 실제로 그랬다).
$keyedUuidRegex = '(?m)([A-Za-z_][A-Za-z0-9_]*)\s*:\s*(' + $uuidRegex + ')'
$identityKeys = @('m_typeUUID', 'm_FileID')
$canonicalV4 = '^[0-9a-f]{8}-[0-9a-f]{4}-4[0-9a-f]{3}-[89ab][0-9a-f]{3}-[0-9a-f]{12}$'

function Get-Rel([string]$full) { return [IO.Path]::GetRelativePath($root, $full) }
function Get-MetaGuid([string]$metaPath) {
    $m = Select-String -LiteralPath $metaPath -Pattern '^guid:\s*(\S+)\s*$' | Select-Object -First 1
    if ($m) { return $m.Matches[0].Groups[1].Value } else { return $null }
}

# ── 모든 .meta GUID 색인(분류용) ──────────────────────────────────────────────
$metaByGuid = @{}
foreach ($meta in Get-ChildItem -LiteralPath $assets -Recurse -File -Filter '*.meta') {
    $g = Get-MetaGuid $meta.FullName
    if ($g) { $metaByGuid[$g.ToLowerInvariant()] = Get-Rel $meta.FullName }
}

# ── 모델 corpus ───────────────────────────────────────────────────────────────
$models = @()
$modelGuids = @{}
$subassetGuids = @{}
$sources = Get-ChildItem -LiteralPath $assets -Recurse -File | Where-Object {
    $_.Extension.ToLowerInvariant() -in @('.glb', '.gltf', '.fbx')
} | Sort-Object FullName
foreach ($src in $sources) {
    $rel = Get-Rel $src.FullName
    $metaPath = $src.FullName + '.meta'
    $entry = [ordered]@{
        source          = $rel
        tracked         = $tracked.Contains($rel)
        sizeBytes       = $src.Length
        sha256          = (Get-FileHash -LiteralPath $src.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
        sidecar         = (Test-Path -LiteralPath $metaPath -PathType Leaf)
        sidecarTracked  = $tracked.Contains($rel + '.meta')
        guid            = $null
        guidCanonicalV4 = $false
        subAssetsSchema = $null
        materials       = @()
        embeddedTextures = @()
    }
    if ($entry.sidecar) {
        $text = Get-Content -LiteralPath $metaPath -Raw
        $entry.guid = Get-MetaGuid $metaPath
        if ($entry.guid) {
            $entry.guidCanonicalV4 = $entry.guid -cmatch $canonicalV4
            $modelGuids[$entry.guid.ToLowerInvariant()] = $rel
        }
        $schema = [regex]::Match($text, '(?m)^subAssets:\s*\n\s+schemaVersion:\s*(\d+)')
        if ($schema.Success) { $entry.subAssetsSchema = [int]$schema.Groups[1].Value }
        foreach ($section in @('materials', 'embeddedTextures')) {
            $block = [regex]::Match($text, "(?ms)^  ${section}:\s*\n(.*?)(?=^  \w|\z)")
            if (-not $block.Success) { continue }
            $items = @()
            foreach ($m in [regex]::Matches($block.Groups[1].Value,
                '(?ms)-\s+key:\s*(\S+)\s*\n(?:\s+name:\s*(.*?)\s*\n)?\s+guid:\s*(\S+)')) {
                $items += [ordered]@{ key = $m.Groups[1].Value; name = $m.Groups[2].Value; guid = $m.Groups[3].Value }
                $subassetGuids[$m.Groups[3].Value.ToLowerInvariant()] = "$rel::$section/$($m.Groups[1].Value)"
            }
            $entry[$section] = $items
        }
    }
    $models += [pscustomobject]$entry
}

# ── 저장 참조 ─────────────────────────────────────────────────────────────────
$references = @()
$unresolved = [Collections.Generic.SortedSet[string]]::new()
$refFiles = Get-ChildItem -LiteralPath $assets -Recurse -File | Where-Object {
    $_.Extension.ToLowerInvariant() -in @('.creator', '.prefab', '.asset')
} | Sort-Object FullName
$binaryCachesSkipped = @()
foreach ($f in $refFiles) {
    $rel = Get-Rel $f.FullName
    # legacy 로더가 모델 옆에 떨구는 이진 쿠킹 캐시(`CEMA` 매직)도 `.asset`이다. 저작
    # 재질 YAML과 확장자가 같아 여기 섞이면 GUID 문자열이 참조로 잡힌다(첫 기준선
    # 뒤 bench 실행이 14개를 만들어 modelRefs가 8→73으로 부풀었다). 매직으로 가른다.
    $stream = [IO.File]::OpenRead($f.FullName)
    try { $magic = New-Object byte[] 4; [void]$stream.Read($magic, 0, 4) } finally { $stream.Dispose() }
    if ([Text.Encoding]::ASCII.GetString($magic) -eq 'CEMA') { $binaryCachesSkipped += $rel; continue }
    $text = Get-Content -LiteralPath $f.FullName -Raw
    $counts = [ordered]@{ model = 0; 'model-subasset' = 0; 'other-meta' = 0; 'type-or-instance' = 0; nil = 0; unresolved = 0 }
    $modelRefs = [Collections.Generic.SortedSet[string]]::new()
    $subRefs = [Collections.Generic.SortedSet[string]]::new()
    $unresolvedHere = [Collections.Generic.SortedSet[string]]::new()
    $keysSeen = [Collections.Generic.SortedSet[string]]::new()
    foreach ($m in [regex]::Matches($text, $keyedUuidRegex)) {
        $key = $m.Groups[1].Value
        $g = $m.Groups[2].Value.ToLowerInvariant()
        [void]$keysSeen.Add($key)
        if ($identityKeys -contains $key) { $counts['type-or-instance']++; continue }
        if ($g -eq '00000000-0000-0000-0000-000000000000') { $counts.nil++; continue }
        if ($modelGuids.ContainsKey($g)) { $counts.model++; [void]$modelRefs.Add("$key=$g"); continue }
        if ($subassetGuids.ContainsKey($g)) { $counts['model-subasset']++; [void]$subRefs.Add("$key=$g"); continue }
        if ($metaByGuid.ContainsKey($g)) { $counts['other-meta']++; continue }
        $counts.unresolved++; [void]$unresolvedHere.Add("$key=$g"); [void]$unresolved.Add("$rel $key=$g")
    }
    $references += [pscustomobject][ordered]@{
        file = $rel; tracked = $tracked.Contains($rel)
        counts = [pscustomobject]$counts
        keys = @($keysSeen)
        modelGuids = @($modelRefs); subassetGuids = @($subRefs); unresolvedGuids = @($unresolvedHere)
    }
}

$summary = [ordered]@{
    models                 = $models.Count
    modelsTracked          = @($models | Where-Object tracked).Count
    sidecarsWithSubAssets  = @($models | Where-Object { $null -ne $_.subAssetsSchema }).Count
    materials              = ($models | ForEach-Object { $_.materials.Count } | Measure-Object -Sum).Sum
    embeddedTextures       = ($models | ForEach-Object { $_.embeddedTextures.Count } | Measure-Object -Sum).Sum
    referenceFiles         = $references.Count
    binaryCachesSkipped    = @($binaryCachesSkipped)
    modelReferences        = ($references | ForEach-Object { $_.counts.model } | Measure-Object -Sum).Sum
    subassetReferences     = ($references | ForEach-Object { $_.counts.'model-subasset' } | Measure-Object -Sum).Sum
    unresolvedReferences   = ($references | ForEach-Object { $_.counts.unresolved } | Measure-Object -Sum).Sum
    unresolved             = @($unresolved)
}

$doc = [ordered]@{
    schema      = 'mbc0-corpus-baseline/1'
    generatedAt = (Get-Date).ToString('s')
    gitHead     = $head
    machine     = $env:COMPUTERNAME
    summary     = [pscustomobject]$summary
    models      = $models
    references  = $references
}
$json = $doc | ConvertTo-Json -Depth 8
[IO.File]::WriteAllText($Out, $json + "`n", (New-Object Text.UTF8Encoding($false)))

"MBC0 corpus baseline → $Out"
"models=$($summary.models) (tracked $($summary.modelsTracked)) sidecarsWithSubAssets=$($summary.sidecarsWithSubAssets) materials=$($summary.materials) embeddedTextures=$($summary.embeddedTextures)"
"referenceFiles=$($summary.referenceFiles) modelRefs=$($summary.modelReferences) subassetRefs=$($summary.subassetReferences) unresolved=$($summary.unresolvedReferences)"
if ($summary.unresolvedReferences -gt 0) { '미해석 GUID:'; $summary.unresolved | ForEach-Object { "  $_" } }
exit 0
