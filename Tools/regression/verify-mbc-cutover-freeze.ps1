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
}
# MBC10 — 아래 표면은 래칫이 아니라 **하드 0**이다. 기준선이 0이어도 "증가만 막는"
# 규칙은 0→1을 잡지만, 표면 이름이 바뀌거나 기준선을 다시 뜨면 통과로 새기 때문에
# 별도 계약으로 못 박는다(§5.2 제거 목록·§8.2 구조).
$hardZeroSurfaces = @(
    'legacy-bridge.BuildLegacyModelFromExperiment', 'ab-switch.CREATOR_EXPERIMENT_VERTEX',
    'dual-state.m_experimentMeshBindings', 'assimp.include', 'pseudo-v5.DeterministicSubAssetId',
    'pseudo-v5.Uuid::FromName-call', 'legacy-bridge.ModelSceneBridge',
    'legacy-fallback.LoadModelViaExperiment', 'diag.material.finalize', 'diag.mesh.resolve',
    'diag.model.instantiate', 'diag.anim.tick')
# ⑤ m_hashingMesh는 모델이 아닌 절차 지오메트리(스프라이트 쿼드·지형·기즈모)의 legacy
#    Mesh 캐시 키로만 남는다 — 아래 파일 밖에서 나타나면 모델 경로로 되돌아온 것이다.
$hashingMeshAllow = @(
    'Engine\RenderEngine\Mesh.h', 'Engine\RenderEngine\Mesh.cpp', 'Engine\RenderEngine\Texture.h',
    'Engine\RenderEngine\Render\Graph\EnhancedDrawIdentity.h', 'Engine\RenderEngine\Render\Graph\EnhancedRenderPass.h',
    'Engine\RenderEngine\Render\Scene\EnhancedSceneRenderer.cpp', 'Engine\RenderEngine\RHI\IRenderDeviceServices.h',
    'Engine\RenderEngine\RHI\DX12\DX12MeshCache.h', 'Engine\RenderEngine\RHI\DX12\DX12MeshCache.cpp',
    'Engine\RenderEngine\RHI\Vulkan\VulkanRenderServices.cpp')
# ⑥ 제품 소비자는 ModelAssetGeneration handle만 쓴다 — experiment 모델 객체를 직접 소비하지 않는다.
$productConsumerPrefixes = @('Engine\SceneRuntime\', 'Engine\RenderEngine\Render\',
    'Engine\RenderEngine\PrimitiveRenderProxy', 'Editor\EngineGUIWindow\')
$experimentConsumerPattern = 'experiment::Model\b|->TryGetMesh\(|\.TryGetMesh\('
# ⑦ generation 게시 진입점은 DataSystem 하나다(부분 게시 경로 0).
$publishPattern = 'm_modelAssetGenerations\.Publish\('

$counts = [ordered]@{}
$filesBySurface = @{}
foreach ($name in $surfaces.Keys) { $counts[$name] = 0; $filesBySurface[$name] = @() }

# ── 하드 계약(래칫이 아니라 0/허용목록) ──────────────────────────────────────
# ① model sidecar writer는 codec/transaction 밖에 생기지 않는다. Editor와
#    AssetCooker는 transaction을 호출할 뿐 직접 문서를 쓰지 않는다.
$sidecarWriterPattern = '\["(subAssets|ModelImporter)"\]\s*(\[|=)'
$sidecarWriterAllow = @(
    'Engine\RenderEngine\Assets\ModelSidecarV2.cpp',
    'Engine\RenderEngine\Assets\ModelAssetAuthoringTransaction.cpp'
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

    if ($relative -notin $hashingMeshAllow) {
        $hashingHits = ([regex]::Matches($code, 'm_hashingMesh')).Count
        if ($hashingHits -gt 0) {
            $hardFailures.Add("m_hashingMesh가 절차 지오메트리 허용목록 밖에 있다($hashingHits): $relative")
        }
    }
    foreach ($prefix in $productConsumerPrefixes) {
        if ($relative.StartsWith($prefix, [StringComparison]::OrdinalIgnoreCase)) {
            $expHits = ([regex]::Matches($code, $experimentConsumerPattern)).Count
            if ($expHits -gt 0) {
                $hardFailures.Add("제품 소비자가 experiment 모델 객체를 직접 소비한다($expHits): $relative")
            }
            break
        }
    }
    if ($relative -ne 'Engine\RenderEngine\DataSystem.cpp') {
        $publishHits = ([regex]::Matches($code, $publishPattern)).Count
        if ($publishHits -gt 0) {
            $hardFailures.Add("generation 게시 진입점이 DataSystem 밖에 있다($publishHits): $relative")
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
if ($assimpPorts -ne 0) { $hardFailures.Add("vcpkg.json에 assimp port가 있다($assimpPorts)") }
foreach ($name in $hardZeroSurfaces) {
    if ($counts[$name] -ne 0) {
        $hardFailures.Add("하드 0 표면에 접촉이 있다: $name = $($counts[$name]) [" + ($filesBySurface[$name] -join ' ') + ']')
    }
}
# ⑧ 상태를 바꾸는 진단 명령 0 — experiment.animlive는 제품 publish를 부르지 않는다
#    (읽기 전용 스냅샷만 읽는다). scene.transformbulk probe는 합성 fixture라 예외.
$consolePath = Join-Path $repoRoot 'Editor\EngineEntry\ConsoleCommandSystem.cpp'
if (Test-Path -LiteralPath $consolePath) {
    $console = Get-CodeText $consolePath
    $animliveStart = $console.IndexOf('static void Cmd_experiment_animlive(')
    if ($animliveStart -lt 0) { $hardFailures.Add('experiment.animlive 진단이 없다(시그니처 변경?)') }
    else {
        $open = $console.IndexOf('{', $animliveStart); $depth = 0; $end = -1
        for ($i = $open; $i -lt $console.Length; $i++) {
            if ($console[$i] -eq '{') { $depth++ } elseif ($console[$i] -eq '}') { $depth--; if ($depth -eq 0) { $end = $i; break } }
        }
        $body = if ($end -gt 0) { $console.Substring($open, $end - $open + 1) } else { '' }
        if ($body -match 'PublishAnimatorPose\(' -or $body -notmatch 'TryGetLastAnimatorPoseMetrics\(') {
            $hardFailures.Add('experiment.animlive가 상태를 바꾼다(PublishAnimatorPose) 또는 읽기 전용 스냅샷을 읽지 않는다')
        }
    }
    if ($console -notmatch 'Cmd_assets_modeldiag') {
        $hardFailures.Add('읽기 전용 모델 소비 스냅샷 명령(assets.modeldiag)이 없다')
    }
}

# ── 출력·판정 ──────────────────────────────────────────────────────────────────
"scanned files=$($files.Count) sidecarWriters=$($sidecarWriters -join ',')"

if ($Baseline) {
    $lines = @("surface`tcount`tfiles")
    foreach ($name in $counts.Keys) {
        $fileList = $filesBySurface[$name] -join ' '
        $lines += if ([string]::IsNullOrEmpty($fileList)) {
            "$name`t$($counts[$name])"
        } else {
            "$name`t$($counts[$name])`t$fileList"
        }
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
'통과 — cutover 동결 표면이 늘지 않았고(제거 표면 하드 0), sidecar writer·generation 게시·m_hashingMesh가 허용목록 안이며 진단은 읽기 전용이다'
exit 0
