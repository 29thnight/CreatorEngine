[CmdletBinding()]
param(
    [string]$Exe = (Join-Path $PSScriptRoot '../../Bin/x64-Release/Editor/CreatorEditor.exe'),
    [string]$Work = (Join-Path $env:TEMP 'creator-inspector-edit-roundtrip'),
    # 자극에 쓰는 사용자 배율. 실제 배율이 낮아야 줄 전환 경계(256 논리 px)가
    # 도크 패널이 보여 주는 폭 안에 들어온다 — 이 기계의 모니터 배율이 1.5 다.
    [string]$Scale = '1'
)
# PHASE 21 W2-I5 — 인스펙터의 **편집과 왕복**에서 값과 필드 신원이 보존되는가.
#
# 계획서 §W2-I 의 판정문:
#   *"드래그/텍스트 편집 중 폭 변경, Tab·팝업·drag-drop, Undo/Redo, 선택 변경과
#     접기/펼치기, 엔티티 활성 왕복, 씬·프리팹 저장/재로드에서 값과 필드 신원이
#     보존돼야 한다"*.
#
# ── 무엇을 신원으로 잴 것인가 ─────────────────────────────────────────────
#
# 값 칸의 ImGui ID 다. 폭이 바뀌어 줄이 내려가거나 축이 세로가 될 때 ID 가
# 달라지면 그 순간 편집 중이던 칸과 되돌리기 대상이 사라진다. 본문마다 값 칸 ID 를
# 그린 차례대로 섞은 요약(`fieldDigest`)과 지금 편집 중인 항목(`activeId`)을
# `editor.inspector` 가 낸다.
#
# ── 드래그는 흉내가 아니라 실물이다 ───────────────────────────────────────
#
# `editor.nav pointer <x> <y>` · `press` · `release` 가 ImGui 의 마우스 상태를
# 덮는다(W2-3 에서 선 창구). 좌표는 본문이 처음 그린 값 칸의 사각형에서 나온다
# (`firstFieldX/Y/W/H`) — 좌표를 손으로 적으면 배치가 바뀔 때마다 검사가 딴 곳을
# 누른다. 그래서 실행을 둘로 가른다: 첫 실행이 좌표를 읽고, 둘째가 그 자리를 끈다.
#
# 세우면서 걸린 것 셋 —
#   ★ 주입은 좌표만 덮고 **이동량은 실제 마우스의 것**이었다. hover 와 클릭은
#     좌표만 보므로 멀쩡했고(`activeId` 도 섰다) **끄는 것만 조용히 죽어 있었다** —
#     `DragBehavior` 가 보는 것은 `io.MouseDelta` 다. 주입이 이동량도 만들도록 고쳤다.
#   ★ 폭을 정한 채로는 **키로 칸에 들어갈 수 없다.** 폭 창구가 자식 창이고 자식
#     창은 ImGui 에서 별도의 탐색 범위라, Tab 을 열 번 넣어도 `navId` 가 움직이지
#     않는다. 키보드 탐색 계약은 폭 창구 없이 도는 `verify-editor-keyboard-nav` 몫이다.
#   ★ 같은 자리를 잇달아 누르면 **두 번 클릭**이 되어 칸이 글자 편집으로 들어간다 —
#     그러면 끌어도 값이 안 움직인다. 회차 사이에 포인터를 치우고 자리를 옮긴다.
#
# 그리고 **보이는 폭 안에서만** 자극한다. 도크 패널의 폭은 창을 키워도 505 px 로
# 그대로라(도크 노드는 절대 폭을 지킨다) 요청이 그보다 넓으면 오른쪽은 잘려 화면에
# 없고, 잘린 자리를 눌러도 아무 일이 없다.
#
# ── 회차 ──────────────────────────────────────────────────────────────────
#
#   ① 편집 중 폭 변경 — 누른 채 경계 너머로 좁혀도 **배치가 전환되지 않는다**
#      (계약: 편집 중 보류). `activeId`·신원·값이 그대로고, 손을 뗀 뒤에야 좁은
#      배치로 넘어간다. 보류와 해제를 **둘 다** 단정한다.
#   ② 끌어서 값이 실제로 바뀌고, `undo` 가 되돌리고 `redo` 가 되살린다.
#   ③ 선택 변경 왕복 · 접기/펼치기 왕복 · 엔티티 활성 왕복 — 값과 신원 보존.
#   ④ 씬 저장/재로드, 프리팹 만들고 소환 — 값 보존.
#
# 사용법:
#   pwsh Tools/regression/verify-inspector-edit-roundtrip.ps1 -Exe Bin/x64-Release/Editor/CreatorEditor.exe
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$Exe = [IO.Path]::GetFullPath($Exe)
$Work = [IO.Path]::GetFullPath($Work)
$repoRoot = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
$settingsPath = Join-Path $repoRoot 'Dynamic_CPP/ProjectSetting/EngineSettings.asset'
if (-not (Test-Path -LiteralPath $Exe)) { throw "Editor executable not found: $Exe" }
if (-not (Test-Path -LiteralPath $settingsPath)) { throw "Project settings not found: $settingsPath" }
if (Get-Process CreatorEditor -ErrorAction SilentlyContinue) {
    throw '이미 떠 있는 에디터를 닫아라 — 이 검사는 남의 에디터를 멈추지 않는다.'
}
Remove-Item -LiteralPath $Work -Recurse -Force -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force -Path $Work | Out-Null

$script:checks = 0
$failures = [Collections.Generic.List[string]]::new()
function Assert([bool]$Condition, [string]$Message) {
    $script:checks++
    if (-not $Condition) { $failures.Add($Message); Write-Host "  FAIL $Message" -ForegroundColor Red }
}
function Near([double]$a, [double]$b, [double]$tolerance) { [Math]::Abs($a - $b) -le $tolerance }

$Target = 'Edit_Target'
$Other = 'Edit_Other'
$Position = @(1.5, -2.25, 3.75)
$ScenePath = 'Assets/Scenes/InspectorEditRoundtrip.creator'
$Prefab = 'InspectorEditRoundtrip'
# 줄 전환 경계(256 논리 px)의 양쪽. `verify-inspector-layout-matrix` 가 훑어 찾은 값이다.
$WideLogical = 280
$NarrowLogical = 240

function Invoke-Editor([string]$Name, [string[]]$Lines, [string]$Workspace) {
    $scriptPath = Join-Path $Work "$Name.txt"; $resultPath = Join-Path $Work "$Name.jsonl"
    Set-Content -LiteralPath $scriptPath -Encoding UTF8 -Value $Lines
    New-Item -ItemType Directory -Force -Path $Workspace | Out-Null
    $env:CREATOR_EDITOR_WORKSPACE_DIR = $Workspace
    $env:CREATOR_EDITOR_LEGACY_INI = Join-Path $Workspace 'none.ini'
    try {
        $process = Start-Process -FilePath $Exe -WorkingDirectory (Split-Path $Exe) -WindowStyle Hidden -PassThru `
            -ArgumentList @('--script', ('"' + $scriptPath + '"'), '--result-format', 'jsonl', '--result-file', ('"' + $resultPath + '"')) `
            -RedirectStandardOutput "$Work/$Name.out" -RedirectStandardError "$Work/$Name.err"
        if (-not $process.WaitForExit(900000)) { $process.Kill(); throw "${Name}: 에디터가 제때 끝나지 않았다" }
    }
    finally {
        Remove-Item Env:CREATOR_EDITOR_WORKSPACE_DIR -ErrorAction SilentlyContinue
        Remove-Item Env:CREATOR_EDITOR_LEGACY_INI -ErrorAction SilentlyContinue
    }
    $rows = @(Get-Content -LiteralPath $resultPath | Where-Object { $_.Trim() } | ForEach-Object { $_ | ConvertFrom-Json })
    if ($rows.Count -ne $Lines.Count) {
        throw "${Name}: 결과 행 $($rows.Count) 이 줄 수 $($Lines.Count) 와 다르다(exit $($process.ExitCode)) — '$($Lines[$rows.Count])' 에서 멈췄다"
    }
    for ($i = 0; $i -lt $rows.Count; $i++) {
        if ($rows[$i].status -ne 'succeeded') { throw "${Name}: '$($Lines[$i])' 가 $($rows[$i].status): $($rows[$i].message)" }
    }
    $rows
}

function New-Prologue {
    @('window.resize 2400 1600', 'wait 60', 'scene.new InspectorEditRoundtrip', 'wait 30',
        "object.create $Target", "object.create $Other", 'wait 20',
        "object.transform $Target $($Position -join ' ')", 'wait 10',
        "scene.select $Target", 'editor.window ###Editor.Inspector focus', 'wait 30',
        "editor.inspector width $WideLogical", 'wait 16')
}

$settingsBytes = [IO.File]::ReadAllBytes($settingsPath)
$settingsText = [IO.File]::ReadAllText($settingsPath)
$scalePattern = '(?m)(^imguiScale: )[^\r\n]+'
if ([regex]::Matches($settingsText, $scalePattern).Count -ne 1) { throw 'imguiScale 설정이 하나가 아니다' }
[IO.File]::WriteAllText($settingsPath,
    [regex]::Replace($settingsText, $scalePattern, "`${1}$Scale"), [Text.UTF8Encoding]::new($false))

$lines = [Collections.Generic.List[string]]::new()
$rows = $null
try {
    # ── 실행 ①: 좌표를 읽는다 ───────────────────────────────────────────────
    $probeRows = Invoke-Editor 'probe' (@(New-Prologue) + @('editor.inspector', 'quit')) (Join-Path $Work 'ws-probe')
    $probe = $probeRows[$probeRows.Count - 2].data
    $transform = @($probe.bodies | Where-Object { $_.type -eq 'Transform' -and $_.open })
    if ($transform.Count -ne 1) { throw "Transform 본문이 한 번 그려지지 않았다(찾은 본문 $($transform.Count))" }
    $field = $transform[0]
    Assert ([double]$field.firstFieldW -gt 0) 'Transform 의 첫 값 칸 사각형이 비었다 — 장부가 칸을 못 봤다'
    Assert ([double]$probe.visibleWidth -ge [double]$probe.contentWidth - 1) `
        ("요청 폭 {0:N0} px 중 {1:N0} px 만 보인다 — 잘린 자리를 눌러도 아무 일이 없다. 자극 폭을 줄여라" -f `
         $probe.contentWidth, $probe.visibleWidth)
    if ([double]$field.firstFieldW -le 0) { throw '좌표를 읽지 못했다' }
    # 칸의 왼쪽 1/4. 가운데는 칸이 좁을 때 이웃과 붙는다.
    $pointX = [int][Math]::Round([double]$field.firstFieldX + [double]$field.firstFieldW * 0.25)
    $pointY = [int][Math]::Round([double]$field.firstFieldY + [double]$field.firstFieldH * 0.5)
    Write-Host ("  첫 값 칸 x $([int]$field.firstFieldX) y $([int]$field.firstFieldY) 폭 $([int]$field.firstFieldW)" +
                " · 누를 자리 ($pointX, $pointY) · 보이는 폭 $([int]$probe.visibleWidth) px")

    # ── 실행 ②: 누르고, 끌고, 왕복한다 ─────────────────────────────────────
    foreach ($line in @(New-Prologue)) { $lines.Add($line) }
    $lines.Add('editor.inspector'); $lines.Add("object.describe $Target")          # [기준넓음]
    $lines.Add("editor.inspector width $NarrowLogical"); $lines.Add('wait 16')
    $lines.Add('editor.inspector'); $lines.Add("object.describe $Target")          # [기준좁음]
    $lines.Add("editor.inspector width $WideLogical"); $lines.Add('wait 16')
    $lines.Add('editor.inspector'); $lines.Add("object.describe $Target")          # [되돌림]

    # ① 편집 중 폭 변경 — 배치 전환은 보류되고, 손을 떼면 풀린다.
    $lines.Add("editor.nav pointer $pointX $pointY"); $lines.Add('wait 10')
    $lines.Add('editor.nav press'); $lines.Add('wait 10')
    $lines.Add('editor.inspector'); $lines.Add("object.describe $Target")          # [누름]
    $lines.Add("editor.inspector width $NarrowLogical"); $lines.Add('wait 16')
    $lines.Add('editor.inspector'); $lines.Add("object.describe $Target")          # [보류]
    $lines.Add('editor.nav release'); $lines.Add('wait 16')
    $lines.Add('editor.inspector'); $lines.Add("object.describe $Target")          # [해제]
    # 포인터를 치우고 시간을 준다 — 같은 자리를 잇달아 누르면 두 번 클릭이 된다.
    $lines.Add('editor.nav pointer 5 5'); $lines.Add('wait 40')
    $lines.Add("editor.inspector width $WideLogical"); $lines.Add('wait 16')

    # ② 끌어서 값을 바꾸고 되돌린다. 주입의 첫 적용은 이동량이 0 이라 걸음을 나눈다.
    $dragX = $pointX + 12
    $lines.Add("editor.nav pointer $dragX $pointY"); $lines.Add('wait 12')
    $lines.Add('editor.nav press'); $lines.Add('wait 12')
    foreach ($step in @(20, 40, 60)) {
        $lines.Add("editor.nav pointer $($dragX + $step) $pointY"); $lines.Add('wait 12')
    }
    $lines.Add('editor.nav release'); $lines.Add('wait 16')
    $lines.Add('editor.nav pointer 5 5'); $lines.Add('wait 16')
    $lines.Add('editor.inspector'); $lines.Add("object.describe $Target")          # [끈뒤]
    $lines.Add('undo'); $lines.Add('wait 16')
    $lines.Add('editor.inspector'); $lines.Add("object.describe $Target")          # [undo]
    $lines.Add('redo'); $lines.Add('wait 16')
    $lines.Add('editor.inspector'); $lines.Add("object.describe $Target")          # [redo]
    $lines.Add('undo'); $lines.Add('wait 16')
    $lines.Add('editor.inspector'); $lines.Add("object.describe $Target")          # [undo2]

    # ③ 선택 변경 · 접기/펼치기 · 엔티티 활성 왕복
    $lines.Add("scene.select $Other"); $lines.Add('wait 16'); $lines.Add('editor.inspector')
    $lines.Add("scene.select $Target"); $lines.Add('wait 16')
    $lines.Add('editor.inspector'); $lines.Add("object.describe $Target")          # [선택왕복]
    $lines.Add('editor.inspector expand on'); $lines.Add('wait 16'); $lines.Add('editor.inspector')
    $lines.Add('editor.inspector expand off'); $lines.Add('wait 16')
    $lines.Add('editor.inspector'); $lines.Add("object.describe $Target")          # [펼침왕복]
    $lines.Add("object.enable $Target off"); $lines.Add('wait 16'); $lines.Add('editor.inspector')
    $lines.Add("object.enable $Target on"); $lines.Add('wait 16')
    $lines.Add('editor.inspector'); $lines.Add("object.describe $Target")          # [활성왕복]

    # ④ 씬 저장/재로드와 프리팹
    $lines.Add("scene.save $ScenePath"); $lines.Add('wait 20')
    $lines.Add("prefab.create $Target $Prefab"); $lines.Add('wait 20')
    $lines.Add("scene.load $ScenePath"); $lines.Add('wait 60')
    $lines.Add("scene.select $Target"); $lines.Add('wait 20')
    $lines.Add('editor.inspector'); $lines.Add("object.describe $Target")          # [재로드]
    $lines.Add("prefab.instantiate $Prefab Edit_Instance"); $lines.Add('wait 30')
    $lines.Add('scene.select Edit_Instance'); $lines.Add('wait 20')
    $lines.Add('editor.inspector'); $lines.Add('object.describe Edit_Instance')    # [프리팹]
    $lines.Add('editor.inspector width off'); $lines.Add('wait 6')
    $lines.Add('quit')

    $rows = Invoke-Editor 'edit' $lines (Join-Path $Work 'ws-edit')
}
finally {
    [IO.File]::WriteAllBytes($settingsPath, $settingsBytes)
}

# (인스펙터 · describe) 쌍이 한 점이다.
$points = [Collections.Generic.List[object]]::new()
for ($i = 0; $i -lt $lines.Count; $i++) {
    if ($lines[$i] -ne 'editor.inspector') { continue }
    $describe = $null
    if ($i + 1 -lt $lines.Count -and $lines[$i + 1] -like 'object.describe *') { $describe = $rows[$i + 1].data }
    $points.Add([pscustomobject]@{ Index = $i; Data = $rows[$i].data; Describe = $describe })
}
function Get-Body([object]$Point, [string]$Type) {
    @($Point.Data.bodies | Where-Object { $_.type -eq $Type -and $_.open })
}
function Get-Digest([object]$Point, [string]$Type) {
    $body = @(Get-Body $Point $Type)
    if ($body.Count -ne 1) { return $null }
    [int64]$body[0].fieldDigest
}
function Get-Height([object]$Point, [string]$Type) {
    $body = @(Get-Body $Point $Type)
    if ($body.Count -ne 1) { return $null }
    [double]$body[0].height
}
function Get-Position([object]$Point) {
    if ($null -eq $Point.Describe) { return $null }
    @($Point.Describe.position | ForEach-Object { [double]$_ })
}

$order = @('기준넓음', '기준좁음', '되돌림', '누름', '보류', '해제', '끈뒤', 'undo', 'redo', 'undo2',
           '다른대상', '선택왕복', '펼침', '펼침왕복', '엔티티끔', '활성왕복', '재로드', '프리팹')
Assert ($points.Count -eq $order.Count) "읽은 점이 $($points.Count) 개다 — $($order.Count) 개를 예상했다"
if ($points.Count -ne $order.Count) { throw '점 수가 달라 판정할 수 없다' }
$named = [ordered]@{}
for ($i = 0; $i -lt $order.Count; $i++) { $named[$order[$i]] = $points[$i] }

$base = $named['기준넓음']
$baseDigest = Get-Digest $base 'Transform'
Assert ($null -ne $baseDigest -and $baseDigest -ne 0) '기준 상태에서 Transform 의 값 칸 신원을 못 읽었다'
$basePosition = Get-Position $base
Assert ($null -ne $basePosition -and (Near $basePosition[0] $Position[0] 0.001)) `
    "기준 위치 $($basePosition -join ',') 가 지정한 $($Position -join ',') 와 다르다"
$wideHeight = Get-Height $base 'Transform'
$narrowHeight = Get-Height $named['기준좁음'] 'Transform'

# 두 폭이 실제로 전환을 사이에 두는가. 아니면 아래 보류 단정이 빈 자극이다.
Assert ($null -ne $narrowHeight -and [Math]::Abs($narrowHeight - $wideHeight) -gt 1.0) `
    "폭 $WideLogical(높이 $wideHeight) 과 $NarrowLogical(높이 $narrowHeight) 사이에 전환이 없다 — 경계를 사이에 둔 두 폭을 골라라"
Assert (Near (Get-Height $named['되돌림'] 'Transform') $wideHeight 1.0) `
    '폭을 되돌렸는데 배치가 돌아오지 않았다'

# ── ① 편집 중 폭 변경 ──────────────────────────────────────────────────────
$pressed = $named['누름']
$activeId = [int64]$pressed.Data.activeId
Assert ($activeId -ne 0) '칸을 눌렀는데 편집 중인 항목이 없다 — 포인터 주입이 칸에 닿지 않았다'
$held = $named['보류']
Assert ([int64]$held.Data.activeId -eq $activeId) `
    "폭을 바꾸자 편집 중인 항목이 $($held.Data.activeId) 로 바뀌었다(누를 때 $activeId)"
Assert ((Get-Digest $held 'Transform') -eq $baseDigest) '편집 중 폭을 바꾸자 값 칸 신원이 달라졌다'
Assert (Near (Get-Height $held 'Transform') $wideHeight 1.0) `
    ("편집 중인데 배치가 {0} → {1} 로 전환됐다 — 편집 중 전환 보류가 풀렸다" -f $wideHeight, (Get-Height $held 'Transform'))
$heldPosition = Get-Position $held
Assert ($null -ne $heldPosition -and (Near $heldPosition[0] $basePosition[0] 0.0001)) `
    "끌지 않았는데 폭 변경만으로 값이 $($heldPosition[0]) 로 움직였다"
$releasedHeight = Get-Height $named['해제'] 'Transform'
Assert (Near $releasedHeight $narrowHeight 1.0) `
    ("손을 뗐는데 배치가 {0} 그대로다(좁은 배치는 {1}) — 보류가 풀리지 않았다" -f $releasedHeight, $narrowHeight)
Assert ((Get-Digest $named['해제'] 'Transform') -eq $baseDigest) '전환 뒤 값 칸 신원이 달라졌다 — 전환이 ID 를 바꿨다'

# ── ② 끌어 바꾸고 되돌린다 ────────────────────────────────────────────────
$dragged = Get-Position $named['끈뒤']
Assert ($null -ne $dragged -and -not (Near $dragged[0] $basePosition[0] 0.0001)) `
    "끌었는데 값이 $($dragged[0]) 그대로다 — 자극이 칸에 닿지 않았으면 아래 되돌리기 단정도 빈 집합이다"
$undone = Get-Position $named['undo']
Assert ($null -ne $undone -and (Near $undone[0] $basePosition[0] 0.001)) `
    "undo 뒤 값이 $($undone[0]) 로 기준 $($basePosition[0]) 과 다르다"
$redone = Get-Position $named['redo']
Assert ($null -ne $redone -and (Near $redone[0] $dragged[0] 0.001)) `
    "redo 뒤 값이 $($redone[0]) 로 끈 값 $($dragged[0]) 과 다르다"
$undone2 = Get-Position $named['undo2']
Assert ($null -ne $undone2 -and (Near $undone2[0] $basePosition[0] 0.001)) '다시 undo 한 값이 기준과 다르다'
foreach ($name in @('끈뒤', 'undo', 'redo', 'undo2')) {
    Assert ((Get-Digest $named[$name] 'Transform') -eq $baseDigest) "${name}: 값 칸 신원이 기준과 다르다"
}

# ── ③ 왕복 ────────────────────────────────────────────────────────────────
Assert ($named['다른대상'].Data.entity -eq $Other) "선택을 $Other 로 바꿨는데 인스펙터가 '$($named['다른대상'].Data.entity)' 를 그렸다"
foreach ($name in @('선택왕복', '펼침왕복', '활성왕복')) {
    $point = $named[$name]
    Assert ($point.Data.entity -eq $Target) "${name}: 인스펙터가 '$($point.Data.entity)' 를 그렸다"
    Assert ((Get-Digest $point 'Transform') -eq $baseDigest) "${name}: 값 칸 신원이 기준 $baseDigest 과 다르다"
    $position = Get-Position $point
    Assert ($null -ne $position -and (Near $position[0] $basePosition[0] 0.001) -and
            (Near $position[1] $basePosition[1] 0.001) -and (Near $position[2] $basePosition[2] 0.001)) `
        "${name}: 위치 $($position -join ',') 가 기준 $($basePosition -join ',') 과 다르다"
}
$collapsedLines = (@($named['선택왕복'].Data.bodies | Where-Object { $_.open }) | Measure-Object propertyLines -Sum).Sum
$expandedLines = (@($named['펼침'].Data.bodies | Where-Object { $_.open }) | Measure-Object propertyLines -Sum).Sum
Assert ($expandedLines -ge $collapsedLines) "펼친 줄 $expandedLines 이 접힌 줄 $collapsedLines 보다 적다"
Assert ($named['엔티티끔'].Data.entity -eq $Target) '엔티티를 껐더니 인스펙터가 대상을 놓쳤다'

# ── ④ 저장·재로드와 프리팹 ────────────────────────────────────────────────
foreach ($name in @('재로드', '프리팹')) {
    $position = Get-Position $named[$name]
    Assert ($null -ne $position -and (Near $position[0] $basePosition[0] 0.001) -and
            (Near $position[1] $basePosition[1] 0.001) -and (Near $position[2] $basePosition[2] 0.001)) `
        "${name}: 위치 $($position -join ',') 가 기준 $($basePosition -join ',') 과 다르다"
    # 새로 생긴 인스턴스라 신원 요약은 달라진다. 값 칸 **수**는 같아야 한다.
    $body = @(Get-Body $named[$name] 'Transform')
    $baseBody = @(Get-Body $base 'Transform')
    Assert ($body.Count -eq 1 -and [int]$body[0].fields -eq [int]$baseBody[0].fields -and
            [int]$body[0].axisFields -eq [int]$baseBody[0].axisFields) `
        "${name}: Transform 의 값 칸 수가 기준과 다르다"
}
Assert ($named['재로드'].Data.entity -eq $Target) '씬 재로드 뒤 인스펙터가 대상을 그리지 않았다'
Assert ($named['프리팹'].Data.entity -eq 'Edit_Instance') '프리팹 인스턴스를 인스펙터가 그리지 않았다'

Write-Host ''
Write-Host ("  배치 높이 — 넓음 {0:N0} · 좁음 {1:N0} · 편집 중 {2:N0}(보류) · 손 뗀 뒤 {3:N0}" -f `
    $wideHeight, $narrowHeight, (Get-Height $held 'Transform'), $releasedHeight)
Write-Host ("  값 — 기준 {0} · 끈 뒤 {1} · undo {2} · redo {3}" -f `
    ($basePosition -join ','), $dragged[0], $undone[0], $redone[0])
Write-Host ("  값 칸 신원 {0} — 폭 변경·전환·선택/펼침/활성 왕복에서 그대로" -f $baseDigest)
Write-Host ''
if ($failures.Count -gt 0) { throw "인스펙터 편집 왕복 검사 실패 $($failures.Count) 건" }
Write-Host "인스펙터 편집 왕복 검사: 단정 $script:checks · 점 $($points.Count) · PASS"
