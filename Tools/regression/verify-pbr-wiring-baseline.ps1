# PHASE 4 W0/W2~W8: product-frame capture, shared PBR, backend defaults and seal identity.
# Controlled DX12 captures gate repeatability; analytic fixtures gate material correctness.
#
# ── 2026-09-15 판정 범위 결정 (사용자) ──
#
#   **PHASE 4 의 판정은 DX12 백엔드로만 한다.** Vulkan 과의 1:1 픽셀 대응은 PHASE 4.9
#   (BackendParityPlan) 로 넘겼다. 이유는 진도다 — 교차 백엔드 판정은 시각 고정
#   수단이 없어 성립하지 않는 질문이었고(§15), vulkan 쪽 별건 결함(기동 창의
#   `gCubeMap`)이 PHASE 4 와 무관하게 게이트를 끝까지 못 가게 막고 있었다.
#
#   ★ **구현과 RHI 계약은 그대로 양 백엔드다.** 미룬 것은 *판정*이지 *배선*이 아니다.
#     새 축을 더할 때는 여전히 중립 어휘(RHI enum)로 더하고 두 백엔드 변환표를
#     모두 채운다 — 어휘에 구멍을 내면 백엔드 비대칭이 생기고, 그것은 PHASE 4.9 에서
#     갚을 빚이 된다.
#
#   ★★ `vk.*` 도 끈다. 그 이름이 범위를 속인다 — `vk.shadow/gbuffer/forward/deferred`
#     는 Vulkan 자가 검증이 아니라 **DX12/Vulkan 대조**라, 끄면 DX12 팔도 함께 꺼진다.
#     DX12는 `dx12.gbuffer`·`dx12.forwardshade`·`dx12.iblshade` 및 실제 제품
#     AO/emission 픽셀로 판정한다. Vulkan 교차 축의 소유권은 BackendParityPlan이다.
#
#   그래서 기본값을 dx12 하나로 두되, vulkan 은 **조용히 사라지지 않는다** — 축 회계에
#   `deferred` 로 이름이 남고 요약이 그것을 부른다. `-Backend dx12,vulkan` 으로
#   언제든 되돌려 잴 수 있다.
param(
    [string]$Editor = (Join-Path $PSScriptRoot '..\..\Bin\x64-Debug\Editor\CreatorEditor.exe'),
    [string]$Work = $env:TEMP,
    [ValidateSet('dx12', 'vulkan')][string[]]$Backend = @('dx12'),
    # `vk.shadow/gbuffer/forward/deferred` 는 이름과 달리 **DX12/Vulkan 대조** 테스트다.
    # 2026-09-15 결정으로 기본은 끔이고, 이 스위치로 되돌린다.
    [switch]$IncludeVulkanSelfTest,
    # Focused revalidation after a capture/fixture change; never reported as the full gate.
    [switch]$ProductOnly,
    [ValidateSet('Debug', 'Release')][string]$Configuration = 'Debug',
    [int]$TimeoutSeconds = 240
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'CommandResults.ps1')
. (Join-Path $PSScriptRoot 'PbrProductPixels.ps1')
$root = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
if (-not $PSBoundParameters.ContainsKey('Editor')) {
    $Editor = Join-Path $root "Bin/x64-$Configuration/Editor/CreatorEditor.exe"
}
$run = Join-Path ([IO.Path]::GetFullPath($Work)) ('creator-pbr-' + [guid]::NewGuid().ToString('N'))
$settings = Join-Path $root 'Dynamic_CPP\ProjectSetting\EngineSettings.asset'
$utf8 = [Text.UTF8Encoding]::new($false)
$original = $null
$process = $null
# W1 fixture 가 model.load 로 복사되어 앉는 자리. finally 가 반드시 치운다 —
# 게이트가 자산 트리에 잔해를 남기면 다음 실행의 전제가 달라진다.
$importedFixtures = @(
    (Join-Path $root 'Dynamic_CPP\Assets\Models\NormalPair'),
    (Join-Path $root 'Dynamic_CPP\Assets\Models\AlphaModes'),
    (Join-Path $root 'Dynamic_CPP\Assets\Models\SamplerModes'),
    (Join-Path $root 'Dynamic_CPP\Assets\Models\SharedMaterial'),
    (Join-Path $root 'Dynamic_CPP\Assets\Models\Lighting'))
# Only these exact generated import directories may be removed. Never consume user assets.
foreach ($imported in $importedFixtures) {
    $absolute = [IO.Path]::GetFullPath($imported)
    $modelRoot = [IO.Path]::GetFullPath((Join-Path $root 'Dynamic_CPP/Assets/Models')) + [IO.Path]::DirectorySeparatorChar
    if (-not $absolute.StartsWith($modelRoot, [StringComparison]::OrdinalIgnoreCase)) { throw 'Fixture path escaped project models' }
    if (Test-Path -LiteralPath $absolute) { throw "Fixture target already exists; preserve it: $absolute" }
}

# ── 축 회계 ─────────────────────────────────────────────────────────────
#
# ★ 이 게이트의 요구는 "fixture 를 추적한다" 가 아니라 **"fixture 가 없는 축을
#   절대 PASS 로 보고하지 않는다"** 다. 둘은 다르다 — 추적은 그것을 보장하는 한
#   방법일 뿐이고, 값이 큰 외부 자산(라이선스 의무가 딸린 것 포함)까지 저장소에
#   넣는 것은 이 게이트가 CI 에서 돌지도 않는 마당에 나쁜 거래다.
#
# ★ 실제로 여기 구멍이 있었다. Gunner 캡처는 `Dynamic_CPP/Assets/Models/
#   Gunner_F_Mythic.glb` 를 열었는데 그 파일은 `.gitignore` 에 막혀 **추적 밖**이다
#   — 이 기계 말고 다른 기여자 기계에서는 그 자리에서 죽거나, 더 나쁘게는 무엇도
#   재지 못한 채 지나간다. 저장소에는 기여자가 다섯이다.
#
# 그래서 축마다 "돌았음/건너뜀(이유)" 을 남기고, 마지막 요약이 **돈 축만** 이름을
# 부른다. 조용히 넘어가는 길을 없애는 것이 목적이지 자산을 늘리는 것이 아니다.
$axisReport = [ordered]@{}
function Set-AxisRan([string]$Name) { $script:axisReport[$Name] = 'ran' }
function Set-AxisSkipped([string]$Name, [string]$Reason) {
    $script:axisReport[$Name] = "skipped: $Reason"
}
# ★ `skipped` 와 `deferred` 를 가른다. skipped 는 **이번 기계에 조건이 없어서**
#   못 잰 것이고(예: 추적 밖 Gunner 자산), deferred 는 **계획이 다른 자리로 옮긴 것**
#   이다. 둘을 한 통에 넣으면 "언젠가 고치면 다시 도는 축" 과 "다른 계획이 소유한 축"
#   이 구분되지 않아, 뒷사람이 없는 결함을 찾아 헤매게 된다.
function Set-AxisDeferred([string]$Name, [string]$Owner) {
    $script:axisReport[$Name] = "deferred: $Owner"
}

# 2026-09-15 결정 — 판정은 DX12 로만. 끈 축은 회계에 **맨 앞에서** 등록한다.
# ★ 처음에는 해당 블록에 닿았을 때 등록했는데, 게이트가 그 앞에서 죽으면 미룬 축이
#   통째로 사라졌다. 미룬 축은 인자만 보면 알 수 있으므로 실행과 무관하게 먼저 적는다.
if ($Backend -notcontains 'vulkan') {
    Set-AxisDeferred 'vulkan/제품 캡처' 'PHASE 4.9 BackendParityPlan (2026-09-15 결정) · -Backend dx12,vulkan 로 되돌려 잰다'
}
if (-not $IncludeVulkanSelfTest) {
    Set-AxisDeferred 'vk/* 백엔드 대조' ('PHASE 4.9 BackendParityPlan · -IncludeVulkanSelfTest 로 되돌린다 · ' +
        'DX12는 dx12.gbuffer·dx12.forwardshade·dx12.iblshade 및 제품 AO/emission 픽셀로 별도 판정')
}

function Invoke-Editor([string]$Name, [string[]]$Commands, [int]$ExpectedExit = 0) {
    $scenario = Join-Path $run "$Name.txt"
    [IO.File]::WriteAllText($scenario, ($Commands -join "`n") + "`n", $utf8)
    $start = [Diagnostics.ProcessStartInfo]::new()
    $start.FileName = (Resolve-Path -LiteralPath $Editor).Path
    $start.ArgumentList.Add('--commandlet-script')
    $start.ArgumentList.Add($scenario)
    $resultPath = Join-Path $run "$Name.results.jsonl"
    $start.ArgumentList.Add('--result-file')
    $start.ArgumentList.Add($resultPath)
    $start.WorkingDirectory = $root
    $start.UseShellExecute = $false
    $start.CreateNoWindow = $true
    $start.WindowStyle = [Diagnostics.ProcessWindowStyle]::Hidden
    $start.RedirectStandardOutput = $true
    $start.RedirectStandardError = $true
    $start.StandardOutputEncoding = $utf8
    $start.StandardErrorEncoding = $utf8
    $script:process = [Diagnostics.Process]::Start($start)
    $stdoutTask = $process.StandardOutput.ReadToEndAsync()
    $stderrTask = $process.StandardError.ReadToEndAsync()
    $finished = $process.WaitForExit($TimeoutSeconds * 1000)
    if (-not $finished) { $process.Kill(); $process.WaitForExit() }
    $stdout = $stdoutTask.GetAwaiter().GetResult()
    $stderr = $stderrTask.GetAwaiter().GetResult()
    [IO.File]::WriteAllText((Join-Path $run "$Name.stdout.txt"), $stdout, $utf8)
    [IO.File]::WriteAllText((Join-Path $run "$Name.stderr.txt"), $stderr, $utf8)
    $exitCode = $process.ExitCode
    $process.Dispose()
    $script:process = $null
    if (-not $finished) { throw "$Name timed out; artifacts: $run" }
    if ($exitCode -ne $ExpectedExit) { throw "$Name exit=$exitCode expected=$ExpectedExit; artifacts: $run" }
    if (-not [string]::IsNullOrWhiteSpace($stderr)) { throw "$Name wrote stderr; artifacts: $run" }
    $results = @(Read-CommandResults $resultPath)
    if ($results.Count -ne $Commands.Count) { throw "$Name terminal result count mismatch; artifacts: $run" }
    for ($i = 0; $i -lt $Commands.Count; $i++) {
        if ($results[$i].command -ne ($Commands[$i] -split ' ', 2)[0]) {
            throw "$Name terminal result order mismatch at $i"
        }
    }
    if ($ExpectedExit -eq 0 -and @($results | Where-Object status -ne 'succeeded').Count) {
        throw "$Name has unsuccessful command results; artifacts: $run"
    }
    return $results
}

function Assert-AlphaModes([string]$Directory) {
    $manifest = Get-Content -LiteralPath (Join-Path $Directory 'manifest.json') -Raw | ConvertFrom-Json
    # ★ OPAQUE / MASK / BLEND 가 서로 다른 coverage 로 나와야 한다. 셋이 같은 값이면
    #   blend mode 를 읽지 않는 회귀이고, 그때 cutoff 단정만 있으면 조용히 통과한다.
    $flags = @($manifest.draws.coverageFlags | Sort-Object -Unique)
    if ($flags.Count -lt 3) {
        throw "Alpha modes did not produce distinct coverage (flags: $($flags -join ', ')): $Directory"
    }
    # ★ BLEND 는 forward 로 간다. 이 단정이 있는 진짜 이유는 alpha 가 아니라 **라우트**다 —
    #   §18 의 draw↔바인딩 조인은 라우트별로 찾는데, 여기 전까지 모든 fixture 의 draw 가
    #   gbuffer 라 forward 장부가 늘 비어 있었다. 두 장부를 합쳐 찾는 잘못된 구현도
    #   통과했을 상태였다. 이 캡처가 그 축을 처음 자극한다.
    $forward = @($manifest.draws | Where-Object route -EQ 'forward')
    if (0 -eq $forward.Count) {
        throw "Alpha fixture produced no forward-route draw (라우트 축이 비었다): $Directory"
    }
}

function Assert-SamplerModes([string]$Directory) {
    $manifest = Get-Content -LiteralPath (Join-Path $Directory 'manifest.json') -Raw | ConvertFrom-Json
    # ★ 여기서 묻는 것은 "샘플러가 재질마다 갈리는가" 다. W7 이전에는 패스가
    #   Initialize 에서 만든 하나를 프레임 내내 모든 draw 에 걸었고, 그래서 이
    #   수가 어느 캡처에서든 **항상 1** 이었다. 게이트는 그 수를 세기만 하고
    #   판정하지 않았다(§18) — 변이를 만들 슬라이스가 W7 이었기 때문이다.
    #
    # ★ 기대값을 3 으로 못 박지 않고 '재질 수와 같다' 로도 적지 않는다. fixture 의
    #   재질 셋이 서로 다른 sampler 를 쓰므로 **2 이상**이면 축이 살아 있고,
    #   정확히 몇인지는 배치 병합 규칙이 정할 몫이다. 다만 1 이면 배선이 끊긴
    #   것이라 그때만 붉어진다.
    $ledger = $manifest.sealLedger
    $identities = @()
    foreach ($route in @('gbuffer', 'forward')) {
        if ($null -ne $ledger.$route -and $null -ne $ledger.$route.bindings) {
            $identities += @($ledger.$route.bindings.samplerIdentity)
        }
    }
    $distinct = @($identities | Where-Object { $_ -ne 0 } | Sort-Object -Unique)
    if ($distinct.Count -lt 2) {
        throw "Sampler fixture produced $($distinct.Count) distinct samplerIdentity — 재질별 sampler 배선이 끊겼다: $Directory"
    }
    # ★ 0 이 섞이면 안 된다. ComputeSamplerIdentity 는 절대 0 을 내지 않으므로
    #   0 은 '적지 않았다'는 뜻이고, 위의 distinct 계산이 그것을 걸러내므로
    #   여기서 따로 물어야 한다 — 안 그러면 일부가 비어도 통과한다.
    if (@($identities | Where-Object { $_ -eq 0 }).Count) {
        throw "Sampler fixture has unstamped samplerIdentity(0) bindings: $Directory"
    }
    # ★ distinct 만 세면 부족하다. 실측이 그것을 가르쳤다 — sampler 가 패스까지
    #   흐르기는 했는데 sealHash 가 sampler 를 안 접어서, 재질 셋이 같은 seal 을
    #   갖고 W8 불변식("같은 seal 은 같은 바인딩")이 옳게 발동해 draw 둘이
    #   버려졌다. 살아남은 하나만 남으니 distinct 는 1 로 보였다. 버려진 수를
    #   함께 물어야 "축이 살았다" 와 "축이 충돌해 죽었다" 가 갈린다.
    foreach ($route in @('gbuffer', 'forward')) {
        $node = $ledger.$route
        if ($null -eq $node) { continue }
        if ($node.bindingConflict -ne 0 -or $node.skipped -ne 0) {
            throw ("Sampler fixture dropped draws (route=$route" +
                " bindingConflict=$($node.bindingConflict) skipped=$($node.skipped)" +
                " reason='$($node.lastReason)'): $Directory")
        }
    }
}

# ★ 같은 이름의 결과가 둘이라 Get-CommandResult 를 못 쓴다(그쪽은 1건을 요구한다).
function Get-AllSucceeded($Results, [string]$Command) {
    $rows = @($Results | Where-Object command -eq $Command)
    if (0 -eq $rows.Count) { throw "No $Command result" }
    foreach ($row in $rows) {
        if ($row.status -ne 'succeeded') {
            throw "$Command status=$($row.status) code=$($row.code): $($row.message)"
        }
    }
    return @($rows | ForEach-Object { $_.data })
}

function Assert-SharedMaterial([string]$Directory, [string]$ModelId) {
    $manifest = Get-Content -LiteralPath (Join-Path $Directory 'manifest.json') -Raw | ConvertFrom-Json
    # 이 fixture 의 draw 만 골라낸다. 캡처는 누적이라(앞 축들이 같은 씬에 남아
    # 있다) 전체 distinct 를 세면 아무 의미가 없다 — modelId 로 못 박는다.
    # 그 값은 material.override 가 돌려준 것이고 게이트가 지어내지 않는다.
    $mine = @($manifest.draws | Where-Object { $_.modelId -eq $ModelId })
    if ($mine.Count -ne 2) {
        throw ("Shared-material fixture produced $($mine.Count) draws (expected 2)" +
            " for modelId=$ModelId : $Directory")
    }
    # ★ 여기서 묻는 것은 "주소가 같은 재질 둘이 값으로 갈리는가" 다.
    #   두 렌더러는 모델 인스턴스화가 붙인 같은 `Material*` 을 갖는다. 밀봉 중복
    #   제거 키가 주소였던 시절이라면 뒤 draw 가 앞 draw 의 스냅샷을 그대로 받아
    #   override 가 통째로 사라진다.
    #
    # ★ 단정을 **bindingConflict 가 아니라 distinct 수**에 건다. 키가 주소로
    #   돌아가면 둘째 draw 는 첫째의 스냅샷을 **그대로 받으므로** 바인딩이 일치해
    #   장부는 침묵한다. PBR-W7 과 정확히 반대 모양이다(그때는 conflict 가 울고
    #   distinct 가 침묵했다) — 미리 정하지 않으면 "게이트가 눈멀었다"로 오진한다.
    $digests = @($mine | ForEach-Object { $_.seal.authoredDigest } | Sort-Object -Unique)
    if ($digests.Count -ne 2) {
        throw ("Shared-material draws share authoredDigest ($($digests -join ',')) —" +
            " 렌더러별 MaterialInstance override 가 밀봉까지 오지 않았다: $Directory")
    }
    $hashes = @($mine | ForEach-Object { $_.seal.hash } | Sort-Object -Unique)
    if ($hashes.Count -ne 2) {
        throw ("Shared-material draws share sealHash ($($hashes -join ',')) —" +
            " 밀봉 중복 제거 키가 값이 아니라 주소로 접혔다: $Directory")
    }
    # 도장이 없으면 위의 두 단정이 '우연히 다른 쓰레기'를 비교한 것일 수 있다.
    if (@($mine | Where-Object { -not $_.seal.stamped }).Count) {
        throw "Shared-material draws are unstamped: $Directory"
    }
    # 값이 갈렸는데 장부가 버렸다면 그것대로 결함이다 — 둘 다 물어야 한다.
    foreach ($route in @('gbuffer', 'forward')) {
        $node = $manifest.sealLedger.$route
        if ($null -eq $node) { continue }
        if ($node.bindingConflict -ne 0 -or $node.skipped -ne 0) {
            throw ("Shared-material fixture dropped draws (route=$route" +
                " bindingConflict=$($node.bindingConflict) skipped=$($node.skipped)" +
                " reason='$($node.lastReason)'): $Directory")
        }
    }
}

function Assert-Capture([string]$Directory, [string]$ExpectedBackend, [string[]]$Models) {
    $manifest = Get-Content -LiteralPath (Join-Path $Directory 'manifest.json') -Raw | ConvertFrom-Json
    if ($manifest.source -ne 'product-live' -or $manifest.backend -ne $ExpectedBackend -or
        -not $manifest.finite -or $manifest.validationCount -ne 0 -or
        $manifest.frameId -le $manifest.requestedAfterFrameId -or
        $manifest.draws.Count -eq 0 -or $manifest.attachments.Count -ne 7) {
        throw "Invalid product capture: $Directory"
    }
    foreach ($model in $Models) {
        $meta = [IO.File]::ReadAllText((Join-Path $root "Dynamic_CPP\Assets\Models\$model.glb.meta"))
        $id = [regex]::Match($meta, '(?m)^assetId:\s*(\S+)').Groups[1].Value
        if (-not $id -or $id -notin @($manifest.draws.modelId)) {
            throw "Capture omits visible model $model ($id): $Directory"
        }
    }
    foreach ($draw in $manifest.draws) {
        if ($draw.modelGeneration -le 0 -or $draw.shaderMetaGeneration -le 0 -or
            $draw.propertyBytes.Count -eq 0) { throw "Unsealed material/model: $Directory" }
        if (@($draw.textures | Where-Object property -EQ 'aoMap').Count -ne 1 -or
            @($draw.textures | Where-Object { $_.space -ne 0 -or $_.register -lt 16 -or $_.register -ge 128 }).Count -ne 0) {
            throw "Missing AO/reflected material texture range: $Directory"
        }
    }
    foreach ($attachment in $manifest.attachments) {
        $size = (Get-Item -LiteralPath (Join-Path $Directory $attachment.file)).Length
        $expected = [long]$attachment.width * $attachment.height * $attachment.channels * 4
        if ($size -ne $expected -or $attachment.nonfinite -ne 0) {
            throw "Invalid $($attachment.name) readback: $Directory"
        }
    }
    $depth = $manifest.attachments | Where-Object name -EQ 'depth'
    $hdr = $manifest.attachments | Where-Object name -EQ 'preToneHdr'
    if ($depth.min -ge 1 -or $depth.min -eq $depth.max -or $hdr.rgbMax -le 0) {
        throw "Empty depth/HDR capture: $Directory"
    }

    # W8 — 세대 원자 밀봉. 이 세 단정이 전부 실장면 프레임에서만 성립한다.
    #   ① 제품 draw는 전부 도장을 받는다(도장 없는 draw = 밀봉 경로를 안 탄 것)
    #   ② 한 프레임 안에서 세대가 섞이지 않는다(위반 0)
    #   ③ 인코더가 조용히 버린 명령이 없다(검은 화면·플리커의 직접 경로)
    if (-not $manifest.PSObject.Properties['sealLedger'] -or -not $manifest.sealLedger.recorded) {
        throw "Capture has no seal ledger (W8 배선이 끊겼다): $Directory"
    }
    foreach ($draw in $manifest.draws) {
        if (-not $draw.PSObject.Properties['seal'] -or -not $draw.seal.stamped -or
            $draw.seal.hash -le 0 -or $draw.seal.frameId -ne $manifest.frameId -or
            $draw.seal.sceneEpoch -ne $manifest.sceneEpoch) {
            throw "Draw seal missing or from another frame: $Directory"
        }
    }
    foreach ($pass in @('gbuffer', 'forward')) {
        $ledger = $manifest.sealLedger.$pass
        if ($ledger.violations -ne 0) {
            throw "Seal violations in $pass ($($ledger.lastReason)): $Directory"
        }
        if ($ledger.unstamped -ne 0) {
            throw "Unstamped product draws in $pass : $Directory"
        }
        if ($ledger.frameId -ne $manifest.frameId) {
            throw "Seal ledger frame mismatch in $pass : $Directory"
        }
        if ($ledger.samplerIdentity -le 0 -and $ledger.stamped -gt 0) {
            throw "Sampler identity was never recorded in $pass : $Directory"
        }
    }
    # W0 — draw별 신원 세 축이 캡처에서 실제로 닿는가.
    #
    # ★ 세 값(resolved PSO key · sampler identity · descriptor version)은 draw 항목이
    #   아니라 sealLedger의 bindings에 있고, draw의 seal.hash로 조인해야 나온다.
    #   값을 draw마다 복사하지 않는 이유는 "같은 밀봉을 공유하는 draw는 같은
    #   바인딩을 쓴다"가 W8의 불변식이라, 복사하면 그 불변식이 표에서 사라지기
    #   때문이다. 그래서 여기서 **조인이 실제로 성립하는지**를 단정한다 —
    #   조인되지 않는 draw는 manifest에 있어도 신원이 없는 draw다.
    #
    # ★ 조인은 **라우트별로** 한다. 두 장부를 합쳐 놓고 찾으면 gbuffer draw가
    #   forward 장부의 항목에 붙어도 통과한다.
    $identityCounts = @{ pipelineId = @{}; samplerIdentity = @{}; descriptorVersion = @{} }
    foreach ($pass in @('gbuffer', 'forward')) {
        $bindings = @{}
        foreach ($binding in @($manifest.sealLedger.$pass.bindings)) {
            if ($null -ne $binding) { $bindings[[string]$binding.sealHash] = $binding }
        }
        foreach ($draw in @($manifest.draws | Where-Object route -EQ $pass)) {
            $binding = $bindings[[string]$draw.seal.hash]
            if ($null -eq $binding) {
                throw "Draw seal $($draw.seal.hash) has no $pass binding (신원 없는 draw): $Directory"
            }
            # 0은 "기록되지 않았다"다. 값으로 읽으면 안 된다 — descriptorVersion은
            # 이 축이 열리기 전까지 writer가 0이었고 그 시절과 구분되어야 한다.
            foreach ($axis in @('pipelineId', 'samplerIdentity', 'descriptorVersion')) {
                if (-not $binding.PSObject.Properties[$axis]) {
                    throw "$pass binding lacks ${axis} (manifest 배선이 끊겼다): $Directory"
                }
                if ([uint64]$binding.$axis -eq 0) {
                    throw "$pass binding has ${axis}=0 for seal $($draw.seal.hash): $Directory"
                }
                $identityCounts[$axis][[string]$binding.$axis] = $true
            }
        }
    }
    # ★ 변이 폭은 **재기만 한다**. 지금 sampler identity는 pass-global이라 distinct가
    #   1이고 그것이 옳다(재질별로 가르는 것은 W7이다). 여기서 ">1이어야 한다"로
    #   단정하면 W7 착수 전까지 게이트가 도는 세트에 있을 수 없다.
    Write-Output ("  draw identity: distinct pso={0} sampler={1} descriptorVersion={2} ({3})" -f
        $identityCounts.pipelineId.Count, $identityCounts.samplerIdentity.Count,
        $identityCounts.descriptorVersion.Count, (Split-Path $Directory -Leaf))

    if ($manifest.sealLedger.encoderDrops -ne 0) {
        throw "Encoder dropped commands ($($manifest.sealLedger.lastEncoderDrop)): $Directory"
    }
    if ($manifest.sealLedger.textureUploadFailures -ne 0) {
        throw "Silent neutral substitution after upload failure: $Directory"
    }
}

try {
    if (Get-Process CreatorEditor -ErrorAction SilentlyContinue) {
        throw 'Close the running Editor before this test; it temporarily selects the startup backend.'
    }
    New-Item -ItemType Directory -Path $run | Out-Null
    $original = [IO.File]::ReadAllBytes($settings)
    $text = $utf8.GetString($original)
    $backendPattern = '(?m)(^render:\r?\n\s{2}backend: )\w+'
    if ([regex]::Matches($text, $backendPattern).Count -ne 1) { throw 'Runtime backend setting is ambiguous.' }
    $captureDirs = @{}
    foreach ($api in $Backend) {
        [IO.File]::WriteAllText($settings, [regex]::Replace($text, $backendPattern, "`${1}$api"), $utf8)
        $primitive = Join-Path $run "$api-primitives"
        $primitiveRepeat = Join-Path $run "$api-primitives-repeat"
        $gunner = Join-Path $run "$api-gunner"
        $gunnerRepeat = Join-Path $run "$api-gunner-repeat"
        $captureMode = if ($api -eq 'dx12') { ' controlled' } else { '' }
        $normalPair = Join-Path $run "$api-normalpair"
        $alphaModes = Join-Path $run "$api-alphamodes"
        $samplerModes = Join-Path $run "$api-samplermodes"
        $sharedMaterial = Join-Path $run "$api-sharedmaterial"
        $lighting = Join-Path $run "$api-lighting"
        $lightingRepeat = Join-Path $run "$api-lighting-repeat"
        $normalPixels = Join-Path $run "$api-normal-pixels"
        $samplerPixels = Join-Path $run "$api-sampler-pixels"
        # W1 fixture 는 자산 트리 밖에 있어 model.load 가 트리 안으로 복사한다.
        # backend 둘이 같은 조건에서 돌도록 매 회차 앞에서 지운다 — 남겨 두면
        # 두 번째 회차만 "제자리 열기" 경로를 타서 두 실행이 같은 것을 재지 않는다.
        foreach ($imported in $importedFixtures) {
            if (Test-Path -LiteralPath $imported) {
                Remove-Item -Recurse -Force -LiteralPath $imported
            }
        }
        # Gunner 는 추적 밖 자산이다. 있으면 재고, 없으면 **건너뛰었다고 적는다**.
        $gunnerAsset = Join-Path $root 'Dynamic_CPP\Assets\Models\Gunner_F_Mythic.glb'
        $hasGunner = Test-Path -LiteralPath $gunnerAsset
        $commands = @(
            "scene.switch `"$root/Dynamic_CPP/Assets/Scenes/FT_Primitives.creator`"",
            # ★ 첫 대기가 렌더 예열을 겸한다 (2026-09-14). 라이브 렌더러의 첫
            #   프레임은 파이프라인 구축과 ShaderMeta 적용으로 Debug 에서
            #   13~28초가 걸린다. 예전에는 30 프레임으로도 통했는데, 그것은
            #   BuildPipeline 이 표시 락을 구축 내내 쥐어 게임 스레드까지
            #   멈춰 세운 덕이었다 — 프레임이 느려서 수가 곧 시간이었다.
            #   뒤의 `wait 30` 들은 그대로 둔다: 예열은 프로세스당 한 번이다.
            'wait 2000',
            # ★ 게임 타깃은 **수요가 있어야** 그려진다 (PHASE 21 W4). 중앙
            #   ViewportHost 가 Scene 모드이면 게임 뷰를 아예 만들지 않으므로
            #   `game` 캡처가 영원히 완료되지 않는다 — 모드를 먼저 세운다.
            'editor.viewport game',
            'wait 30',
            "render.pbr.capture `"$primitive`" game$captureMode")
        if ($api -eq 'dx12') {
            $commands += @('wait 90', "render.pbr.capture `"$primitiveRepeat`" game controlled")
        }
        if ($hasGunner) {
            $commands += @(
                "model.loadcached `"$root/Dynamic_CPP/Assets/Models/Gunner_F_Mythic.glb`"",
                'model.place Gunner_F_Mythic',
                'object.transform Gunner_F_Mythic 0 0 2 0 180 0 0.025 0.025 0.025',
                'wait 30',
                "render.pbr.capture `"$gunner`" game$captureMode")
            if ($api -eq 'dx12') {
                $commands += @('wait 90', "render.pbr.capture `"$gunnerRepeat`" game controlled")
            }
        }
        $commands += @(
            # ★ W1 — normal-map 저작 유무의 정본이 하나인지는 **실장면 프레임**에서만
            #   물을 수 있다. 격리 fixture 에는 스냅샷이 없어 draw.useNormalMap 폴백이
            #   쓰이기 때문이다(EnhancedGBufferPass.cpp:439). 노멀맵 있는 재질과 없는
            #   재질을 **한 자산의 두 primitive** 로 두어 같은 노드·같은 프레임 아래
            #   draw 둘이 뜨게 한다 — 그래야 변인이 재질 하나로 좁혀진다.
            "model.load `"$root/Tools/regression/fixtures/pbr-normal-pair/NormalPair.gltf`"",
            'model.place NormalPair',
            'object.transform NormalPair 0 0 2 0 180 0 1 1 1',
            'wait 30',
            "render.pbr.capture `"$normalPair`" game",
            "render.pbr.normalpair `"$normalPair`"",
            # ★ W0 — alpha mode 삼종과 **비균등 스케일**을 한 캡처에서 겸한다.
            #   비균등 스케일은 자산이 아니라 씬 변환이라 공짜다(1.7/0.6/1.0).
            #   BLEND 재질이 forward 로 가므로 이 캡처가 forward 장부를 처음 채운다.
            "model.load `"$root/Tools/regression/fixtures/pbr-alpha-mask/AlphaModes.gltf`"",
            'model.place AlphaModes',
            'object.transform AlphaModes 0 0 2 0 180 0 1.7 0.6 1.0',
            'wait 30',
            "render.pbr.capture `"$alphaModes`" game",
            # ★ W7 — 같은 이미지 하나를 sampler 3 종으로 참조하는 자산. UV 가
            #   0..2 라 wrap 이 그림에 드러나고, 그 중 하나가 MIRRORED_REPEAT 라
            #   새 RHIAddressMode::Mirror 열거자의 유일한 소비자다.
            "model.load `"$root/Tools/regression/fixtures/pbr-sampler/SamplerModes.gltf`"",
            'model.place SamplerModes',
            'object.transform SamplerModes 0 0 2 0 180 0 1 1 1',
            'wait 30',
            "render.pbr.capture `"$samplerModes`" game",
            # ★ W8 — primitive 둘이 재질 하나를 공유하는 자산. 모델 인스턴스화가
            #   두 MeshRenderer 에 **같은 `Material*`** 과 같은 authored base 를
            #   붙이고, 각자 자기 MaterialInstance 를 갖는다. 거기에 서로 다른
            #   override 를 얹으면 "주소는 같은데 값이 다른" 상태가 된다 — W8 이
            #   밀봉 키를 주소에서 값으로 바꾼 바로 그 이유다.
            #   ★ 씬 저작으로는 이 상태를 못 만든다(표기 셋이 모두 사본을 준다).
            "model.load `"$root/Tools/regression/fixtures/pbr-shared-material/SharedMaterial.gltf`"",
            'model.place SharedMaterial',
            'object.transform SharedMaterial 0 0 2 0 180 0 1 1 1',
            'material.override SharedMaterial 0 roughness 0.10',
            'material.override SharedMaterial 1 roughness 0.90',
            'wait 30',
            "render.pbr.capture `"$sharedMaterial`" game",
            # Isolate known geometry while retaining the actual product lighting/post chain.
            "scene.switch `"$root/Dynamic_CPP/Assets/Scenes/FT_Primitives.creator`"",
            'wait 30',
            'object.transform MainCamera 0 0 -6 0 0 0 1 1 1')
        foreach ($name in @('Ground','P_Cube','P_Sphere','P_IcoSphere','P_Cylinder','P_Cone','P_Torus','P_Suzanne')) {
            $commands += "object.delete $name"
        }
        $commands += @(
            "model.load `"$root/Tools/regression/fixtures/pbr-lighting/Lighting.gltf`"",
            'model.place Lighting',
            'object.transform Lighting 0 0 0 0 0 0 1 1 1',
            'wait 30')
        if ($api -eq 'dx12') {
            $commands += @("render.pbr.capture `"$lighting`" game controlled", 'wait 90',
                "render.pbr.capture `"$lightingRepeat`" game controlled")
        } else {
            $commands += "render.pbr.capture `"$lighting`" game"
        }
        $commands += @('object.delete Lighting', 'model.place NormalPair',
            'object.transform NormalPair 0 0 0 0 0 0 1 1 1', 'wait 30',
            "render.pbr.capture `"$normalPixels`" game", 'object.delete NormalPair',
            'model.place SamplerModes', 'object.transform SamplerModes 0 0 0 0 0 0 1 1 1',
            'wait 30', "render.pbr.capture `"$samplerPixels`" game", 'quit')
        $results = @(Invoke-Editor $api $commands)
        $expectedCaptures = if ($hasGunner) { 6 } else { 5 }
        $expectedCaptures += $(if ($api -eq 'dx12') { 4 } else { 3 })
        if ($api -eq 'dx12') { $expectedCaptures += $(if ($hasGunner) {2} else {1}) }
        $captures = @($results | Where-Object command -eq 'render.pbr.capture')
        if ($captures.Count -ne $expectedCaptures -or
            @($captures | Where-Object { $_.data.frameId -le 0 }).Count) {
            throw "$api capture did not return $expectedCaptures completed frames; artifacts: $run"
        }
        Assert-Capture $primitive $api @('Prim_Cube', 'Prim_Sphere', 'Prim_Cylinder')
        Set-AxisRan "$api/primitives"
        if ($api -eq 'dx12') {
            Assert-Capture $primitiveRepeat $api @('Prim_Cube','Prim_Sphere','Prim_Cylinder')
            Assert-PbrRepeatability $primitive $primitiveRepeat (Join-Path $run 'dx12-primitives-repeatability.json')
            Set-AxisRan 'dx12/primitives-hdr+display-repeatability'
        }
        if ($hasGunner) {
            Assert-Capture $gunner $api @('Gunner_F_Mythic')
            Set-AxisRan "$api/gunner"
            if ($api -eq 'dx12') {
                Assert-Capture $gunnerRepeat $api @('Gunner_F_Mythic')
                Assert-PbrRepeatability $gunner $gunnerRepeat (Join-Path $run 'dx12-gunner-repeatability.json')
                Set-AxisRan 'dx12/gunner-hdr+display-repeatability'
            }
        } else {
            # 조용히 넘어가지 않는다. 이 줄이 없으면 "Gunner 축을 쟀다" 와
            # "Gunner 축이 없었다" 가 요약에서 구분되지 않는다.
            Set-AxisSkipped "$api/gunner" 'Dynamic_CPP/Assets/Models/Gunner_F_Mythic.glb 없음 (추적 밖 자산)'
        }
        # 이 캡처의 모델 확인은 Models 목록이 아니라 render.pbr.normalpair 가 한다.
        # Assert-Capture 의 모델 검사는 `<이름>.glb.meta` 를 전제하는데 이 fixture 는
        # 하위 폴더의 `.gltf` 다. 그리고 여기서 물어야 할 것은 "모델이 떴나" 가
        # 아니라 "대조쌍이 섰고 두 유도가 같은 답을 내나" 다.
        Assert-Capture $normalPair $api @()
        $normalVerdict = Get-SucceededCommand $results 'render.pbr.normalpair'
        if (-not $normalVerdict.passed) {
            throw "$api normal-map pair verification failed; artifacts: $run"
        }
        Set-AxisRan "$api/normal-pair"
        Assert-Capture $alphaModes $api @()
        Assert-AlphaModes $alphaModes
        Set-AxisRan "$api/alpha-modes+nonuniform-scale"
        Assert-Capture $samplerModes $api @()
        Assert-SamplerModes $samplerModes
        Set-AxisRan "$api/sampler"
        Assert-Capture $sharedMaterial $api @()
        # modelId 는 material.override 가 돌려준 값이다 — 게이트가 지어내지 않는다.
        $overrides = Get-AllSucceeded $results 'material.override'
        if ($overrides.Count -ne 2) {
            throw "$api expected two material.override results, found $($overrides.Count)"
        }
        if ($overrides[0].modelId -ne $overrides[1].modelId) {
            throw "$api shared-material overrides landed on different models"
        }
        if ($overrides[0].meshId -eq $overrides[1].meshId) {
            throw "$api shared-material overrides landed on the same renderer"
        }
        Assert-SharedMaterial $sharedMaterial $overrides[0].modelId
        Set-AxisRan "$api/shared-material-seal"
        Assert-Capture $lighting $api @()
        $lightingMeta = Join-Path $root 'Dynamic_CPP/Assets/Models/Lighting/Lighting.gltf.meta'
        $lightingId = (Get-Content -LiteralPath $lightingMeta | Where-Object { $_ -match '^assetId:' } | Select-Object -First 1) -replace '^assetId:\s*',''
        if (-not $lightingId) { throw 'Imported lighting model identity is missing' }
        Assert-PbrLightingPixels $lighting $lightingId (Join-Path $run "$api-lighting-pixels.json")
        Set-AxisRan "$api/ao+emission-product-pixels"
        if ($api -eq 'dx12') {
            Assert-Capture $lightingRepeat $api @()
            Assert-PbrRepeatability $lighting $lightingRepeat (Join-Path $run 'dx12-repeatability.json')
            Set-AxisRan 'dx12/hdr+display-repeatability'
        }
        foreach ($case in @(@('NormalPair',$normalPixels),@('SamplerModes',$samplerPixels))) {
            Assert-Capture $case[1] $api @()
            $meta = Join-Path $root "Dynamic_CPP/Assets/Models/$($case[0])/$($case[0]).gltf.meta"
            $id = (Get-Content -LiteralPath $meta | Where-Object { $_ -match '^assetId:' } | Select-Object -First 1) -replace '^assetId:\s*',''
            Assert-PbrMaterialPixels $case[1] $id $case[0] (Join-Path $run "$api-$($case[0])-pixels.json")
            Set-AxisRan "$api/$($case[0])-product-pixels"
        }
        $captureDirs[$api] = @{ primitives = $primitive }
        if ($hasGunner) { $captureDirs[$api]['gunner'] = $gunner }
        $ranHere = @("primitives", $(if ($hasGunner) { 'gunner' }), 'normal-pair',
            'alpha-modes+nonuniform-scale', 'sampler',
            'shared-material-seal', 'ao+emission-pixels', 'normal-pixels', 'sampler-pixels') | Where-Object { $_ }
        Write-Output "$api product capture PASS ($($ranHere -join ', ')): $run"
    }
    # W9 — 두 backend 캡처의 float32 readback을 실제로 맞댄다.
    #
    # ★ W0부터 이 .f32 들을 읽는 코드가 저장소에 하나도 없었다. 게이트는 파일
    #   크기만 재고 있었고 계획서의 RMSE 는 손으로 한 번 잰 값이었다. 여기서
    #   비로소 "같은 그림인가"가 게이트의 질문이 된다. 한 backend 만 돌린
    #   실행에서는 맞댈 짝이 없으므로 건너뛴다(그 사실을 출력한다).
    if ($captureDirs.Count -eq 2) {
        $left = $captureDirs['dx12']
        $right = $captureDirs['vulkan']
        # 양쪽 backend 가 **둘 다 가진** 축만 맞댄다. 한쪽에만 있는 축을 맞대면
        # 존재하지 않는 디렉터리를 열게 된다.
        $comparable = @($left.Keys | Where-Object { $right.ContainsKey($_) } | Sort-Object)
        foreach ($fixture in $comparable) {
            $outputJson = Join-Path $run "compare-$fixture.json"
            $results = @(Invoke-Editor "compare-$fixture" @(
                "render.pbr.compare `"$($left[$fixture])`" `"$($right[$fixture])`" `"$outputJson`"",
                'quit'))
            $data = Get-SucceededCommand $results 'render.pbr.compare'
            if (-not $data.passed) { throw "Cross-backend pixel comparison failed: $fixture" }
            if (-not (Test-Path -LiteralPath $outputJson)) {
                throw "Comparison result was not written: $outputJson"
            }
            Set-AxisRan "compare/$fixture"
        }
        Write-Output "cross-backend capture compare PASS ($($comparable -join ', '))"
    }
    elseif ($Backend -notcontains 'vulkan') {
        # 2026-09-15 결정으로 판정이 DX12 하나다. 이건 '못 쟀다'가 아니라 '다른 계획이
        # 소유한다' 이므로 skipped 가 아니라 deferred 로 적는다.
        Set-AxisDeferred 'compare' 'PHASE 4.9 BackendParityPlan — 교차 백엔드 픽셀 판정은 시각 고정이 선행이다(§15)'
        Write-Output 'cross-backend capture compare DEFERRED (PHASE 4.9 — 2026-09-15 결정)'
    }
    else {
        Set-AxisSkipped 'compare' 'backend 하나만 실행됨'
        Write-Output "cross-backend capture compare SKIPPED (backend 하나만 실행됨)"
    }

    if ($ProductOnly) {
        Set-AxisSkipped 'isolated GPU/material/generation regressions' '-ProductOnly: this run validates product captures only'
        Write-Output "PBR PRODUCT-ONLY PASS (not full baseline or cutover): $run"
        return
    }
    # The paired harness owns both DX12 and Vulkan test devices; keep its Editor
    # host on DX12 independently of the final product-capture backend above.
    [IO.File]::WriteAllText($settings, [regex]::Replace($text, $backendPattern, '${1}dx12'), $utf8)
    # ★ `vk.*` 는 이름이 범위를 속인다 — 넷 다 **DX12/Vulkan 대조** 테스트다
    #   (`RunVulkanGBufferTest` 안에 `dx12Capture` 와 `vkCapture` 가 함께 있다).
    #   그래서 끄면 vulkan 팔만이 아니라 **DX12 팔도 함께 꺼진다.** 2026-09-15 결정은
    #   그 사실을 알고 내린 것이고, 잃는 것은 아래에 적어 회계에 남긴다.
    if ($IncludeVulkanSelfTest) {
        $null = Invoke-Editor 'defaults' @('vk.shadow', 'vk.gbuffer', 'vk.forward', 'vk.deferred', 'quit')
        Set-AxisRan 'vk/* 백엔드 대조'
    }
    else {
        # 회계 등록은 맨 앞에서 이미 했다. 여기서는 로그에만 남긴다.
        Write-Output 'vk.* 백엔드 대조 DEFERRED (PHASE 4.9 — 2026-09-15 결정 · DX12 팔도 함께 꺼진다)'
    }
    $null = Invoke-Editor 'forward-shade' @('dx12.forwardshade', 'quit')
    Set-AxisRan 'dx12.forwardshade'
    foreach ($command in @('dx12.ibl','dx12.iblshade','dx12.ssao','dx12.ssgi','dx12.fog')) {
        $results = @(Invoke-Editor ($command -replace '\.','-') @($command,'quit'))
        $null = Get-SucceededCommand $results $command
        Set-AxisRan $command
    }
    # Material codec/seal contracts moved out of the Editor registry in PHASE 14.5.
    $contractLog = Join-Path $run 'experiment-contract.log'
    & pwsh -NoProfile -File (Join-Path $PSScriptRoot 'verify-experiment-contract.ps1') -Configuration $Configuration *> $contractLog
    if ($LASTEXITCODE -ne 0) { throw "Standalone material contracts failed; log: $contractLog" }
    Set-AxisRan 'standalone/material-contracts'
    $contractCommands = @('experiment.matresolve', 'experiment.matmigrate', 'experiment.cooked')
    $null = Invoke-Editor 'material-contracts' ($contractCommands + 'quit')
    foreach ($command in $contractCommands) { Set-AxisRan $command }
    foreach ($name in @('seal', 'parity', 'coverage', 'emission', 'transform', 'uv', 'mip', 'occlusion')) {
        $results = @(Invoke-Editor $name @("render.pbr.$name", 'quit'))
        $data = Get-SucceededCommand $results "render.pbr.$name"
        if (-not $data.passed) { throw "PBR $name did not execute its verification" }
        Set-AxisRan "render.pbr.$name"
    }
    $generationLog = Join-Path $run 'generation-atomicity.log'
    & pwsh -NoProfile -File (Join-Path $PSScriptRoot 'verify-model-generation-atomicity.ps1') `
        -Editor $Editor -Work $run -PreserveArtifacts *> $generationLog
    if ($LASTEXITCODE -ne 0) { throw "Model generation atomicity failed: $generationLog" }
    Set-AxisRan 'model/generation-atomicity'
    $recoveryLog = Join-Path $run 'model-auto-recovery.log'
    & pwsh -NoProfile -File (Join-Path $PSScriptRoot 'verify-model-auto-recovery.ps1') `
        -Editor $Editor -Work $run *> $recoveryLog
    if ($LASTEXITCODE -ne 0) { throw "Model automatic recovery failed: $recoveryLog" }
    Set-AxisRan 'model/auto-recovery'
    # A later successful test must not erase an earlier failure exit code.
    $results = @(Invoke-Editor 'negative-exit' @('render.livecheck 1 1', 'dx12.gbuffer', 'quit') 4)
    if ((Get-CommandResult $results 'render.livecheck').status -ne 'failed') {
        throw 'Negative exit probe did not exercise failure.'
    }
    $null = Get-SucceededCommand $results 'dx12.gbuffer'
    $results = @(Invoke-Editor 'negative-capture' @("render.pbr.capture `"$run`" game", 'quit') 4)
    $capture = Get-CommandResult $results 'render.pbr.capture'
    if ($capture.status -ne 'failed' -or $capture.code -ne 'render.pbr.capture.rejected') {
        throw 'Capture did not reject an existing output directory.'
    }
    $ranAxes = @($axisReport.Keys | Where-Object { $axisReport[$_] -eq 'ran' })
    $deferredAxes = @($axisReport.Keys | Where-Object { $axisReport[$_] -like 'deferred:*' })
    $skipped = @($axisReport.Keys | Where-Object { $axisReport[$_] -notlike 'deferred:*' -and $axisReport[$_] -ne 'ran' })
    Write-Output ("PBR W0/W1/W2/W3/W4/W5/W6/W7-normal/UV/mip baseline PASS (dx12 판정)" +
        " — 잰 축 $($ranAxes.Count) · 건너뛴 축 $($skipped.Count) · 미룬 축 $($deferredAxes.Count)" +
        " (장시간 검증·W9 cutover는 별도 판정): $run")
    if ($skipped.Count) {
        Write-Output "  ※ 위 PASS 는 건너뛴 축에 대해 아무 말도 하지 않는다: $($skipped -join ', ')"
    }
    if ($deferredAxes.Count) {
        Write-Output "  ※ 미룬 축은 다른 계획이 소유한다(고칠 결함이 아니다): $($deferredAxes -join ', ')"
    }
}
finally {
    # ★ 축 회계는 **실패해도 찍는다**. 처음에는 성공 경로에만 두었는데, 이 게이트는
    #   지금 뒤쪽(experiment contract)에서 기존 결함으로 멈추기 때문에 회계가 영영
    #   보이지 않았다. 무엇을 쟀는지는 실패했을 때야말로 알아야 한다.
    # ★ 0 건일 때도 찍는다. 처음에는 Count 로 감쌌는데, 첫 축을 재기도 전에
    #   죽으면 표가 통째로 사라져 "회계가 없다" 와 "잰 축이 0" 이 구분되지 않았다.
    Write-Output '── 축 회계 ──'
    if (0 -eq $axisReport.Count) {
        Write-Output '  (잰 축 0 — 첫 축에 닿기 전에 멈췄다)'
    }
    if ($axisReport.Count) {
        foreach ($name in $axisReport.Keys) {
            Write-Output ("  {0,-24} {1}" -f $name, $axisReport[$name])
        }
    }
    if ($process -and -not $process.HasExited) { $process.Kill(); $process.WaitForExit() }
    if (Test-Path -LiteralPath $run) {
        $axisReport | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $run 'axes.json') -Encoding utf8
    }
    if ($null -ne $original) { [IO.File]::WriteAllBytes($settings, $original) }
    foreach ($imported in $importedFixtures) {
        if (Test-Path -LiteralPath $imported) {
            Remove-Item -Recurse -Force -LiteralPath $imported -ErrorAction SilentlyContinue
        }
    }
}
