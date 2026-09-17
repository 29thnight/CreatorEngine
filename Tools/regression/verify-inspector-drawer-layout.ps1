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
# Import Settings: 자산을 고르면 인스펙터가 `.meta` 를 `DrawYamlNodeEditor` 로 그린다. 추적되는
# 모델 메타 하나(긴 해시 문자열 · 맵 · 맵을 담은 배열)를 `editor.browser select` 로 고르고,
# 접힌 채 한 번, `editor.inspector expand on` 으로 펼친 채 네 폭을 잰다. 펼친 줄 수가 접힌 줄
# 수보다 커야 안쪽이 자극된 것이다.
#
# RectTransform(W2-I2): 컴포넌트를 붙여서는 생기지 않는다 — 엔티티 유형이 정한다(UI 는
# RectTransform 만, 캔버스는 둘 다). `object.create <이름> UI` 로 만든 엔티티를 같은 네 폭에서 잰다.
#
# 리플렉션 모양(W2-I3): 일반 경로로 그려지는 컴포넌트 중 중첩 구조체·배열·맵을 가진 것이 사실상
# 없어(컨테이너 필드를 가진 셋은 전용 드로어이거나 숨긴 필드) 실제 데이터로는 분기 대부분이
# 자극되지 않는다. `editor.inspector fixture on` 이 합성 값(`InspectorLayoutFixture.h`)을
# `ReflectionFixture` 본문으로 그린다. 줄 수를 **타입 정의에서 센 값과 같게** 단정한다 — 원소
# 하나라도 공통 줄을 지나지 않으면 수가 모자란다. 접힌 채 13, 펼친 채 55.
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
    'BehaviorTreeComponent', 'StateMachineComponent', 'Animator', 'MeshRenderer', 'PlayerInputComponent', 'TerrainComponent',
    'RectTransformComponent')
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
    ImportSettings        = @('Editor/EngineGUIWindow/DrawYamlNodeEditor.cpp', 'DrawYamlNodeEditor', 'DrawEntry', 'DrawScalar', 'BeginContainer')
    Canvas                = @('Editor/EngineGUIWindow/InspectorWindow.cpp', 'InspectorWindow::ImGuiDrawHelperCanvas')
    RectTransformComponent = @('Editor/EngineGUIWindow/ImGuiDrawHelperRectTransformComponent.cpp', 'ImGuiDrawHelperRectTransformComponent', 'DrawVec2Row', 'DrawAnchorPresetPopup', 'DrawAnchorIconButton')
    VolumeComponent       = @('Editor/EngineGUIWindow/InspectorWindow.cpp', 'InspectorWindow::ImGuiDrawHelperVolume')
    BehaviorTreeComponent = @('Editor/EngineGUIWindow/InspectorWindow.cpp', 'InspectorWindow::ImGuiDrawHelperBT')
    StateMachineComponent = @('Editor/EngineGUIWindow/InspectorWindow.cpp', 'InspectorWindow::ImGuiDrawHelperFSM')
}
# 전용 드로어를 가진 컴포넌트 열둘 — 계획서 §W2-I 재기준선 표.
$Drawers = @('SoundComponent', 'DecalComponent', 'ImageComponent', 'SpriteRenderer', 'Canvas', 'VolumeComponent',
    'BehaviorTreeComponent', 'StateMachineComponent', 'Animator', 'MeshRenderer', 'PlayerInputComponent', 'TerrainComponent')
# 유형이 붙이는 공간 컴포넌트. 값은 `object.create` 의 유형이다.
$SpatialDrawers = [ordered]@{ RectTransformComponent = 'UI' }
$Targets = @($Drawers) + @($SpatialDrawers.Keys)
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
$ImportAsset = 'Animation/Cha_Mon_5.fbx'
foreach ($name in @($Migrated) + 'ImportSettings') {
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
# Import Settings 는 엔티티를 고르기 전에 잰다 — 엔티티 선택이 자산 선택보다 먼저 그려진다.
$lines += "editor.browser go $(Split-Path $ImportAsset -Parent)"; $lines += 'wait 120'
$lines += "editor.browser select $ImportAsset"; $lines += 'wait 120'
# 폭마다 여러 번 읽는다. `wait` 는 게임 스레드 프레임이고 인스펙터는 표시 스레드에서 그려져,
# 같은 기다림이 어떤 때는 인스펙터 한 프레임도 못 채운다(기동 직후·컴포넌트 적재 중). 판정은
# 요청이 반영된 마지막 읽기로 한다 — 끝까지 반영되지 않으면 그 사실이 실패로 남는다.
$Reads = 3
function Add-WidthReads([int]$Width) {
    $script:lines += "editor.inspector width $Width"
    for ($r = 0; $r -lt $Reads; $r++) { $script:lines += 'wait 12'; $script:lines += 'editor.inspector' }
}
Add-WidthReads 720
$lines += 'editor.inspector expand on'
foreach ($width in $Widths) { Add-WidthReads $width }
$lines += 'editor.inspector expand off'
$importEnd = $lines.Count
foreach ($type in $Drawers) { $lines += "object.create Drawer_$type"; $lines += "component.add Drawer_$type $type" }
foreach ($type in $SpatialDrawers.Keys) { $lines += "object.create Drawer_$type $($SpatialDrawers[$type])" }
$lines += 'wait 30'
foreach ($type in $Targets) {
    $lines += "scene.select Drawer_$type"
    foreach ($width in $Widths) { Add-WidthReads $width }
}
# 리플렉션 자극물. 줄 수의 기대값은 `InspectorLayoutFixture.h` 에서 센다:
#   접힘 — 윗단 스칼라·벡터 8 + branch(depth·title·leaf 셋) 5 = 13 (컨테이너 머리는 닫힘)
#   펼침 — 8 + branch 8(samples 원소 셋) + weights 3 · names 2 · points 2 · smallIds 2 · fixed 3 ·
#          tags 3 · scores 2 · leaves 2×3 · branches 2×8 = 55
$FixtureCollapsedLines = 13
$FixtureExpandedLines = 55
$fixtureStart = $lines.Count
$lines += 'object.create Drawer_Fixture'; $lines += 'wait 30'; $lines += 'scene.select Drawer_Fixture'
$lines += 'editor.inspector fixture on'
Add-WidthReads 720
$lines += 'editor.inspector expand on'
foreach ($width in $Widths) { Add-WidthReads $width }
$lines += 'editor.inspector expand off'; $lines += 'editor.inspector fixture off'
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

# 읽기를 (구간 · 대상 · 폭 · 펼침) 으로 묶고, 요청이 반영된 마지막 읽기를 표본으로 고른다.
function Select-Samples([int]$From, [int]$To, [scriptblock]$Settled) {
    $groups = [ordered]@{}
    $target = ''; $width = 0; $expand = $false
    for ($i = $From; $i -lt $To; $i++) {
        $line = $lines[$i]
        if ($line -like 'scene.select *') { $target = $line.Substring('scene.select Drawer_'.Length) }
        elseif ($line -like 'editor.inspector width *') { $width = [int]($line.Substring('editor.inspector width '.Length) -replace 'off', '0') }
        elseif ($line -eq 'editor.inspector expand on') { $expand = $true }
        elseif ($line -eq 'editor.inspector expand off') { $expand = $false }
        elseif ($line -eq 'editor.inspector' -and $width -gt 0) {
            $key = "$target|$width|$expand"
            if (-not $groups.Contains($key)) { $groups[$key] = [Collections.Generic.List[object]]::new() }
            $groups[$key].Add([pscustomobject]@{ Drawer = $target; Width = $width; Expanded = $expand; Data = $rows[$i].data })
        }
    }
    foreach ($group in $groups.Values) {
        $ready = @($group | Where-Object { $_.Data.requestedWidth -eq $_.Width -and (& $Settled $_) })
        if ($ready.Count -gt 0) { $ready[-1] } else { $group[$group.Count - 1] }
    }
}

# ── Import Settings ─────────────────────────────────────────────────────────
$importReads = @(Select-Samples -From 0 -To $importEnd -Settled { param($s) @($s.Data.bodies | Where-Object { $_.type -eq 'ImportSettings' -and $_.open }).Count -eq 1 })
$importSamples = @(foreach ($read in $importReads) {
    $d = $read.Data
    $body = @($d.bodies | Where-Object { $_.type -eq 'ImportSettings' })
    [pscustomobject]@{ Width = $read.Width; Expanded = $read.Expanded
        Found = $body.Count; Open = ($body.Count -eq 1 -and $body[0].open); Lines = $(if ($body.Count) { [int]$body[0].propertyLines } else { 0 })
        Overflow = $(if ($body.Count) { [double]$body[0].overflow } else { 0 }); Content = $d.contentWidth; Scale = $d.uiScale }
})
Assert ($importSamples.Count -eq $Widths.Count + 1) "Import Settings 표본 $($importSamples.Count) 이 $($Widths.Count + 1) 이 아니다"
$collapsed = $importSamples | Where-Object { -not $_.Expanded } | Select-Object -First 1
foreach ($sample in $importSamples) {
    $tag = "ImportSettings@$($sample.Width)$(if ($sample.Expanded) { ' 펼침' } else { ' 접힘' })"
    Assert ($sample.Open) "${tag}: '$ImportAsset' 의 Import Settings 본문이 열린 채 한 번 그려지지 않았다(찾은 본문 $($sample.Found)) — 자산 선택이 닿지 않았다"
    Assert ([Math]::Abs($sample.Content - $sample.Width * $sample.Scale) -le 1) "${tag}: 받은 폭 $($sample.Content) 이 요청 × 배율과 다르다"
    Assert ($sample.Lines -gt 0) "${tag}: 공통 배치 줄이 0 이다"
    Assert ($sample.Overflow -le 0.5) "${tag}: 넘침 $($sample.Overflow) px"
    if ($sample.Expanded -and $collapsed) {
        Assert ($sample.Lines -gt $collapsed.Lines) "${tag}: 펼친 줄 $($sample.Lines) 이 접힌 줄 $($collapsed.Lines) 보다 크지 않다 — 안쪽 맵·배열이 자극되지 않았다"
    }
}

# ── 리플렉션 자극물 ─────────────────────────────────────────────────────────
$fixtureReads = @(Select-Samples -From $fixtureStart -To ($rows.Count) -Settled { param($s)
    $s.Data.entity -eq 'Drawer_Fixture' -and @($s.Data.bodies | Where-Object { $_.type -eq 'ReflectionFixture' -and $_.open }).Count -eq 1 })
Assert ($fixtureReads.Count -eq $Widths.Count + 1) "리플렉션 자극물 표본 $($fixtureReads.Count) 이 $($Widths.Count + 1) 이 아니다"
$fixtureSamples = @(foreach ($read in $fixtureReads) {
    $body = @($read.Data.bodies | Where-Object { $_.type -eq 'ReflectionFixture' })
    [pscustomobject]@{ Width = $read.Width; Expanded = $read.Expanded; Found = $body.Count
        Open = ($body.Count -eq 1 -and $body[0].open); Lines = $(if ($body.Count) { [int]$body[0].propertyLines } else { 0 })
        Overflow = $(if ($body.Count) { [double]$body[0].overflow } else { 0 }) }
})
foreach ($sample in $fixtureSamples) {
    $tag = "ReflectionFixture@$($sample.Width)$(if ($sample.Expanded) { ' 펼침' } else { ' 접힘' })"
    Assert ($sample.Open) "${tag}: 자극물 본문이 열린 채 한 번 그려지지 않았다(찾은 본문 $($sample.Found))"
    $expected = if ($sample.Expanded) { $FixtureExpandedLines } else { $FixtureCollapsedLines }
    Assert ($sample.Lines -eq $expected) "${tag}: 공통 배치 줄 $($sample.Lines) 이 타입 정의에서 센 $expected 와 다르다 — 공통 줄을 지나지 않는 필드·원소가 있다"
    Assert ($sample.Overflow -le 0.5) "${tag}: 넘침 $($sample.Overflow) px"
}

$samples = @(Select-Samples -From $importEnd -To $fixtureStart -Settled { param($s)
    $s.Data.entity -eq "Drawer_$($s.Drawer)" -and @($s.Data.bodies | Where-Object { $_.type -eq $s.Drawer -and $_.open }).Count -eq 1 })
Assert ($samples.Count -eq $Targets.Count * $Widths.Count) "표본 $($samples.Count) 이 $($Targets.Count)×$($Widths.Count) 가 아니다"

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
foreach ($name in @('GameObjectBaseInfo', 'Transform') + $Targets) {
    $bodies = @($samples | ForEach-Object { $s = $_; $s.Data.bodies | Where-Object { $_.type -eq $name -and $_.open } |
        ForEach-Object { [pscustomobject]@{ Width = $s.Width; Lines = [int]$_.propertyLines; Overflow = [double]$_.overflow } } })
    if ($bodies.Count -eq 0) { Assert $false "$name 본문을 한 번도 못 봤다"; continue }
    $worst = ($bodies | Measure-Object Overflow -Maximum).Maximum
    $fewest = ($bodies | Measure-Object Lines -Minimum).Minimum
    $byWidth = ($Widths | ForEach-Object { $w = $_; $at = @($bodies | Where-Object Width -eq $w)
        $o = if ($at.Count) { ($at | Measure-Object Overflow -Maximum).Maximum } else { '-' }; "{0}:{1:0.#}" -f $w, $o }) -join ' '
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

$fixtureByWidth = ($fixtureSamples | Where-Object Expanded | ForEach-Object { "{0}:{1:0.#}" -f $_.Width, $_.Overflow }) -join ' '
$fixtureCollapsed = $fixtureSamples | Where-Object { -not $_.Expanded } | Select-Object -First 1
$table.Add(("  {0,-24} {1,-4} 줄 {2,-3} 넘침 {3} (접힌 줄 {4})" -f 'ReflectionFixture', '자극',
    (($fixtureSamples | Where-Object Expanded | Measure-Object Lines -Minimum).Minimum), $fixtureByWidth, $fixtureCollapsed.Lines))
$importByWidth = ($importSamples | Where-Object Expanded | ForEach-Object { "{0}:{1:0.#}" -f $_.Width, $_.Overflow }) -join ' '
$table.Add(("  {0,-24} {1,-4} 줄 {2,-3} 넘침 {3} (접힌 줄 {4})" -f 'ImportSettings', '이관',
    (($importSamples | Where-Object Expanded | Measure-Object Lines -Minimum).Minimum), $importByWidth, $collapsed.Lines))
Write-Host ''
$table | ForEach-Object { Write-Host $_ }
Write-Host ''
if ($failures.Count -gt 0) { throw "인스펙터 드로어 배치 검사 실패 $($failures.Count) 건" }
Write-Host "인스펙터 드로어 배치 검사: 단정 $script:checks · 이관 $($Migrated.Count + 1)/$($Targets.Count + 3) · PASS"
