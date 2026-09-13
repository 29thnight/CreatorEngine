# Edit → Play → Stop 왕복 구조 대조 (E3 착수 전 필수 게이트)
#
# 왜 필요한가
# ───────────
# E3는 play-mode 소유권을 SceneManager에서 Editor로 옮긴다. 그런데 회귀 세트 60여 종
# 어디에도 재생 왕복이 씬 구조를 보존하는지 재는 검사가 없었다 — 있는 것은 UI 텍스트
# 로그 통과 횟수 같은 프록시뿐이라, 재생 후 엔티티가 사라지거나 계층이 뒤집혀도
# 전부 초록으로 남는다. 이관을 시작하기 전에 "지금 동작"을 먼저 못 박아야 옮긴 뒤에
# "동작이 같다"를 주장할 수 있다.
#
# ── 판정 항목 ──
#
#   1  측정이 실제로 나왔다      — 세 지점의 digest가 모두 파싱되고 오브젝트 수가 0이 아니다
#   2  엔진이 정상 종료했다      — 크래시하고도 통과하는 게이트가 이 저장소에 실재했다
#   3  재생 진입이 실제로 일어났다 — play.state의 gameStart/editorSceneLoaded가 전이한다
#   4  ★ 정지 후 편집 상태로 복원 — 오브젝트 수와 해시가 재생 전과 정확히 일치
#
# ── PHASE 21 W5 가 얹은 판정 (두 번째 실행, jsonl) ──
#
#   5  확정 신호 — committed 는 스냅샷 뒤에만 참이고 정지 뒤 거짓이다. 상태 머신은
#      Stopped → PlayingPossessed → Stopped 로 돌고 표시 타깃은 game 으로 갔다가
#      재생 전 모드(scene)로 돌아온다
#   6  ★ 스냅샷 실패 주입 — 요청이 되돌아오고(gameStart=0) UI 상태가 Playing 으로
#      보이지 않으며, 편집 Undo 이력이 **살아 있다**(통지가 스냅샷 뒤로 갔다)
#   7  입력 소유자 — Possessed·전경·비일시정지에서만 game 이고 Eject/Pause/전경
#      상실에서 editor 로 돌아온다. 게임의 커서 숨김 요청은 소유가 아닐 때 적용되지
#      않고 소유가 돌아오면 다시 적용되며, Stop 이 요청 자체를 지운다
#   8  확정 전 pause/possess 는 precondition 으로 거부된다
#
# 전경(foreground)은 검증 손잡이로 강제한다 — 게이트는 창을 숨겨 띄우므로 OS
# 전경을 만들 수 없다. 강제하지 않은 상태에서 owner 가 editor 인 것도 함께 본다
# (그것이 "숨긴 창의 게임은 입력을 받지 않는다" 다).
#
# 3번이 핵심이다. 이것이 없으면 "재생을 아예 안 했는데 상태가 그대로라 통과"라는
# 거짓 통과가 성립한다 — 이 저장소가 이미 여러 번 겪은 양식이다.
#
# 처음에는 "재생 중 digest가 편집 상태와 다르다"로 3번을 재려 했는데 그건 틀린
# 관측이다. 재생 진입은 좌표를 바꾸지 않고 m_scenePhase만 바꾸므로, 스크립트가
# 무언가를 움직이지 않는 한 digest는 같다. 그래서 상태 플래그를 직접 본다.
param(
    [string]$Exe = (Join-Path $PSScriptRoot "..\..\Bin\x64-Debug\Editor\CreatorEditor.exe"),
    [string]$Work = ""
)

$ErrorActionPreference = "Stop"
$repoRoot = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent

if (-not (Test-Path -LiteralPath $Exe)) {
    throw "Editor executable is missing: $Exe"
}

if ([string]::IsNullOrWhiteSpace($Work)) {
    $Work = Join-Path ([IO.Path]::GetTempPath()) ("CE_PlayRoundtrip_" + [guid]::NewGuid().ToString("N"))
}
New-Item -ItemType Directory -Force -Path $Work | Out-Null

# ★ workspace 를 격리한다(W5). 이 게이트는 개발자의 `active.workspace` 를 빌려
#   띄우고 있었고, 그 파일이 Host 의 표시 모드를 싣는다. 어떤 실행이 Game 모드로
#   끝나면(변이 ⑧이 그랬다) 다음 실행의 "재생 전 모드"가 game 이 되어 복원 단정이
#   엉뚱한 이유로 붉는다 — 그리고 그 붉음이 다른 변이의 붉음으로 읽힌다. 실행마다
#   빈 폴더를 주면 재생 전 모드는 언제나 기본값(scene)이다.
$priorWorkspaceDir = $env:CREATOR_EDITOR_WORKSPACE_DIR
$priorLegacyIni = $env:CREATOR_EDITOR_LEGACY_INI
$env:CREATOR_EDITOR_WORKSPACE_DIR = (Join-Path $Work "workspace")
$env:CREATOR_EDITOR_LEGACY_INI = (Join-Path $Work "workspace\legacy.ini")
New-Item -ItemType Directory -Force -Path $env:CREATOR_EDITOR_WORKSPACE_DIR | Out-Null

try {
    $commandFile = Join-Path $Work "commands.txt"
    $stdout = Join-Path $Work "stdout.txt"
    $stderr = Join-Path $Work "stderr.txt"

    # 시작 씬이 뜬 뒤 편집 상태를 찍고, 재생 → 몇 프레임 → 재생 중 상태 → 정지 →
    # 몇 프레임 → 복원 상태를 찍는다. play/stop은 SetGameStart만 세우고 실제 씬 구조
    # 변경은 다음 프레임의 ApplyPendingSceneStructureChange가 하므로 wait가 필수다.
    # 재생 중에 오브젝트를 하나 만든다. 이것이 이 게이트의 핵심이다 — 아무것도
    # 바꾸지 않고 재생 전후를 비교하면 "복원했다"가 아니라 "건드린 게 없다"를 재게
    # 된다. 정지가 백업에서 되살리는 것이 맞다면 이 오브젝트는 사라져야 한다.
    $spawnName = "CE_PlayProbe_Spawned"
    [IO.File]::WriteAllLines($commandFile, @(
        "wait 5"
        "play.state"
        "scene.transformdigest edit"
        "play"
        "wait 5"
        "object.create $spawnName"
        "wait 3"
        "play.state"
        "scene.transformdigest playing"
        "stop"
        "wait 5"
        "play.state"
        "scene.transformdigest restored"
        "quit"
    ))

    $process = Start-Process -FilePath $Exe `
        -ArgumentList "--commandlet-script", $commandFile `
        -WorkingDirectory (Split-Path $Exe -Parent) `
        -RedirectStandardOutput $stdout -RedirectStandardError $stderr -PassThru
    if (-not $process.WaitForExit(180000)) {
        $process.Kill()
        throw "play round-trip probe timed out"
    }
    $process.WaitForExit()
    if ($process.ExitCode -ne 0) {
        $errorText = if (Test-Path -LiteralPath $stderr) {
            Get-Content -LiteralPath $stderr -Raw
        } else { "" }
        throw "play round-trip probe exited with $($process.ExitCode): $errorText"
    }

    $output = Get-Content -LiteralPath $stdout -Raw

    # 엔진이 찍는 해시는 **열거 순서에 민감**하다. 그런데 재생 왕복은 엔티티를
    # 파괴하고 백업에서 되살리므로 슬롯 인덱스가 재배정된다(실측: Main Camera와
    # Directional Light가 1↔2로 뒤바뀐다). 내용은 동일한데 해시만 달라지므로,
    # 엔진 해시를 그대로 비교하면 "복원 실패"로 오판한다.
    #
    # 계획서가 요구하는 속성은 "scene·hierarchy가 복원된다"이지 "슬롯 번호가
    # 같다"가 아니다. 그래서 행을 이름 기준으로 정렬해 내용 집합을 비교하고,
    # 슬롯 순서는 실패시키지 않되 PASS 줄에 남겨 변화가 눈에 보이게 한다.
    function Get-Digest([string]$label) {
        $summaryPattern = '\[tfdigest:' + [regex]::Escape($label) +
            '\] 합계 오브젝트 (\d+) · 해시 ([0-9a-f]{16})'
        $summary = [regex]::Match($output, $summaryPattern)
        if (-not $summary.Success) {
            throw "digest '$label' was not emitted — the probe did not reach that point"
        }

        $rowPattern = '\[tfdigest:' + [regex]::Escape($label) + '\] (\d+\|[^\r\n]+)'
        $rows = @([regex]::Matches($output, $rowPattern) | ForEach-Object {
            $_.Groups[1].Value
        })
        # index 를 뗀 내용만 남긴다 — 슬롯 재배정은 복원 실패가 아니다.
        $content = @($rows | ForEach-Object { ($_ -split '\|', 2)[1] } | Sort-Object)
        $order = @($rows | ForEach-Object { ($_ -split '\|')[1] }) -join ','

        return [pscustomobject]@{
            Label = $label
            Count = [int]$summary.Groups[1].Value
            Hash  = $summary.Groups[2].Value
            Content = ($content -join "`n")
            Order = $order
            RowCount = $rows.Count
        }
    }

    $edit = Get-Digest "edit"
    $playing = Get-Digest "playing"
    $restored = Get-Digest "restored"

    # W5 가 뒤에 덧붙인 필드까지 읽는다. 앞의 다섯은 그대로다 — 순서가 계약이다.
    $states = @([regex]::Matches($output,
        '\[play\.state\] gameStart=(\d) paused=\d editorSceneLoaded=(\d) pending=(\d) entities=(\d+) ' +
        'committed=(\d) state=(\w+) owner=(\w+) target=(\w+) foreground=(\d) failures=(\d+)') |
        ForEach-Object {
            [pscustomobject]@{
                GameStart = [int]$_.Groups[1].Value
                EditorSceneLoaded = [int]$_.Groups[2].Value
                Pending = [int]$_.Groups[3].Value
                Entities = [int]$_.Groups[4].Value
                Committed = [int]$_.Groups[5].Value
                State = $_.Groups[6].Value
                Owner = $_.Groups[7].Value
                Target = $_.Groups[8].Value
                Foreground = [int]$_.Groups[9].Value
                Failures = [int]$_.Groups[10].Value
            }
        })
    if ($states.Count -ne 3) {
        throw "expected 3 play.state samples, got $($states.Count)"
    }

    # 1: 빈 측정을 성공으로 읽지 않는다.
    if ($edit.Count -le 0) {
        throw "edit digest measured 0 objects — nothing was compared"
    }

    # 3: 재생 전이가 실제로 일어났는가. 이 단정이 없으면 'play가 무시됐는데 상태가
    #    그대로라 통과'가 성립한다. 좌표가 아니라 상태 플래그를 본다.
    if ($states[0].GameStart -ne 0) {
        throw "scene was already in play mode before the probe pressed play"
    }
    if ($states[1].GameStart -ne 1 -or $states[1].EditorSceneLoaded -ne 1) {
        throw ("play did not take effect: gameStart=$($states[1].GameStart) " +
            "editorSceneLoaded=$($states[1].EditorSceneLoaded) — " +
            "the restore assertion below would be vacuous")
    }
    if ($states[2].GameStart -ne 0 -or $states[2].EditorSceneLoaded -ne 0) {
        throw ("stop did not take effect: gameStart=$($states[2].GameStart) " +
            "editorSceneLoaded=$($states[2].EditorSceneLoaded)")
    }
    if ($states[2].Pending -ne 0) {
        throw "a scene structure change was still pending after stop settled"
    }

    # 5: 확정 신호와 상태 머신(W5). 요청(gameStart)과 확정(committed)이 같은
    #    프레임에 같은 값인 것은 우연이 아니라 전이가 끝난 뒤 읽었기 때문이다 —
    #    실패 주입 구간(아래 두 번째 실행)이 둘이 갈리는 자리를 잰다.
    if ($states[0].Committed -ne 0 -or $states[0].State -ne 'Stopped' -or $states[0].Owner -ne 'editor') {
        throw ("before play the controller was not idle: committed=$($states[0].Committed) " +
            "state=$($states[0].State) owner=$($states[0].Owner)")
    }
    if ($states[1].Committed -ne 1 -or $states[1].State -ne 'PlayingPossessed' -or $states[1].Target -ne 'game') {
        throw ("play did not commit into PlayingPossessed on the game target: committed=$($states[1].Committed) " +
            "state=$($states[1].State) target=$($states[1].Target)")
    }
    # 복원의 기준은 **재생 전 표본**이다. 격리된 빈 workspace 라 그것은 scene 이고,
    # 둘 다 단정한다 — 기준이 game 이면 "복원했다"가 아니라 "안 바꿨다"를 재게 된다.
    if ($states[0].Target -ne 'scene') {
        throw "the isolated workspace did not start on the scene target: target=$($states[0].Target)"
    }
    if ($states[2].Committed -ne 0 -or $states[2].State -ne 'Stopped' -or $states[2].Target -ne $states[0].Target -or $states[2].Owner -ne 'editor') {
        throw ("stop did not return to Stopped on the prior ($($states[0].Target)) target: committed=$($states[2].Committed) " +
            "state=$($states[2].State) target=$($states[2].Target) owner=$($states[2].Owner)")
    }
    if ($states[2].Failures -ne 0) {
        throw "a play transaction failed during the plain round trip: failures=$($states[2].Failures)"
    }
    # 창을 숨겨 띄웠으므로 전경이 아니고, 그래서 Possessed 여도 owner 는 editor 다.
    # 이것이 "숨긴 창의 게임은 입력을 받지 않는다" 이고, 전경을 강제한 구간과 짝이다.
    if ($states[1].Foreground -ne 0 -or $states[1].Owner -ne 'editor') {
        throw ("a hidden editor window claimed game input: foreground=$($states[1].Foreground) " +
            "owner=$($states[1].Owner) — focus gating is not in effect")
    }

    # 행이 실제로 파싱됐는지 확인한다 — 빈 집합을 비교해 '차이 0'으로 통과하는
    # 양식을 이 저장소가 이미 겪었다.
    if ($edit.RowCount -ne $edit.Count -or $restored.RowCount -ne $restored.Count) {
        throw ("digest rows were not fully parsed: edit=$($edit.RowCount)/$($edit.Count) " +
            "restored=$($restored.RowCount)/$($restored.Count)")
    }

    # 3-b: 재생 중 생성이 실제로 반영됐는가. 이것이 없으면 아래 복원 단정이
    #      "아무것도 안 바꾸고 같은지 봤다"가 된다.
    if ($playing.Count -ne ($edit.Count + 1)) {
        throw ("the probe object was not created during play: edit=$($edit.Count) " +
            "playing=$($playing.Count) — the restore assertion would be vacuous")
    }
    if ($playing.Content -notmatch [regex]::Escape($spawnName)) {
        throw "the spawned probe object is missing from the playing digest"
    }

    # 4: 정지 후 편집 상태로 복원 — 오브젝트 수와 내용 집합이 정확히 일치해야 한다.
    #    재생 중 만든 오브젝트는 백업에 없으므로 사라져야 한다.
    if ($restored.Content -match [regex]::Escape($spawnName)) {
        throw "an object created during play survived stop — the scene was not restored from the backup"
    }
    if ($restored.Count -ne $edit.Count) {
        throw ("stop did not restore the object count: edit=$($edit.Count) " +
            "restored=$($restored.Count)")
    }
    if ($restored.Content -ne $edit.Content) {
        throw ("stop did not restore the scene content.`nedit:`n$($edit.Content)`n" +
            "restored:`n$($restored.Content)")
    }

    # 슬롯 재배정은 실패시키지 않되 눈에 보이게 남긴다. E3가 이 성질을 바꾸면
    # PASS 줄의 표기가 달라져 로그에서 드러난다.
    $orderNote = if ($restored.Order -eq $edit.Order) { "stable" } else {
        "reassigned($($edit.Order) -> $($restored.Order))"
    }
    "play round-trip: PASS (objects=$($edit.Count), content=restored, slots=$orderNote)"

    # ── 두 번째 실행 (W5) — 구조화된 결과(jsonl)로 상태 머신을 잰다 ──────────
    #
    # 첫 실행은 stdout 정규식이다(E3 착수 전 게이트가 그렇게 섰다). W5 의 필드는
    # 열다섯이 넘어 정규식이 곧 거짓말을 시작하므로 결과 파일을 줄 단위로 읽는다.
    # 같은 명령이 여러 번 나오므로 **순서 목록**으로 다룬다 — 이름으로 접으면
    # 마지막 표본만 남아 앞의 전이가 사라진다.
    $w5Script = Join-Path $Work "w5.txt"
    $w5Result = Join-Path $Work "w5.jsonl"
    $w5Stdout = Join-Path $Work "w5.out"
    $w5Stderr = Join-Path $Work "w5.err"
    $undoProbe = "CE_W5_UndoProbe"
    [IO.File]::WriteAllLines($w5Script, @(
        "wait 45"
        # ⑥ 실패 주입. 편집 이력을 하나 밀어 두고 실패시킨 뒤 그대로인지 본다.
        "object.create $undoProbe"
        "undo.state w5_pre"
        "play.inject_snapshot_failure 1"
        "play"
        "wait 6"
        "play.state"                       # [0] 실패 뒤
        "undo.state w5_after_fail"
        "editor.viewport"                  # [v0] 타깃이 game 으로 가지 않았다
        # ⑦ 정상 재생. 전경을 강제해 소유권 규칙 자체를 잰다.
        "play.foreground_override on"
        "play"
        "wait 6"
        "play.state"                       # [1] Possessed · game
        "play.cursor hide"
        "wait 1"
        "play.state"                       # [2] 커서 숨김 적용
        "play.eject"
        "wait 4"
        "play.state"                       # [3] Ejected · editor · 커서 복귀
        "play.possess"
        "wait 4"
        "play.state"                       # [4] Possessed · game · 커서 다시 숨김
        "play.pause"
        "wait 2"
        "play.state"                       # [5] paused · editor
        "play.resume"
        "wait 2"
        "play.state"                       # [6] game
        "play.foreground_override off"
        "wait 2"
        "play.state"                       # [7] 전경 상실 · editor
        "play.foreground_override on"
        "wait 2"
        "play.state"                       # [8] 전경 복귀 · game
        "stop"
        "wait 8"
        "play.state"                       # [9] Stopped · 커서 요청 소멸
        "editor.viewport"                  # [v1] 타깃 복원
        "play.foreground_override auto"
        # ⑧ 확정 전 조작은 거부된다. 배치 러너가 실패에서 멈추므로 맨 뒤다.
        "play.pause"
        "quit"
    ))
    foreach ($f in @($w5Result, $w5Stdout, $w5Stderr)) {
        if (Test-Path -LiteralPath $f) { Remove-Item -LiteralPath $f }
    }
    $w5Process = Start-Process -FilePath $Exe `
        -ArgumentList @('--script', ('"' + $w5Script + '"'), '--result-format', 'jsonl',
                        '--result-file', ('"' + $w5Result + '"')) `
        -WorkingDirectory (Split-Path $Exe -Parent) -WindowStyle Hidden `
        -RedirectStandardOutput $w5Stdout -RedirectStandardError $w5Stderr -PassThru
    if (-not $w5Process.WaitForExit(300000)) {
        $w5Process.Kill()
        throw "W5 state-machine probe timed out"
    }
    if (-not (Test-Path -LiteralPath $w5Result)) {
        throw "W5 probe produced no result file (exit $($w5Process.ExitCode); see $w5Stdout)"
    }
    $rows = @(Get-Content -LiteralPath $w5Result | Where-Object { $_.Trim().Length -gt 0 } |
        ForEach-Object { $_ | ConvertFrom-Json })
    $w5States = @($rows | Where-Object { $_.command -eq 'play.state' })
    $w5Views = @($rows | Where-Object { $_.command -eq 'editor.viewport' })
    if ($w5States.Count -ne 10) { throw "expected 10 W5 play.state samples, got $($w5States.Count)" }
    if ($w5Views.Count -ne 2) { throw "expected 2 W5 editor.viewport samples, got $($w5Views.Count)" }
    foreach ($row in $rows) {
        if ($row.command -ne 'play.pause' -and $row.status -ne 'succeeded') {
            throw "W5 probe command '$($row.command)' failed: $($row.message)"
        }
    }
    function Expect-State($sample, [string]$label, [hashtable]$expected) {
        foreach ($key in $expected.Keys) {
            $actual = $sample.data.$key
            if ("$actual" -ne "$($expected[$key])") {
                throw ("W5 [$label] $key expected '$($expected[$key])' got '$actual' " +
                    "(state=$($sample.data.state) owner=$($sample.data.inputOwner) " +
                    "target=$($sample.data.viewportTarget) cursorHidden=$($sample.data.cursorHidden))")
            }
        }
    }
    $s = $w5States
    # 6: 실패 주입 — 요청이 되돌아오고 아무것도 재생으로 보이지 않는다.
    Expect-State $s[0] 'after snapshot failure' @{ gameStart = $false; committed = $false; editorSceneLoaded = $false;
        pending = $false; state = 'Stopped'; inputOwner = 'editor'; viewportTarget = 'scene'; failureCount = 1 }
    if ([string]::IsNullOrWhiteSpace($s[0].data.lastFailure)) { throw "W5: the failed transaction left no reason" }
    $undoPre = [regex]::Match((Get-Content -LiteralPath $w5Stdout -Raw),
        '\[undo\.state:w5_pre\] isGameMode=(\d) gameStart=\d editUndo=(\d+)')
    $undoAfter = [regex]::Match((Get-Content -LiteralPath $w5Stdout -Raw),
        '\[undo\.state:w5_after_fail\] isGameMode=(\d) gameStart=\d editUndo=(\d+)')
    if (-not $undoPre.Success -or -not $undoAfter.Success) { throw "W5: undo.state samples around the failure were not emitted" }
    if ([int]$undoPre.Groups[2].Value -lt 1) { throw "W5: the edit stack was empty before the failure — the preservation check would be vacuous" }
    if ($undoAfter.Groups[2].Value -ne $undoPre.Groups[2].Value) {
        throw ("W5: a failed play transaction cleared the edit undo history " +
            "(editUndo $($undoPre.Groups[2].Value) -> $($undoAfter.Groups[2].Value)) — PlayModeEvent fired before the snapshot")
    }
    if ($undoAfter.Groups[1].Value -ne '0') { throw "W5: a failed play transaction switched Undo into game mode" }
    if ($w5Views[0].data.mode -ne 'scene' -or $w5Views[0].data.playCommitted -ne $false) {
        throw "W5: the viewport followed a failed play: mode=$($w5Views[0].data.mode) committed=$($w5Views[0].data.playCommitted)"
    }
    # 7: 소유자·커서
    Expect-State $s[1] 'possessed'        @{ committed = $true; state = 'PlayingPossessed'; inputOwner = 'game'; viewportTarget = 'game'; foreground = $true; paused = $false }
    Expect-State $s[2] 'cursor hidden'    @{ inputOwner = 'game'; cursorHidden = $true; cursorHideRequested = $true }
    Expect-State $s[3] 'ejected'          @{ state = 'PlayingEjected'; inputOwner = 'editor'; viewportTarget = 'scene'; cursorHidden = $false; cursorHideRequested = $true; committed = $true }
    Expect-State $s[4] 'possessed again'  @{ state = 'PlayingPossessed'; inputOwner = 'game'; viewportTarget = 'game'; cursorHidden = $true }
    Expect-State $s[5] 'paused'           @{ state = 'PlayingPossessed'; paused = $true; inputOwner = 'editor'; cursorHidden = $false }
    Expect-State $s[6] 'resumed'          @{ paused = $false; inputOwner = 'game'; cursorHidden = $true }
    Expect-State $s[7] 'focus lost'       @{ foreground = $false; inputOwner = 'editor'; cursorHidden = $false; state = 'PlayingPossessed' }
    Expect-State $s[8] 'focus regained'   @{ foreground = $true; inputOwner = 'game'; cursorHidden = $true }
    Expect-State $s[9] 'stopped'          @{ gameStart = $false; committed = $false; pending = $false; state = 'Stopped'; inputOwner = 'editor';
        viewportTarget = 'scene'; cursorHidden = $false; cursorHideRequested = $false; failureCount = 1 }
    if ($w5Views[1].data.mode -ne 'scene' -or $w5Views[1].data.inputOwner -ne 'editor') {
        throw "W5: stop did not restore the prior viewport: mode=$($w5Views[1].data.mode) owner=$($w5Views[1].data.inputOwner)"
    }
    if ([int]$w5Views[1].data.gameModeFrames -le [int]$w5Views[0].data.gameModeFrames) {
        throw "W5: the host never showed the game target during play (gameModeFrames did not grow)"
    }
    # 8: 확정 전 pause 는 거부된다.
    $refused = @($rows | Where-Object { $_.command -eq 'play.pause' })
    if ($refused.Count -lt 2) { throw "W5: expected the trailing play.pause probe, got $($refused.Count) play.pause rows" }
    if ($refused[-1].status -eq 'succeeded' -or $refused[-1].code -ne 'play.not_committed') {
        throw "W5: play.pause before commit was not refused: status=$($refused[-1].status) code=$($refused[-1].code)"
    }
    "play state machine (W5): PASS (failure-injection=refused-and-undo-kept, owner=game/editor x9, cursor=follows-ownership, target=game->scene)"
}
finally {
    $env:CREATOR_EDITOR_WORKSPACE_DIR = $priorWorkspaceDir
    $env:CREATOR_EDITOR_LEGACY_INI = $priorLegacyIni
    if (Test-Path -LiteralPath $Work) {
        $verified = [IO.Path]::GetFullPath($Work)
        $tempBase = [IO.Path]::GetFullPath([IO.Path]::GetTempPath())
        if ($verified.StartsWith($tempBase, [StringComparison]::OrdinalIgnoreCase) -and
            (Split-Path $verified -Leaf).StartsWith(
                "CE_PlayRoundtrip_", [StringComparison]::OrdinalIgnoreCase)) {
            Remove-Item -LiteralPath $verified -Recurse -Force -ErrorAction SilentlyContinue
        }
    }
}
