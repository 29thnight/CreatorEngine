# PrefabEditor 소유권 경계 (E3-4)
#
# 무엇을 지키는가
# ─────────────
# 프리팹 편집 모드가 Editor 소유로 남아 있고, Core와 Player가 그것을 모르는 상태를
# 지킨다.
#
# 왜 정적으로 보는가
# ────────────────
# Player에서 "PrefabEditor가 없다"는 관측할 것이 없는 성질이다 — 정상이 곧 부재다.
# 게다가 링커는 참조되지 않는 코드를 이미 버리므로, 옮기기 전에도 Player.exe에
# PrefabEditor 흔적은 0건이었다(실측). 즉 바이너리를 봐서는 이관 전후를 구분할 수
# 없다. 이 이관이 바꾼 것은 바이너리가 아니라 **컴파일 대상과 층 경계**다.
#
# ⚠ 통과가 곧 검증은 아니다
# ────────────────────────
# 부재 단정만 두면 대상을 못 찾아도 0건이 나온다. 찾을 수 있어야 하는 것을 먼저 찾는다.
$ErrorActionPreference = "Stop"
$repoRoot = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent

$failures = @()

# ── 양성 확인 1: Editor 쪽에 실물이 있다 ──
foreach ($rel in @("Editor\EngineEntry\PrefabEditor.h", "Editor\EngineEntry\PrefabEditor.cpp")) {
    if (-not (Test-Path -LiteralPath (Join-Path $repoRoot $rel))) {
        throw "$rel 이 없다 — 아래 부재 단정이 전부 무의미해진다"
    }
}
$editorSource = Get-Content (Join-Path $repoRoot "Editor\EngineEntry\PrefabEditor.cpp") -Raw -Encoding UTF8
if ($editorSource -notmatch 'void PrefabEditor::Open\(') {
    $failures += "Editor/EngineEntry/PrefabEditor.cpp에 Open 구현이 없다 — 껍데기만 옮겨졌는가?"
}

# ── 양성 확인 2: Editor 층 프로젝트가 그것을 컴파일한다 ──
#
# E6-2(2026-08-23)부터 소유는 Academy_4Q 직접 편입이 아니라 에디터 층
# 정적 라이브러리이고, E7-d(2026-08-24)부터 그 라이브러리는 통합
# Editor.vcxproj다(Editor\ 직하 소재라 항목 경로는 EngineEntry\ 접두).
# 게이트가 지키는 성질은 "에디터 층이 컴파일하고 Player 링크 사슬에 없다"이지
# 어느 에디터 프로젝트인가가 아니다.
$editorProject = Get-Content (Join-Path $repoRoot "Editor\Editor.vcxproj") -Raw -Encoding UTF8
if ($editorProject -notmatch 'ClCompile Include="EngineEntry\\PrefabEditor\.cpp"') {
    $failures += "Editor.vcxproj가 PrefabEditor.cpp를 컴파일하지 않는다"
}
$playerProject = Get-Content (Join-Path $repoRoot "Player\Player.vcxproj") -Raw -Encoding UTF8
if ($playerProject -match 'Editor\.vcxproj|EditorRuntime') {
    $failures += "Player.vcxproj가 에디터 라이브러리를 참조한다 — 저작 도구가 Player 링크 사슬에 들어간다"
}

# ── 부재 단정 1: Core 프로젝트가 더 이상 컴파일하지 않는다 ──
# ScriptBinder는 Player가 링크하는 정적 라이브러리다. 여기 남아 있으면 저작 도구가
# 출하 게임의 라이브러리에 그대로 들어간다.
$coreProject = Get-Content (Join-Path $repoRoot "Engine\SceneRuntime\SceneRuntime.vcxproj") -Raw -Encoding UTF8
if ($coreProject -match 'PrefabEditor\.(cpp|h)') {
    $failures += "SceneRuntime.vcxproj가 아직 PrefabEditor를 컴파일한다 — Player가 링크하는 라이브러리에 저작 도구가 들어간다"
}

# ── 부재 단정 2: Core 소스가 심볼을 쓰지 않는다 ──
#
# ⚠ 주석은 걸러낸다. Core의 여러 파일이 "PrefabEditor가 하던 일"을 설명하는 주석을
#   갖고 있고, 그것까지 위반으로 세면 설명을 지워야 통과하는 게이트가 된다 —
#   이 저장소의 include 경계 검사가 실제로 그 문제를 안고 있다.
# E7-b(2026-08-24)부터 Core 소스는 Engine\ 그룹 밑이다. 옛 루트 경로를 그대로
# 두면 Test-Path 필터가 전부 걸러 — 또는 우연히 남은 잔재 폴더 하나만 걸려 —
# 부재 단정이 빈 집합 위에서 도는 무의미한 게이트가 된다.
$coreRoots = @('Engine\SceneRuntime', 'Engine\RenderEngine', 'Engine\Utility_Framework') |
    ForEach-Object { Join-Path $repoRoot $_ } | Where-Object { Test-Path $_ }
$coreFiles = Get-ChildItem $coreRoots -Recurse -File -Include *.h,*.hpp,*.cpp,*.inl |
    Where-Object { $_.FullName -notmatch '\\(x64|Debug|Release|GameBuild|\.vs|ThirdParty|vcpkg_installed|Generated)\\' }
if ($coreFiles.Count -eq 0) { throw "Core 소스를 하나도 못 찾았다 — 단정이 무의미해진다" }

foreach ($file in $coreFiles) {
    $text = Get-Content -LiteralPath $file.FullName -Raw -Encoding UTF8
    # 한 줄 주석과 블록 주석 제거 (문자열 리터럴 안의 // 는 무시 — 이 저장소에는 없다)
    $code = [regex]::Replace($text, '/\*.*?\*/', '', 'Singleline')
    $code = [regex]::Replace($code, '//[^\r\n]*', '')
    if ($code -match '\bPrefabEditors?\b') {
        $rel = [System.IO.Path]::GetRelativePath($repoRoot, $file.FullName)
        $failures += "$rel 이 PrefabEditor 심볼을 쓴다 — Core는 Editor 저작 도구를 몰라야 한다"
    }
}

# ── 부재 단정 3: Player 소스가 모른다 ──
$playerFiles = Get-ChildItem (Join-Path $repoRoot "Player") -Recurse -File -Include *.cpp,*.h,*.hpp -ErrorAction SilentlyContinue |
    Where-Object { $_.FullName -notmatch '\\(x64|Debug|Release|\.vs)\\' }
if ($playerFiles.Count -eq 0) { throw "Player 소스를 하나도 못 찾았다 — 단정이 무의미해진다" }
foreach ($file in $playerFiles) {
    if ((Get-Content -LiteralPath $file.FullName -Raw -Encoding UTF8) -match 'PrefabEditor') {
        $rel = [System.IO.Path]::GetRelativePath($repoRoot, $file.FullName)
        $failures += "$rel 이 PrefabEditor를 참조한다"
    }
}

if ($failures.Count -gt 0) {
    "PrefabEditor 소유권 실패 $($failures.Count)건:"
    $failures | ForEach-Object { "  $_" }
    exit 1
}

"prefab editor ownership: PASS (Editor 소유, Core 심볼 0건, Player 참조 0건)"
