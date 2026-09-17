[CmdletBinding()]
param(
    [string]$Exe = (Join-Path $PSScriptRoot '../../Bin/x64-Release/Editor/CreatorEditor.exe'),
    [string]$Work = (Join-Path $env:TEMP 'creator-inspector-drawer-layout')
)
# PHASE 21 W2-I4 — 인스펙터 전용 드로어의 공통 배치 이관.
#
# 계획서 §W2-I 의 판정: *"가용 content 폭 240/320/480/720 logical px … 가로 잘림·겹침·
# 0-size 입력칸은 실패"*, *"미이관 표면 0"*. 착수 때 둘 다 잴 수 없었다 — 인스펙터
# 도크는 창을 좁혀도 절대 폭을 지키고, 이관은 소스 셈으로만 읽혔다(파일 단위 셈이
# 12 를 0 으로 읽은 적이 있다). `editor.inspector` 가 둘을 연다:
#   `width <논리 px>` 로 본문을 그 폭 안에서 그리고,
#   본문마다 공통 배치 줄(`begin_property_line`) 수와 오른쪽 넘침 px 를 낸다.
#
# 단정은 **이관을 끝낸 드로어**($Migrated)에만 건다: 네 폭 모두 공통 배치 줄 > 0,
# 넘침 0. 아직 옮기지 않은 드로어는 판정하지 않고 수만 낸다 — 이관이 끝날 때마다
# 목록으로 옮긴다. 목록 밖 드로어가 넘침 0 · 줄 > 0 이 되면 알린다(목록에 넣어라).
#
# 자 자신도 단정한다: 요청 폭 × 배율 = 받은 폭, 폭을 줄이면 넘침이 실제로 생기는
# 드로어가 있다(없으면 자극이 안 된 것), 상단 공간 드로어는 줄 > 0.
#
# 소스 축: 이관한 드로어 함수 본문에 **배율을 받지 않는 고정 폭**이 남으면 실패다 —
# `SetNextItemWidth(150)`, `ImVec2(150, 20)` 같은 숫자 리터럴. 런타임 축은 배율 2.25 인
# 기계에서 "넘치지 않았다" 만 보므로 배율 1 에서 넘칠 고정 폭을 못 잡는다(착수 때
# SpriteRenderer 가 줄 5 · 넘침 0 이면서 `ImVec2(150, 20)` 을 들고 있었다).
#
# 사용법:
#   pwsh Tools/regression/verify-inspector-drawer-layout.ps1 -Exe Bin/x64-Release/Editor/CreatorEditor.exe
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$Exe = [IO.Path]::GetFullPath($Exe)
$Work = [IO.Path]::GetFullPath($Work)
if (-not (Test-Path -LiteralPath $Exe)) { throw "Editor executable not found: $Exe" }
if (Get-Process CreatorEditor -ErrorAction SilentlyContinue) {
    throw '이미 떠 있는 에디터를 닫아라 — 이 검사는 남의 에디터를 멈추지 않는다.'
}
Remove-Item -LiteralPath $Work -Recurse -Force -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force -Path $Work | Out-Null

# 이관을 끝낸 드로어. 옮길 때마다 여기에 더한다.
$Migrated = @('GameObjectBaseInfo', 'Transform',
    'SoundComponent', 'DecalComponent', 'ImageComponent', 'SpriteRenderer', 'Canvas', 'VolumeComponent',
    'BehaviorTreeComponent', 'StateMachineComponent', 'Animator', 'MeshRenderer', 'PlayerInputComponent', 'TerrainComponent')
# 이관한 드로어의 그리는 함수 — 소스 축이 본문을 잘라 읽는다. 첫 칸이 파일(저장소 기준), 나머지가
# 함수 이름이다. 드로어가 부르는 같은 파일의 조각 함수도 적는다(고정 폭이 그리로 숨을 수 있다).
$DrawerSources = @{
    SoundComponent        = @('Editor/EngineGUIWindow/InspectorWindow.cpp', 'InspectorWindow::ImGuiDrawHelperSoundComponent', 'DrawNamedPicker')
    DecalComponent        = @('Editor/EngineGUIWindow/InspectorWindow.cpp', 'InspectorWindow::ImGuiDrawHelperDecal', 'DrawAssetSlot')
    ImageComponent        = @('Editor/EngineGUIWindow/InspectorWindow.cpp', 'InspectorWindow::ImGuiDrawHelperImageComponent', 'NameButton')
    SpriteRenderer        = @('Editor/EngineGUIWindow/InspectorWindow.cpp', 'InspectorWindow::ImGuiDrawHelperSpriteRenderer')
    Animator              = @('Editor/EngineGUIWindow/ImGuiDrawHelperAnimator.cpp', 'ImGuiDrawHelperAnimator')
    MeshRenderer          = @('Editor/EngineGUIWindow/ImGuiDrawHelperMeshRenderer.cpp', 'ImGuiDrawHelperMeshRenderer', 'DrawMaterialTextureSlot', 'ReadOnlyLine')
    PlayerInputComponent  = @('Editor/EngineGUIWindow/ImGuiDrawHelperPlayerInput.cpp', 'ImGuiDrawHelperPlayerInput')
    TerrainComponent      = @('Editor/EngineGUIWindow/ImGuiDrawHelperTerrainComponent.cpp', 'ImGuiDrawHelperTerrainComponent', 'DrawBrushMasks', 'ButtonRow')
    Canvas                = @('Editor/EngineGUIWindow/InspectorWindow.cpp', 'InspectorWindow::ImGuiDrawHelperCanvas')
    VolumeComponent       = @('Editor/EngineGUIWindow/InspectorWindow.cpp', 'InspectorWindow::ImGuiDrawHelperVolume')
    BehaviorTreeComponent = @('Editor/EngineGUIWindow/InspectorWindow.cpp', 'InspectorWindow::ImGuiDrawHelperBT')
    StateMachineComponent = @('Editor/EngineGUIWindow/InspectorWindow.cpp', 'InspectorWindow::ImGuiDrawHelperFSM')
}
# 전용 드로어를 가진 컴포넌트 열둘 — 계획서 §W2-I 재기준선 표.
$Drawers = @('SoundComponent', 'DecalComponent', 'ImageComponent', 'SpriteRenderer', 'Canvas', 'VolumeComponent',
    'BehaviorTreeComponent', 'StateMachineComponent', 'Animator', 'MeshRenderer', 'PlayerInputComponent', 'TerrainComponent')
$Widths = @(240, 320, 480, 720)

$script:checks = 0
$failures = [Collections.Generic.List[string]]::new()
function Assert([bool]$Condition, [string]$Message) {
    $script:checks++
    if (-not $Condition) { $failures.Add($Message); Write-Host "  FAIL $Message" -ForegroundColor Red }
}

# ── 소스 축: 이관한 드로어 본문의 고정 폭 ─────────────────────────────────────
# 함수 머리부터 중괄호 짝이 닫힐 때까지를 본문으로 읽는다. 주석은 지우고 센다.
$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
function Get-FunctionBody([string]$File, [string]$Name) {
    $text = [IO.File]::ReadAllText((Join-Path $repoRoot $File))
    $head = [regex]::Match($text, '(?m)^[ \t]*\w[\w:<>\*& ]*[ *&]' + [regex]::Escape($Name) + '\s*\(')
    if (-not $head.Success) { return $null }
    $open = $text.IndexOf('{', $head.Index)
    $depth = 0
    for ($i = $open; $i -lt $text.Length; $i++) {
        if ($text[$i] -eq '{') { $depth++ }
        elseif ($text[$i] -eq '}') { $depth--; if ($depth -eq 0) { return $text.Substring($open, $i - $open + 1) } }
    }
    return $null
}
$fixedWidth = '(SetNextItemWidth|PushItemWidth)\(\s*\d[\d.]*f?\s*\)|ImVec2\(\s*[1-9][\d.]*f?\s*,|ImVec2\([^()]*,\s*[1-9][\d.]*f?\s*\)'
foreach ($name in $Migrated) {
    if (-not $DrawerSources.ContainsKey($name)) { continue }
    $file = $DrawerSources[$name][0]
    foreach ($function in @($DrawerSources[$name] | Select-Object -Skip 1)) {
        $body = Get-FunctionBody $file $function
        Assert ($null -ne $body) "${name}: 그리는 함수 '$function' 를 '$file' 에서 못 찾았다"
        if ($null -eq $body) { continue }
        $code = [regex]::Replace($body, '//[^\r\n]*', '')
        $hits = @([regex]::Matches($code, $fixedWidth) | ForEach-Object { $_.Value })
        Assert ($hits.Count -eq 0) "${name}: '$function' 에 배율을 받지 않는 고정 폭이 남았다 — $($hits -join ' · ')"
    }
}

$lines = @('window.resize 2400 1600', 'wait 60', 'scene.new InspectorDrawerLayout', 'wait 30',
    'editor.window ###Editor.Inspector focus', 'wait 10')
foreach ($type in $Drawers) { $lines += "object.create Drawer_$type"; $lines += "component.add Drawer_$type $type" }
$lines += 'wait 30'
foreach ($type in $Drawers) {
    $lines += "scene.select Drawer_$type"
    foreach ($width in $Widths) { $lines += "editor.inspector width $width"; $lines += 'wait 6'; $lines += 'editor.inspector' }
}
$lines += 'editor.inspector width off'; $lines += 'wait 6'; $lines += 'editor.inspector'; $lines += 'quit'

$scriptPath = Join-Path $Work 'run.txt'; $resultPath = Join-Path $Work 'run.jsonl'
Set-Content -LiteralPath $scriptPath -Encoding UTF8 -Value $lines
$workspace = Join-Path $Work 'ws'; New-Item -ItemType Directory -Force -Path $workspace | Out-Null
$env:CREATOR_EDITOR_WORKSPACE_DIR = $workspace
$env:CREATOR_EDITOR_LEGACY_INI = Join-Path $workspace 'none.ini'
try {
    $process = Start-Process -FilePath $Exe -WorkingDirectory (Split-Path $Exe) -WindowStyle Hidden -PassThru `
        -ArgumentList @('--script', ('"' + $scriptPath + '"'), '--result-format', 'jsonl', '--result-file', ('"' + $resultPath + '"')) `
        -RedirectStandardOutput "$Work/run.out" -RedirectStandardError "$Work/run.err"
    if (-not $process.WaitForExit(900000)) { $process.Kill(); throw '에디터가 제때 끝나지 않았다' }
}
finally {
    Remove-Item Env:CREATOR_EDITOR_WORKSPACE_DIR -ErrorAction SilentlyContinue
    Remove-Item Env:CREATOR_EDITOR_LEGACY_INI -ErrorAction SilentlyContinue
}
$rows = @(Get-Content -LiteralPath $resultPath | Where-Object { $_.Trim() } | ForEach-Object { $_ | ConvertFrom-Json })
# 컴포넌트를 붙이다 죽으면 행이 모자란다 — DecalComponent 가 빈 텍스처 이름으로 폴더를 열어 죽던 자리.
if ($rows.Count -ne $lines.Count) {
    throw "결과 행 $($rows.Count) 이 줄 수 $($lines.Count) 와 다르다(exit $($process.ExitCode)) — '$($lines[$rows.Count])' 에서 멈췄다. run.out 의 스택을 봐라"
}
for ($i = 0; $i -lt $rows.Count; $i++) {
    if ($rows[$i].status -ne 'succeeded') { throw "'$($lines[$i])' 가 $($rows[$i].status): $($rows[$i].message)" }
}

$samples = [Collections.Generic.List[object]]::new()
$selected = ''
for ($i = 0; $i -lt $rows.Count; $i++) {
    if ($lines[$i] -like 'scene.select *') { $selected = $lines[$i].Substring('scene.select Drawer_'.Length) }
    if ($lines[$i] -ne 'editor.inspector' -or -not $selected) { continue }
    $width = [int]($lines[$i - 2] -replace 'editor.inspector width ', '' -replace 'off', '0')
    if ($width -eq 0) { continue }
    $samples.Add([pscustomobject]@{ Drawer = $selected; Width = $width; Data = $rows[$i].data })
}
Assert ($samples.Count -eq $Drawers.Count * $Widths.Count) "표본 $($samples.Count) 이 $($Drawers.Count)×$($Widths.Count) 가 아니다"

# ── 자 ──────────────────────────────────────────────────────────────────────
foreach ($sample in $samples) {
    $d = $sample.Data
    Assert ($d.entity -eq "Drawer_$($sample.Drawer)") "$($sample.Drawer)@$($sample.Width): 인스펙터가 '$($d.entity)' 를 그렸다 — 선택이 닿지 않았다"
    Assert ([Math]::Abs($d.contentWidth - $sample.Width * $d.uiScale) -le 1) "$($sample.Drawer)@$($sample.Width): 받은 폭 $($d.contentWidth) 이 요청 × 배율 $($sample.Width * $d.uiScale) 과 다르다"
    Assert (@($d.bodies | Where-Object { $_.type -eq $sample.Drawer -and $_.open }).Count -eq 1) "$($sample.Drawer)@$($sample.Width): 그 컴포넌트 본문이 열린 채 한 번 그려지지 않았다"
}
$off = $rows[-2].data
Assert ($off.requestedWidth -eq 0 -and $off.contentWidth -gt 0) '폭을 끈 뒤 창 폭으로 돌아오지 않았다'
$narrowOverflow = @($samples | Where-Object { $_.Width -eq 240 } | ForEach-Object { $_.Data.bodies } | Where-Object { $_.overflow -gt 0.5 })
Assert ($narrowOverflow.Count -gt 0 -or $Migrated.Count -ge $Drawers.Count + 2) '240 에서 넘치는 본문이 하나도 없다 — 폭이 좁아지지 않았거나 넘침을 못 잰다'

# ── 이관 끝난 드로어 ────────────────────────────────────────────────────────
$table = [Collections.Generic.List[string]]::new()
foreach ($name in @('GameObjectBaseInfo', 'Transform') + $Drawers) {
    $bodies = @($samples | ForEach-Object { $s = $_; $s.Data.bodies | Where-Object { $_.type -eq $name -and $_.open } |
        ForEach-Object { [pscustomobject]@{ Width = $s.Width; Lines = [int]$_.propertyLines; Overflow = [double]$_.overflow } } })
    if ($bodies.Count -eq 0) { Assert $false "$name 본문을 한 번도 못 봤다"; continue }
    $worst = ($bodies | Measure-Object Overflow -Maximum).Maximum
    $fewest = ($bodies | Measure-Object Lines -Minimum).Minimum
    $byWidth = ($Widths | ForEach-Object { $w = $_; $o = ($bodies | Where-Object Width -eq $w | Measure-Object Overflow -Maximum).Maximum; "{0}:{1:0.#}" -f $w, $o }) -join ' '
    $state = if ($name -in $Migrated) { '이관' } else { '미이관' }
    $table.Add(("  {0,-24} {1,-4} 줄 {2,-3} 넘침 {3}" -f $name, $state, $fewest, $byWidth))
    if ($name -in $Migrated) {
        Assert ($fewest -gt 0) "${name}: 이관한 드로어인데 공통 배치 줄이 0 인 폭이 있다"
        Assert ($worst -le 0.5) "${name}: 이관한 드로어인데 넘침 $worst px — 고정 폭이 남았다"
    }
    elseif ($fewest -gt 0 -and $worst -le 0.5) {
        Write-Host "  알림 ${name}: 네 폭 모두 줄 > 0 · 넘침 0 — 이관을 끝냈다면 `$Migrated 에 넣어라" -ForegroundColor Yellow
    }
}

Write-Host ''
$table | ForEach-Object { Write-Host $_ }
Write-Host ''
if ($failures.Count -gt 0) { throw "인스펙터 드로어 배치 검사 실패 $($failures.Count) 건" }
Write-Host "인스펙터 드로어 배치 검사: 단정 $script:checks · 이관 $($Migrated.Count)/$($Drawers.Count + 2) · PASS"
