[CmdletBinding()]
param(
    [string]$Exe = (Join-Path $PSScriptRoot '../../Bin/x64-Release/Editor/CreatorEditor.exe'),
    [string]$Work = (Join-Path $env:TEMP 'creator-editor-keyboard-nav')
)
# PHASE 21 W2-1 — 키보드 탐색 계약.
#
# 계획서 §7.1 의 판정문은 이것이다:
#
#   *"custom widget은 ImGui ID, nav, focus, disabled, clipping, tooltip,
#     testability를 보존해야 하며 별도 input framework를 만들지 않는다."*
#
# ── 그 문장에 자가 없었다 ─────────────────────────────────────────────────
#
# nav 상태를 내보내는 표면이 0 이고 키를 주입할 표면도 0 이었다. 그래서 "키보드
# 탐색이 유지된다" 는 문장일 뿐이었다 — W8-3 의 검증 레이어와 같은 모양이다
# (레이어는 켜 놓고 아무도 큐를 읽지 않았다).
#
# W2-1 이 `rhi::validation` 과 같은 꼴의 장부(`editor::nav`)를 세우고
# `editor.nav [reset|key ...]` 로 읽고 자극한다. 이 게이트가 그 자다.
#
# ── 실제로 무엇이 틀려 있었나 ─────────────────────────────────────────────
#
# `ImGui::RenderNavCursor` 는 위젯이 **직접** 불러야 한다 — `ItemAdd` 가 대신
# 그려 주지 않는다(표준 `ButtonEx` 도 스스로 부른다). custom widget 넷 중
# `EditorPropertyRow` 하나만 그것을 불렀고, 나머지 셋은 `ItemAdd` +
# `ButtonBehavior` 를 하므로 **키보드로 닿고 Enter 로 눌리기까지 하는데 어디에
# 서 있는지 보이지 않았다.** 테마는 `ImGuiCol_NavCursor` 를 `Primary` 로 이미
# 정해 두었다 — 색은 있고 그리는 곳이 없었다.
#
# ── 두 축으로 잰다 ────────────────────────────────────────────────────────
#
#  ① **런타임.** Tab 을 주입해 nav 를 돌리고 장부를 읽는다. "부르는 줄이 있다"
#     가 아니라 "그 상황에서 실제로 그렸다" 를 재는 유일한 방법이다.
#  ② **소스 대조.** 런타임은 nav 가 닿는 자리만 본다. 툴바의 `EditorModeButton`
#     처럼 이 하네스가 키보드로 닿지 못하는 자리가 있어서, 그 계약은 소스에서
#     양쪽을 뽑아 맞댄다(`ItemAdd` 를 부르는 파일 전부가 신고와 커서를 부르는가).
#
# ── 눈먼 초록을 막는 장치 ────────────────────────────────────────────────
#
#  · `keyboardEnabled` 를 **먼저** 본다. 거짓이면 위반 0 은 축이 없다는 뜻이다.
#  · `visitedWidgets` 로 **자극이 닿았는지** 본다. 비어 있으면 그 0 은
#    "지켰다" 가 아니라 "자극하지 못했다" 다 — 이 게이트는 그 둘을 다른 문장으로
#    적는다.
#  · `delegatedFrames` 가 관측 프레임을 통째로 덮지 않는지 본다. 면제가 대상을
#    통째로 비우는 것이 이 저장소가 겪은 눈먼 초록의 한 양식이다.
#  · 소스 축은 **이름을 전부 찍는다.** 개수만 맞대면 양쪽이 같이 눈멀 수 있다.
#
# ── 못 잡는 것 ────────────────────────────────────────────────────────────
#
#  · `EditorModeButton` · `EditorSectionHeader` 에 nav 가 닿는 자극을 이 하네스가
#    만들지 못한다(전자는 메뉴바 레이어, 후자는 지금 뜨는 컴포넌트에 없다).
#    그 둘의 계약은 ②가 소스로만 지킨다 — 보고서가 방문한 이름과 못 한 이름을
#    따로 찍으므로 그 사실이 조용히 묻히지 않는다.
#  · 픽셀. 커서가 **어떤 색으로** 그려졌는지는 보지 않는다(테마 감사의 몫이다).
#  · 마우스 조작. 이 게이트는 키보드 축만 잰다.
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$Exe = [IO.Path]::GetFullPath($Exe)
$Work = [IO.Path]::GetFullPath($Work)
$editorRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../../Editor'))

if (-not (Test-Path -LiteralPath $Exe)) { throw "Editor executable not found: $Exe" }
if (-not (Test-Path -LiteralPath $editorRoot)) { throw "Editor 트리를 못 찾았다: $editorRoot" }
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

# 소스 줄에서 주석을 걷는다. `//` 뒤의 글자는 코드가 아니다 — 이것을 안 하면
# "직접 호출 금지" 단정이 **금지를 설명하는 주석** 때문에 붉어진다.
function Strip-Comment([string]$Line) {
    $at = $Line.IndexOf('//')
    if ($at -ge 0) { return $Line.Substring(0, $at) }
    return $Line
}

function Get-CodeText([string]$Path) {
    ($(foreach ($line in [IO.File]::ReadAllLines($Path)) { Strip-Comment $line }) -join "`n")
}

Write-Host ''
Write-Host '키보드 탐색 계약 (PHASE 21 W2-1)'
Write-Host ''

# ══ ① 소스 대조 ═══════════════════════════════════════════════════════════
#
# `ItemAdd` 를 부르는 것은 "표준 위젯을 쓰지 않고 아이템을 직접 등록한다" 는
# 뜻이고, 그 순간부터 nav·focus·disabled 는 그 코드의 책임이 된다. 그래서
# **그 집합**이 계약의 대상이다. 파일 이름을 적어 두는 것이 아니라 소스에서
# 뽑는다 — 새 custom widget 이 늘면 자동으로 이 게이트에 걸린다.
$sources = @(Get-ChildItem -LiteralPath $editorRoot -Recurse -Filter '*.cpp' |
             Where-Object { $_.FullName -notmatch '\\RenderTests\\' })
Assert ($sources.Count -gt 100) "Editor 트리에서 .cpp 를 $($sources.Count) 개만 찾았다 — 훑는 범위가 틀렸다"

$itemAddFiles = [Collections.Generic.List[string]]::new()
foreach ($file in $sources) {
    $code = Get-CodeText $file.FullName
    # 단어 경계로 잡는다. 앞자리를 `[^\w:]` 로 두면 `ImGui::ItemAdd(` 가 통째로
    # 빠진다 — 실제로 처음에 그렇게 써서 다섯 중 하나만 잡혔고, 그때 "계약
    # 대상인데 ItemAdd 가 사라졌다" 는 엉뚱한 진단이 나왔다.
    if ($code -match '(?m)\bItemAdd\s*\(') { $itemAddFiles.Add($file.Name) }
}
$itemAddNames = @($itemAddFiles | Sort-Object)
Write-Host ("  ItemAdd 를 부르는 파일 " + $itemAddNames.Count + " 개: " + ($itemAddNames -join ', '))

# 계약 대상 — 계획서 §7.1 의 네 family 와 W2-I4 가 더한 패널.
#
# ★ 이 목록은 **허용 목록이기도 하다.** 계획서 §7.1 은 custom draw surface 를
#   넷으로 제한하는데 실물은 다섯이다(W2-I4 의 `EditorInspectorPanel`). 그
#   어긋남을 게이트가 들고 있어야 목록이 조용히 늘지 않는다.
$contracted = @(
    'EditorInspectorPanel.cpp'
    'EditorModeButton.cpp'
    'EditorPropertyRow.cpp'
    'EditorSectionHeader.cpp'
)
# 면제 — 계약을 요구하지 않는 자리. **이름으로** 적고 수를 찍는다. 면제가
# 대상을 통째로 비우면 이 게이트는 아무것도 안 보는 것이다.
$exempt = @{
    'ProfilerWindow.cpp' = 'PHASE 14 프로파일러의 타임라인. W2 의 custom draw family 가 아니며 계획서 §7.1 의 허용 목록 밖이다'
}

$expected = @(($contracted + $exempt.Keys) | Sort-Object)
$unexpected = @($itemAddNames | Where-Object { $expected -notcontains $_ })
Assert ($unexpected.Count -eq 0) `
    ("ItemAdd 를 부르는 새 파일이 생겼다: " + ($unexpected -join ', ') +
     ". custom draw 를 늘리려면 계획서 §7.1 의 허용 목록과 이 게이트를 함께 고쳐라")
$missing = @($expected | Where-Object { $itemAddNames -notcontains $_ })
Assert ($missing.Count -eq 0) `
    ("계약 대상인데 ItemAdd 가 사라졌다: " + ($missing -join ', ') +
     ". 표준 위젯으로 옮겼으면 이 게이트의 목록에서도 빼라")
Assert ($exempt.Count -lt $itemAddNames.Count) `
    "면제가 대상을 통째로 비웠다(면제 $($exempt.Count) · 전체 $($itemAddNames.Count))"

foreach ($name in $contracted) {
    $path = @($sources | Where-Object { $_.Name -eq $name } | Select-Object -First 1).FullName
    $code = Get-CodeText $path
    Assert ($code -match 'nav::announce_item\s*\(') `
        "$name : ItemAdd 를 부르면서 nav::announce_item 을 부르지 않는다 — 장부가 이 아이템을 custom widget 의 것으로 알지 못해 판정이 통째로 빈다"
    Assert ($code -match 'nav::draw_cursor\s*\(') `
        "$name : nav::draw_cursor 를 부르지 않는다 — 키보드로 닿아도 어디에 서 있는지 보이지 않는다"
}
Write-Host ("  계약 대상 " + $contracted.Count + " 개: " + ($contracted -join ', '))
foreach ($name in ($exempt.Keys | Sort-Object)) {
    Write-Host ("  면제: " + $name + " — " + $exempt[$name])
}

# 장부를 우회하는 길을 막는다. `RenderNavCursor` 를 직접 부르면 커서는 그려지되
# 그린 사실이 장부에 남지 않아, 런타임 판정이 **옳은 코드를 위반으로 읽는다.**
$direct = [Collections.Generic.List[string]]::new()
foreach ($file in $sources) {
    if ('EditorNavContract.cpp' -eq $file.Name) { continue }
    $code = Get-CodeText $file.FullName
    if ($code -match 'Render(NavCursor|NavHighlight)\s*\(') { $direct.Add($file.Name) }
}
Assert ($direct.Count -eq 0) `
    ("ImGui::RenderNavCursor 를 장부 밖에서 직접 부르는 파일: " + ($direct -join ', ') +
     ". editor::nav::draw_cursor 를 써라")

# disabled 축은 런타임으로 자극하지 못한다(툴바에 nav 가 닿지 않는다). 소스로 지킨다.
$modeButton = Get-CodeText (@($sources | Where-Object { $_.Name -eq 'EditorModeButton.cpp' } | Select-Object -First 1).FullName)
Assert ($modeButton -match 'ImGuiItemFlags_Disabled') `
    "EditorModeButton : 꺼진 버튼을 ImGuiItemFlags_Disabled 로 신고하지 않는다 — 키보드 탐색이 죽은 버튼 위에 멈춘다"

# ══ ② 런타임 ══════════════════════════════════════════════════════════════
$lines = [Collections.Generic.List[string]]::new()
$lines.Add('window.resize 1600 1000')
$lines.Add('wait 150')
# Inspector 에 내용이 있어야 custom widget 이 그려진다.
$lines.Add('scene.populate 8 0')
$lines.Add('wait 60')
$lines.Add('scene.select W7_3')
$lines.Add('wait 60')
$lines.Add('editor.window ###Editor.Inspector focus')
$lines.Add('wait 60')
# ★ reset 은 **자극 앞**이다. 부팅 구간에는 nav 가 아무 데도 서지 않아 수가
#   전부 0 인데, 그것을 판정에 섞으면 자극이 닿았는지 알 수 없다.
$lines.Add('editor.nav reset')
foreach ($round in 1..40) {
    $lines.Add('editor.nav key tab')
    $lines.Add('wait 6')
}
$lines.Add('editor.nav')

$scriptPath = Join-Path $Work 'script.txt'
$resultPath = Join-Path $Work 'result.jsonl'
$stdoutPath = Join-Path $Work 'out.txt'
Set-Content -LiteralPath $scriptPath -Encoding UTF8 -Value ($lines.ToArray() + @('quit'))

$workspaceDir = Join-Path $Work 'workspace'
New-Item -ItemType Directory -Force -Path $workspaceDir | Out-Null
$priorWorkspace = $env:CREATOR_EDITOR_WORKSPACE_DIR
$priorLegacy = $env:CREATOR_EDITOR_LEGACY_INI
$env:CREATOR_EDITOR_WORKSPACE_DIR = $workspaceDir
$env:CREATOR_EDITOR_LEGACY_INI = Join-Path $workspaceDir 'none.ini'
try {
    $proc = Start-Process -FilePath $Exe -WorkingDirectory (Split-Path $Exe) -WindowStyle Hidden -PassThru `
        -ArgumentList @('--script', ('"' + $scriptPath + '"'), '--result-format', 'jsonl',
                        '--result-file', ('"' + $resultPath + '"')) `
        -RedirectStandardOutput $stdoutPath -RedirectStandardError (Join-Path $Work 'err.txt')
    if (-not $proc.WaitForExit(600000)) { $proc.Kill(); throw '에디터가 600 초 안에 끝나지 않았다.' }
}
finally {
    if ($null -ne $priorWorkspace) { $env:CREATOR_EDITOR_WORKSPACE_DIR = $priorWorkspace }
    else { Remove-Item Env:CREATOR_EDITOR_WORKSPACE_DIR -ErrorAction SilentlyContinue }
    if ($null -ne $priorLegacy) { $env:CREATOR_EDITOR_LEGACY_INI = $priorLegacy }
    else { Remove-Item Env:CREATOR_EDITOR_LEGACY_INI -ErrorAction SilentlyContinue }
}

Assert (Test-Path -LiteralPath $resultPath) `
    "결과 파일이 없다 (종료 코드 $($proc.ExitCode); $stdoutPath)"
$rows = @(Get-Content -LiteralPath $resultPath | Where-Object { $_.Trim() } |
          ForEach-Object { $_ | ConvertFrom-Json })
$navRows = @($rows | Where-Object { $_.command -eq 'editor.nav' })
Assert ($navRows.Count -ge 2) "editor.nav 결과가 $($navRows.Count) 줄이다 — 명령이 돌지 않았다"
$final = $navRows[$navRows.Count - 1]

# ── 축이 실재하는가 ───────────────────────────────────────────────────────
Assert ($final.data.keyboardEnabled -eq $true) `
    '키보드 탐색이 꺼져 있다 — 아래 위반 0 은 계약을 지켰다는 뜻이 아니라 축이 없다는 뜻이다'
Assert ($final.data.frames -gt 0) '장부가 프레임을 한 번도 관측하지 않았다'
Assert ($final.data.announced -gt 0) `
    'custom widget 아이템 신고가 0 이다 — Inspector 에 그려진 것이 없거나 신고가 끊겼다'
Assert ($final.data.cursorsDrawn -gt 0) `
    'nav 커서를 한 번도 그리지 않았다 — `draw_cursor` 가 어느 경로에서도 실행되지 않았다'

# ── 자극이 닿았는가 ───────────────────────────────────────────────────────
$visited = @($final.data.visitedWidgets)
Write-Host ('  nav 가 머문 custom widget: ' + (($visited | Sort-Object) -join ', '))
$notVisited = @($contracted | ForEach-Object { [IO.Path]::GetFileNameWithoutExtension($_) } |
                Where-Object { $visited -notcontains $_ -and $visited -notcontains ($_ + '.drag') })
if ($notVisited.Count -gt 0) {
    Write-Host ('  ★ 자극하지 못한 대상: ' + ($notVisited -join ', ') +
                ' — 이 둘의 계약은 소스 대조(①)만이 지킨다')
}
Assert ($final.data.keysDelivered -gt 0) `
    "키가 하나도 전달되지 않았다(delivered=$($final.data.keysDelivered)) — 주입 경로가 끊겼다"
Assert ($final.data.keysPending -eq 0) `
    "예약한 키가 $($final.data.keysPending) 개 남았다 — 대기 프레임이 모자라 자극이 덜 들어갔다"
Assert ($visited.Count -ge 2) `
    "nav 가 머문 custom widget 이 $($visited.Count) 종이다 — 자극이 닿지 않았으면 아래 위반 0 은 아무것도 증명하지 않는다"

# ── 면제가 대상을 비우지 않았는가 ─────────────────────────────────────────
Assert ($final.data.delegatedFrames -lt $final.data.frames) `
    "표준 위젯에 넘긴 프레임이 관측 프레임 전부다($($final.data.delegatedFrames)/$($final.data.frames)) — 판정이 아무것도 보지 않는다"

# ── 판정 ──────────────────────────────────────────────────────────────────
$silentNames = @($final.data.silentWidgets) -join ', '
Assert ($final.data.silentFrames -eq 0) `
    "키보드 탐색이 선 자리에 커서를 안 그린 프레임 $($final.data.silentFrames) 회 — $silentNames"
$disabledNames = @($final.data.disabledWidgets) -join ', '
Assert ($final.data.disabledFrames -eq 0) `
    "키보드 탐색이 손댈 수 없는 자리에 선 프레임 $($final.data.disabledFrames) 회 — $disabledNames"
Assert ($final.status -eq 'succeeded') "editor.nav 가 실패로 끝났다 — $($final.message)"

# ★ 종료 코드는 맨 뒤다. 앞에 세우면 배치 러너의 exit 4 한 줄이 위의 단정들을
#   통째로 가려, 붉은 줄이 고칠 자리를 가리키지 못한다.
Assert ($proc.ExitCode -eq 0) "에디터가 종료 코드 $($proc.ExitCode) 로 끝났다 — $stdoutPath"

Write-Host ''
Write-Host ("  프레임 $($final.data.frames) · 신고 $($final.data.announced) · 커서 $($final.data.cursorsDrawn) · " +
            "위임 $($final.data.delegatedFrames) · 키 $($final.data.keysDelivered)")
Write-Host ''
Write-Host ("키보드 탐색 계약 OK — 계약 대상 $($contracted.Count) · 면제 $($exempt.Count) · " +
            "방문 $($visited.Count) · 미자극 $($notVisited.Count) · 단정 $($script:checks) 건")
