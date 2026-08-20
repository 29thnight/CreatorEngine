# 계층 표기 불변식 대조 (SceneGraphRedesignPlan 트랙 E — 루트 규약 통일)
#
# 시나리오와 이 검사가 무엇을 메우는지는 hierarchy_convention.txt 상단에 있다.
#
# ── 판정 항목 ──
#
#   1  씬 두 개를 다 쟀다              — 시나리오가 도중에 멈추지 않았다
#   2  오브젝트 수가 최소치 이상       — 경로가 안 먹어 빈 씬을 재고 통과하는 것을 막는다
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
# 음성 시험(2026-08-20 실측, 통일 직전):
#   Test1        최상위(0) 1 · 최상위(-1) 2   -> 판정 3 실패
#   FT_Material  최상위(0) 2 · 최상위(-1) 2   -> 판정 3 실패
#
# 사용법:
#   pwsh Tools\regression\verify-hierarchy-convention.ps1
param(
    [string]$Exe = "C:\Users\lance\source\CreatorEngine\x64\Debug\Academy_4Q.exe",
    [string]$Work = $env:TEMP,
    [int]$TimeoutSeconds = 300,
    [int]$MinObjects = 10,
    [string]$DeepScene = "Test1",
    [string]$WideScene = "FT_Material"
)

$exeDir = [System.IO.Path]::GetDirectoryName($Exe)
if (-not (Test-Path $Exe)) { "실행 파일이 없다: $Exe"; exit 1 }

$template = Join-Path $PSScriptRoot "hierarchy_convention.txt"
if (-not (Test-Path $template)) { "시나리오가 없다: $template"; exit 1 }

$repoRoot = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
$sceneDir = Join-Path $repoRoot "Dynamic_CPP\Assets\Scenes"

$deep = Join-Path $sceneDir "$DeepScene.creator"
$wide = Join-Path $sceneDir "$WideScene.creator"
foreach ($p in @($deep, $wide)) {
    if (-not (Test-Path $p)) { "씬이 없다: $p"; exit 1 }
}

$scenario = Join-Path $Work "hierarchy_convention_resolved.txt"
((Get-Content $template -Raw) -replace '\{\{DEEP_SCENE\}\}', ($deep -replace '\\', '/')) `
    -replace '\{\{WIDE_SCENE\}\}', ($wide -replace '\\', '/') |
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
           ' · 쌍불일치 (\d+) · 고아 (\d+) · 순회미도달 (\d+)'
$found = [regex]::Matches($text, $pattern)

$labels = @($DeepScene, $WideScene)
$failed = @()

if ($found.Count -lt 2) {
    "측정이 $($found.Count) 건뿐이다 (기대 2). 시나리오가 도중에 멈췄다."
    if ($proc.ExitCode -ne 0) { "  종료 코드: 0x{0:X8}" -f $proc.ExitCode }
    exit 1
}

for ($i = 0; $i -lt 2; $i++) {
    $g = $found[$i].Groups
    $name        = $labels[$i]
    $total       = [int]$g[1].Value
    $topRoot     = [int]$g[2].Value
    $topInvalid  = [int]$g[3].Value
    $mismatch    = [int]$g[4].Value
    $orphan      = [int]$g[5].Value
    $unreachable = [int]$g[6].Value

    "{0,-14} 오브젝트 {1,3} · 최상위(0표기) {2,2} · 최상위(-1표기) {3,2} · 쌍불일치 {4} · 고아 {5} · 순회미도달 {6}" -f `
        $name, $total, $topRoot, $topInvalid, $mismatch, $orphan, $unreachable

    if ($total -lt $MinObjects) {
        $failed += "$name 오브젝트가 $total 개다 — 씬이 로드되지 않았을 가능성이 크다(경로 형식 확인). 빈 씬을 재고 통과하면 안 된다"
    }
    if ($topRoot -lt 1) {
        $failed += "$name 최상위 오브젝트가 0개다 — 검사가 아무것도 재지 못했다"
    }
    if ($topInvalid -ne 0) {
        $failed += "$name 최상위(-1표기)가 $topInvalid 건이다 — 루트 규약이 갈렸다(Scene::AttachExistingGameObject / SceneManager 리맵의 폴백 확인)"
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
