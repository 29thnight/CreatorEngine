[CmdletBinding()]
param(
    [string]$Exe = (Join-Path $PSScriptRoot '../../Bin/x64-Release/Editor/CreatorEditor.exe'),
    [string]$Work = (Join-Path $env:TEMP 'creator-inspector-spatial-policy')
)
# PHASE 21 W2-I1 — 공간 컴포넌트의 공통 순회와 편집 정책(Transform 2안).
#
# 계획서 §W2-I 의 판정:
#   · *"일반 엔티티·Empty·UI·Canvas(ScreenSpace/WorldSpace)별 기대 컴포넌트 신원을 기준으로
#     헤더/펼친 본문 호출 수를 센다. 타입 수만 세거나 동일 함수의 호출문 수로 대체하지 않는다."*
#   · *"UI 체크박스 생략만으로 개별 비활성화 제한을 통과 처리하지 않는다. 개별 변경 경로의 거부와
#     엔티티 전체 비활성화·재활성화를 각각 확인한다."*
#
# 착수 때 둘 다 깨져 있었다. 인스펙터는 공간 컴포넌트를 순회 앞에서 따로 그리고 순회에서
# 건너뛰었고(본문 인스턴스 0 으로 기록돼 신원을 셀 수 없었다), `object.property <대상> Transform
# m_isEnabled false` 가 성공해 공간 컴포넌트를 개별로 껐다 — 체크박스가 없다는 것이 정책의 전부였다.
#
# ── 무엇을 단정하는가 ───────────────────────────────────────────────────────
#
# 신원: 엔티티마다 `object.describe` 가 내는 컴포넌트 인스턴스 id 집합과 인스펙터 본문의 인스턴스
#   집합이 같고, 인스턴스마다 본문(=머리줄)이 정확히 한 번이다. 두 번째 길로 한 번 더 그리면 같은
#   id 가 둘이 되고, 빠뜨리면 하나가 모자란다. 전용 드로어가 본문을 두 번 그리면 공통 줄 수가
#   Transform 3 · RectTransform 7 을 넘는다.
# 순서: RectTransform → Transform → 나머지(붙은 순서).
# 체크박스: 공간 컴포넌트는 없고 나머지는 있다.
# 개별 변경 거부: `object.property … m_isEnabled false` 가 공간 컴포넌트에서 거부되고 값이 그대로다.
#   대조군 — 일반 컴포넌트(MeshRenderer)에서는 같은 명령이 성공한다(가드가 통째로 막는 것이 아니다).
# 개별 제거 거부: `component.remove … Transform` 이 거부된다.
# 엔티티 전이: `object.enable <캔버스> off` 뒤 엔티티와 **공간 컴포넌트를 포함한** 모든 컴포넌트가
#   꺼지고, `on` 뒤 모두 켜진다. `undo` 가 끈 상태를 되돌린다.
#
# 사용법:
#   pwsh Tools/regression/verify-inspector-spatial-policy.ps1 -Exe Bin/x64-Release/Editor/CreatorEditor.exe
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

$script:checks = 0
$failures = [Collections.Generic.List[string]]::new()
function Assert([bool]$Condition, [string]$Message) {
    $script:checks++
    if (-not $Condition) { $failures.Add($Message); Write-Host "  FAIL $Message" -ForegroundColor Red }
}

# 엔티티: 이름 · object.create 유형 · 만든 뒤 적용할 명령 · 기대 컴포넌트 유형(붙은 순서)
$Entities = @(
    @{ Name = 'Policy_Empty';  Type = 'Empty';  Setup = @(); Expect = @('Transform') }
    @{ Name = 'Policy_Mesh';   Type = 'Mesh';   Setup = @('component.add Policy_Mesh MeshRenderer'); Expect = @('Transform', 'MeshRenderer') }
    @{ Name = 'Policy_UI';     Type = 'UI';     Setup = @(); Expect = @('RectTransformComponent') }
    @{ Name = 'Policy_Screen'; Type = 'Canvas'; Setup = @(); Expect = @('RectTransformComponent', 'Transform', 'Canvas') }
    @{ Name = 'Policy_World';  Type = 'Canvas'; Setup = @('object.property Policy_World Canvas RenderMode WorldSpace'); Expect = @('RectTransformComponent', 'Transform', 'Canvas') }
)
$SpatialLines = @{ Transform = 3; RectTransformComponent = 7 }
$Spatial = @('RectTransformComponent', 'Transform')

$lines = [Collections.Generic.List[string]]::new()
$marks = @{}
function Add([string]$Line, [string]$Mark = '') { if ($Mark) { $marks[$Mark] = $lines.Count }; $lines.Add($Line) }

# 창이 만들어지기 전에 첫 명령이 닿을 수 있다(빌드 직후 기동이 느리면 window.resize 가
# 'No editor window' 로 거부된다). 기다림을 앞에 둔다.
Add 'wait 60'; Add 'window.resize 1800 1400'; Add 'wait 60'; Add 'scene.new InspectorSpatialPolicy'; Add 'wait 30'
Add 'editor.window ###Editor.Inspector focus'; Add 'wait 10'
foreach ($e in $Entities) {
    Add $(if ($e.Type -eq 'Empty') { "object.create $($e.Name)" } else { "object.create $($e.Name) $($e.Type)" })
    foreach ($s in $e.Setup) { Add $s "setup:$($e.Name):$s" }
}
# `component.add` 는 기본 유형(Empty)에 MeshRenderer 를 붙인다. Mesh 유형이 이미 가졌다면 실패해도 된다.
Add 'wait 30'
Add 'object.properties Policy_Screen Canvas' 'mode:Screen'
Add 'object.properties Policy_World Canvas' 'mode:World'
foreach ($e in $Entities) {
    Add "object.describe $($e.Name)" "describe:$($e.Name)"
    Add "scene.select $($e.Name)"
    for ($r = 0; $r -lt 3; $r++) { Add 'wait 12'; Add 'editor.inspector' "read:$($e.Name):$r" }
}
# 개별 변경·제거 거부
Add 'object.property Policy_Empty Transform m_isEnabled false' 'deny:Empty:Transform'
Add 'object.property Policy_UI RectTransformComponent m_isEnabled false' 'deny:UI:RectTransformComponent'
Add 'object.property Policy_Screen Transform m_isEnabled false' 'deny:Screen:Transform'
Add 'object.property Policy_Screen RectTransformComponent m_isEnabled false' 'deny:Screen:RectTransformComponent'
Add 'component.remove Policy_Empty Transform' 'remove:Empty:Transform'
Add 'component.remove Policy_UI RectTransformComponent' 'remove:UI:RectTransformComponent'
Add 'object.describe Policy_Screen' 'after-deny:Screen'
# 대조군: 일반 컴포넌트는 개별로 끌 수 있다
Add 'object.property Policy_Mesh MeshRenderer m_isEnabled false' 'allow:Mesh'
Add 'object.describe Policy_Mesh' 'after-allow:Mesh'
Add 'object.property Policy_Mesh MeshRenderer m_isEnabled true'
# 엔티티 전이
Add 'object.enable Policy_Screen off' 'entity-off'
Add 'object.describe Policy_Screen' 'after-off'
Add 'object.enable Policy_Screen on' 'entity-on'
Add 'object.describe Policy_Screen' 'after-on'
Add 'object.enable Policy_World off'
Add 'undo' 'undo'
Add 'object.describe Policy_World' 'after-undo'
Add 'quit'

$scriptPath = Join-Path $Work 'run.txt'; $resultPath = Join-Path $Work 'run.jsonl'
Set-Content -LiteralPath $scriptPath -Encoding UTF8 -Value $lines
$workspace = Join-Path $Work 'ws'; New-Item -ItemType Directory -Force -Path $workspace | Out-Null
$env:CREATOR_EDITOR_WORKSPACE_DIR = $workspace
$env:CREATOR_EDITOR_LEGACY_INI = Join-Path $workspace 'none.ini'
try {
    $process = Start-Process -FilePath $Exe -WorkingDirectory (Split-Path $Exe) -WindowStyle Hidden -PassThru `
        -ArgumentList @('--script', ('"' + $scriptPath + '"'), '--result-format', 'jsonl', '--result-file', ('"' + $resultPath + '"')) `
        -RedirectStandardOutput "$Work/run.out" -RedirectStandardError "$Work/run.err"
    if (-not $process.WaitForExit(600000)) { $process.Kill(); throw '에디터가 제때 끝나지 않았다' }
}
finally {
    Remove-Item Env:CREATOR_EDITOR_WORKSPACE_DIR -ErrorAction SilentlyContinue
    Remove-Item Env:CREATOR_EDITOR_LEGACY_INI -ErrorAction SilentlyContinue
}
$rows = @(Get-Content -LiteralPath $resultPath | Where-Object { $_.Trim() } | ForEach-Object { $_ | ConvertFrom-Json })
if ($rows.Count -ne $lines.Count) {
    throw "결과 행 $($rows.Count) 이 줄 수 $($lines.Count) 와 다르다(exit $($process.ExitCode)) — '$($lines[$rows.Count])' 에서 멈췄다"
}
# 거부를 기대한 줄 밖은 모두 성공해야 한다.
$expectFailure = @($marks.Keys | Where-Object { $_ -like 'deny:*' -or $_ -like 'remove:*' } | ForEach-Object { $marks[$_] })
for ($i = 0; $i -lt $rows.Count; $i++) {
    if ($expectFailure -contains $i) { continue }
    if ($rows[$i].status -ne 'succeeded') { throw "'$($lines[$i])' 가 $($rows[$i].status): $($rows[$i].message)" }
}
function Row([string]$Mark) { $rows[$marks[$Mark]] }
function Components($Describe) { @($Describe.data.components) }

# 캔버스 두 모드가 실제로 갈렸는지. 이것이 없으면 '월드 공간' 표본이 화면 공간을 두 번 잰 것일 수 있다.
$screenMode = "$((Row 'mode:Screen').data.values.RenderMode)"; $worldMode = "$((Row 'mode:World').data.values.RenderMode)"
Assert ($screenMode -match '^(0|ScreenSpaceOverlay)$') "Policy_Screen 의 RenderMode 가 '$screenMode' 다 — 화면 공간이 아니다"
Assert ($worldMode -match '^(2|WorldSpace)$') "Policy_World 의 RenderMode 가 '$worldMode' 다 — 월드 공간 설정이 닿지 않았다"

# ── 신원 · 순서 · 체크박스 ──────────────────────────────────────────────────
$table = [Collections.Generic.List[string]]::new()
foreach ($e in $Entities) {
    $described = Components (Row "describe:$($e.Name)")
    Assert ((@($described | ForEach-Object { $_.type }) -join ',') -eq ($e.Expect -join ',')) `
        "$($e.Name): 실제 보유 구성 '$(@($described | ForEach-Object { $_.type }) -join ',')' 이 기대 '$($e.Expect -join ',')' 와 다르다 — fixture 가 틀렸다"
    $ids = @($described | ForEach-Object { [uint32]($_.id.TrimStart('#')) })

    # 요청이 반영된(그 엔티티를 그리고 컴포넌트 본문이 보유 수만큼 나온) 마지막 읽기
    $reads = @(0..2 | ForEach-Object { (Row "read:$($e.Name):$_").data })
    $ready = @($reads | Where-Object { $_.entity -eq $e.Name -and @($_.bodies | Where-Object { $_.type -ne 'GameObjectBaseInfo' }).Count -ge $ids.Count })
    $read = if ($ready.Count) { $ready[-1] } else { $reads[-1] }
    Assert ($read.entity -eq $e.Name) "$($e.Name): 인스펙터가 '$($read.entity)' 를 그렸다 — 선택이 닿지 않았다"
    $bodies = @($read.bodies | Where-Object { $_.type -ne 'GameObjectBaseInfo' })

    foreach ($id in $ids) {
        $count = @($bodies | Where-Object { [uint32]$_.instance -eq $id }).Count
        Assert ($count -eq 1) "$($e.Name): 인스턴스 #$id 의 머리줄·본문이 $count 번 그려졌다(기대 1)"
    }
    $strays = @($bodies | Where-Object { $ids -notcontains [uint32]$_.instance })
    Assert ($strays.Count -eq 0) "$($e.Name): 보유하지 않은 인스턴스의 본문이 그려졌다 — $(@($strays | ForEach-Object { "$($_.type)#$($_.instance)" }) -join ', ')"

    # 순서: 공간 컴포넌트가 위, RectTransform 이 Transform 보다 먼저, 나머지는 붙은 순서
    $expectedOrder = @($Spatial | Where-Object { $e.Expect -contains $_ }) + @($e.Expect | Where-Object { $Spatial -notcontains $_ })
    Assert ((@($bodies | ForEach-Object { $_.type }) -join ',') -eq ($expectedOrder -join ',')) `
        "$($e.Name): 표시 순서 '$(@($bodies | ForEach-Object { $_.type }) -join ',')' 가 정책 순서 '$($expectedOrder -join ',')' 와 다르다"

    foreach ($body in $bodies) {
        $isSpatial = $Spatial -contains $body.type
        Assert ($body.open) "$($e.Name): $($body.type) 본문이 펼쳐져 있지 않다 — 줄 수를 잴 수 없다"
        Assert ([bool]$body.enabledToggle -eq (-not $isSpatial)) `
            "$($e.Name): $($body.type) 머리줄의 개별 활성 체크박스가 $(if ($body.enabledToggle) { '있다' } else { '없다' }) — 정책은 $(if ($isSpatial) { '없음' } else { '있음' })"
        if ($isSpatial) {
            Assert ([int]$body.propertyLines -eq $SpatialLines[$body.type]) `
                "$($e.Name): $($body.type) 본문의 공통 줄 $($body.propertyLines) 이 $($SpatialLines[$body.type]) 과 다르다 — 본문이 두 번 그려졌거나 빠졌다"
        }
    }
    $table.Add(("  {0,-14} {1,-44} 체크박스 {2}" -f $e.Name, (@($bodies | ForEach-Object { "$($_.type)x1" }) -join ' '),
        (@($bodies | ForEach-Object { if ($_.enabledToggle) { 'O' } else { '-' } }) -join '')))
}

# ── 개별 변경·제거 거부 ─────────────────────────────────────────────────────
foreach ($mark in @($marks.Keys | Where-Object { $_ -like 'deny:*' } | Sort-Object)) {
    $row = Row $mark
    Assert ($row.status -ne 'succeeded') "${mark}: 공간 컴포넌트의 개별 비활성화가 성공했다"
    Assert ("$($row.message)" -match 'object\.enable') "${mark}: 거부 사유가 엔티티 전이 경로를 알려 주지 않는다 — '$($row.message)'"
}
foreach ($mark in @($marks.Keys | Where-Object { $_ -like 'remove:*' })) {
    Assert ((Row $mark).status -ne 'succeeded') "${mark}: 공간 컴포넌트의 개별 제거가 성공했다"
}
$afterDeny = Components (Row 'after-deny:Screen')
Assert (@($afterDeny | Where-Object { -not $_.enabled }).Count -eq 0) '거부 뒤 캔버스의 컴포넌트 중 꺼진 것이 있다 — 거부했다면서 값을 썼다'
Assert ((Row 'allow:Mesh').status -eq 'succeeded') "대조군: 일반 컴포넌트(MeshRenderer)의 개별 비활성화가 거부됐다 — 가드가 통째로 막는다"
Assert (@(Components (Row 'after-allow:Mesh') | Where-Object { $_.type -eq 'MeshRenderer' -and -not $_.enabled }).Count -eq 1) '대조군: MeshRenderer 가 꺼지지 않았다'
Assert (@(Components (Row 'after-allow:Mesh') | Where-Object { $_.type -eq 'Transform' -and $_.enabled }).Count -eq 1) '대조군: MeshRenderer 를 끄자 Transform 까지 꺼졌다'

# ── 엔티티 전이 ─────────────────────────────────────────────────────────────
$off = (Row 'after-off').data
Assert (-not $off.enabled) '엔티티를 껐는데 엔티티가 켜져 있다'
foreach ($c in @($off.components)) { Assert (-not $c.enabled) "엔티티를 껐는데 $($c.type) 이 켜져 있다 — 엔티티 전이가 공간 컴포넌트를 건너뛰었다" }
$on = (Row 'after-on').data
Assert ($on.enabled) '엔티티를 다시 켰는데 엔티티가 꺼져 있다'
foreach ($c in @($on.components)) { Assert ($c.enabled) "엔티티를 다시 켰는데 $($c.type) 이 꺼져 있다" }
$undone = (Row 'after-undo').data
Assert ($undone.enabled) 'object.enable off 를 undo 했는데 엔티티가 꺼져 있다'
foreach ($c in @($undone.components)) { Assert ($c.enabled) "undo 뒤 $($c.type) 이 꺼져 있다" }

Write-Host ''
$table | ForEach-Object { Write-Host $_ }
Write-Host ''
if ($failures.Count -gt 0) { throw "공간 컴포넌트 정책 검사 실패 $($failures.Count) 건" }
Write-Host "공간 컴포넌트 정책 검사: 단정 $script:checks · 엔티티 $($Entities.Count) · PASS"
