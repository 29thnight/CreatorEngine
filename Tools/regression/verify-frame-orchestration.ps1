# 프레임 오케스트레이션 단일 소유 (E3-7)
#
# 무엇을 지키는가
# ─────────────
# 재생 중 시뮬레이션 순서를 `Runtime::TickSimulationFrame` 하나가 소유하고,
# 두 Host가 그것을 호출만 하는 상태를 지킨다.
#
# 왜 필요한가
# ──────────
# 이관 전에는 Editor와 Player가 각자 프레임 루프를 들고 있었고, 재생 중 구간은
# 순서까지 글자 그대로 같았다(관리 틱 두 함수는 주석만 다르고 본문이 완전히 동일).
# 복제된 순서는 한쪽만 고치면 조용히 갈라지고, 그러면 "에디터에서는 되는데 빌드하면
# 안 된다"가 된다. 그 갈라짐은 런타임 검사로 잡기 어렵다 — 두 Host를 같은 시나리오로
# 나란히 태우는 하네스가 없다. 그래서 소스에서 못 박는다.
#
# ⚠ 통과가 곧 검증은 아니다
# ────────────────────────
# 부재 단정("Host가 Physics를 직접 부르지 않는다")만 두면 파일을 못 찾아도 0건이
# 나온다. 그래서 **찾을 수 있어야 하는 것을 먼저 찾는다** — primitive가 실재하고
# 순서를 담고 있는지, 두 Host가 실제로 그것을 부르는지.
$ErrorActionPreference = "Stop"
$repoRoot = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent

$failures = @()

# ── 양성 확인 1: primitive가 실재하고 순서를 담는다 ──
$runtimeFrame = Join-Path $repoRoot "Engine\SceneRuntime\RuntimeFrame.cpp"
if (-not (Test-Path -LiteralPath $runtimeFrame)) {
    throw "RuntimeFrame.cpp가 없다 — 아래 단정이 전부 무의미해진다: $runtimeFrame"
}
$frameText = Get-Content -LiteralPath $runtimeFrame -Raw -Encoding UTF8

# ⚠ 순서 비교는 반드시 TickSimulationFrame **본문 안에서만** 한다. 관리 틱 두 함수는
#   파일 앞쪽 익명 네임스페이스에 정의돼 있어서, 파일 전체에서 첫 등장을 찾으면
#   호출이 아니라 정의를 잡아 거짓 실패가 난다 — 이 게이트를 처음 쓸 때 실제로
#   그렇게 틀렸다. 거짓 실패는 거짓 통과만큼 나쁘다.
$fnStart = $frameText.IndexOf('void TickSimulationFrame(float deltaSeconds)')
if ($fnStart -lt 0) {
    throw "RuntimeFrame.cpp에서 TickSimulationFrame 정의를 못 찾았다 — 순서 단정이 무의미해진다"
}
$frameText = $frameText.Substring($fnStart)

# 순서 자체를 단정한다. 이름만 있고 순서가 뒤집히면 통합의 의미가 없다.
$phases = @(
    @{ Name = 'Initialization'; Pattern = 'SceneManagers->Initialization\(\)' },
    @{ Name = 'InputEvents';    Pattern = 'SceneManagers->InputEvents\(' },
    @{ Name = 'pre-physics';    Pattern = 'TickManagedPrePhysics\(' },
    @{ Name = 'Physics';        Pattern = 'SceneManagers->Physics\(' },
    @{ Name = 'GameLogic';      Pattern = 'SceneManagers->GameLogic\(' },
    @{ Name = 'post-physics';   Pattern = 'TickManagedPostPhysics\(' }
)
$previousIndex = -1
foreach ($phase in $phases) {
    $match = [regex]::Match($frameText, $phase.Pattern)
    if (-not $match.Success) {
        $failures += "RuntimeFrame에 $($phase.Name) 단계가 없다 — 순서 단정이 무의미해진다"
        continue
    }
    if ($match.Index -lt $previousIndex) {
        $failures += "RuntimeFrame의 단계 순서가 어긋났다: $($phase.Name)이 앞 단계보다 먼저 나온다"
    }
    $previousIndex = $match.Index
}

# 일시정지 분기가 InputEvents 뒤·pre-physics 앞이어야 한다(옛 두 Host가 그랬다).
$pausingIndex = $frameText.IndexOf('SceneManagers->Pausing();')
$inputIndex = [regex]::Match($frameText, 'SceneManagers->InputEvents\(').Index
$prePhysicsIndex = [regex]::Match($frameText, 'TickManagedPrePhysics\(\s*deltaSeconds').Index
if ($pausingIndex -lt 0) {
    $failures += "RuntimeFrame에 일시정지 분기가 없다"
} elseif ($pausingIndex -lt $inputIndex -or ($prePhysicsIndex -gt 0 -and $pausingIndex -gt $prePhysicsIndex)) {
    $failures += "일시정지 분기 위치가 어긋났다 — InputEvents 뒤, pre-physics 앞이어야 한다"
}

# ── 양성 확인 2: 두 Host가 primitive를 부른다 ──
$hosts = @(
    @{ Rel = "Editor\EngineEntry\EditorMain.cpp"; Name = "Editor" },
    @{ Rel = "Player\PlayerMain.cpp";      Name = "Player" }
)
$hostTexts = @{}
foreach ($h in $hosts) {
    $path = Join-Path $repoRoot $h.Rel
    if (-not (Test-Path -LiteralPath $path)) {
        throw "$($h.Rel)이 없다 — 단정이 무의미해진다"
    }
    $text = Get-Content -LiteralPath $path -Raw -Encoding UTF8
    $hostTexts[$h.Name] = $text
    if ($text -notmatch 'Runtime::TickSimulationFrame\(') {
        $failures += "$($h.Rel)이 Runtime::TickSimulationFrame을 부르지 않는다 — 프레임 순서가 다시 갈라졌다"
    }
    if ($text -notmatch 'Runtime::ResolveFrameDelta\(\)') {
        $failures += "$($h.Rel)이 Runtime::ResolveFrameDelta를 쓰지 않는다 — delta 규약이 다시 복제됐다"
    }
}

# ── 부재 단정: Host가 시뮬레이션 단계를 직접 부르지 않는다 ──
#
# Editor는 편집 모드(재생 중이 아닌 상태) 루프를 따로 갖는다. 그 경로는 런타임에
# 없는 상태라 primitive 밖이고, 거기서 GameLogic(0.0f)과 InputEvents를 직접 부른다.
# 그래서 이 둘은 예외로 둔다 — 대신 Physics와 관리 틱은 어느 Host도 직접 못 부른다.
foreach ($h in $hosts) {
    $text = $hostTexts[$h.Name]
    foreach ($forbidden in @('SceneManagers->Physics\(', 'clr\.TickPrePhysics\(', 'clr\.TickPostPhysics\(')) {
        if ($text -match $forbidden) {
            $failures += "$($h.Rel)이 시뮬레이션 단계를 직접 부른다($forbidden) — Runtime이 소유해야 한다"
        }
    }
}

# ── 부재 단정: GameLogic의 기본 인자가 되살아나지 않는다 ──
# `= 0`이면 호출부가 인자를 생략해 delta 0이 조용히 들어간다. "delta 0은
# 일시정지에서만"이라는 규약이 지켜지는지 호출부만 봐서는 알 수 없게 된다.
$sceneManagerHeader = Get-Content (Join-Path $repoRoot "Engine\SceneRuntime\SceneManager.h") -Raw -Encoding UTF8
if ($sceneManagerHeader -match 'void\s+GameLogic\s*\(\s*float\s+\w+\s*=') {
    $failures += "SceneManager::GameLogic에 기본 인자가 되살아났다 — delta 0이 조용히 들어갈 수 있다"
}

if ($failures.Count -gt 0) {
    "프레임 오케스트레이션 실패 $($failures.Count)건:"
    $failures | ForEach-Object { "  $_" }
    exit 1
}

"frame orchestration: PASS (순서는 Runtime 단일 소유, 두 Host가 호출만, GameLogic 기본 인자 없음)"
