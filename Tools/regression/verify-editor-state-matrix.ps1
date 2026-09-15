[CmdletBinding()]
param(
    [string]$Exe = (Join-Path $PSScriptRoot '../../Bin/x64-Release/Editor/CreatorEditor.exe'),
    [string]$Work = (Join-Path $env:TEMP 'creator-editor-state-matrix')
)
# PHASE 21 W2-3 — 상태 행렬.
#
# 계획서 W2 의 요구는 한 줄이다:
#
#   *"hover/active/focus/nav/disabled/mixed/error 상태 matrix를 고정한다."*
#
# ── 행렬이 없었다 ─────────────────────────────────────────────────────────
#
# 있던 것은 위젯 둘의 색 열거뿐이고(`mode_button_surface`,
# `section_header_surface`) 그 둘의 항목마저 서로 다르다. 일곱 상태를 한자리에서
# 볼 수단이 0 이었다. 그래서 행렬을 **선언**으로 만들었다(`editor::state`):
# 위젯이 "나는 이 상태들을 구분한다" 를 코드로 적고 매 프레임 지금 상태를
# 신고한다. 판정은 **선언과 관측을 맞대는 것**이다.
#
# ── 일곱 중 둘은 자가 없는 게 아니라 대상이 없다 ─────────────────────────
#
# `mixed` — 씬에는 다중 선택이 있는데 Inspector 는 `m_selectedEntity` 하나만
# 그린다. 값이 갈리는 상황이 위젯에 오지 않는다(W2-I3 의 몫).
# `error` — 값 검증이라는 원천이 아예 없다.
# 둘은 `not_applicable` 로 **선언하고 이유를 남긴다.** 목록에서 지우면 다음
# 사람이 "일곱 중 다섯만 있네" 를 처음부터 다시 발견해야 한다.
#
# ── 자극 표면도 없었다 ────────────────────────────────────────────────────
#
# `hover`·`active` 는 포인터가 있어야 관측된다. 그 표면이 0 이라 W2-1 이 키를
# 만든 자리와 같은 일이 되풀이됐다 — `editor.nav pointer|press|release` 를
# 더했다.
#
# ★ 이벤트 큐로는 안 됐다. `io.AddMousePosEvent` 로 넣으면 다음 프레임의
#   `ImGui_ImplWin32_NewFrame` 이 자기 좌표를 **뒤에** 넣어 덮어쓴다(실측:
#   주입이 한 번도 서지 않았다). 그래서 고정 상태로 들고 있다가 `NewFrame`
#   **뒤**에 매 프레임 얹는다.
#
# ── 세 회차인 이유 ────────────────────────────────────────────────────────
#
# 어디를 가리킬지는 위젯이 그려진 자리가 정한다. 좌표를 게이트에 박으면 배치가
# 바뀌는 날 조용히 빗나가고, 빗나간 포인터는 "hover 가 없다" 로 보인다. 그래서
# 장부에서 **자리를 읽고** 그 한가운데를 찌른다.
#
# ★ 그런데 자리 읽기 한 번으로는 모자랐다(실측). 1 회차에서 읽은 자리를 찌르자
#   패널이 펼쳐져 **1 회차에 없던 위젯**(`EditorPropertyRow.drag`)이 드러났고
#   Inspector 자체도 121 px 올라갔다. 자극이 배치를 바꾸기 때문이다. 한 바퀴만
#   도는 게이트는 "열어야 보이는 것" 을 영영 못 찌르고 그것을 미자극으로
#   보고한다 — 사실은 **자극할 기회를 스스로 없앤 것**이다.
#
#   그래서 두 바퀴를 돈다. 회차 A 가 자리를 읽고, 회차 B 가 그 자리를 찌른 뒤
#   **새로 드러난 자리를 다시 읽고**, 회차 C 가 A 의 자극을 같은 순서로 재현한
#   다음 B 가 찾은 자리까지 찌른다. B 와 C 의 자극 접두사가 같으므로 배치가
#   같다 — 이것이 없으면 C 의 좌표가 다른 배치의 것을 가리킨다.
#
#   두 바퀴 뒤에도 드러나지 않는 위젯이 남을 수 있다. 무한히 돌지 않고 미자극
#   목록에 이름으로 남긴다.
#
# ── 변이로 잰 이빨(2026-09-15) ───────────────────────────────────────────
#
#   · declare 호출 하나를 지운다              → 잡는다(소스 대조 ①)
#   · 창별 Tab 순회를 하나로 줄인다           → 잡는다(button.nav 미자극)
#   · disabled 를 input_allowed 만 읽게 한다  → 잡는다(drag.disabled 미자극)
#     ★ 이때 "disabled 를 낸 위젯이 하나 이상" 단정은 **통과했다.** 다른 위젯이
#       채웠기 때문이다. 미자극 0 을 판정으로 세우지 않았다면 이 변이는 초록으로
#       지나갔다 — 축마다 하나씩 세는 단정은 계측 하나가 죽은 것을 못 본다.
#   · 주입을 꺼 버린다(apply_injected_pointer 즉시 return) → 잡는다(active 셋)
#
#   ★ 못 잡은 변이 하나: `apply_injected_pointer()` 를 `BeginFrame` **앞**으로
#     옮기는 것. 자극하지 못한 변이다 — 이 게이트는 창을 숨겨 띄우므로
#     `ImGui_ImplWin32_NewFrame` 이 마우스 좌표를 갱신하지 않고, 그래서 앞뒤가
#     등가가 된다. 사람이 쓰는 조건(포커스를 가진 창)에서는 등가가 아니니
#     **그 호출을 앞으로 옮겨도 된다고 읽지 마라.** 뒤에 있어야 한다.

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$Exe = [IO.Path]::GetFullPath($Exe)
$Work = [IO.Path]::GetFullPath($Work)
$editorRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../../Editor'))

if (-not (Test-Path -LiteralPath $Exe)) { throw "Editor executable not found: $Exe" }
if (Get-Process CreatorEditor -ErrorAction SilentlyContinue) {
    throw '이미 떠 있는 에디터를 닫아라 — endpoint 파일은 프로젝트에 하나다.'
}
if (Test-Path -LiteralPath $Work) { Remove-Item -LiteralPath $Work -Recurse -Force }
New-Item -ItemType Directory -Force -Path $Work | Out-Null

$script:checks = 0
function Assert([bool]$Condition, [string]$Message) {
    if (-not $Condition) { throw $Message }
    $script:checks++
}
function Strip-Comment([string]$Line) {
    $at = $Line.IndexOf('//')
    if ($at -ge 0) { return $Line.Substring(0, $at) }
    return $Line
}
function Get-CodeText([string]$Path) {
    ($(foreach ($line in [IO.File]::ReadAllLines($Path)) { Strip-Comment $line }) -join "`n")
}

Write-Host ''
Write-Host '상태 행렬 (PHASE 21 W2-3)'
Write-Host ''

# ══ ① 소스 대조 ═══════════════════════════════════════════════════════════
#
# 행렬을 선언으로 만들었으니, **선언하지 않은 위젯이 없는지**가 첫 단정이다.
# 상태를 신고하는 자리(`state::announce`)와 선언하는 자리(`state::declare`)는
# 짝이어야 한다 — 신고만 하면 무엇을 요구받는지 모르고, 선언만 하면 영원히
# 관측되지 않는다.
$sources = @(Get-ChildItem -LiteralPath $editorRoot -Recurse -Filter '*.cpp' |
             Where-Object { $_.FullName -notmatch '\\RenderTests\\' })
Assert ($sources.Count -gt 100) "Editor 트리에서 .cpp 를 $($sources.Count) 개만 찾았다 — 훑는 범위가 틀렸다"

$declaring = [Collections.Generic.List[string]]::new()
$announcing = [Collections.Generic.List[string]]::new()
$declaredNames = [Collections.Generic.List[string]]::new()
foreach ($file in $sources) {
    if ('EditorStateContract.cpp' -eq $file.Name) { continue }
    $code = Get-CodeText $file.FullName
    if ($code -match 'state::declare\s*\(') { $declaring.Add($file.Name) }
    if ($code -match 'state::announce\s*\(') { $announcing.Add($file.Name) }
    foreach ($hit in [regex]::Matches($code, 'state::declare\s*\(\s*"([^"]+)"')) {
        $declaredNames.Add($hit.Groups[1].Value)
    }
}
$declaringFiles = @($declaring | Sort-Object)
$announcingFiles = @($announcing | Sort-Object)
$sourceWidgets = @($declaredNames | Sort-Object -Unique)
Write-Host ("  선언하는 파일 " + $declaringFiles.Count + " 개 · 선언된 위젯 이름 " + $sourceWidgets.Count + " 개")

$onlyAnnounce = @($announcingFiles | Where-Object { $declaringFiles -notcontains $_ })
Assert ($onlyAnnounce.Count -eq 0) `
    ("상태를 신고하면서 선언하지 않는 파일: " + ($onlyAnnounce -join ', ') +
     ". 무엇을 구분해야 하는지 적지 않으면 판정이 요구할 것이 없다")
$onlyDeclare = @($declaringFiles | Where-Object { $announcingFiles -notcontains $_ })
Assert ($onlyDeclare.Count -eq 0) `
    ("선언만 하고 신고하지 않는 파일: " + ($onlyDeclare -join ', ') +
     ". 선언한 상태가 영원히 관측되지 않는다")

# W2-2 가 계약 대상으로 뽑은 여섯과 같아야 한다. 절마다 강제 단위가 다르다는
# 것을 배웠지만(§7.1 정정), 상태 행렬의 단위는 "아이템을 그리는 custom widget"
# 이고 그것은 clipping 절의 집합과 같다 — 배지 하나만 상태가 없다.
$expectedFiles = @(
    'EditorAxisField3.cpp'
    'EditorInspectorPanel.cpp'
    'EditorModeButton.cpp'
    'EditorPropertyRow.cpp'
    'EditorSectionHeader.cpp'
    'SceneViewportOverlay.cpp'
)
$missingFiles = @($expectedFiles | Where-Object { $declaringFiles -notcontains $_ })
Assert ($missingFiles.Count -eq 0) `
    ("상태 행렬을 선언하지 않는 custom widget: " + ($missingFiles -join ', '))
$extraFiles = @($declaringFiles | Where-Object { $expectedFiles -notcontains $_ })
Assert ($extraFiles.Count -eq 0) `
    ("목록 밖에서 상태를 선언하는 파일: " + ($extraFiles -join ', ') +
     ". 새 custom widget 이면 이 게이트의 기대치에 넣어라")

# ══ ② 런타임 ══════════════════════════════════════════════════════════════

function Invoke-Editor([string[]]$Commands, [string]$Tag, [string]$WorkspaceDir) {
    $scriptPath = Join-Path $Work "$Tag.txt"
    $resultPath = Join-Path $Work "$Tag.jsonl"
    Set-Content -LiteralPath $scriptPath -Encoding UTF8 -Value ($Commands + @('quit'))

    $priorWorkspace = $env:CREATOR_EDITOR_WORKSPACE_DIR
    $priorLegacy = $env:CREATOR_EDITOR_LEGACY_INI
    $env:CREATOR_EDITOR_WORKSPACE_DIR = $WorkspaceDir
    $env:CREATOR_EDITOR_LEGACY_INI = Join-Path $WorkspaceDir 'none.ini'
    try {
        $proc = Start-Process -FilePath $Exe -WorkingDirectory (Split-Path $Exe) -WindowStyle Hidden -PassThru `
            -ArgumentList @('--script', ('"' + $scriptPath + '"'), '--result-format', 'jsonl',
                            '--result-file', ('"' + $resultPath + '"')) `
            -RedirectStandardOutput (Join-Path $Work "$Tag.out") `
            -RedirectStandardError (Join-Path $Work "$Tag.err")
        if (-not $proc.WaitForExit(600000)) { $proc.Kill(); throw "에디터가 600 초 안에 끝나지 않았다 ($Tag)." }
    }
    finally {
        if ($null -ne $priorWorkspace) { $env:CREATOR_EDITOR_WORKSPACE_DIR = $priorWorkspace }
        else { Remove-Item Env:CREATOR_EDITOR_WORKSPACE_DIR -ErrorAction SilentlyContinue }
        if ($null -ne $priorLegacy) { $env:CREATOR_EDITOR_LEGACY_INI = $priorLegacy }
        else { Remove-Item Env:CREATOR_EDITOR_LEGACY_INI -ErrorAction SilentlyContinue }
    }
    Assert (Test-Path -LiteralPath $resultPath) "결과 파일이 없다 ($Tag, 종료 코드 $($proc.ExitCode))"
    $rows = @(Get-Content -LiteralPath $resultPath | Where-Object { $_.Trim() } |
              ForEach-Object { $_ | ConvertFrom-Json })

    # ★ 명령 하나가 실패해도 게이트는 초록일 수 있다. 실제로 그랬다 —
    #   `component.add` 가 타입 이름을 못 알아들어 `invalid_arguments` 로 죽었는데
    #   행렬은 "그 위젯이 안 그려졌다" 만 말했고, 원인은 자극 스크립트의 오타였다.
    #   자극이 도착했다는 전제부터 세우지 않으면 뒤의 판정은 전부 헛것이다.
    $failed = @($rows | Where-Object { 'succeeded' -ne $_.status })
    Assert (0 -eq $failed.Count) `
        ("$Tag 회차에서 실패한 명령 " + $failed.Count + " 건: " +
         (@($failed | ForEach-Object { $_.command + '(' + $_.status + ': ' + $_.message + ')' }) -join ' · '))
    $exitText = '0x{0:X8}' -f $proc.ExitCode
    Assert (0 -eq $proc.ExitCode) "$Tag 회차 종료 코드가 비정상이다 ($exitText)"

    return [pscustomobject]@{ ExitCode = $proc.ExitCode; Rows = $rows }
}

function Read-State($Run, [string]$Tag) {
    $rows = @($Run.Rows | Where-Object { $_.command -eq 'editor.state' })
    Assert ($rows.Count -ge 1) "$Tag 회차에서 editor.state 가 돌지 않았다"
    return $rows[$rows.Count - 1]
}

# 자리를 가진 위젯만 찌를 수 있다. 넓이가 0 이면 그리다 만 것이고, 선언이 빈
# 위젯(배지)은 찔러도 낼 상태가 없다.
function Select-Targets($StateRow) {
    return @($StateRow.data.widgets | Where-Object {
        ($_.rect[2] - $_.rect[0]) -gt 2 -and ($_.rect[3] - $_.rect[1]) -gt 2 -and
        (@($_.declared).Count -gt 0)
    })
}

# 옮기고 → 누르고 → 뗀다. 세 프레임으로 나눠야 `ButtonBehavior` 가 전이를 본다.
# 한 프레임에 겹치면 hover 가 서기 전에 클릭이 도착한다.
#
# ★ 한가운데 한 점으로는 모자랐다. 패널 머리줄은 가운데가 켜짐 토글이고, 그
#   위에서는 `overToggle` 단락 평가로 `ButtonBehavior` 가 아예 돌지 않아 눌러도
#   `held` 가 서지 않는다 — 관측은 "active 가 올 수 없다" 처럼 보였다. 위젯마다
#   어디에 무엇이 있는지 게이트가 알 수는 없으니 **자리 안의 여러 점**을 찌른다.
function Add-Poke([Collections.Generic.List[string]]$Into, $Targets) {
    foreach ($t in $Targets) {
        $cy = [int](($t.rect[1] + $t.rect[3]) / 2)
        # ★ 각 점을 **두 번** 누른다. 이 위젯들의 누름은 대개 토글이라(패널 접기,
        #   켜짐 스위치) 한 번만 누르면 그 뒤의 자극이 접힌 화면을 상대하게 된다 —
        #   실제로 한 번 누른 회차에서는 값 줄이 통째로 사라졌다. 짝수로 눌러
        #   원래 상태로 되돌린다.
        foreach ($ratio in @(0.5, 0.5, 0.82, 0.82)) {
            $cx = [int]($t.rect[0] + ($t.rect[2] - $t.rect[0]) * $ratio)
            $Into.Add("editor.nav pointer $cx $cy"); $Into.Add('wait 12')
            $Into.Add('editor.nav press');           $Into.Add('wait 12')
            $Into.Add('editor.nav release');         $Into.Add('wait 12')
        }
    }
}

# 회차 B 와 C 가 **같은 자극 접두사**를 갖게 한다. 자극이 배치를 바꾸므로
# 접두사가 다르면 B 가 읽은 자리가 C 에서 다른 것을 가리킨다.
function Add-Prefix([Collections.Generic.List[string]]$Into, $Targets) {
    Add-Poke $Into $Targets
    # 포인터를 아무것도 없는 자리로 물린다 — 키보드 축의 hover 가 섞이지 않게.
    $Into.Add('editor.nav pointer 5 5'); $Into.Add('wait 12')
    # ★ Tab 순회는 **자리도 바꾼다.** 포커스가 간 아이템이 보이도록 패널이
    #   스크롤되기 때문이다(실측: `EditorPropertyRow.drag` 는 Tab 을 돌려야
    #   비로소 그려졌다). 그래서 자리를 읽는 회차에도 반드시 들어가야 한다 —
    #   빼면 그 위젯은 영영 표적이 되지 못한다.
    #
    # ★ 창마다 따로 돈다. Tab 은 **키보드를 쥔 창 안에서만** 움직이므로 한 창만
    #   순회하면 다른 창의 위젯은 nav 를 영영 못 낸다(실측: 씬 뷰 툴바 버튼).
    foreach ($window in @('###Editor.Inspector', '###Editor.Viewport', '###Editor.Inspector')) {
        $Into.Add("editor.window $window focus"); $Into.Add('wait 20')
        foreach ($round in 1..25) { $Into.Add('editor.nav key tab'); $Into.Add('wait 6') }
    }
}

# 세 회차가 **같은 배치**를 보게 한다. 배치가 다르면 앞 회차가 읽은 자리가
# 뒤 회차에서 다른 것을 가리킨다.
$workspaceDir = Join-Path $Work 'workspace'
New-Item -ItemType Directory -Force -Path $workspaceDir | Out-Null

$setup = @(
    'window.resize 1600 1000'
    'wait 150'
    'scene.populate 6 0'
    'wait 50'
    'scene.select W7_2'
    'wait 50'
    'editor.window ###Editor.Inspector focus'
    'wait 60'
)

# ── 회차 A: 자리를 읽는다 ────────────────────────────────────────────────
$runA = Invoke-Editor ($setup + @('editor.state')) 'survey-a' $workspaceDir
$stateA = Read-State $runA 'A'
$targetsA = Select-Targets $stateA
Assert ($targetsA.Count -ge 2) `
    "A 회차가 찌를 수 있는 위젯을 $($targetsA.Count) 개만 찾았다 — 자리를 가진 위젯이 너무 적다"

# ── 회차 B: A 의 자리를 찌르고, 그 뒤에 드러난 자리를 다시 읽는다 ────────
$driveB = [Collections.Generic.List[string]]::new()
foreach ($c in $setup) { $driveB.Add($c) }
Add-Prefix $driveB $targetsA
$driveB.Add('editor.state')

$runB = Invoke-Editor $driveB.ToArray() 'survey-b' $workspaceDir
$stateB = Read-State $runB 'B'
$targetsB = Select-Targets $stateB

$namesA = @($targetsA | ForEach-Object { $_.widget })
$revealed = @($targetsB | Where-Object { $namesA -notcontains $_.widget })
foreach ($t in $targetsA) {
    Write-Host ('  자리(A) {0,-30} ({1:N0},{2:N0})-({3:N0},{4:N0})' -f `
        $t.widget, $t.rect[0], $t.rect[1], $t.rect[2], $t.rect[3])
}
foreach ($t in $revealed) {
    Write-Host ('  자리(B) {0,-30} ({1:N0},{2:N0})-({3:N0},{4:N0})  <- A 의 자극이 드러냈다' -f `
        $t.widget, $t.rect[0], $t.rect[1], $t.rect[2], $t.rect[3])
}

# ── 회차 C: B 와 같은 접두사로 배치를 재현한 뒤, 새 자리까지 찌른다 ──────
$driveC = [Collections.Generic.List[string]]::new()
foreach ($c in $setup) { $driveC.Add($c) }
$driveC.Add('editor.state reset')
$driveC.Add('wait 30')
Add-Prefix $driveC $targetsA    # <- B 와 같은 순서·같은 좌표. 배치가 같아진다.
# Tab 이 값 줄을 편집 모드에 넣어 두었을 수 있다. 그 상태로 포인터를 얹으면
# 그리기를 넘겨받은 표준 위젯이 hover 를 가져가므로 먼저 편집을 닫는다.
$driveC.Add('editor.nav key escape'); $driveC.Add('wait 12')
Add-Poke $driveC $revealed      # <- B 가 그 배치에서 찾은 자리.
# disabled 축: 엔티티를 잠근다. 인스펙터가 잠긴 대상에 `BeginDisabled` 를 씌운다.
$driveC.Add('object.lock W7_2 true'); $driveC.Add('wait 40')
$driveC.Add('editor.state')

$runC = Invoke-Editor $driveC.ToArray() 'drive' $workspaceDir
$final = Read-State $runC 'C'

Assert ($final.data.frames -gt 0) '장부가 프레임을 한 번도 관측하지 않았다'
Assert ($final.data.announced -gt 0) '상태 신고가 0 이다 — 그려진 위젯이 없다'

# ── 행렬을 찍는다. 판정보다 먼저 **보이게** 한다 ─────────────────────────
Write-Host ''
Write-Host '  위젯                            선언                          관측'
$stimulated = 0
$unstimulated = [Collections.Generic.List[string]]::new()
foreach ($w in $final.data.widgets) {
    Write-Host ('  {0,-30} [{1,-26}] [{2}]' -f `
        $w.widget, (@($w.declared) -join ','), (@($w.observed) -join ','))
    foreach ($state in @($w.missing)) { $unstimulated.Add($w.widget + '.' + $state) }
    $stimulated += @($w.observed).Count
}

# ── 소스에서 선언된 이름이 런타임에 전부 나타났는가 ──────────────────────
#
# 양쪽을 독립으로 유도해 맞댄다 — 소스는 `state::declare` 의 문자열에서, 런타임은
# 장부에서. 나타나지 않은 위젯은 **그려진 적이 없다**는 뜻이고, 그것은 미자극과
# 다른 결함이다(자극 이전에 자리가 없다).
$runtimeWidgets = @($final.data.widgets | ForEach-Object { $_.widget })
$absent = @($sourceWidgets | Where-Object { $runtimeWidgets -notcontains $_ })

# 나타나지 않아도 되는 것은 사유와 함께 여기 적는다. 목록 밖이 사라지면 붉어진다.
#
# ★ 둘이 빠지는데 **이유가 서로 다르다.** 하나는 그릴 코드가 없고, 하나는 그릴
#   대상을 만들 표면이 없다. 같은 "안 나타남" 으로 뭉뚱그리면 다음 사람이 둘 다
#   자극 부족으로 읽는다.
$mayBeAbsent = @{
    'EditorModeButton' =
        'draw_mode_button 의 소비자가 0 이다 — §7.1 이 적은 family 인데 그 자리를 ' +
        '툴바의 사설 버튼이 차지하고 있다(W2-2 게이트가 소비자 0 을 수로 못 박는다). ' +
        '소비자가 생기면 이 면제를 지워라.'
    'EditorSectionHeader' =
        '소비자가 ImGuiDrawHelperRectTransformComponent 하나뿐이고 그것은 ' +
        'RectTransformComponent 를 가진 엔티티에만 돈다. 그런데 그 컴포넌트는 ' +
        '엔티티 타입이 UI/Canvas 일 때 생성자가 붙이는 것이라(Entity.cpp:64,72) ' +
        'component.add 로 붙일 수 없고 실제로 ComponentFactory 등록 표 26 종에 없다. ' +
        'object.create 의 타입은 Empty/Light/Camera/Mesh 뿐이고 UI 계층을 세우는 ' +
        'ui.navprobe 는 커맨드릿 전용이라 라이브 CLI 에서 부를 수 없다 — 즉 이 ' +
        '하네스에 UI 엔티티를 만들 표면이 0 이다. 표면이 서면 면제를 지우고 ' +
        '머리줄을 행렬 판정에 넣어라.'
}
foreach ($name in $absent) {
    Assert ($mayBeAbsent.ContainsKey($name)) `
        ("선언했는데 런타임에 한 번도 그려지지 않은 위젯: $name. " +
         '자극이 모자란 것이 아니라 자리가 없는 것이다 — 그리는 경로를 확인하고, ' +
         '그려질 수 없는 이유가 있으면 이 게이트의 면제 표에 사유와 함께 적어라.')
    Write-Host ("  면제: $name — " + $mayBeAbsent[$name])
}
foreach ($name in $mayBeAbsent.Keys) {
    Assert ($absent -contains $name) `
        ("면제해 둔 $name 이 이제 그려진다 — 면제를 지우고 행렬 판정에 넣어라.")
}

# ── 판정 ──────────────────────────────────────────────────────────────────
#
# ★ "선언한 상태가 전부 관측됐다" 를 요구하지 **않는다.** 이 하네스가 닿지
#   못하는 자리가 실재한다(씬 뷰 툴바에는 Tab 이 가지 않는다). 요구하는 것은
#   둘이다.
#     · 자극한 위젯은 hover·active·nav·disabled 를 **실제로** 냈다 — 포인터와
#       키가 헛찌르지 않았다는 증거다. 이것이 없으면 아래 0 들이 전부 무의미하다.
#     · 미관측 상태는 **이름으로 전부 나온다** — 초록 안에 숨지 않는다.
$withHover = @($final.data.widgets | Where-Object { @($_.observed) -contains 'hover' })
Assert ($withHover.Count -ge 1) `
    '포인터를 찔렀는데 hover 를 낸 위젯이 하나도 없다 — 주입이 서지 않았거나 자리가 빗나갔다'
$withActive = @($final.data.widgets | Where-Object { @($_.observed) -contains 'active' })
Assert ($withActive.Count -ge 1) `
    '눌렀는데 active 를 낸 위젯이 하나도 없다 — 누름 전이가 서지 않았다'
$withNav = @($final.data.widgets | Where-Object { @($_.observed) -contains 'nav' })
Assert ($withNav.Count -ge 1) `
    'Tab 을 30 회 넣었는데 nav 를 낸 위젯이 하나도 없다'
$withDisabled = @($final.data.widgets | Where-Object { @($_.observed) -contains 'disabled' })
Assert ($withDisabled.Count -ge 1) `
    '엔티티를 잠갔는데 disabled 를 낸 위젯이 하나도 없다 — 잠금이 위젯까지 내려가지 않는다'

# 두 바퀴를 돈 값이 한 바퀴보다 나아야 한다. 회차 B 가 새 자리를 찾았다면
# 그 위젯은 최종 판정에 반드시 있어야 한다 — 없으면 접두사 재현이 깨진 것이다.
foreach ($t in $revealed) {
    Assert ($runtimeWidgets -contains $t.widget) `
        ("B 회차가 찾은 $($t.widget) 이 C 회차에 없다 — 자극 접두사가 같은 배치를 " +
         '재현하지 못했다. 두 회차의 자극 순서와 좌표가 어긋났는지 확인하라.')
}

# mixed·error 는 어느 위젯도 구분한다고 선언해서는 안 된다 — 원천이 없다.
# 누가 선언하면 원천이 생겼다는 뜻이고, 그때 이 게이트를 고쳐야 한다.
foreach ($w in $final.data.widgets) {
    foreach ($state in @('mixed', 'error')) {
        Assert (@($w.declared) -notcontains $state) `
            ("$($w.widget) 이 $state 를 구분한다고 선언했다 — 원천이 생겼으면 " +
             "이 게이트의 기대치와 계획서 W2 를 함께 고쳐라")
        Assert (@($w.notApplicable) -contains $state) `
            ("$($w.widget) 이 $state 를 '올 수 없다' 로도 선언하지 않았다 — " +
             "일곱 중 하나가 행렬에서 빈칸으로 남는다")
    }
    Assert ($w.reason.Length -gt 0) `
        "$($w.widget) 이 올 수 없다고만 하고 이유를 남기지 않았다"

    # "올 수 없다" 고 적어 둔 상태가 관측됐다면 그 선언이 낡은 것이다. 면제는
    # 사실이어야 하고, 사실이 바뀌면 붉어져야 한다 — 조용히 맞아떨어지는 면제는
    # 다음 사람에게 거짓말을 남긴다.
    $contradiction = @(@($w.observed) | Where-Object { @($w.notApplicable) -contains $_ })
    Assert (0 -eq $contradiction.Count) `
        ("$($w.widget) 이 '올 수 없다' 고 선언한 상태를 실제로 냈다: " +
         ($contradiction -join ',') + '. 선언을 고치고 사유를 다시 적어라.')
}

# ── 미자극은 0 이어야 한다 ───────────────────────────────────────────────
#
# 처음엔 미자극을 **이름으로 찍기만** 했다. 그랬더니 계측을 망가뜨리는 변이가
# 전부 초록으로 지나갔다 — 관측이 사라져도 "자극하지 못한 것" 으로 읽혔기
# 때문이다. 지금은 그려지는 위젯 넷이 선언한 모든 상태를 실제로 낸다(실측). 그
# 값을 판정으로 세운다.
#
# 하네스가 닿지 못하는 자리가 새로 생기면 여기에 **사유와 함께** 적어라. 허용치를
# 늘리지 말고 판정에서 빼되 수는 남긴다 — 비어 있는 표가 지금의 사실이다.
$mayBeUnstimulated = @{}
$unexpected = @($unstimulated | Where-Object { -not $mayBeUnstimulated.ContainsKey($_) })
Write-Host ''
foreach ($name in @($mayBeUnstimulated.Keys | Where-Object { $unstimulated -contains $_ })) {
    Write-Host ("  미자극 면제: $name — " + $mayBeUnstimulated[$name])
}
Assert (0 -eq $unexpected.Count) `
    ("선언했는데 한 번도 관측되지 않은 상태 " + $unexpected.Count + ' 건: ' +
     ($unexpected -join ', ') + ". 셋 중 하나다 — 계측이 그 상태를 못 읽거나, " +
     '자극이 그 위젯에 닿지 않거나, 그 상태가 애초에 올 수 없거나. ' +
     "세 번째라면 declare 의 not_applicable 로 옮기고 이유를 적어라.")
foreach ($name in $mayBeUnstimulated.Keys) {
    Assert ($unstimulated -contains $name) `
        "미자극으로 면제해 둔 $name 이 이제 관측된다 — 면제를 지워라."
}
Write-Host ('  관측된 상태 합계 ' + $stimulated + ' · 위젯 ' + @($final.data.widgets).Count +
            ' · 프레임 ' + $final.data.frames + ' · 신고 ' + $final.data.announced +
            ' · B 가 드러낸 자리 ' + $revealed.Count)

Write-Host ''
Write-Host ("상태 행렬 OK — 위젯 " + @($final.data.widgets).Count + " · 미자극 " + $unstimulated.Count +
            " · 단정 " + $script:checks + " 건")
exit 0
