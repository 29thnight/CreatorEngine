# DDOL 씬 이송 — 캔버스 캐시 재등록 대조 (트랙 E · E5-R2 후속)
#
# ── 이 검사가 메우는 구멍 ──
#
# DontDestroyOnLoad 이송 경로(Scene::DetachEntityHierarchy →
# AttachExistingEntityHierarchy)는 SceneManager의 씬 로드 안에서만 불린다.
# 회귀 세트의 다른 검사는 전부 단일 씬이거나 씬 전환을 하더라도 DDOL 오브젝트를
# 두지 않아, 이 경로를 **단 한 번도 태운 적이 없었다.**
#
# 그 사각지대에서 살아남은 결함: Canvas의 캐시 등록이 OnDeserialized에만 있어
# 이송된 캔버스가 새 씬의 목록에 들어가지 않았다. 오브젝트는 살아 넘어오므로
# ImageComponent 연결 같은 것은 멀쩡해 보이고, 캔버스 목록만 비어 UI 내비게이션이
# 조용히 죽는다.
#
# ── 왜 ui.status의 숫자를 세는가 ──
#
# 캔버스 목록은 Scene의 private 멤버이고 밖으로 나오는 유일한 창구가 ui.status다.
# 그 줄이 "캔버스 N개 [이름(자식수), ...]" 형태로 목록을 통째로 찍는다 —
# 개수만이 아니라 이름까지 나오므로 "1개"가 무엇인지 확인할 수 있다.
#
# ── 판정 항목 ──
#
#   1  이송 전 캔버스 1개  — 시나리오가 실제로 캔버스를 태웠는가(0이면 검사가 눈 감음)
#   2  DDOL=1              — scene.ddol 지정이 먹었는가
#   3  이송 후 캔버스 1개  — ★ 본 판정. 재등록이 일어났는가
#
# 음성 시험(2026-08-20, 상태 셋을 실제로 돌렸다):
#
#   수정 전 코드(등록이 OnDeserialized에만)  이송 전 1 · 이송 후 0  -> 판정 3 실패
#   등록을 전부 무력화                        이송 전 0 · 이송 후 0  -> 판정 1·3 실패
#   수정 후                                   이송 전 1 · 이송 후 1  -> 통과
#
# 첫 줄이 이 게이트가 겨냥한 결함의 정확한 모습이다 — 등록 자체는 되지만 이송을
# 못 따라간다. 둘째 줄은 판정 1이 "검사가 눈 감음"을 잡는지 확인한 것이다.
#
# 사용법:
#   pwsh Tools\regression\verify-ddol-canvas.ps1
param(
    [string]$Exe = "C:\Users\lance\source\CreatorEngine\x64\Debug\Academy_4Q.exe",
    [string]$Work = $env:TEMP,
    [int]$TimeoutSeconds = 300,
    # 출발 씬은 캔버스를 갖고, 목적 씬은 갖지 않아야 한다 — 그래야 이송 후의
    # "1개"가 이송분임이 확정된다. 이제 **둘 다 시나리오가 만든다**(2026-08-21):
    # 출발은 scene.new + object.create로, 목적은 빈 씬을 저장해서. 그래서 여기
    # 남는 인자는 목적지 파일 이름 하나뿐이다.
    #
    # 예전 기본값(UITestScene · FT_Material)은 둘 다 저작 자산이었고, 그중
    # UITestScene은 아예 .gitignore로 무시되는 파일이라 이 게이트가 이 기계
    # 밖에서는 돌지 않았다.
    [string]$SceneWithoutCanvas = "DdolCanvasDest"
)

$exeDir = [System.IO.Path]::GetDirectoryName($Exe)
if (-not (Test-Path $Exe)) { "실행 파일이 없다: $Exe"; exit 1 }

$template = Join-Path $PSScriptRoot "ddol_canvas_probe.txt"
if (-not (Test-Path $template)) { "시나리오가 없다: $template"; exit 1 }

$repoRoot = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
$sceneDir = Join-Path $repoRoot "Dynamic_CPP\Assets\Scenes"

$dstScene = Join-Path $sceneDir "$SceneWithoutCanvas.creator"

# ★ 실행 전에 지운다. 남겨 두면 이전 실행이 만든 파일 덕에 scene.save가 실패해도
# scene.switch가 성공해 버린다 — 저장 경로가 죽은 채 게이트가 통과하는 거짓 통과다.
if (Test-Path $dstScene) { Remove-Item -LiteralPath $dstScene -Force }

# 경로는 실행 직전에 채운다 — 시나리오 파일에 절대 경로를 박으면 다른 작업
# 폴더에서 조용히 실패하고, 그 실패가 "캔버스 0개"라는 정상처럼 보이는 모습으로
# 나타난다.
$scenario = Join-Path $Work "ddol_canvas_resolved.txt"
(Get-Content $template -Raw) `
    -replace '\{\{SCENE_WITHOUT_CANVAS\}\}', ($dstScene -replace '\\', '/') |
    Set-Content $scenario -Encoding UTF8

$outPath = Join-Path $Work "ddol_canvas.out"
$errPath = Join-Path $Work "ddol_canvas.err"

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

if (-not (Test-Path $outPath)) { "표준 출력이 없다: $outPath"; exit 1 }
$out = Get-Content -LiteralPath $outPath

# ui.status는 std::printf로 stdout에 나간다(Debug->LogWarning은 stdout에 안 간다 —
# verify-asan-lifecycle.ps1의 같은 주석 참고).
$statusLines = @($out | Where-Object { $_ -match '\[UI 상태\]' })
$ddolLine = @($out | Where-Object { $_ -match 'scene\.ddol 지정' }) | Select-Object -First 1

function Get-CanvasCount([string]$line) {
    if ($line -match '캔버스\s+(\d+)개') { return [int]$Matches[1] }
    return -1
}
function Get-CanvasNames([string]$line) {
    if ($line -match '캔버스\s+\d+개\s+\[(.*)\]\s*$') { return $Matches[1] }
    return ""
}

$failed = @()

$rootRefs = [regex]::Matches(($out -join "`n"),
    '\[object\.rootref\]\s+DdolRootRef\s+root=(-?\d+)\s+name=(\S+)')
if ($rootRefs.Count -ne 2) {
    $failed += "DDOL rootref 측정이 $($rootRefs.Count)회다(기대 출발/도착 2회)"
}
else {
    $sourceRootSlot = [int]$rootRefs[0].Groups[1].Value
    $targetRootSlot = [int]$rootRefs[1].Groups[1].Value
    if ($rootRefs[0].Groups[2].Value -ne 'DdolCanvas' -or
        $rootRefs[1].Groups[2].Value -ne 'DdolCanvas') {
        $failed += "DDOL rootref가 이송 전후 DdolCanvas를 가리키지 않는다"
    }
    if ($sourceRootSlot -eq $targetRootSlot) {
        $failed += "DDOL rootref 슬롯이 이송 전후 모두 $sourceRootSlot 이다 — 목적지 padding이 remap을 강제하지 못했다"
    }
}

$hierarchyLines = @($out | Where-Object { $_ -match '\[scene\.hierarchycheck\]' })
if ($hierarchyLines.Count -ne 1) {
    $failed += "scene.hierarchycheck 줄이 $($hierarchyLines.Count)개다(기대 1) — DDOL 이송 후 계층 검사가 실행되지 않았다"
}
else {
    $hierarchyPattern = '오브젝트\s+(\d+).*최상위\(-1표기\)\s+(\d+).*쌍불일치\s+(\d+).*고아\s+(\d+).*순회미도달\s+(\d+).*Store불일치\s+(\d+)'
    $hierarchy = [regex]::Match($hierarchyLines[0], $hierarchyPattern)
    if (-not $hierarchy.Success) {
        $failed += "scene.hierarchycheck 전체 계층 측정값을 읽지 못했다"
    }
    else {
        $values = 1..6 | ForEach-Object { [int]$hierarchy.Groups[$_].Value }
        if ($values[0] -lt 2) { $failed += "DDOL 이송 후 Entity가 $($values[0])개뿐이다(기대 2 이상)" }
        if ($values[1] -ne 0) { $failed += "DDOL 이송 후 최상위(-1표기)가 $($values[1])건이다" }
        if ($values[2] -ne 0) { $failed += "DDOL 이송 후 부모/children 쌍불일치가 $($values[2])건이다" }
        if ($values[3] -ne 0) { $failed += "DDOL 이송 후 고아가 $($values[3])건이다" }
        if ($values[4] -ne 0) { $failed += "DDOL 이송 후 순회미도달이 $($values[4])건이다" }
        if ($values[5] -ne 0) { $failed += "DDOL 이송 후 HierarchyStore shadow 불일치가 $($values[5])건이다" }
    }
}

# 판정 0 — 목적지 씬이 실제로 만들어졌는가. 이 줄이 없으면 이송이 애초에 일어날
# 수 없고, 그때의 실패 모습("캔버스 0개")은 재등록 결함과 구분되지 않는다.
$saveOk = @($out | Where-Object { $_ -match '\[CLI\] 씬 저장:' }).Count -ge 1
if (-not $saveOk) {
    "목적지 씬이 저장되지 않았다 — scene.save가 실패했다($dstScene)."
    "이송 판정은 의미가 없다. 출력: $outPath"
    exit 1
}

if ($statusLines.Count -lt 2) {
    "ui.status 줄이 $($statusLines.Count)개다 (기대: 2 — 이송 전/후)"
    "시나리오가 중간에 멈췄을 수 있다. 출력: $outPath"
    exit 1
}

$before = Get-CanvasCount $statusLines[0]
$after  = Get-CanvasCount $statusLines[-1]
$afterNames = Get-CanvasNames $statusLines[-1]
$ddolOk = ($null -ne $ddolLine) -and ($ddolLine -match 'DDOL=1')

"이송 전 캔버스 $before 개 (0이면 검사가 빈 씬을 재고 있다)"
"DDOL 지정  : $ddolOk"
"이송 후 캔버스 $after 개 [$afterNames]  ← 본 판정"
if ($rootRefs.Count -eq 2) {
    "rootref 슬롯 $sourceRootSlot -> $targetRootSlot · 이름 DdolCanvas 유지"
}
if ($hierarchyLines.Count -eq 1) { $hierarchyLines[0] }
""

if ($before -lt 1) {
    $failed += "이송 전 캔버스가 $before 개다 — 출발 씬에 캔버스가 없으면 이 검사는 아무것도 재지 못한다"
}
if (-not $ddolOk) {
    $failed += "scene.ddol 지정이 확인되지 않았다 — 대상 이름(Canvas)이 출발 씬에 있는지 확인할 것"
}
if ($after -lt 1) {
    $failed += "이송 후 캔버스가 $after 개다 — DDOL 이송된 캔버스가 새 씬의 캐시에 재등록되지 않았다(Canvas::OnAddedToScene 확인)"
}
if ($after -gt $before) {
    $failed += "이송 후 캔버스가 $after 개로 늘었다 — 캐시에 중복이 들어갔다(Scene::AddCanvas의 멱등성 확인)"
}

if ($proc.ExitCode -ne 0) { $failed += ("종료 코드 비정상: 0x{0:X8}" -f $proc.ExitCode) }

if ($failed.Count -gt 0) {
    "실패 $($failed.Count)건:"
    $failed | ForEach-Object { "  $_" }
    exit 1
}

"전체 통과 — DDOL 이송된 캔버스가 새 씬의 캐시에 재등록된다"
exit 0
