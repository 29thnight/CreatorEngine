# legacy Skeleton 은퇴 경계 (I6-B)
#
# legacy `Skeleton`(Assimp가 만들고 역브리지가 재시공하는 `Bone*` 트리)의 제품
# 접촉을 정적으로 잰다. I6-B는 이 타입을 은퇴시키는 슬라이스이고, 그 이행은
# **접촉 수가 단조 감소**하는 것으로만 보인다.
#
# ★ 왜 정적·래칫인가: 은퇴는 그림을 바꾸지 않는다(바꾸면 결함이다). 게다가
#   I6 정찰이 적어 둔 대로 **은퇴 슬라이스는 자기 A/B 대조군을 없앤다** —
#   타입이 죽으면 off 팔에서 지을 것이 없어진다. 그래서 축은 "그림이 같다"가
#   아니라 "소비 0"과 "빌드가 막는다"다.
#
# ★ 주석은 세지 않는다. 재는 것은 **코드 접촉**인데 원문 그대로 세면 은퇴를
#   설명하는 주석 한 줄이 래칫을 거꾸로 올린다(B0의 착수 주석이 실제로 그랬다).
#   verify-player-runtime-hygiene이 같은 함정을 이미 한 번 밟았다.
param()

$ErrorActionPreference = 'Stop'
$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot "..\.."))

function Get-CodeText([string]$path) {
    $body = [IO.File]::ReadAllText($path)
    # 블록 주석 먼저, 그 다음 줄 주석. 순서를 바꾸면 블록 안의 `//`가 남는다.
    $body = [regex]::Replace($body, '(?s)/\*.*?\*/', ' ')
    return [regex]::Replace($body, '(?m)//.*$', ' ')
}

# 함수 signature 줄부터 중괄호 짝을 세어 본문만 떼어낸다.
function Get-FunctionBody([string]$code, [string]$signature) {
    $start = $code.IndexOf($signature)
    if ($start -lt 0) { return $null }
    $open = $code.IndexOf('{', $start)
    if ($open -lt 0) { return $null }
    $depth = 0
    for ($i = $open; $i -lt $code.Length; $i++) {
        if ($code[$i] -eq '{') { $depth++ }
        elseif ($code[$i] -eq '}') {
            $depth--
            if ($depth -eq 0) { return $code.Substring($open, $i - $open + 1) }
        }
    }
    return $null
}

$fail = @()

# ── 계약 1: experiment 재생 바인딩이 legacy 스켈레톤을 전제하지 않는다 ───────
#
# 예전 바인딩은 `m_Skeleton`이 없으면 즉시 돌아섰고 본·클립 계수를 legacy와
# 대조했다 — 공유 자산이 없으면 experiment 경로가 **원리적으로 켜지지 않는**
# 구조라 은퇴의 첫 자물쇠였다. 이제 m_Motion 단독으로 서고, 불변식은
# experiment 내부(본이 있다·루트가 범위 안)에서 독립 유도한다.
$animatorPath = Join-Path $repoRoot "Engine\SceneRuntime\Animator.cpp"
if (-not (Test-Path -LiteralPath $animatorPath)) {
    "Animator.cpp가 없다: $animatorPath"; exit 1
}
$animatorCode = Get-CodeText $animatorPath

$guarded = @(
    @{ Sig = 'void Animator::EnsureExperimentAnimationBinding()'; Why = 'experiment 바인딩' },
    @{ Sig = 'void Animator::UpdateAnimation()'; Why = '클립 인덱스 클램프' })

foreach ($entry in $guarded) {
    $body = Get-FunctionBody $animatorCode $entry.Sig
    if ($null -eq $body) {
        $fail += "본문을 못 찾았다(시그니처 변경?): $($entry.Sig)"
        continue
    }
    $hits = ([regex]::Matches($body, 'm_Skeleton')).Count
    if ($hits -ne 0) {
        $fail += "$($entry.Why)이 legacy Skeleton을 $hits 건 만진다 — 창구를 쓰라: $($entry.Sig)"
    }
}

# ── 계약 2: 접촉 래칫 — 파일별 상한을 넘지 않는다 ───────────────────────────
#
# 값은 2026-09-01 I6-B0 직후 실측이다. I6-B/C/D가 내려갈 때마다 함께 낮춘다.
# 표에 없는 파일이 나타나면 **새 소비자**라 실패다 — 은퇴 중인 타입에 소비가
# 늘어나는 것이 이 게이트가 막으려는 유일한 방향이다.
$ratchet = @{
    'Engine/SceneRuntime/Animator.cpp'                = 16
    'Editor/EngineEntry/ConsoleCommandSystem.cpp'     = 16
    'Engine/SceneRuntime/AnimationEventBridge.cpp'    = 11
    'Engine/SceneRuntime/ModelSceneBridge.cpp'        = 8
    'Engine/RenderEngine/ModelLoader.cpp'             = 6
    'Engine/RenderEngine/ExperimentModelMigration.cpp' = 5
    'Engine/SceneRuntime/AnimationJob.cpp'            = 5
    'Engine/RenderEngine/Model.cpp'                   = 2
    'Engine/SceneRuntime/Animator.h'                  = 2
    'Engine/RenderEngine/AnimatorData.h'              = 1
    'Engine/RenderEngine/Model.h'                     = 1
}

$measured = @{}
foreach ($tree in @('Engine', 'Editor')) {
    $treeRoot = Join-Path $repoRoot $tree
    if (-not (Test-Path -LiteralPath $treeRoot)) { continue }
    Get-ChildItem -Path $treeRoot -Recurse -File -Include '*.h', '*.hpp', '*.cpp' `
        -ErrorAction SilentlyContinue | ForEach-Object {
        $rel = $_.FullName.Substring($repoRoot.Length).TrimStart('\', '/').Replace('\', '/')
        # RenderTests는 격리 하네스라 제품 접촉이 아니다(I6-E에서 함께 죽는다).
        if ($rel.StartsWith('Editor/RenderTests')) { return }
        $hits = ([regex]::Matches((Get-CodeText $_.FullName), 'm_Skeleton')).Count
        if ($hits -gt 0) { $measured[$rel] = $hits }
    }
}

foreach ($rel in ($measured.Keys | Sort-Object)) {
    $now = $measured[$rel]
    if (-not $ratchet.ContainsKey($rel)) {
        $fail += "래칫 표에 없는 새 legacy Skeleton 소비자: $rel ($now 건)"
        continue
    }
    if ($now -gt $ratchet[$rel]) {
        $fail += "래칫 역주행 $rel — 상한 $($ratchet[$rel])건, 실측 $now 건"
    }
}

$total = 0
foreach ($v in $measured.Values) { $total += $v }
$ceiling = 0
foreach ($v in $ratchet.Values) { $ceiling += $v }

if ($fail.Count -gt 0) {
    "legacy Skeleton 은퇴 경계 실패 $($fail.Count)건:"
    $fail | ForEach-Object { "  $_" }
    exit 1
}

"legacy Skeleton 은퇴 경계 통과 — 접촉 $total/$ceiling 건 · 파일 $($measured.Count)개 · 바인딩 자립 확인"
exit 0
