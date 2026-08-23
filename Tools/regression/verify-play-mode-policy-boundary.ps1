# 재생 전환 정책의 소유권 경계 (E3-2)
#
# 무엇을 지키는가
# ─────────────
# Editor의 재생 정책(Undo 이력 폐기)이 Core로 되돌아오지 않는지, 그리고 Player가
# 그 정책을 링크하지 않는지 본다.
#
# 왜 필요한가
# ──────────
# BeginPlayTransaction은 Player도 탄다 — Player의 유일한 재생 진입 경로다. 예전에는
# 그 안에서 UndoCommandManager를 직접 비웠고, 그래서 출하 게임이 매 시작마다 쓰지도
# 않는 Undo 스택을 비우고 있었다. 지금은 PlayModeEvent 통지만 하고 Editor의
# PlayModeController가 구독한다. 이 성질은 런타임 관측으로는 확인하기 어렵다 —
# Player에서 아무 일도 안 일어나는 것이 정상이라 "안 일어남"을 재려면 정적으로 봐야 한다.
#
# ⚠ 통과가 곧 검증은 아니다
# ────────────────────────
# 이 검사는 "심볼이 없다"를 단정한다. 대상 파일을 못 찾거나 정규식이 빗나가도
# 0건이 나오므로, 아래에서 **찾을 수 있어야 하는 것을 먼저 찾는다** — 파일이 실재하고
# 이관된 코드가 Editor 쪽에 실제로 있는지 확인한 뒤에야 부재 단정이 의미를 갖는다.
$ErrorActionPreference = "Stop"
$repoRoot = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent

$failures = @()

# ── 양성 확인 1: Editor 소유자가 실재하고 Undo를 실제로 다룬다 ──
$controller = Join-Path $repoRoot "EngineEntry\EditorPlayModeController.cpp"
if (-not (Test-Path -LiteralPath $controller)) {
    throw "EditorPlayModeController.cpp가 없다 — 아래 부재 단정이 전부 무의미해진다: $controller"
}
$controllerText = Get-Content -LiteralPath $controller -Raw -Encoding UTF8
# (E1-6 이관으로 inline 별칭 UndoCommandManager가 사라졌다 — GetInstance() 직접 호출)
if ($controllerText -notmatch 'UndoManager::GetInstance\(\)->Clear\(\)') {
    $failures += "EditorPlayModeController가 Undo 이력을 비우지 않는다 — 정책이 어디로 갔는가?"
}
if ($controllerText -notmatch 'PlayModeEvent\.AddLambda') {
    $failures += "EditorPlayModeController가 PlayModeEvent를 구독하지 않는다 — 정책이 영영 안 돈다"
}

# ── 양성 확인 2: Core가 통지를 실제로 던진다 ──
$sceneManager = Join-Path $repoRoot "ScriptBinder\SceneManager.cpp"
$sceneManagerText = Get-Content -LiteralPath $sceneManager -Raw -Encoding UTF8
if ($sceneManagerText -notmatch 'PlayModeEvent\.Broadcast\(true\)') {
    $failures += "SceneManager가 재생 진입 통지를 던지지 않는다 — 구독자가 있어도 안 불린다"
}

# ── 부재 단정 1: Core가 Undo를 모른다 ──
#
# ReflectionFunction.h는 E1-6 이관 전까지 ReflectionUndo.h를 물고
# MakeCustomChangeCommand로 UndoManager를 실사용하던 마지막 Core 지점이다 —
# 재유입을 여기서 막는다. 이관 경위를 설명하는 // 주석은 걸러낸다(그것까지
# 세면 설명을 지워야 통과하는 게이트가 된다 — E3-4의 주석 관용 원칙).
foreach ($rel in @("ScriptBinder\SceneManager.cpp", "ScriptBinder\SceneManager.h",
                   "Utility_Framework\ReflectionFunction.h")) {
    $path = Join-Path $repoRoot $rel
    $codeLines = Get-Content -LiteralPath $path -Encoding UTF8 |
        ForEach-Object { ($_ -split '//', 2)[0] }
    $text = $codeLines -join "`n"
    if ($text -match 'UndoCommandManager|UndoManager|ReflectionUndo') {
        $failures += "$rel 이 Undo를 다시 참조한다 (E3 판정 기준: Core는 Undo를 모른다)"
    }
}

# ── 양성 확인 3: Player가 시작 씬 로드 성공 시 재생을 켠다 (E3-6) ──
#
# ⚠ 이것은 런타임 커버리지의 구멍을 메우는 정적 단정이다.
#
# 이 요청은 예전에 SceneManager::LoadSceneImmediate가 EngineMode::IsPlayer()를
# 물어보고 스스로 했다. E3-6이 PlayerMain으로 옮겼는데, **Player 스모크는 이것을
# 검증하지 못한다** — SceneManager::Initialization이 DrainPendingLifecycle을 재생
# 여부와 무관하게 매 프레임 부르므로, 스모크가 보는 OnInitialized/OnBeginSimulation
# 마커는 SetGameStart(true)가 없어도 그대로 나온다. 스모크가 실제로 지키는 것은
# "Scene loaded" 마커뿐이다.
#
# SetGameStart(true)가 빠지면 조용히 망가지는 것들: 시뮬레이션 phase 전이,
# 입력(InputActionManager·PlayerInput이 !IsGameStart면 즉시 return), 애니메이션 잡.
# 스모크는 전부 통과한 채로 입력이 죽은 게임이 출하된다. 그래서 소스에 못 박는다.
$playerMain = Join-Path $repoRoot "Player\PlayerMain.cpp"
if (-not (Test-Path -LiteralPath $playerMain)) {
    throw "PlayerMain.cpp가 없다 — 단정이 무의미해진다: $playerMain"
}
$playerMainText = Get-Content -LiteralPath $playerMain -Raw -Encoding UTF8
if ($playerMainText -notmatch 'SetGameStart\(true\)') {
    $failures += "PlayerMain이 시작 씬 로드 후 재생을 켜지 않는다 — 입력·애니메이션이 죽은 채 출하된다(스모크는 이것을 못 잡는다)"
}
if ($playerMainText -notmatch 'Scene loaded: ') {
    $failures += "PlayerMain이 'Scene loaded' 스모크 마커를 찍지 않는다 — Tools/build.ps1의 게임 빌드 검증이 깨진다"
}

# ── 부재 단정 2: Player가 Undo/선택 정책을 링크하지 않는다 ──
# 빌드 산출물(.obj/.pdb)은 Core를 링크해서 심볼이 잡히므로 소스만 본다.
$playerSources = Get-ChildItem (Join-Path $repoRoot "Player") -Recurse -File `
    -Include *.cpp,*.h,*.hpp -ErrorAction SilentlyContinue |
    Where-Object { $_.FullName -notmatch '\\(x64|Debug|Release|\.vs)\\' }
if ($playerSources.Count -eq 0) {
    throw "Player 소스를 하나도 못 찾았다 — 부재 단정이 무의미해진다"
}
foreach ($file in $playerSources) {
    $text = Get-Content -LiteralPath $file.FullName -Raw -Encoding UTF8
    if ($text -match 'UndoCommandManager|resetSelectedObjectEvent|EditorPlayModeController') {
        $rel = [System.IO.Path]::GetRelativePath($repoRoot, $file.FullName)
        $failures += "$rel 이 Editor 재생 정책을 참조한다 — Player는 이것을 링크하면 안 된다"
    }
}

# ── 부재 단정 3: 선택 해제는 Core에 남아 있어야 한다 ──
# 이것은 "옮기지 않았는지"를 보는 역방향 단정이다. resetSelectedObjectEvent.Broadcast()가
# AllDestroyMark보다 **먼저** 불려야 정지 후 m_selectedEntity가 파괴된 엔티티를 가리키지
# 않는다. 실측: 이 한 줄을 빼면 재생 왕복이 ACCESS_VIOLATION으로 죽는다.
#
# ⚠ 반드시 EndPlayTransaction 본문 안에서만 비교한다. AllDestroyMark는 이 파일에
#   5번 나오고(종료·씬 교체 경로 등) 파일 전체에서 IndexOf를 쓰면 엉뚱한 등장과
#   비교해 거짓 실패가 난다 — 이 게이트를 처음 쓸 때 실제로 그렇게 틀렸다.
$fnStart = $sceneManagerText.IndexOf("void SceneManager::EndPlayTransaction()")
if ($fnStart -lt 0) {
    $failures += "EndPlayTransaction을 못 찾았다 — 순서 단정이 무의미해진다"
} else {
    $fnEnd = $sceneManagerText.IndexOf("`nvoid SceneManager::", $fnStart + 1)
    if ($fnEnd -lt 0) { $fnEnd = $sceneManagerText.Length }
    $body = $sceneManagerText.Substring($fnStart, $fnEnd - $fnStart)

    $broadcastIdx = $body.IndexOf("resetSelectedObjectEvent.Broadcast();")
    $destroyIdx = $body.IndexOf("AllDestroyMark();")
    if ($broadcastIdx -lt 0) {
        $failures += "정지 경로의 resetSelectedObjectEvent.Broadcast()가 사라졌다 — 정지 후 선택이 파괴된 엔티티를 가리킨다(실측: 크래시)"
    } elseif ($destroyIdx -lt 0) {
        $failures += "EndPlayTransaction 안에서 AllDestroyMark() 호출을 못 찾았다 — 순서 단정이 무의미해진다"
    } elseif ($broadcastIdx -gt $destroyIdx) {
        $failures += "선택 해제가 AllDestroyMark 뒤로 밀렸다 — 파괴된 엔티티를 가리키는 한 프레임이 생긴다(실측: 크래시)"
    }
}

if ($failures.Count -gt 0) {
    "재생 정책 경계 실패 $($failures.Count)건:"
    $failures | ForEach-Object { "  $_" }
    exit 1
}

"play-mode policy boundary: PASS (Undo는 Editor 소유, Player 참조 0건, 선택 해제는 파괴 이전 유지)"
