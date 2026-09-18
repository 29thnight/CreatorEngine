[CmdletBinding()]
param(
    [string]$Exe = (Join-Path $PSScriptRoot '../../Bin/x64-Release/Editor/CreatorEditor.exe'),
    [string]$Work = (Join-Path $env:TEMP 'creator-inspector-layout-matrix'),
    [string]$Scales = '1,1.25,1.5,2'
)
# PHASE 21 W2-I5 — 인스펙터 배치의 **폭 × 배율 행렬**과 **줄 전환 경계**.
#
# 계획서 §W2-I 의 판정문 중 이 검사가 지는 몫:
#   *"가용 content 폭 240/320/480/720 logical px 와 실제 줄 전환 경계 양쪽을 검사한다.
#     사용자 배율과 OS DPI 100/125/150/200% 를 구분한다"*,
#   *"숫자 최소 폭 … 가로 잘림·겹침·0-size 입력칸은 실패"*.
#
# ── 넘침 0 은 "칸이 있다" 가 아니다 ────────────────────────────────────────
#
# 드로어 배치 검사(`verify-inspector-drawer-layout.ps1`)는 오른쪽 넘침을 잰다.
# 그런데 값 칸이 1 px 로 눌려도 오른쪽 끝은 작업 영역 **안**이라 넘침은 0 이다.
# 착수 때 실제로 그랬다 — 240 폭에서 배열 원소 칸이 1 px, 일반 리플렉션 경로의
# `vector3` 칸이 24 px 이었고 검사 셋이 전부 초록이었다. 그래서 값 칸 장부를
# 열었다(`editor.inspector` 의 `minFieldWidth`·`minAxisWidth`·`fields`·
# `fieldDigest`, 하한은 `valueMin`·`axisValueMin`).
#
#   · 일반 칸 하한 `valueMin`   — 한 줄을 혼자 쓰는 칸.
#   · 축 칸 하한 `axisValueMin` — 한 줄을 셋·넷이 나눠 쓰는 칸(badge 는 별도).
#   둘 다 폰트에서 나오므로 **배율을 따라간다**. 검사가 논리 px 리터럴을 들지 않는다.
#
# ── 이 자가 재는 것은 배치지 "화면에 보이는가" 가 아니다 ──────────────────
#
# 폭 창구는 인스펙터 도크 패널 **안에** 자식 창을 그 폭으로 세운다. 그런데 도크
# 패널의 폭은 창 크기를 바꿔도 그대로다(2400·3400·3800 셋 다 505 px — 도크 노드는
# 절대 폭을 지킨다). 그래서 요청이 505 px 를 넘으면 본문은 그 폭으로 **배치되지만**
# 패널 밖은 잘려 화면에 없다. 배치 계산은 잘림과 무관하므로 아래 단정은 그대로
# 성립하지만, "사용자가 그것을 본다" 는 이 자의 주장이 아니다 — 그 축은 W8-2 의
# chrome 골든(픽셀)과 사람의 확인이 진다. 자가 스스로 그 사실을 내도록
# `visibleWidth` 를 함께 읽고 표에 적는다.
#
# ── 배율 축 ───────────────────────────────────────────────────────────────
#
# 모니터 배율은 바꿀 수 없으므로 `imguiScale`(사용자 배율)로 민다 —
# `verify-editor-theme`·`verify-content-browser-splitter` 와 같은 방법이고, 실제
# 배율은 사용자 배율 × 모니터 DPI 다. 배율마다 에디터를 새로 띄운다.
#
# 판정의 핵심은 **논리 불변**이다. 같은 논리 폭에서 잰 값을 실제 배율로 나누면
# 배율이 달라도 같아야 한다. 배율을 받지 않는 고정 픽셀이 하나라도 섞이면 이
# 비율이 깨진다 — 소스 축(드로어 배치 검사)이 리터럴을 잡고, 이 축이 그 리터럴이
# 어디서 왔든(테마 토큰 오용·ImGui 기본값) 결과로 잡는다.
#
# ── 전환 경계 ─────────────────────────────────────────────────────────────
#
# "240/320/480/720 네 점" 만 재면 전환이 일어나는 자리를 건너뛴다. 폭을 촘촘히
# 훑어(4 논리 px 걸음) 본문 높이가 바뀌는 자리를 찾고, 그 **양쪽 모두** 넘침 0 ·
# 하한 충족 · 줄 수 동일을 단정한다. 전환이 한 번도 안 잡히면 그것도 실패다 —
# 훑은 구간이 전환을 품지 않았다는 뜻이라 이 축이 빈 집합이 된다.
#
# 사용법:
#   pwsh Tools/regression/verify-inspector-layout-matrix.ps1 -Exe Bin/x64-Release/Editor/CreatorEditor.exe
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

# 대상 둘. UI 는 RectTransform(두 칸 줄), 일반 엔티티는 Transform(축 셋) ·
# MeshRenderer(전용 드로어) · 리플렉션 자극물(중첩·배열·맵)을 한꺼번에 낸다.
$Targets = [ordered]@{ Matrix_UI = 'UI'; Matrix_Mesh = '' }
$Widths = @(240, 320, 480, 720)
# 전환 경계를 훑는 구간. 두 칸 줄과 축 셋의 전환이 모두 이 안에 있다(실측).
$SweepFrom = 224; $SweepTo = 432; $SweepStep = 4
$FixtureLines = 55   # `InspectorLayoutFixture.h` 의 타입 정의에서 센 값(펼친 채)

$settingsBytes = [IO.File]::ReadAllBytes($settingsPath)
$settingsText = [IO.File]::ReadAllText($settingsPath)
$scalePattern = '(?m)(^imguiScale: )[^\r\n]+'
if ([regex]::Matches($settingsText, $scalePattern).Count -ne 1) { throw 'imguiScale 설정이 하나가 아니다' }
$utf8 = [Text.UTF8Encoding]::new($false)

# `wait` 는 게임 스레드 프레임이고 인스펙터는 표시 스레드에서 그려진다. 폭마다
# 여러 번 읽고 요청이 반영된 마지막 읽기를 표본으로 쓴다(드로어 배치 검사와 같은 규약).
$Reads = 3
function Add-WidthReads([System.Collections.Generic.List[string]]$Lines, [int]$Width) {
    $Lines.Add("editor.inspector width $Width")
    for ($r = 0; $r -lt $Reads; $r++) { $Lines.Add('wait 10'); $Lines.Add('editor.inspector') }
}

function Invoke-Round([string]$Name, [string[]]$Lines) {
    $scriptPath = Join-Path $Work "$Name.txt"; $resultPath = Join-Path $Work "$Name.jsonl"
    Set-Content -LiteralPath $scriptPath -Encoding UTF8 -Value $Lines
    $workspace = Join-Path $Work "ws-$Name"; New-Item -ItemType Directory -Force -Path $workspace | Out-Null
    $env:CREATOR_EDITOR_WORKSPACE_DIR = $workspace
    $env:CREATOR_EDITOR_LEGACY_INI = Join-Path $workspace 'none.ini'
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

# 한 회차의 명령. 대상마다 네 폭을 재고, 첫 대상에서는 경계를 훑는다.
function New-RoundScript([bool]$Sweep) {
    $lines = [Collections.Generic.List[string]]::new()
    foreach ($line in @('window.resize 2400 1600', 'wait 60', 'scene.new InspectorLayoutMatrix', 'wait 30',
        'editor.window ###Editor.Inspector focus', 'wait 10')) { $lines.Add($line) }
    foreach ($name in $Targets.Keys) {
        $type = $Targets[$name]
        $lines.Add("object.create $name $type".TrimEnd())
    }
    $lines.Add('component.add Matrix_Mesh MeshRenderer')
    $lines.Add('wait 30')
    $lines.Add('editor.inspector fixture on')
    $lines.Add('editor.inspector expand on')
    foreach ($name in $Targets.Keys) {
        $lines.Add("scene.select $name")
        foreach ($width in $Widths) { Add-WidthReads $lines $width }
        if ($Sweep) {
            for ($width = $SweepFrom; $width -le $SweepTo; $width += $SweepStep) { Add-WidthReads $lines $width }
        }
    }
    $lines.Add('editor.inspector expand off')
    $lines.Add('editor.inspector fixture off')
    $lines.Add('editor.inspector width off')
    $lines.Add('wait 6')
    $lines.Add('quit')
    $lines
}

# 읽기를 (대상 · 폭) 으로 묶고 요청이 반영된 마지막 읽기를 고른다.
function Select-Samples([string[]]$Lines, [object[]]$Rows) {
    $groups = [ordered]@{}
    $target = ''; $width = 0
    for ($i = 0; $i -lt $Lines.Count; $i++) {
        $line = $Lines[$i]
        if ($line -like 'scene.select *') { $target = $line.Substring('scene.select '.Length) }
        elseif ($line -like 'editor.inspector width *') {
            $tail = $line.Substring('editor.inspector width '.Length)
            $width = if ($tail -eq 'off') { 0 } else { [int]$tail }
        }
        elseif ($line -eq 'editor.inspector' -and $width -gt 0) {
            $key = "$target|$width"
            if (-not $groups.Contains($key)) { $groups[$key] = [Collections.Generic.List[object]]::new() }
            $groups[$key].Add([pscustomobject]@{ Target = $target; Width = $width; Data = $Rows[$i].data })
        }
    }
    foreach ($group in $groups.Values) {
        $ready = @($group | Where-Object {
            $_.Data.requestedWidth -eq $_.Width -and $_.Data.entity -eq $_.Target -and
            @($_.Data.bodies | Where-Object { $_.open }).Count -gt 1 })
        if ($ready.Count -gt 0) { $ready[-1] } else { $group[$group.Count - 1] }
    }
}

# 한 표본의 본문을 이름으로 꺼낸다.
function Get-Body([object]$Sample, [string]$Type) {
    @($Sample.Data.bodies | Where-Object { $_.type -eq $Type -and $_.open })
}

$scaleList = @($Scales -split '[,;\s]+' | Where-Object { $_ } | ForEach-Object { [double]$_ })
$rounds = [ordered]@{}
$dpi = $null
try {
    $first = $true
    foreach ($userScale in $scaleList) {
        $text = $userScale.ToString('0.###', [Globalization.CultureInfo]::InvariantCulture)
        $tag = 's' + $text.Replace('.', '')
        [IO.File]::WriteAllText($settingsPath,
            [regex]::Replace($settingsText, $scalePattern, "`${1}$text"), $utf8)
        Write-Host "── 사용자 배율 $text$(if ($first) { ' (경계 훑기 포함)' })"
        $lines = @(New-RoundScript $first)
        $rows = Invoke-Round $tag $lines
        $samples = @(Select-Samples $lines $rows)
        $rounds[$text] = [pscustomobject]@{ UserScale = $userScale; Samples = $samples; Swept = $first }
        $first = $false
    }
}
finally {
    [IO.File]::WriteAllBytes($settingsPath, $settingsBytes)
}

# ── ① 자 자신과 자극 ────────────────────────────────────────────────────────
$expectedBodies = @{ Matrix_UI = @('GameObjectBaseInfo', 'RectTransformComponent')
                     Matrix_Mesh = @('GameObjectBaseInfo', 'Transform', 'MeshRenderer', 'ReflectionFixture') }
foreach ($text in $rounds.Keys) {
    $round = $rounds[$text]
    # 훑는 구간이 네 폭 중 일부를 품으므로 **서로 다른 폭**의 수로 센다.
    $distinct = [Collections.Generic.HashSet[int]]::new()
    foreach ($width in $Widths) { [void]$distinct.Add($width) }
    if ($round.Swept) {
        for ($width = $SweepFrom; $width -le $SweepTo; $width += $SweepStep) { [void]$distinct.Add($width) }
    }
    $expected = $distinct.Count * $Targets.Count
    Assert ($round.Samples.Count -eq $expected) "배율 ${text}: 표본 $($round.Samples.Count) 이 $expected 이 아니다"
    $scaleOfRound = $null
    foreach ($sample in $round.Samples) {
        $d = $sample.Data
        $tag = "배율 $text · $($sample.Target)@$($sample.Width)"
        if ($null -eq $scaleOfRound) { $scaleOfRound = [double]$d.uiScale }
        Assert ([Math]::Abs([double]$d.uiScale - $scaleOfRound) -le 0.001) "${tag}: 배율이 회차 안에서 움직였다($($d.uiScale) vs $scaleOfRound)"
        Assert ([Math]::Abs([double]$d.contentWidth - $sample.Width * [double]$d.uiScale) -le 1) `
            "${tag}: 받은 폭 $($d.contentWidth) 이 요청 × 배율 $($sample.Width * $d.uiScale) 과 다르다"
        Assert ([double]$d.valueMin -gt 0 -and [double]$d.axisValueMin -gt 0) "${tag}: 최소 가독 폭이 0 이다 — 하한을 못 읽었다"
        Assert ([double]$d.visibleWidth -gt 0) "${tag}: 보이는 폭이 0 이다 — 패널이 접혔거나 자식 창이 통째로 잘렸다"
        foreach ($type in $expectedBodies[$sample.Target]) {
            Assert ((Get-Body $sample $type).Count -eq 1) "${tag}: 본문 '$type' 이 열린 채 한 번 그려지지 않았다"
        }
    }
    # 모니터 DPI 는 이 기계의 성질이다 — 회차마다 같아야 사용자 배율 축이 성립한다.
    $derived = $scaleOfRound / $round.UserScale
    if ($null -eq $dpi) { $dpi = $derived }
    Assert ([Math]::Abs($derived - $dpi) -le 0.01) `
        "배율 ${text}: 실제 배율/사용자 배율 $derived 이 첫 회차의 $dpi 와 다르다 — 배율이 섞였다"
    $visible = @($round.Samples | ForEach-Object { [double]$_.Data.visibleWidth } | Measure-Object -Maximum).Maximum
    Write-Host ("    실제 배율 {0:N2} (사용자 {1} × 모니터 {2:N2}) · 패널이 실제로 보여 주는 폭 {3:N0} px" -f `
        $scaleOfRound, $text, $dpi, $visible)
}

# ── ② 폭 × 배율 행렬 ───────────────────────────────────────────────────────
#
# 값 칸이 하한 아래면 실패다. 넘침만 보면 못 잡는 축이 바로 이것이다.
$baseText = @($rounds.Keys)[0]
$baseline = @{}
foreach ($text in $rounds.Keys) {
    $round = $rounds[$text]
    foreach ($sample in $round.Samples | Where-Object { $Widths -contains $_.Width }) {
        $d = $sample.Data
        $valueMin = [double]$d.valueMin; $axisMin = [double]$d.axisValueMin
        foreach ($type in $expectedBodies[$sample.Target]) {
            $body = @(Get-Body $sample $type)
            if ($body.Count -ne 1) { continue }
            $b = $body[0]
            $tag = "배율 $text · $($sample.Target)@$($sample.Width) · $type"
            Assert ([double]$b.overflow -le 0.5) "${tag}: 넘침 $($b.overflow) px"
            # 칸 장부는 `drag_property_float` 만 본다. 글상자·콤보·체크박스는 줄이
            # 준 폭을 그대로 쓰므로 그 축은 `minLineValue` 로 센다.
            Assert ([double]$b.minLineValue -ge $axisMin - 0.5) `
                ("${tag}: 줄이 값에 준 폭이 {0:N1} px 로 하한 {1:N1} 아래다" -f $b.minLineValue, $axisMin)
            if ([double]$b.minFieldWidth -gt 0) {
                Assert ([double]$b.minFieldWidth -ge $valueMin - 0.5) `
                    ("${tag}: 값 칸이 {0:N1} px 로 하한 {1:N1} 아래다(줄 '{2}') — 0-size 입력칸은 실패다" -f `
                     $b.minFieldWidth, $valueMin, $b.narrowestField)
            }
            if ([double]$b.minAxisWidth -gt 0) {
                Assert ([double]$b.minAxisWidth -ge $axisMin - 0.5) `
                    ("${tag}: 축 칸이 {0:N1} px 로 하한 {1:N1} 아래다(줄 '{2}') — 줄을 내렸어야 한다" -f `
                     $b.minAxisWidth, $axisMin, $b.narrowestAxis)
            }
            $key = "$($sample.Target)|$($sample.Width)|$type"
            if ($text -eq $baseText) {
                $baseline[$key] = [pscustomobject]@{ Lines = [int]$b.propertyLines; Fields = [int]$b.fields
                    AxisFields = [int]$b.axisFields; Scale = [double]$d.uiScale
                    Field = [double]$b.minFieldWidth; Axis = [double]$b.minAxisWidth
                    LineValue = [double]$b.minLineValue }
                continue
            }
            # ★ 논리 불변. 같은 논리 폭이면 실제 배율로 나눈 값이 배율 사이에 같아야 한다.
            $base = $baseline[$key]
            Assert ($null -ne $base) "${tag}: 기준 배율 $baseText 에 같은 칸이 없다"
            if ($null -eq $base) { continue }
            Assert ([int]$b.propertyLines -eq $base.Lines) "${tag}: 공통 줄 $($b.propertyLines) 이 배율 $baseText 의 $($base.Lines) 와 다르다"
            Assert ([int]$b.fields -eq $base.Fields -and [int]$b.axisFields -eq $base.AxisFields) `
                "${tag}: 값 칸 수 $($b.fields)/$($b.axisFields) 가 배율 $baseText 의 $($base.Fields)/$($base.AxisFields) 와 다르다"
            foreach ($axis in @(
                @{ Name = '값 칸'; Now = [double]$b.minFieldWidth; Base = $base.Field }
                @{ Name = '축 칸'; Now = [double]$b.minAxisWidth; Base = $base.Axis }
                @{ Name = '줄 폭'; Now = [double]$b.minLineValue; Base = $base.LineValue })) {
                if ($axis.Base -le 0) { continue }
                $logicalNow = $axis.Now / [double]$d.uiScale
                $logicalBase = $axis.Base / $base.Scale
                Assert ([Math]::Abs($logicalNow - $logicalBase) -le [Math]::Max(1.0, $logicalBase * 0.02)) `
                    ("${tag}: $($axis.Name) 의 논리 폭 {0:N1} 이 배율 $baseText 의 {1:N1} 과 다르다 — 배율을 받지 않는 고정 폭이 섞였다" -f `
                     $logicalNow, $logicalBase)
            }
        }
    }
}

# ── ③ 필드 신원 ────────────────────────────────────────────────────────────
#
# 계획서가 주입하라고 이름까지 적은 결함 하나가 *"전환 시 ID 변경"* 이다. 폭이
# 바뀌어 줄이 내려가거나 축이 세로가 될 때 값 칸의 ImGui ID 가 달라지면, 그 순간
# 편집 중이던 칸과 되돌리기 대상이 사라진다. 한 회차 안에서는 컴포넌트 인스턴스
# id 가 고정이므로 **폭이 바뀌어도 신원 요약이 같아야 한다.**
foreach ($text in $rounds.Keys) {
    $round = $rounds[$text]
    foreach ($targetName in $Targets.Keys) {
        foreach ($type in $expectedBodies[$targetName]) {
            $digests = @($round.Samples | Where-Object { $_.Target -eq $targetName } | Sort-Object Width |
                ForEach-Object {
                    $body = @(Get-Body $_ $type)
                    if ($body.Count -ne 1) { return }
                    [pscustomobject]@{ Width = $_.Width; Digest = [int64]$body[0].fieldDigest }
                })
            $withFields = @($digests | Where-Object { $_.Digest -ne 0 })
            if ($withFields.Count -eq 0) { continue }   # 값 칸이 없는 본문(이름·체크박스만)
            $reference = $withFields[0]
            foreach ($point in $withFields) {
                Assert ($point.Digest -eq $reference.Digest) `
                    "배율 $text · $targetName · ${type}@$($point.Width): 값 칸 신원 $($point.Digest) 이 $($reference.Width) 폭의 $($reference.Digest) 와 다르다 — 전환이 ID 를 바꿨다"
            }
        }
    }
}

# ── ④ 줄 전환 경계 ─────────────────────────────────────────────────────────
$sweptRound = @($rounds.Values | Where-Object { $_.Swept })[0]
$transitions = 0
foreach ($targetName in $Targets.Keys) {
    foreach ($type in $expectedBodies[$targetName]) {
        $series = @($sweptRound.Samples |
            Where-Object { $_.Target -eq $targetName -and $_.Width -ge $SweepFrom -and $_.Width -le $SweepTo } |
            Sort-Object Width |
            ForEach-Object {
                $body = @(Get-Body $_ $type)
                if ($body.Count -ne 1) { return }
                [pscustomobject]@{ Width = $_.Width; Height = [double]$body[0].height
                    Lines = [int]$body[0].propertyLines; Overflow = [double]$body[0].overflow
                    Field = [double]$body[0].minFieldWidth; Axis = [double]$body[0].minAxisWidth
                    LineValue = [double]$body[0].minLineValue
                    NarrowField = $body[0].narrowestField; NarrowAxis = $body[0].narrowestAxis
                    ValueMin = [double]$_.Data.valueMin; AxisMin = [double]$_.Data.axisValueMin }
            })
        Assert ($series.Count -ge 2) "$targetName · ${type}: 훑은 표본이 $($series.Count) 개다"
        if ($series.Count -lt 2) { continue }
        $lines0 = $series[0].Lines
        for ($i = 0; $i -lt $series.Count; $i++) {
            $point = $series[$i]
            $tag = "경계 훑기 · $targetName · $type @$($point.Width)"
            Assert ($point.Overflow -le 0.5) "${tag}: 넘침 $($point.Overflow) px"
            Assert ($point.Lines -eq $lines0) "${tag}: 공통 줄 $($point.Lines) 이 $lines0 과 다르다 — 폭이 필드 수를 바꿨다"
            if ($point.Field -gt 0) {
                Assert ($point.Field -ge $point.ValueMin - 0.5) `
                    ("${tag}: 값 칸 {0:N1} px 이 하한 {1:N1} 아래다(줄 '{2}')" -f $point.Field, $point.ValueMin, $point.NarrowField)
            }
            if ($point.Axis -gt 0) {
                Assert ($point.Axis -ge $point.AxisMin - 0.5) `
                    ("${tag}: 축 칸 {0:N1} px 이 하한 {1:N1} 아래다(줄 '{2}')" -f $point.Axis, $point.AxisMin, $point.NarrowAxis)
            }
            Assert ($point.LineValue -ge $point.AxisMin - 0.5) `
                ("${tag}: 줄이 값에 준 폭이 {0:N1} px 로 하한 {1:N1} 아래다" -f $point.LineValue, $point.AxisMin)
            if ($i -gt 0 -and [Math]::Abs($point.Height - $series[$i - 1].Height) -gt 1.0) {
                $transitions++
                Write-Host ("    전환 {0} · {1}: {2} → {3} 논리 px 에서 높이 {4:N0} → {5:N0}" -f `
                    $targetName, $type, $series[$i - 1].Width, $point.Width, $series[$i - 1].Height, $point.Height)
            }
        }
    }
}
# 전환을 한 번도 안 지났으면 이 축은 빈 집합이다.
Assert ($transitions -ge 2) "훑은 구간 $SweepFrom~$SweepTo 에서 전환이 $transitions 번뿐이다 — 경계를 품지 않은 구간을 훑었다"

# ── 표 ─────────────────────────────────────────────────────────────────────
Write-Host ''
foreach ($text in $rounds.Keys) {
    $round = $rounds[$text]
    foreach ($targetName in $Targets.Keys) {
        foreach ($type in $expectedBodies[$targetName]) {
            $cells = @($Widths | ForEach-Object {
                $w = $_
                $sample = @($round.Samples | Where-Object { $_.Target -eq $targetName -and $_.Width -eq $w })
                if ($sample.Count -eq 0) { return '-' }
                $body = @(Get-Body $sample[0] $type)
                if ($body.Count -ne 1) { return '-' }
                "{0}:{1:0}/{2:0}" -f $w, $body[0].minFieldWidth, $body[0].minAxisWidth
            })
            Write-Host ("  배율 {0,-4} {1,-22} 칸 폭(값/축) {2}" -f $text, $type, ($cells -join '  '))
        }
    }
}

# 자극이 닿았는지. 자극물 줄 수가 타입 정의에서 센 값이어야 안쪽까지 그린 것이다.
$fixture = @(@($rounds.Values)[0].Samples | Where-Object { $_.Target -eq 'Matrix_Mesh' -and $_.Width -eq 720 })
Assert ($fixture.Count -eq 1) '자극물 표본을 못 찾았다'
if ($fixture.Count -eq 1) {
    $body = @(Get-Body $fixture[0] 'ReflectionFixture')
    Assert ($body.Count -eq 1 -and [int]$body[0].propertyLines -eq $FixtureLines) `
        "자극물의 공통 줄이 $FixtureLines 이 아니다 — 안쪽 중첩·배열이 자극되지 않았다"
    Assert ($body.Count -eq 1 -and ([int]$body[0].fields + [int]$body[0].axisFields) -ge 20) `
        "자극물의 값 칸이 $(if ($body.Count) { [int]$body[0].fields + [int]$body[0].axisFields } else { 0 }) 개다 — 칸 장부가 자극되지 않았다"
}

Write-Host ''
if ($failures.Count -gt 0) { throw "인스펙터 배치 행렬 검사 실패 $($failures.Count) 건" }
Write-Host "인스펙터 배치 행렬 검사: 단정 $script:checks · 배율 $($scaleList.Count) · 폭 $($Widths.Count) · 전환 $transitions · PASS"
