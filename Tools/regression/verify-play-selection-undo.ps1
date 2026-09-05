# Edit→Play→Stop 왕복에서 선택과 Undo가 어떻게 되는가 (E3-2+3 착수 전 필수 게이트)
#
# 왜 필요한가
# ───────────
# E3-2+3은 play-mode transaction을 EditorPlayModeController로 옮기고 Undo를
# SceneManager에서 들어낸다. 그런데 세트 어디에도 selection/undo를 구동·단정하는
# 검사가 없었다 — verify-play-roundtrip.ps1도 씬 내용만 본다. 옮기기 전에
# "지금 동작"을 못 박아야 옮긴 뒤에 "동작이 같다"를 주장할 수 있다.
#
# ⚠ 계획서의 판정 문구를 그대로 따르지 않는다
# ─────────────────────────────────────────
# 계획서는 "재생 후 selection이 **복원된다**"고 적었지만 코드는 복원하지 않는다.
# EndPlayTransaction이 resetSelectedObjectEvent를 던지고, 그 유일한 구독자인
# Scene::ResetSelectedEntity가 포인터를 널로 만들 뿐이다(해제). 애초에 선택은
# 씬 YAML에 실리지 않아 스냅샷에 담기지도 않으므로 복원될 경로 자체가 없다.
# 선택이 Entity* 원시 포인터인데 정지가 엔티티를 전부 파괴하는 것을 생각하면
# 해제가 안전한 동작이고, "복원"은 리팩터가 아니라 기능이다.
#
# 그래서 이 게이트는 **해제**를 단정한다. 이것이 지금 참인 것이다.
#
# ⚠ 편집 스택과 게임 스택을 따로 본다
# ──────────────────────────────────
# UndoManager가 어느 스택에 넣을지 고르는 기준 m_isGameMode는 이름과 달리
# "에디터 UI의 Play 버튼을 눌렀는가"다 — 저장소 전체에서 MenuBarWindow 한 줄만
# 쓴다. CLI로 재생하면 영원히 false라, "지금 유효한 스택" 하나만 보면 편집 스택을
# 보면서 게임 스택을 검사한다고 착각하게 된다. 그 착각이 곧 아무것도 검증하지
# 않는 게이트다. 그래서 둘을 따로 찍고, 그 어긋남 자체를 단정한다.
param(
    [string]$Exe = (Join-Path $PSScriptRoot "..\..\Bin\x64-Debug\Editor\CreatorEditor.exe"),
    [string]$Work = ""
)

$ErrorActionPreference = "Stop"

if (-not (Test-Path -LiteralPath $Exe)) {
    throw "Editor executable is missing: $Exe"
}

if ([string]::IsNullOrWhiteSpace($Work)) {
    $Work = Join-Path ([IO.Path]::GetTempPath()) ("CE_SelUndo_" + [guid]::NewGuid().ToString("N"))
}
New-Item -ItemType Directory -Force -Path $Work | Out-Null

try {
    $commandFile = Join-Path $Work "commands.txt"
    $stdout = Join-Path $Work "stdout.txt"
    $stderr = Join-Path $Work "stderr.txt"

    # 시작 씬에 반드시 있는 오브젝트를 고른다(verify-play-roundtrip이 확인한 3종 중 하나).
    $selectTarget = "Main Camera"
    $undoProbe = "CE_UndoProbe"

    [IO.File]::WriteAllLines($commandFile, @(
        "wait 5"
        "scene.selection edit_before"      # 기준선 — 비어 있어야 이후 단정이 의미를 갖는다
        "undo.state edit_pre"
        # 2026-09-05: `object.create.undoable` 이 사라졌다 — `object.create` 가
        # Undo 를 남기게 되면서 우회로일 이유가 없어졌다. 이 게이트가 필요로 하는
        # 것은 "편집 스택에 항목 하나를 확실히 밀어 넣는 것" 이고, 아래 무의미성
        # 방지 3 이 그것이 실제로 먹었는지를 여전히 단정한다.
        "object.create $undoProbe"
        "undo.state edit_pushed"           # 무의미성 방지: push가 실제로 먹었는가
        "scene.select $selectTarget"
        "scene.selection edit_selected"    # 무의미성 방지: select가 실제로 먹었는가
        "play"
        "wait 5"
        "scene.selection playing"
        "undo.state playing"
        "stop"
        "wait 5"
        "scene.selection restored"
        "undo.state after_stop"
        "quit"
    ))

    $process = Start-Process -FilePath $Exe `
        -ArgumentList "--console", "--script", $commandFile `
        -WorkingDirectory (Split-Path $Exe -Parent) `
        -RedirectStandardOutput $stdout -RedirectStandardError $stderr -PassThru
    if (-not $process.WaitForExit(180000)) {
        $process.Kill()
        throw "selection/undo probe timed out"
    }
    $process.WaitForExit()
    if ($process.ExitCode -ne 0) {
        $errorText = if (Test-Path -LiteralPath $stderr) {
            Get-Content -LiteralPath $stderr -Raw
        } else { "" }
        throw "selection/undo probe exited with $($process.ExitCode): $errorText"
    }

    $output = Get-Content -LiteralPath $stdout -Raw

    function Get-Selection([string]$label) {
        $head = [regex]::Match($output,
            '\[selection:' + [regex]::Escape($label) + '\] primary=(.+?) multi=(\d+)')
        if (-not $head.Success) {
            throw "selection '$label' was not emitted — the probe did not reach that point"
        }
        return [pscustomobject]@{
            Label   = $label
            Primary = $head.Groups[1].Value
            Multi   = [int]$head.Groups[2].Value
        }
    }

    function Get-Undo([string]$label) {
        $m = [regex]::Match($output,
            '\[undo\.state:' + [regex]::Escape($label) + '\] isGameMode=(\d) gameStart=(\d) ' +
            'editUndo=(\d+) editRedo=(\d+) gameUndo=(\d+) gameRedo=(\d+)')
        if (-not $m.Success) {
            throw "undo.state '$label' was not emitted — the probe did not reach that point"
        }
        return [pscustomobject]@{
            Label      = $label
            IsGameMode = [int]$m.Groups[1].Value
            GameStart  = [int]$m.Groups[2].Value
            EditUndo   = [int]$m.Groups[3].Value
            EditRedo   = [int]$m.Groups[4].Value
            GameUndo   = [int]$m.Groups[5].Value
            GameRedo   = [int]$m.Groups[6].Value
        }
    }

    $selBefore   = Get-Selection "edit_before"
    $selSelected = Get-Selection "edit_selected"
    $selPlaying  = Get-Selection "playing"
    $selRestored = Get-Selection "restored"

    $undoPre     = Get-Undo "edit_pre"
    $undoPushed  = Get-Undo "edit_pushed"
    $undoPlaying = Get-Undo "playing"
    $undoStop    = Get-Undo "after_stop"

    # ── 무의미성 방지 1: 기준선이 실제로 비어 있는가 ──
    # 시작부터 무언가 선택돼 있었다면 "정지 후 비었다"가 무엇을 뜻하는지 알 수 없다.
    if ($selBefore.Primary -ne "(none)") {
        throw "something was already selected before the probe selected anything: $($selBefore.Primary)"
    }

    # ── 무의미성 방지 2: select가 실제로 먹었는가 ──
    # 이것이 없으면 scene.select가 조용히 no-op이어도 "정지 후 비었다"가 참이 된다.
    if ($selSelected.Primary -ne $selectTarget) {
        throw ("scene.select did not take effect: expected '$selectTarget' " +
            "got '$($selSelected.Primary)' — the clear assertion below would be vacuous")
    }

    # ── 무의미성 방지 3: undo push가 실제로 먹었는가 ──
    if ($undoPushed.EditUndo -ne ($undoPre.EditUndo + 1)) {
        throw ("object.create did not push onto the edit stack: " +
            "before=$($undoPre.EditUndo) after=$($undoPushed.EditUndo) — " +
            "the clear assertion below would be vacuous")
    }

    # ── 판정 A: 재생 진입은 선택을 건드리지 않는다 ──
    # BeginPlayTransaction에는 선택 관련 코드가 없다. 이것이 참인지 못 박는다.
    if ($selPlaying.Primary -ne $selectTarget) {
        throw ("entering play changed the selection: expected '$selectTarget' " +
            "got '$($selPlaying.Primary)'")
    }

    # ── 판정 B: 재생 진입이 편집 Undo 이력을 비운다 ──
    # BeginPlayTransaction의 UndoCommandManager->Clear()가 하는 일.
    if ($undoPlaying.GameStart -ne 1) {
        throw "play did not take effect — undo assertions below would be vacuous"
    }
    if ($undoPlaying.EditUndo -ne 0 -or $undoPlaying.EditRedo -ne 0) {
        throw ("entering play did not clear the edit undo history: " +
            "editUndo=$($undoPlaying.EditUndo) editRedo=$($undoPlaying.EditRedo)")
    }

    # ── 판정 C: CLI 재생도 m_isGameMode를 켠다 (LC6 §9에서 고쳤다) ──
    #
    # 이 줄은 원래 그 반대를 못 박고 있었다 — "CLI 재생은 m_isGameMode를 켜지
    # 않는다(known defect)". 대입이 MenuBarWindow의 Play 버튼 안에 인라인으로
    # 있었고 저장소 전체에서 그 한 줄이 유일한 쓰기였기 때문이다. 그래서 같은
    # 조작인데 사람이 버튼으로 하면 게임 스택에, 에이전트가 HTTP로 하면 편집
    # 스택에 쌓였다.
    #
    # LC6이 그 대입을 Editor::PlayModeController로 옮겨 GUI·CLI가 같은 이벤트를
    # 지나게 했다. 이제 **결함을 기록하는 줄이 아니라 계약을 단정하는 줄**이다 —
    # 되돌아가면 붉어져야 한다. 기록만 하고 통과시키면 되돌아가도 조용하다.
    if ($undoPlaying.IsGameMode -ne 1) {
        throw ("CLI 재생이 m_isGameMode를 켜지 않았다(got $($undoPlaying.IsGameMode)) — " +
            "GUI Play와 서비스 play가 다른 Undo 스택을 쓰게 된다(§9 동등성)")
    }
    if ($undoStop.IsGameMode -ne 0) {
        throw ("정지가 m_isGameMode를 끄지 않았다(got $($undoStop.IsGameMode)) — " +
            "정지 뒤의 편집이 게임 스택에 쌓인다")
    }
    $gameModeNote = "gamemode=play(1)/stop(0)"

    # ── 판정 D: 정지는 선택을 해제한다 (복원이 아니다) ──
    if ($selRestored.Primary -ne "(none)") {
        throw ("stop did not clear the selection: primary='$($selRestored.Primary)' — " +
            "resetSelectedObjectEvent must fire before AllDestroyMark, otherwise the " +
            "selection points at a destroyed entity")
    }
    if ($selRestored.Multi -ne 0) {
        throw "stop did not clear the multi-selection: multi=$($selRestored.Multi)"
    }

    # ── 판정 E: 정지 후에도 편집 이력은 돌아오지 않는다 ──
    # Clear()는 스택을 버리는 것이지 물러 두는 것이 아니다.
    if ($undoStop.EditUndo -ne 0) {
        throw ("the edit undo history came back after stop (expected 0): " +
            "editUndo=$($undoStop.EditUndo)")
    }

    ("play selection/undo: PASS (select=cleared-on-stop, editUndo={0}->0 on play, " +
        "gameStack={1}/{2}, {3})") -f `
        $undoPushed.EditUndo, $undoPlaying.GameUndo, $undoPlaying.GameRedo, $gameModeNote
}
finally {
    if (Test-Path -LiteralPath $Work) {
        $verified = [IO.Path]::GetFullPath($Work)
        $tempBase = [IO.Path]::GetFullPath([IO.Path]::GetTempPath())
        if ($verified.StartsWith($tempBase, [StringComparison]::OrdinalIgnoreCase) -and
            (Split-Path $verified -Leaf).StartsWith(
                "CE_SelUndo_", [StringComparison]::OrdinalIgnoreCase)) {
            Remove-Item -LiteralPath $verified -Recurse -Force -ErrorAction SilentlyContinue
        }
    }
}
