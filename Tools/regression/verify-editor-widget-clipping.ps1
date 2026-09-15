[CmdletBinding()]
param(
    [string]$Exe = (Join-Path $PSScriptRoot '../../Bin/x64-Release/Editor/CreatorEditor.exe'),
    [string]$Work = (Join-Path $env:TEMP 'creator-editor-widget-clipping')
)
# PHASE 21 W2-2 — 잘라 그리기 계약.
#
# 계획서 §7.1 의 같은 문장에서 이번엔 두 절이다:
#
#   *"custom widget은 ImGui ID, nav, focus, disabled, clipping, tooltip,
#     testability를 보존해야 하며 …"*
#
# ── clipping 과 tooltip 은 한 규칙의 양쪽이다 ─────────────────────────────
#
# 자기 칸보다 넓은 글자를 안 자르면 옆 칸의 버튼·아이콘 **위로** 그려진다
# (글자가 대개 나중이라 덮는 쪽이다). 잘랐는데 전체를 tooltip 으로 돌려주지
# 않으면 이름을 영영 못 읽는다. 앞쪽은 눈에 띄는 고장이고 뒤쪽은 **조용히
# 사라지는 정보**라 더 오래 남는다. 그래서 둘을 나란히 센다.
#
# ── 규칙은 이미 서 있었고, 한 위젯만 알고 있었다 ─────────────────────────
#
# `EditorPropertyRow` 가 정본이다: 재고, 넘치면 자르고, 잘린 줄에 tooltip 을
# 준다. 실측(2026-09-15)으로 형제들은 일부만 알고 있었다 —
# `EditorInspectorPanel` 은 자르기만 하고 돌려주지 않았고(주석에는 "둘 다 안
# 읽힌다" 고 적어 두었다), `EditorSectionHeader` 는 **둘 다 없었다**.
#
# ── 자극이 없으면 0 은 아무 뜻이 없다 ────────────────────────────────────
#
# 이 축의 함정은 nav 보다 깊다. `EditorPropertyRow` 의 라벨 열은 **라벨에 맞춰
# 커지므로** 평상시 잘릴 일이 없고, Inspector 도크는 창을 좁혀도 폭이 그대로다
# (실측: 창 2200 → 900 인데 Inspector 는 둘 다 634 px). 그래서 창 크기로는
# 자를 상황을 만들 수 없다 — 그냥 재면 위반 0 이 나오는데 그것은 계약을
# 지켰다는 뜻이 아니라 **한 번도 자극하지 않았다**는 뜻이다.
#
# 자극은 배치로 만든다. 1 회차가 저장한 `active.workspace` 의 도킹 데이터에서
# 오른쪽 열의 가로 `SizeRef` 를 고쳐 다시 물린다. 좁히기가 실제로 먹었는지를
# 파일에서 먼저 단정하고(치환 건수), 런타임에서는 `truncated > 0` 으로 다시
# 확인한다.
#
# ★ 대조군은 "손대지 않은 기본 배치" 가 아니다. 처음엔 그렇게 짰는데 실측에서
#   기본 배치도 이미 좁아(1600x1000 에서 오른쪽 열 352 px) 182 건이 잘렸다 —
#   대조군이 대조가 아니었다. 그래서 **같은 저장본을 반대 방향으로 넓힌다.**
#   한 출처에서 두 수를 유도하고 방향만 반대로 두는 것이 대조다. 넓힌 쪽이
#   0 이어야 좁힌 쪽의 수가 폭 때문이라는 말이 선다.
#
# ── 두 축 ─────────────────────────────────────────────────────────────────
#
#  ① 런타임(좁은 배치 + 넓은 대조군). "자르는 줄이 있다" 가 아니라 "그 상황에서
#     실제로 자르고 돌려줬다" 를 잰다.
#  ② 소스 대조. 런타임이 닿지 못하는 자리를 지킨다 — 실제로
#     `EditorModeButton` 은 **소비자가 0 이라** 영영 자극할 수 없다.

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

function Strip-Comment([string]$Line) {
    $at = $Line.IndexOf('//')
    if ($at -ge 0) { return $Line.Substring(0, $at) }
    return $Line
}
function Get-CodeText([string]$Path) {
    ($(foreach ($line in [IO.File]::ReadAllLines($Path)) { Strip-Comment $line }) -join "`n")
}

Write-Host ''
Write-Host '잘라 그리기 계약 (PHASE 21 W2-2)'
Write-Host ''

# ══ ① 소스 대조 ═══════════════════════════════════════════════════════════
#
# 글자를 **직접** 그리는 자리가 계약의 대상이다. 표준 위젯을 쓰면 ImGui 가
# 알아서 자르지만, `RenderText`/`AddText` 를 직접 부르는 순간 자르기와 tooltip
# 은 그 코드의 책임이 된다. 목록을 손으로 적지 않고 소스에서 뽑는다.
$sources = @(Get-ChildItem -LiteralPath $editorRoot -Recurse -Filter '*.cpp' |
             Where-Object { $_.FullName -notmatch '\\RenderTests\\' })
Assert ($sources.Count -gt 100) "Editor 트리에서 .cpp 를 $($sources.Count) 개만 찾았다 — 훑는 범위가 틀렸다"

$textFiles = [Collections.Generic.List[string]]::new()
foreach ($file in $sources) {
    $code = Get-CodeText $file.FullName
    if ($code -match '(?m)\b(RenderText|RenderTextClipped|AddText)\s*\(') { $textFiles.Add($file.Name) }
}
$textNames = @($textFiles | Sort-Object)
Write-Host ("  글자를 직접 그리는 파일 " + $textNames.Count + " 개: " + ($textNames -join ', '))

# 계약 대상 — §7.1 의 네 family, W2-I4 가 더한 패널, 그리고 툴바의 다섯째 자리.
#
# ★ `SceneViewportOverlay` 는 §7.1 의 목록 밖이다. 그런데 `InvisibleButton`
#   위에 프레임·글자·포커스 링·tooltip 을 직접 그리므로 실질은 custom draw 다.
#   W2-1 의 소스 축이 `ItemAdd` 호출처로 대상을 뽑아 이 자리를 놓쳤다 —
#   같은 어긋남이 기제만 바꿔 되풀이된 것이라 여기서는 목록에 넣는다.
$contracted = @(
    'EditorAxisField3.cpp'
    'EditorInspectorPanel.cpp'
    'EditorModeButton.cpp'
    'EditorPropertyRow.cpp'
    'EditorSectionHeader.cpp'
    'SceneViewportOverlay.cpp'
)
# 면제 — 이름으로 적고 이유를 남긴다.
$exempt = @{
    'ProfilerWindow.cpp'  = 'PHASE 14 프로파일러의 타임라인. 자기 클립 안에서만 그리고 W2 의 custom draw family 가 아니다'
    'HierarchyWindow.cpp' = '평탄 목록의 행. W7-3 의 clipper 와 자기 PushClipRect 로 이미 잘리고, 이름 전체는 행 자체가 보여 준다'
    'ContentsBrowserWindow.cpp' = 'W7-1 스냅샷의 타일·트리. 자기 PushClipRect 안에서만 그린다'
    'InspectorWindow.cpp' = '패널 바깥의 머리글. custom draw family 가 아니라 창 본문이다'
    'SceneViewWindow.cpp' = 'W4 의 캔버스 위 안내 문구. 캔버스 클립 안이다'
    'EditorWindowChrome.cpp' = '창 프레임의 제목. 겹칠 상황이면 자르는 대신 **아예 그리지 않는다** — 잘라 그리기와 다른 전략이고 그 조건이 소스에 그대로 있다'
    'GameViewWindow.cpp' = '카메라 없음 안내. 글자가 상수라 사용자 데이터로 늘어나지 않는다'
    'MenuBarWindow.cpp' = '메뉴 아이콘과 BT 노드 편집기. ★ 노드 편집기 두 자리(node.Name·node.ScriptName)는 사용자 이름을 고정 폭 상자에 자르지 않고 그린다 — 같은 결함의 실제 사례이지만 계획서 §7.1 이 node editor 를 범위 밖으로 못 박았다. 별도 작업으로 남긴다'
}

$expected = @($contracted + @($exempt.Keys) | Sort-Object -Unique)
$unexpected = @($textNames | Where-Object { $expected -notcontains $_ })
Assert ($unexpected.Count -eq 0) `
    ("글자를 직접 그리는데 계약에도 면제에도 없는 파일: " + ($unexpected -join ', ') +
     ". 계약 대상이면 editor::clipping::announce_text 를 붙이고, 아니면 이유를 적어 면제에 넣어라")

$missing = @($contracted | Where-Object { $textNames -notcontains $_ })
Assert ($missing.Count -eq 0) `
    ("계약 대상인데 글자를 그리는 자리가 사라졌다: " + ($missing -join ', ') +
     ". 목록이 실물보다 오래 살아남았다")

Write-Host ("  계약 대상 " + $contracted.Count + " 개 · 면제 " + $exempt.Count + " 개")
foreach ($name in ($exempt.Keys | Sort-Object)) {
    Write-Host ("    면제: " + $name + " — " + $exempt[$name])
}

# 계약 대상은 전부 장부에 신고해야 한다.
foreach ($name in $contracted) {
    $file = @($sources | Where-Object { $_.Name -eq $name } | Select-Object -First 1)
    Assert ($file.Count -eq 1) "$name 을 Editor 트리에서 못 찾았다"
    $code = Get-CodeText $file[0].FullName
    Assert ($code -match 'clipping::announce_text\s*\(') `
        "$name : 글자를 직접 그리면서 clipping::announce_text 를 부르지 않는다 — 판정이 이 자리를 못 본다"
}

# 클립을 창과 교차하지 않고 미는 것은 **구조적인 누수**다. ImGuizmo 가 정확히
# 그래서 패널 위로 새어 나갔다(W4, 2026-09-14). 전체 화면 클립은 다른 함수라
# 이 정규식에 걸리지 않는다.
$leaky = [Collections.Generic.List[string]]::new()
foreach ($file in $sources) {
    $code = Get-CodeText $file.FullName
    if ($code -match '(?m)PushClipRect\s*\([^;]*,\s*false\s*\)') { $leaky.Add($file.Name) }
}
Assert ($leaky.Count -eq 0) `
    ("클립을 창과 교차하지 않고 미는 파일: " + ($leaky -join ', ') +
     ". 그리기가 패널 밖으로 샌다 — 교차 인자를 true 로 둬라")

# ── 자극할 수 없는 자리를 이름으로 남긴다 ────────────────────────────────
#
# `EditorModeButton` 은 **소비자가 0 이다**(2026-09-15 실측). §7.1 은 이것을
# 뷰포트 툴바의 family 로 적었지만 툴바는 자기 버튼을 따로 갖고 있다. 그리는
# 곳이 없으니 런타임 축이 영영 닿지 못한다 — 그 사실을 게이트가 들고 있어야
# 초록이 거짓말을 하지 않는다. 누가 소비자를 이으면 여기서 붉어지고, 그때
# 런타임 기대치로 옮기면 된다.
$modeButtonConsumers = [Collections.Generic.List[string]]::new()
foreach ($file in $sources) {
    if ('EditorModeButton.cpp' -eq $file.Name) { continue }
    if ($file.Name -match 'SelfTest') { continue }
    $code = Get-CodeText $file.FullName
    if ($code -match 'draw_mode_button\s*\(') { $modeButtonConsumers.Add($file.Name) }
}
Assert ($modeButtonConsumers.Count -eq 0) `
    ("EditorModeButton 에 소비자가 생겼다: " + ($modeButtonConsumers -join ', ') +
     ". 이 게이트의 런타임 기대치에 그 위젯을 넣어라 — 더는 '자극 불가' 가 아니다")

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
    Assert (Test-Path -LiteralPath $resultPath) `
        "결과 파일이 없다 ($Tag, 종료 코드 $($proc.ExitCode))"
    return [pscustomobject]@{
        ExitCode = $proc.ExitCode
        Rows = @(Get-Content -LiteralPath $resultPath | Where-Object { $_.Trim() } |
                 ForEach-Object { $_ | ConvertFrom-Json })
    }
}

# Inspector 에 내용을 채우고 장부를 읽는 공통 회차.
$measure = @(
    'window.resize 1600 1000'
    'wait 150'
    'scene.populate 6 0'
    'wait 50'
    'scene.select W7_2'
    'wait 50'
    'editor.window ###Editor.Inspector focus'
    'wait 50'
    # reset 은 자극 앞이다 — 부팅 구간은 그려진 것이 적어 수가 전부 0 이고,
    # 그것을 판정에 섞으면 자극이 닿았는지 알 수 없다.
    'editor.clipping reset'
    'wait 90'
    'editor.clipping'
)

# ── 1 회차: 지금 배치를 저장시킨다 ────────────────────────────────────────
$baseDir = Join-Path $Work 'workspace-base'
New-Item -ItemType Directory -Force -Path $baseDir | Out-Null
Invoke-Editor @(
    'window.resize 1600 1000'
    'wait 150'
    'editor.window ###Editor.Inspector focus'
    'wait 40'
    'editor.workspace save'
    'wait 40'
) 'save' $baseDir | Out-Null

$baseFile = Join-Path $baseDir 'active.workspace'
Assert (Test-Path -LiteralPath $baseFile) "1 회차가 배치를 저장하지 않았다: $baseFile"

# ── 한 저장본에서 두 배치를 유도한다 ──────────────────────────────────────
#
# 오른쪽 열(Hierarchy·Inspector)의 가로 `SizeRef` 를 고친다. 값을 박아 두지
# 않고 **파일에서 읽어** 가장 작은 가로값을 고른다 — 기본 배치가 바뀌어도
# 따라간다. 치환 건수를 단정하는 것이 요점이다: 고치기가 조용히 실패하면
# 아래 런타임 판정은 같은 배치를 두 번 재면서 초록이 된다.
$baseText = Get-Content -LiteralPath $baseFile -Raw
$widths = @([regex]::Matches($baseText, 'SizeRef=(\d+),') | ForEach-Object { [int]$_.Groups[1].Value })
Assert ($widths.Count -ge 2) "도킹 데이터에서 SizeRef 를 $($widths.Count) 개만 찾았다 — 저장 형식이 바뀌었다"
$rightColumn = @($widths | Sort-Object)[0]
Assert ($rightColumn -gt 200) "오른쪽 열이 이미 $rightColumn px 다 — 좁힐 것이 없다"
$needle = "SizeRef=" + $rightColumn + ","
$replaced = ([regex]::Matches($baseText, $needle)).Count
Assert ($replaced -ge 2) "오른쪽 열 치환이 $replaced 건만 먹었다 — 도킹 데이터의 열을 못 찾았다"

$script:layoutWidths = @{}
function New-Layout([string]$Name, [int]$Width) {
    $dir = Join-Path $Work ("workspace-" + $Name)
    New-Item -ItemType Directory -Force -Path $dir | Out-Null
    $text = [regex]::Replace($baseText, $needle, ("SizeRef=" + $Width + ","))
    Assert ($text -ne $baseText) "$Name 배치를 만들지 못했다 — 치환이 먹지 않았다"
    Set-Content -LiteralPath (Join-Path $dir 'active.workspace') -Value $text -NoNewline
    # ★ 쓴 값을 **적어 둔다.** 아래 출력이 리터럴을 찍고 있었는데, 폭을 바꾸는
    #   변이를 걸자 게이트가 바뀐 값이 아니라 원래 적어 둔 숫자를 찍었다 —
    #   출력이 한 일이 아니라 의도를 말하면 진단이 거짓이 된다.
    $script:layoutWidths[$Name] = $Width
    return $dir
}

$narrowDir = New-Layout 'narrow' 150
$wideDir   = New-Layout 'wide' 900
Write-Host ("  오른쪽 열 " + $rightColumn + " px → 좁은 " + $script:layoutWidths['narrow'] +
            " px / 넓은 " + $script:layoutWidths['wide'] + " px (" + $replaced + " 곳)")

# 좁힌 쪽이 실제로 더 좁아야 한다. 이 단정이 없으면 좁히기가 방향을 잃어도
# 아래 판정이 같은 배치를 두 번 재면서 초록이 된다.
Assert ($script:layoutWidths['narrow'] -lt $script:layoutWidths['wide']) `
    ("좁힌 폭 " + $script:layoutWidths['narrow'] + " px 가 넓힌 폭 " +
     $script:layoutWidths['wide'] + " px 보다 좁지 않다 — A/B 가 서지 않는다")

# ── 2·3 회차 ──────────────────────────────────────────────────────────────
$narrow = Invoke-Editor $measure 'narrow' $narrowDir
$wide = Invoke-Editor $measure 'wide' $wideDir

function Get-Final($Run) {
    $rows = @($Run.Rows | Where-Object { $_.command -eq 'editor.clipping' })
    Assert ($rows.Count -ge 2) "editor.clipping 결과가 $($rows.Count) 줄이다 — 명령이 돌지 않았다"
    return $rows[$rows.Count - 1]
}

$narrowFinal = Get-Final $narrow
$wideFinal = Get-Final $wide

# ── 축이 실재하는가 ───────────────────────────────────────────────────────
foreach ($pair in @(@{ n = '좁은'; v = $narrowFinal }, @{ n = '넓은'; v = $wideFinal })) {
    Assert ($pair.v.data.frames -gt 0) "$($pair.n) 회차: 장부가 프레임을 한 번도 관측하지 않았다"
    Assert ($pair.v.data.announced -gt 0) `
        "$($pair.n) 회차: 글자 신고가 0 이다 — Inspector 에 그려진 것이 없거나 신고가 끊겼다"
}

# ── 자극이 닿았는가 ───────────────────────────────────────────────────────
$measured = @($narrowFinal.data.measuredWidgets)
Write-Host ('  폭을 잰 위젯: ' + (($measured | Sort-Object) -join ', '))
Assert ($measured.Count -ge 3) `
    "좁은 회차에서 폭을 잰 위젯이 $($measured.Count) 개다 — 자극이 Inspector 에 닿지 않았다"
Assert ($narrowFinal.data.truncated -gt 0) `
    ("좁은 회차에서 자른 신고가 0 이다 — 배치를 좁혔는데 아무것도 넘치지 않았다. " +
     "이 상태의 '위반 0' 은 계약을 지켰다는 뜻이 아니라 한 번도 자극하지 않았다는 뜻이다")
Write-Host ('  좁은 회차에서 자른 위젯: ' + ((@($narrowFinal.data.truncatedWidgets) | Sort-Object) -join ', '))

# 대조군은 반대 방향이다. 같은 저장본을 넓혔으니 자를 일이 없어야 한다 —
# 이것이 없으면 좁은 회차의 수가 폭 때문인지 원래 그런지 가를 수 없다.
Assert ($wideFinal.data.truncated -eq 0) `
    ("대조군(넓힌 배치)에서도 자른 신고가 $($wideFinal.data.truncated) 건이다 — " +
     "폭이 원인이 아니라면 좁은 회차의 수도 아무것도 증명하지 못한다. " +
     "잰 위젯: " + ((@($wideFinal.data.truncatedWidgets)) -join ', '))

# ── 판정 ──────────────────────────────────────────────────────────────────
foreach ($pair in @(@{ n = '좁은'; v = $narrowFinal }, @{ n = '넓은'; v = $wideFinal })) {
    Assert ($pair.v.data.overflowFrames -eq 0) `
        ("$($pair.n) 회차: 칸보다 넓은 글자를 자르지 않은 신고 $($pair.v.data.overflowFrames) 건 — " +
         (@($pair.v.data.overflowWidgets) -join ', '))
    Assert ($pair.v.data.silentFrames -eq 0) `
        ("$($pair.n) 회차: 잘라 놓고 전체를 돌려주지 않은 신고 $($pair.v.data.silentFrames) 건 — " +
         (@($pair.v.data.silentWidgets) -join ', '))
    Assert ($pair.v.data.unbalanced -eq 0) `
        ("$($pair.n) 회차: 클립 스택이 균형을 잃은 횟수 $($pair.v.data.unbalanced) 회 — " +
         (@($pair.v.data.unbalancedWidgets) -join ', '))
    Assert ($pair.v.status -eq 'succeeded') `
        "$($pair.n) 회차: editor.clipping 이 $($pair.v.status) 로 끝났다"
}

Write-Host ''
Write-Host ('  ★ 자극하지 못한 대상: EditorModeButton — 소비자가 0 이라 그려지는 곳이 없다. ' +
            '이 위젯의 계약은 소스 대조(①)만이 지킨다')
Write-Host ('  좁은 회차 프레임 ' + $narrowFinal.data.frames + ' · 신고 ' + $narrowFinal.data.announced +
            ' · 자름 ' + $narrowFinal.data.truncated +
            '   /   넓은 회차 프레임 ' + $wideFinal.data.frames + ' · 신고 ' + $wideFinal.data.announced +
            ' · 자름 ' + $wideFinal.data.truncated)

# 종료 코드는 **맨 끝**이다. 앞에 두면 그 하나가 붉어질 때 뒤의 단정들이
# 통째로 가려진다(W2-1 에서 겪은 그대로).
Assert (0 -eq $narrow.ExitCode) "좁은 회차 종료 코드 $($narrow.ExitCode)"
Assert (0 -eq $wide.ExitCode) "넓은 회차 종료 코드 $($wide.ExitCode)"

Write-Host ''
Write-Host ("잘라 그리기 계약 OK — 계약 대상 " + $contracted.Count + " · 면제 " + $exempt.Count +
            " · 잰 위젯 " + $measured.Count + " · 미자극 1 · 단정 " + $script:checks + " 건")
exit 0
