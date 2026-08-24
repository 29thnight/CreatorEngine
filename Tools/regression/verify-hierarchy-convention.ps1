# 계층 표기 불변식 대조 (SceneGraphRedesignPlan 트랙 E — 루트 규약 통일)
#
# 시나리오와 이 검사가 무엇을 메우는지는 hierarchy_convention.txt 상단에 있다.
#
# ── 판정 항목 ──
#
#   1  네 지점을 다 쟀다              — 시나리오가 도중에 멈추지 않았다
#   2  오브젝트 수가 최소치 이상       — 저작이 안 먹어 빈 씬을 재고 통과하는 것을 막는다
#                                        (실측: 경로가 POSIX 형태였을 때 3개가 나왔다)
#   3  최상위(-1표기) 0건              — ★ 본 판정. 표기가 하나로 모였는가
#   4  쌍불일치 0건                    — 부모의 children에 있는데 m_parentIndex가 다르다
#   5  고아 0건                        — 아무의 children에도 없다
#   6  순회미도달 0건                  — 씬 루트에서 children만 따라 닿지 못한다
#
# 3이 이 게이트의 존재 이유다. 4~6은 통일 이전에도 0이었다(로더가 특수 처리로
# 메우고 있었다) — 즉 통일은 "고장을 고친" 것이 아니라 **로더의 특수 처리에
# 기대지 않도록 표기를 하나로 줄인** 것이다. 그 특수 처리가 사라지거나 새 쓰기
# 경로가 생기면 4~6이 먼저 깨진다.
#
# ── 측정 지점이 넷인 이유 (2026-08-20 CLI 이전) ──
#
# 예전에는 저작 씬 둘(Test1·FT_Material)을 열어 쟀는데, 그것은 **로드 경로를
# 한 번 통과한 값**이라 생성 경로의 표기 분열을 원리적으로 볼 수 없었다.
# 실제로 그 사각지대에 결함이 살아 있었다(Scene::CreateEntity, 커밋 2cc6fecf).
#
# 이제 깊은/넓은 계층을 **CLI로 저작하고**, 각각 ①갓 만든 직후와 ②저장·재로드 후를
# 잰다. ①이 생성 경로를 직접 재는 자리이고, 예전 게이트에는 없던 축이다.
#
# 음성 시험:
#   · 통일 직전(2026-08-20, 저작 씬 기준): Test1 최상위(-1) 2 · FT_Material 2 -> 판정 3 실패
#   · 생성 경로 결함(커밋 2cc6fecf 되돌림): **①갓 만든 직후**에서 판정 3 실패.
#     ②왕복 후는 로더가 정규화하므로 통과한다 — 그 비대칭이 이 게이트가 새로
#     얻은 감도다.
#
# 사용법:
#   pwsh Tools\regression\verify-hierarchy-convention.ps1
param(
    [string]$Exe = (Join-Path $PSScriptRoot "..\..\Bin\x64-Debug\Editor\CreatorEditor.exe"),
    [string]$Work = $env:TEMP,
    [int]$TimeoutSeconds = 300,
    # 깊은 계층: 한 줄로 길게 — 조상 사슬이 끊기는 부류를 잡는다.
    [int]$DeepChain = 14,
    # 넓은 계층: 형제가 많다 — children 목록이 큰 경우. 각 형제는 자식 하나를 갖는다.
    [int]$WideSiblings = 18
)

$exeDir = [System.IO.Path]::GetDirectoryName($Exe)
if (-not (Test-Path $Exe)) { "실행 파일이 없다: $Exe"; exit 1 }

$template = Join-Path $PSScriptRoot "hierarchy_convention.txt"
if (-not (Test-Path $template)) { "시나리오가 없다: $template"; exit 1 }

# ── 저작 블록 생성 (자산·게이트 CLI 이전) ──
#
# 깊은 계층: Deep0 -> Deep1 -> ... 한 줄 사슬. 순회가 끝까지 닿아야 한다.
$deepAuthor = @()
for ($i = 0; $i -lt $DeepChain; $i++) {
    $deepAuthor += "object.create Deep$i Empty"
    if ($i -gt 0) { $deepAuthor += "object.parent Deep$i Deep$($i - 1)" }
}
$deepBlock = ($deepAuthor -join "`n")

# 넓은 계층: 루트 아래 형제 N개, 각자 자식 하나. children 목록이 큰 경우와
# 2단 깊이를 함께 만든다.
$wideAuthor = @()
for ($i = 0; $i -lt $WideSiblings; $i++) {
    $wideAuthor += "object.create Wide$i Empty"
    $wideAuthor += "object.create WideKid$i Empty"
    $wideAuthor += "object.parent WideKid$i Wide$i"
}
$wideBlock = ($wideAuthor -join "`n")

# 하한: 저작한 수에서 나온다 — 손으로 유지하지 않는다. 씬 루트가 하나 더 잡히므로
# 저작 수 자체를 하한으로 쓴다(그보다 적으면 저작이 실패한 것이다).
$deepMin = $DeepChain
$wideMin = $WideSiblings * 2

$deepTmp = Join-Path $Work "HierarchyDeep.creator"
$wideTmp = Join-Path $Work "HierarchyWide.creator"
foreach ($p in @($deepTmp, $wideTmp)) { if (Test-Path $p) { Remove-Item $p -Force } }

$scenario = Join-Path $Work "hierarchy_convention_resolved.txt"
((((Get-Content $template -Raw) -replace '\{\{DEEP_AUTHOR\}\}', $deepBlock) `
    -replace '\{\{WIDE_AUTHOR\}\}', $wideBlock) `
    -replace '\{\{DEEP_TMP\}\}', ($deepTmp -replace '\\', '/')) `
    -replace '\{\{WIDE_TMP\}\}', ($wideTmp -replace '\\', '/') |
    Set-Content $scenario -Encoding UTF8

$outPath = Join-Path $Work "hierarchy_convention.out"
$errPath = Join-Path $Work "hierarchy_convention.err"

$proc = Start-Process -FilePath $Exe -ArgumentList "--script", $scenario `
    -WorkingDirectory $exeDir `
    -RedirectStandardOutput $outPath `
    -RedirectStandardError $errPath -PassThru

$proc.WaitForExit($TimeoutSeconds * 1000) | Out-Null
if (-not $proc.HasExited) {
    $proc.Kill()
    "타임아웃 ($TimeoutSeconds 초). 시나리오가 끝나지 않았다."
    exit 1
}

$text = Get-Content $outPath -Raw
$pattern = '\[scene\.hierarchycheck\] 오브젝트 (\d+) · 최상위\(0표기\) (\d+) · 최상위\(-1표기\) (\d+)' +
           ' · 쌍불일치 (\d+) · 고아 (\d+) · 순회미도달 (\d+) · Store불일치 (\d+)'
$found = [regex]::Matches($text, $pattern)

# 측정 순서는 시나리오가 정한다: 깊은·갓만듦 → 깊은·왕복후 → 넓은·갓만듦 → 넓은·왕복후.
$labels  = @('깊은·갓만듦', '깊은·왕복후', '넓은·갓만듦', '넓은·왕복후')
$minimums = @($deepMin, $deepMin, $wideMin, $wideMin)
$failed = @()

if ($found.Count -ne 4) {
    "측정이 $($found.Count) 건뿐이다 (기대 4). 시나리오가 도중에 멈췄다."
    if ($proc.ExitCode -ne 0) { "  종료 코드: 0x{0:X8}" -f $proc.ExitCode }
    exit 1
}

for ($i = 0; $i -lt 4; $i++) {
    $g = $found[$i].Groups
    $name        = $labels[$i]
    $MinObjects  = $minimums[$i]
    $total       = [int]$g[1].Value
    $topRoot     = [int]$g[2].Value
    $topInvalid  = [int]$g[3].Value
    $mismatch    = [int]$g[4].Value
    $orphan      = [int]$g[5].Value
    $unreachable = [int]$g[6].Value
    $storeMismatch = [int]$g[7].Value

    "{0,-14} 오브젝트 {1,3} · 최상위(0표기) {2,2} · 최상위(-1표기) {3,2} · 쌍불일치 {4} · 고아 {5} · 순회미도달 {6} · Store불일치 {7}" -f `
        $name, $total, $topRoot, $topInvalid, $mismatch, $orphan, $unreachable, $storeMismatch

    if ($total -lt $MinObjects) {
        $failed += "$name 오브젝트가 $total 개다(기대 $MinObjects 이상) — CLI 저작이 안 먹었거나 씬이 로드되지 않았다. 빈 씬을 재고 통과하면 안 된다"
    }
    if ($topRoot -lt 1) {
        $failed += "$name 최상위 오브젝트가 0개다 — 검사가 아무것도 재지 못했다"
    }
    if ($topInvalid -ne 0) {
        # '갓만듦'에서만 나오면 **생성 경로**가 범인이다(Scene::CreateEntity).
        # '왕복후'에서도 나오면 로더/Attach 쪽이다. 라벨이 그 둘을 갈라 준다.
        $failed += "$name 최상위(-1표기)가 $topInvalid 건이다 — 루트 규약이 갈렸다(갓만듦에서만 나오면 Scene::CreateEntity, 왕복후에도 나오면 AttachExistingEntity/SceneManager 리맵 폴백)"
    }
    if ($mismatch -ne 0) {
        $failed += "$name 쌍불일치 $mismatch 건 — 부모의 children에 실렸는데 m_parentIndex가 그 부모가 아니다"
    }
    if ($orphan -ne 0) {
        $failed += "$name 고아 $orphan 건 — 아무의 children에도 없다"
    }
    if ($unreachable -ne 0) {
        $failed += "$name 순회미도달 $unreachable 건 — 씬 루트에서 children만 따라 닿지 못한다(서브트리가 통째로 빠진다)"
    }
    if ($storeMismatch -ne 0) {
        $failed += "$name HierarchyStore shadow 불일치 $storeMismatch 건 — Entity 계층 정본 쓰기가 Store에 반영되지 않았다"
    }
}

if ($proc.ExitCode -ne 0) { $failed += ("종료 코드 비정상: 0x{0:X8}" -f $proc.ExitCode) }

if ($failed.Count -gt 0) {
    ""
    "실패 $($failed.Count)건:"
    $failed | ForEach-Object { "  $_" }
    exit 1
}

""
"전체 통과 — 계층 표기가 하나로 모였고 순회가 모든 오브젝트에 닿는다"
exit 0
