# PHASE 3.75 모델 자산 빅뱅 전환 — 변경 동결 래칫 (MBC0)
#
# ModelAssetBigBangCutoverPlan §5.2·§8.2가 "제품에서 제거할 것"으로 적은 legacy·experiment
# 표면의 **코드 접촉 수**를 정적으로 잰다. cutover 동안 이 표면은 늘어나면 안 된다 —
# MBC9/MBC10이 0으로 내릴 때까지는 단조 감소만 허용한다.
#
# ★ 왜 정적·래칫인가. 동결의 위반은 그림을 바꾸지 않는다(폴백을 한 겹 더 붙이면 오히려
#   그림이 "고쳐진" 것처럼 보인다 — 2026-09-02 MeshRenderer 순서 해킹·[material.finalize]
#   무조건 출력이 그랬고, 계획서 §1이 그것을 목표가 아니라고 못 박았다). 그래서 축은
#   픽셀이 아니라 "접촉 수가 늘지 않는다"와 "sidecar writer가 새로 생기지 않는다"다.
#
# ★ 주석은 세지 않는다. 재는 것은 코드 접촉인데 원문 그대로 세면 은퇴를 설명하는 주석
#   한 줄이 래칫을 거꾸로 올린다(verify-legacy-skeleton-retirement가 같은 함정을 밟았다).
#
# 사용:
#   pwsh Tools/regression/verify-mbc-cutover-freeze.ps1            # 기준선과 대조
#   pwsh Tools/regression/verify-mbc-cutover-freeze.ps1 -Baseline  # 기준선을 다시 뜬다
#
# 기준선 갱신은 **접촉 수를 내린 슬라이스**가 자기 커밋에서 한다. 올리는 갱신은 없다.
param(
    [switch]$Baseline
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$baselinePath = Join-Path $PSScriptRoot 'mbc_cutover_freeze.baseline.tsv'

# 제품 범위. RenderTests(검사 코드)와 ThirdParty·산출물은 뺀다 — 검사 코드는 legacy 표기를
# **거부하는 단정**에 그 이름을 적어야 하므로 여기 들어오면 래칫이 검사를 벌한다.
$scanRoots = @('Engine', 'Editor', 'Tools', 'Player')
$excludedSegments = @('ThirdParty', 'Build', 'Bin', 'x64', 'vcpkg_installed', 'RenderTests',
    '.git', 'Artifacts', 'output', 'tmp')
$codeExtensions = @('.h', '.hpp', '.cpp', '.inl')

function Test-IsExcluded([string]$fullPath) {
    $relative = [IO.Path]::GetRelativePath($repoRoot, $fullPath)
    foreach ($segment in ($relative -split '[\\/]')) {
        if ($excludedSegments -contains $segment) { return $true }
    }
    return $false
}

function Get-CodeText([string]$path) {
    $body = [IO.File]::ReadAllText($path)
    # 블록 주석 먼저, 그 다음 줄 주석. 순서를 바꾸면 블록 안의 `//`가 남는다.
    $body = [regex]::Replace($body, '(?s)/\*.*?\*/', ' ')
    return [regex]::Replace($body, '(?m)//.*$', ' ')
}

$files = @()
foreach ($root in $scanRoots) {
    $rootPath = Join-Path $repoRoot $root
    if (-not (Test-Path -LiteralPath $rootPath)) { continue }
    $files += @(Get-ChildItem -LiteralPath $rootPath -Recurse -File |
        Where-Object { $codeExtensions -contains $_.Extension.ToLowerInvariant() } |
        Where-Object { -not (Test-IsExcluded $_.FullName) })
}
if ($files.Count -eq 0) { '스캔 대상 소스가 0개다 — 경로가 잘못됐다.'; exit 1 }

# ── 동결 표면 (§5.2 / §8.2) ────────────────────────────────────────────────────
# 이름 → 정규식. 접촉 수 = 주석 제거 뒤 매치 수. 래칫: 기준선 이하만 통과.
$surfaces = [ordered]@{
    'legacy-bridge.BuildLegacyModelFromExperiment' = 'BuildLegacyModelFromExperiment'
    'ab-switch.CREATOR_EXPERIMENT_VERTEX'          = 'CREATOR_EXPERIMENT_VERTEX'
    'dual-state.m_experimentMeshBindings'          = 'm_experimentMeshBindings'
    'legacy-binding.m_hashingMesh'                 = 'm_hashingMesh'
    'assimp.include'                               = '#\s*include\s*[<"]assimp/'
    'pseudo-v5.DeterministicSubAssetId'            = 'DeterministicSubAssetId'
    'pseudo-v5.Uuid::FromName-call'                = 'Uuid::FromName\s*\('
    'legacy-bridge.ModelSceneBridge'               = 'ModelSceneBridge'
    'legacy-fallback.LoadModelViaExperiment'       = 'LoadModelViaExperiment'
    'diag.material.finalize'                       = '\[material\.finalize\]'
    'diag.mesh.resolve'                            = '\[mesh\.resolve\]'
    'diag.model.instantiate'                       = '\[model\.instantiate\]'
    'diag.anim.tick'                               = '\[anim\.tick\]'
    'diag.experiment.animlive'                     = 'experiment\.animlive'
}

$counts = [ordered]@{}
$filesBySurface = @{}
foreach ($name in $surfaces.Keys) { $counts[$name] = 0; $filesBySurface[$name] = @() }

# ── 하드 계약(래칫이 아니라 0/허용목록) ──────────────────────────────────────
# ① model sidecar writer는 허용목록 밖에 생기지 않는다. MBC3가 ModelAssetAuthoringTransaction
#    하나로 합칠 때까지 현존 둘만 허용한다. 셋째가 생기면 즉시 실패.
$sidecarWriterPattern = '\["(subAssets|ModelImporter)"\]\s*(\[|=)'
$sidecarWriterAllow = @(
    'Editor\EngineEntry\EditorAssetDatabase.cpp',
    'Tools\AssetCooker\ModelIdentityRefresher.cpp'
)
# ② 검사 전용 seam은 제품 코드에서 0건. 정의(Assets/)는 제외한다.
$testSeamPattern = 'DeriveIdentityWithProfile|InsertUncheckedForTest'
$testSeamAllowPrefix = 'Engine\RenderEngine\Assets\'
# ③ 새 계층이 legacy GUID를 namespace/key로 흘리지 않는다 — Assets/ 안에 FromName 0건.
$assetsLayerForbidden = 'Uuid::FromName|IsAssetIdV4|CreateRandomV4'

$hardFailures = New-Object System.Collections.Generic.List[string]
$sidecarWriters = New-Object System.Collections.Generic.List[string]

foreach ($file in $files) {
    $code = Get-CodeText $file.FullName
    $relative = [IO.Path]::GetRelativePath($repoRoot, $file.FullName)

    foreach ($name in $surfaces.Keys) {
        $hits = ([regex]::Matches($code, $surfaces[$name])).Count
        if ($hits -gt 0) {
            $counts[$name] += $hits
            $filesBySurface[$name] += "$relative($hits)"
        }
    }

    if ([regex]::IsMatch($code, $sidecarWriterPattern)) {
        $sidecarWriters.Add($relative)
        if ($sidecarWriterAllow -notcontains $relative) {
            $hardFailures.Add("model sidecar writer가 허용목록 밖에 있다: $relative")
        }
    }
    if (-not $relative.StartsWith($testSeamAllowPrefix, [StringComparison]::OrdinalIgnoreCase)) {
        $seamHits = ([regex]::Matches($code, $testSeamPattern)).Count
        if ($seamHits -gt 0) {
            $hardFailures.Add("검사 전용 seam이 제품 코드에 있다($seamHits): $relative")
        }
    }
    if ($relative.StartsWith($testSeamAllowPrefix, [StringComparison]::OrdinalIgnoreCase)) {
        $forbidden = ([regex]::Matches($code, $assetsLayerForbidden)).Count
        if ($forbidden -gt 0) {
            $hardFailures.Add("Assets/ 계층이 legacy 신원 API를 부른다($forbidden): $relative")
        }
    }
}

# ④ Assimp 제품 의존은 vcpkg.json에 있다(현재 1) — 래칫 표면으로 같이 센다.
$vcpkgJson = Join-Path $repoRoot 'vcpkg.json'
$assimpPorts = 0
if (Test-Path -LiteralPath $vcpkgJson) {
    $assimpPorts = ([regex]::Matches((Get-Content -LiteralPath $vcpkgJson -Raw),
        '"name"\s*:\s*"assimp"')).Count
}
$counts['assimp.vcpkg-port'] = $assimpPorts
$filesBySurface['assimp.vcpkg-port'] = @("vcpkg.json($assimpPorts)")

# ── 출력·판정 ──────────────────────────────────────────────────────────────────
"scanned files=$($files.Count) sidecarWriters=$($sidecarWriters -join ',')"

if ($Baseline) {
    $lines = @("surface`tcount`tfiles")
    foreach ($name in $counts.Keys) {
        $lines += "$name`t$($counts[$name])`t$($filesBySurface[$name] -join ' ')"
    }
    [IO.File]::WriteAllText($baselinePath, (($lines -join "`n") + "`n"),
        (New-Object Text.UTF8Encoding($false)))
    "기준선을 썼다: $baselinePath"
    foreach ($name in $counts.Keys) { "  {0,-48} {1,4}" -f $name, $counts[$name] }
    if ($hardFailures.Count -gt 0) {
        '하드 계약 위반(기준선을 떠도 이것은 통과가 아니다):'
        $hardFailures | ForEach-Object { "  $_" }
        exit 1
    }
    exit 0
}

if (-not (Test-Path -LiteralPath $baselinePath)) {
    "기준선이 없다: $baselinePath (-Baseline으로 먼저 뜬다)"
    exit 1
}
$baselineCounts = @{}
foreach ($line in (Get-Content -LiteralPath $baselinePath | Select-Object -Skip 1)) {
    if ([string]::IsNullOrWhiteSpace($line)) { continue }
    $parts = $line -split "`t"
    $baselineCounts[$parts[0]] = [int]$parts[1]
}

$failures = New-Object System.Collections.Generic.List[string]
$lowered = New-Object System.Collections.Generic.List[string]
foreach ($name in $counts.Keys) {
    if (-not $baselineCounts.ContainsKey($name)) {
        $failures.Add("기준선에 없는 표면(스크립트와 기준선이 어긋남): $name")
        continue
    }
    $now = $counts[$name]
    $was = $baselineCounts[$name]
    $mark = if ($now -gt $was) { '↑ 위반' } elseif ($now -lt $was) { '↓ 감소' } else { '=' }
    "  {0,-48} {1,4} / 기준 {2,4}  {3}" -f $name, $now, $was, $mark
    if ($now -gt $was) {
        $failures.Add("$name : $was → $now  [" + ($filesBySurface[$name] -join ' ') + ']')
    } elseif ($now -lt $was) {
        $lowered.Add("$name : $was → $now")
    }
}
foreach ($name in $baselineCounts.Keys) {
    if (-not $counts.Contains($name)) {
        $failures.Add("스크립트에 없는 기준선 표면: $name")
    }
}

if ($lowered.Count -gt 0) {
    '접촉이 줄었다 — 이 슬라이스 커밋에서 -Baseline으로 래칫을 내려 고정하라:'
    $lowered | ForEach-Object { "  $_" }
}
if ($hardFailures.Count -gt 0 -or $failures.Count -gt 0) {
    if ($hardFailures.Count -gt 0) { '하드 계약 위반:'; $hardFailures | ForEach-Object { "  $_" } }
    if ($failures.Count -gt 0) { '래칫 위반(접촉 증가):'; $failures | ForEach-Object { "  $_" } }
    exit 1
}
'통과 — cutover 동결 표면이 늘지 않았고 sidecar writer는 허용목록 안에 있다'
exit 0
