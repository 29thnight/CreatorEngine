# 렌더 지오메트리 신원 경계 (I6-C)
#
# 렌더 패스가 legacy `Mesh*` **포인터를 신원으로** 쓰지 않는지 정적으로 잰다.
# 예전에는 지오메트리 맵 키·배치 키·정렬 기준이 전부 게임 객체의 주소였다.
# 렌더가 게임 자료구조를 신원으로 드는 그 결합이 I6이 지우려는 것이고, 값 키는
# 자산 신원(experiment stableKey)에서 나온다.
#
# ★ 왜 정적 검사인가: 이 전환은 **그림을 바꾸지 않는다**(바꾸면 결함이다).
#   그래서 A/B 픽셀 축은 "아무것도 안 깨졌다"만 말하고 되돌림은 못 잡는다.
#   I6 정찰이 적어 둔 대로, 은퇴·전환 슬라이스의 게이트는 "그림이 같다"가
#   아니라 **"소비 0"과 "빌드가 막는다"** 쪽이어야 한다.
#
# 허용되는 유일한 `draw.mesh`는 legacy 업로드 폴백 한 줄이다(캐시가 legacy
# 배열로 올리는 경로 — Assimp 폴백·A/B off가 죽는 I6-E에서 함께 사라진다).
param()

$ErrorActionPreference = 'Stop'
$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot "..\.."))

$passes = @(
    "Engine\RenderEngine\Render\Passes\Geometry\EnhancedGBufferPass",
    "Engine\RenderEngine\Render\Passes\Geometry\EnhancedForwardPass",
    "Engine\RenderEngine\Render\Passes\Geometry\EnhancedShadowPass")

$fail = @()
$uploadOnly = 0
$keyUsers = 0

foreach ($pass in $passes) {
    $cppPath = Join-Path $repoRoot ($pass + ".cpp")
    $hdrPath = Join-Path $repoRoot ($pass + ".h")
    if (-not (Test-Path -LiteralPath $cppPath)) { "패스 소스가 없다: $cppPath"; exit 1 }
    $cpp = Get-Content -LiteralPath $cppPath
    $hdr = if (Test-Path -LiteralPath $hdrPath) { Get-Content -LiteralPath $hdrPath -Raw } else { "" }
    $name = Split-Path $pass -Leaf

    # ① draw.mesh는 업로드 폴백 줄에서만 나타난다.
    $meshUses = @($cpp | Where-Object { $_ -match 'draw\.mesh' })
    foreach ($line in $meshUses) {
        if ($line -match 'GetOrUpload\(') { $uploadOnly++; continue }
        $fail += "${name}: draw.mesh를 신원으로 쓴다 — $($line.Trim())"
    }

    # ② 배치·지오메트리 맵이 포인터로 키를 잡지 않는다.
    if ($hdr -match 'map<\s*Mesh\s*\*') {
        $fail += "${name}: 지오메트리 맵이 Mesh* 키다"
    }
    if ($hdr -match '(?m)^\s*Mesh\s*\*\s*mesh') {
        $fail += "${name}: 배치가 Mesh* 를 든다"
    }

    # ③ 신원 창구를 실제로 쓴다 — ①②가 통과해도 아무도 키를 안 쓰면 의미가 없다.
    if (($cpp -join "`n") -notmatch 'enhanced_draw::GeometryKey') {
        $fail += "${name}: 신원 창구(enhanced_draw::GeometryKey)를 쓰지 않는다"
    } else { $keyUsers++ }
}

# ④ 창구 자체가 서 있는가.
$identity = Join-Path $repoRoot "Engine\RenderEngine\Render\Graph\EnhancedDrawIdentity.h"
if (-not (Test-Path -LiteralPath $identity)) {
    $fail += "신원 창구 헤더가 없다: EnhancedDrawIdentity.h"
}

if ($fail.Count -gt 0) {
    "render geometry identity: FAIL"
    $fail | ForEach-Object { "  [실패] $_" }
    exit 1
}

"render geometry identity: PASS (패스 $($passes.Count) · 신원 창구 사용 $keyUsers · 업로드 폴백 전용 draw.mesh $uploadOnly)"
exit 0
