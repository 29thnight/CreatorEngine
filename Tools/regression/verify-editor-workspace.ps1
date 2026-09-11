[CmdletBinding()]
param(
    [string]$Exe = (Join-Path $PSScriptRoot '../../Bin/x64-Debug/Editor/CreatorEditor.exe'),
    [string]$Work = (Join-Path $env:TEMP 'creator-editor-workspace')
)
# PHASE 21 W0 전반 — 에디터 크롬을 밖에서 본다(계획서 §1.9).
#
# 선언 배선 게이트(`verify-editor-declaration-wiring.ps1`)와 **파일을 나눈 이유**는
# 뜨는 상태가 다르기 때문이다. 그쪽은 개발자의 평소 상태에서 한 번 띄워 선언 표를
# 본다. 이쪽은 W0 후반에 `imgui.ini` fixture 네 벌과 재시작·손상 ini 를 태우게
# 되므로 준비된 상태로 에디터를 여러 번 띄운다. 한 파일에 섞으면 선언 검사가
# 남의 이유로 에디터를 다시 띄우게 된다.
#
# 세 관측을 태운다.
#
# ① `editor.dock` — 살아 있는 도크 노드 트리. 이것이 `imgui_internal.h` 의
#    `ImGuiDockNode` 를 가장 깊이 읽으므로 **ImGui 판 카나리아**가 여기 붙는다
#    (계획서 §9 가 W8 에 요구한 것). 판이 올라가면 컴파일은 통과하면서 읽는 값만
#    어긋날 수 있다 — 1.92 에서 그 구조체의 플래그 멤버가 셋으로 갈렸다.
#
# ② `editor.theme` — 적용된 스타일 값. "에디터 스킨이 실제로 적용됐는가" 와
#    "배율 출처가 하나인가" 를 본다. 적용 경로가 둘(최초·변경 시 재적용)이라
#    한쪽만 고치면 갈린다.
#
# ③ `editor.layout` — `imgui.ini` 항목을 선언 표와 맞댄다. 같은 창 이름이 두 번
#    있는 것이 §1.4 가 기록한 사고의 꼴이다(공백 하나가 달라 Content Browser
#    항목이 둘로 갈렸다).
#
# ★ 중앙 노드의 **존재는 단정하지 않는다.** 실측으로 지금 0 이고 중앙
#   `ViewportHost` 노드를 세우는 것은 W4 의 일이다. 존재를 여기 적으면 W4 착수
#   전까지 이 게이트가 내내 붉어 도는 세트에 들어갈 수 없다. 유일성만 본다.
#
# ★ 배율의 출처는 단정한다. W1 이 obsolete `io.FontGlobalScale` 을 걷었으므로
#   `scaleMatches` 는 정식 경로 `style.FontScaleMain` 과 설정값의 일치를 본다.
#   되돌아가는 길은 `verify-imgui-obsolete-surface.ps1` 이 소스에서 막는다 —
#   `IMGUI_DISABLE_OBSOLETE_FUNCTIONS` 는 `ImGuiIO` 레이아웃을 바꾸는 매크로라
#   제품 구성에 켤 수 없다(계획서 §3.2 의 실측).
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$Exe = [IO.Path]::GetFullPath($Exe)
$Work = [IO.Path]::GetFullPath($Work)
New-Item -ItemType Directory -Force -Path $Work | Out-Null

if (-not (Test-Path -LiteralPath $Exe)) { throw "Editor executable not found: $Exe" }
# 프로젝트에 endpoint 파일은 하나다. 개발자가 띄워 둔 에디터를 죽이지 않는다.
if (Get-Process CreatorEditor -ErrorAction SilentlyContinue) { throw 'Close the existing editor before running this isolated gate.' }

$script:checks = 0
function Assert([bool]$Condition, [string]$Message) {
    if (-not $Condition) { throw $Message }
    $script:checks++
}

function Invoke-EditorBatch([string[]]$Commands, [string]$Tag) {
    $scriptPath = Join-Path $Work "$Tag.txt"
    $resultPath = Join-Path $Work "$Tag.jsonl"
    $stdoutPath = Join-Path $Work "$Tag.out"
    $stderrPath = Join-Path $Work "$Tag.err"
    foreach ($file in @($resultPath, $stdoutPath, $stderrPath)) {
        if (Test-Path -LiteralPath $file) { Remove-Item -LiteralPath $file }
    }
    Set-Content -LiteralPath $scriptPath -Encoding UTF8 -Value $Commands
    $proc = Start-Process -FilePath $Exe -WorkingDirectory (Split-Path $Exe) -WindowStyle Hidden -PassThru `
        -ArgumentList @('--script', ('"' + $scriptPath + '"'), '--result-format', 'jsonl',
                        '--result-file', ('"' + $resultPath + '"')) `
        -RedirectStandardOutput $stdoutPath -RedirectStandardError $stderrPath
    if (-not $proc.WaitForExit(300000)) {
        $proc.Kill()
        throw "Editor did not exit within 300s while running the workspace gate ($Tag)."
    }
    return [pscustomobject]@{
        ExitCode = $proc.ExitCode
        Result   = $resultPath
        Stdout   = $stdoutPath
    }
}

# ── ini 워밍업 ──────────────────────────────────────────────────────────
#
# `imgui.ini` 가 아직 없는 기계에서는 ③ 의 항목 단정이 잴 것이 없다. 그것을
# "항목 0 이니 통과" 로 넘기면 빈 집합을 성공으로 읽는 양식이 된다. 그래서
# **조용히 건너뛰지 않고** 에디터를 한 번 더 돌려 만든다. ImGui 는 저장 주기가
# 5초(`io.IniSavingRate`)라 충분히 기다려야 한다.
$iniPath = Join-Path (Split-Path $Exe) 'Saved/Config/imgui.ini'
if (-not (Test-Path -LiteralPath $iniPath)) {
    Write-Host "[workspace] imgui.ini 가 없다 — 레이아웃을 한 번 저장시키려고 에디터를 먼저 돌린다."
    Invoke-EditorBatch @('wait 400', 'quit') 'warmup' | Out-Null
}

$run = Invoke-EditorBatch @('wait 45', 'editor.dock', 'editor.theme', 'editor.layout', 'quit') 'workspace'

# ★ 종료 코드 단정은 맨 뒤다(선언 게이트와 같은 이유). 배치 러너는 명령 하나가
#   실패하면 4로 나가는데, 앞에서 그것을 보면 아래 단정이 한 번도 돌지 않아
#   "exited 4" 한 줄만 남는다. 그러면 붉은 줄이 고칠 곳을 가리키지 못한다.
Assert (Test-Path -LiteralPath $run.Result) "No result file produced at $($run.Result) (batch exited $($run.ExitCode); see $($run.Stdout))"
$lines = @(Get-Content -LiteralPath $run.Result | Where-Object { $_.Trim().Length -gt 0 })
$results = @{}
foreach ($line in $lines) {
    $parsed = $line | ConvertFrom-Json
    $results[$parsed.command] = $parsed
}

# ── ① 도크 배치 ─────────────────────────────────────────────────────────
Assert ($results.ContainsKey('editor.dock')) 'editor.dock produced no result line'
$dock = $results['editor.dock']
Assert ($dock.status -eq 'succeeded') "editor.dock failed: $($dock.message)"
Assert ($dock.data.clean -eq $true) "Dock audit is dirty: $($dock.message)"
Assert ($dock.data.undockedSlots -eq 0) "Windows being drawn that lost their dock slot: $($dock.data.undockedSlots)"
Assert ($dock.data.ghostTabs -eq 0) "Dock nodes holding window ids that are not declared: $($dock.data.ghostTabs)"

# ImGui 판 카나리아. 판이 바뀌면 `imgui_internal.h` 구조체를 읽는 코드를 다시
# 검증해야 한다 — 붉어지는 것이 이 단정의 목적이다.
Assert ($dock.data.versionKnown -eq $true) `
    "ImGui version changed (num=$($dock.data.imguiVersionNum)); re-verify the imgui_internal.h reads and bump expected_imgui_version_num"

# 중앙 노드는 **유일성만** 본다. 존재는 W4 가 만든다.
Assert ($dock.data.centralNodes -le 1) "More than one central dock node: $($dock.data.centralNodes)"

# 빈 트리 위에서 도는 부재 단정을 막는다. 개수를 못 박지는 않고 하한만 둔다.
Assert ($dock.data.nodes -ge 3) "Only $($dock.data.nodes) dock nodes; the probe likely read nothing"
Assert ($dock.data.leafNodes -ge 2) "Only $($dock.data.leafNodes) leaf dock nodes; the layout did not split"
Assert ($dock.data.dockedWindows -ge 2) "Only $($dock.data.dockedWindows) windows are docked; the gate would be vacuous"

# W0 후반 성능 기준선이 실제로 실렸는가. 이 값들은 `ImGui::Render()` 뒤에 따로
# 얹히므로(`capture_chrome_draw_totals`) 그 호출이 빠지면 -1 인 채로 남는다.
# 숫자 자체는 단정하지 않는다 — 창을 하나 더 그리면 정당하게 바뀐다.
#
# ★ `uiCpuMs` 의 크기는 여기서 보지 않는다. 이 게이트는 Debug 로 도는데
#   Debug 시간은 Release 와 방향까지 다를 수 있어 성능 판정에 쓸 수 없다.
#   기준선 수치는 Release 실행으로 따로 기록한다.
Assert ($dock.data.imguiVertices -gt 0) `
    "ImGui draw totals were never amended onto the snapshot (vertices=$($dock.data.imguiVertices)); capture_chrome_draw_totals is not running after ImGui::Render()"
Assert ($dock.data.imguiDrawCommands -gt 0) `
    "ImGui draw command count is $($dock.data.imguiDrawCommands); the UI drew nothing this frame"
Assert ($dock.data.uiCpuMs -gt 0) `
    "uiCpuMs is $($dock.data.uiCpuMs); the editor UI frame timer is not running"

# 트리가 한 뿌리에서 내려오는가. 부모가 0 인 노드가 둘이면 도크스페이스가 갈렸다.
$dump = @(Get-Content -LiteralPath $run.Stdout)
$rootRows = @($dump | Where-Object { $_ -match '^\d+\t0\t' })
Assert ($rootRows.Count -eq 1) "Expected exactly one root dock node row, found $($rootRows.Count)"

# ── ② 스타일 ────────────────────────────────────────────────────────────
Assert ($results.ContainsKey('editor.theme')) 'editor.theme produced no result line'
$theme = $results['editor.theme']
Assert ($theme.status -eq 'succeeded') "editor.theme failed: $($theme.message)"
Assert ($theme.data.clean -eq $true) "Theme audit is dirty: $($theme.message)"
Assert ($theme.data.styleApplied -eq $true) 'The editor skin was not applied; the style equals the ImGui default'
Assert ($theme.data.scaleMatches -eq $true) `
    "Scale source diverged: style.FontScaleMain=$($theme.data.fontScaleMain) preference=$($theme.data.preferenceScale)"

# 선언된 창 라벨의 아이콘이 폰트 블롭에 실제로 있는가. IconsFontAwesome6.h 에
# 정의만 있고 블롭에 글리프가 없으면 네모 한 칸이 조용히 그려진다.
Assert ($theme.data.missingGlyphLabels -eq 0) `
    "$($theme.data.missingGlyphLabels) window label(s) reference a glyph missing from the icon font; they draw as boxes. See the [AUDIT] lines for which window, and pick an ICON_FA_* constant that is already used elsewhere in the editor"

Assert ($theme.data.colors -ge 40) "Only $($theme.data.colors) style colors captured; the probe likely read nothing"
Assert ($theme.data.scalars -ge 10) "Only $($theme.data.scalars) style scalars captured; the probe likely read nothing"
Assert ($theme.data.differing -ge 10) "Only $($theme.data.differing) colors differ from the ImGui default"

# ── ③ 레이아웃과 ini ────────────────────────────────────────────────────
Assert ($results.ContainsKey('editor.layout')) 'editor.layout produced no result line'
$layout = $results['editor.layout']
Assert ($layout.status -eq 'succeeded') "editor.layout failed: $($layout.message)"
Assert ($layout.data.clean -eq $true) "Layout audit is dirty: $($layout.message)"
Assert ($layout.data.duplicateEntries -eq 0) `
    "Same window name appears twice in imgui.ini: $($layout.data.duplicateEntries)"

# ImGui 가 실제로 쓰는 경로가 프로젝트의 설정 폴더인가. 여기가 어긋나면 배치가
# 조용히 다른 파일로 저장되고, 사용자는 레이아웃이 저장되지 않는 것으로 본다.
Assert ($layout.data.iniPath -like '*Saved?Config?imgui.ini') `
    "imgui.ini path is not the project config path: $($layout.data.iniPath)"

Assert ($layout.data.iniExists -eq $true) `
    "imgui.ini does not exist even after the warm-up run: $($layout.data.iniPath)"
Assert ($layout.data.iniEntries -ge 5) "Only $($layout.data.iniEntries) window entries in imgui.ini; the parse likely broke"
Assert ($layout.data.matched -ge 3) "Only $($layout.data.matched) ini entries match a declared window; the name contract likely broke"

# 내용이 다 맞았으면 마지막으로 프로세스가 깨끗하게 나갔는지 본다.
Assert ($run.ExitCode -eq 0) "Workspace gate batch exited $($run.ExitCode) though every check passed; see $($run.Stdout)"

# ── imgui.ini fixture 통과 (PHASE 21 W0 후반, 계획서 §1.4) ──────────────
#
# 실물 넷과 합성 손상본 하나를 차례로 정본 자리에 놓고 에디터를 띄운다. 출처는
# `fixtures/imgui-ini/README.md` 에 있다.
#
# ★ **여기서 단정하는 것과 기록만 하는 것이 갈린다.** legacy ini 는 §1.4 가
#   기록한 분열(Content Browser 항목이 공백 2·1 로 두 줄)을 실제로 싣고 있고,
#   ini 가 있으면 도크 빌더가 돌지 않으므로 배치는 그 파일이 정한다. 그래서
#   `undocked`·`ghost` 는 legacy 에서 붉을 수 있고 **그것을 초록으로 만드는 것이
#   W3 의 이주다.** 지금 단정으로 적으면 W3 착수 전까지 이 게이트가 내내 붉어
#   도는 세트에 들어갈 수 없다 — 대신 표로 찍어 기준선으로 남긴다.
#
#   단정하는 것은 **어느 ini 를 물려도 참이어야 하는 것**뿐이다: 에디터가 뜨고,
#   프레임이 돌고, 내부 API 판이 맞고, 스킨과 배율이 서고, 라벨 글리프가 있다.
#   손상본에서도 같다 — 사용자 파일이 깨졌다고 에디터가 못 뜨면 안 된다.
$fixtureDir = Join-Path $PSScriptRoot 'fixtures/imgui-ini'
Assert (Test-Path -LiteralPath $fixtureDir) "ini fixture directory is missing: $fixtureDir"

$fixtures = @(Get-ChildItem -LiteralPath $fixtureDir -Filter '*.ini' | Sort-Object Name)
Assert ($fixtures.Count -ge 6) `
    "Expected at least 6 imgui.ini fixtures (4 real + 2 damaged), found $($fixtures.Count) in $fixtureDir"

# 개발자의 ini 를 잃지 않는다. 이 게이트는 남의 상태를 빌려 쓰는 것뿐이다.
$iniBackup = Join-Path $Work 'imgui.ini.developer-backup'
if (Test-Path -LiteralPath $iniPath) { Copy-Item -LiteralPath $iniPath -Destination $iniBackup -Force }

$fixtureRows = @()
try {
    foreach ($fixture in $fixtures) {
        $tag = 'fixture-' + [IO.Path]::GetFileNameWithoutExtension($fixture.Name)
        Copy-Item -LiteralPath $fixture.FullName -Destination $iniPath -Force

        $fxRun = Invoke-EditorBatch @('wait 45', 'editor.dock', 'editor.layout', 'editor.theme', 'quit') $tag
        Assert (Test-Path -LiteralPath $fxRun.Result) `
            "Fixture $($fixture.Name): the editor produced no result file (exited $($fxRun.ExitCode)); it likely failed to start on this ini. See $($fxRun.Stdout)"

        $fxResults = @{}
        foreach ($line in @(Get-Content -LiteralPath $fxRun.Result | Where-Object { $_.Trim().Length -gt 0 })) {
            $parsed = $line | ConvertFrom-Json
            $fxResults[$parsed.command] = $parsed
        }

        foreach ($name in @('editor.dock', 'editor.layout', 'editor.theme')) {
            Assert ($fxResults.ContainsKey($name)) "Fixture $($fixture.Name): $name produced no result line"
        }
        $fxDock = $fxResults['editor.dock']
        $fxTheme = $fxResults['editor.theme']
        $fxLayout = $fxResults['editor.layout']

        # 프레임이 돌지 않았으면 아래 값이 전부 0 이라 표가 거짓말을 한다.
        Assert ($fxDock.code -ne 'editor.dock.no_frame') `
            "Fixture $($fixture.Name): no display frame ran, so the chrome snapshot is empty. The editor started but never presented."

        # ini 와 무관하게 참이어야 하는 것들.
        Assert ($fxDock.data.versionKnown -eq $true) `
            "Fixture $($fixture.Name): ImGui version is not the pinned one (num=$($fxDock.data.imguiVersionNum))"
        Assert ($fxDock.data.centralNodes -le 1) `
            "Fixture $($fixture.Name): more than one central dock node ($($fxDock.data.centralNodes))"
        Assert ($fxTheme.data.styleApplied -eq $true) `
            "Fixture $($fixture.Name): the editor skin was not applied"
        Assert ($fxTheme.data.scaleMatches -eq $true) `
            "Fixture $($fixture.Name): scale source diverged (style.FontScaleMain=$($fxTheme.data.fontScaleMain) preference=$($fxTheme.data.preferenceScale))"
        Assert ($fxTheme.data.missingGlyphLabels -eq 0) `
            "Fixture $($fixture.Name): $($fxTheme.data.missingGlyphLabels) window label(s) draw as boxes"
        Assert ($fxLayout.data.iniEntries -ge 1) `
            "Fixture $($fixture.Name): the ini parsed to zero window entries; the fixture or the parser is broken"

        $fixtureRows += [pscustomobject]@{
            fixture  = $fixture.Name
            entries  = $fxLayout.data.iniEntries
            matched  = $fxLayout.data.matched
            docked   = $fxDock.data.dockedWindows
            undocked = $fxDock.data.undockedSlots
            ghost    = $fxDock.data.ghostTabs
            nodes    = $fxDock.data.nodes
        }
    }
}
finally {
    # 손상본을 개발자 자리에 남겨 두고 나가지 않는다.
    if (Test-Path -LiteralPath $iniBackup) { Copy-Item -LiteralPath $iniBackup -Destination $iniPath -Force }
    elseif (Test-Path -LiteralPath $iniPath) { Remove-Item -LiteralPath $iniPath -Force }
}

Write-Host ''
Write-Host 'imgui.ini fixture baseline (W0 후반 — undocked/ghost 를 0 으로 만드는 것이 W3 의 이주다):'
# ★ entries/matched 는 **fixture 파일의 값이 아니라 에디터가 그것을 읽고 다시 쓴
#   뒤의 값**이다. Debug 45프레임이 약 9초이고 `io.IniSavingRate` 가 5초라, 명령이
#   파일을 읽을 때는 이미 ImGui 가 덮어쓴 뒤다. 이주 관점에서는 오히려 이쪽이
#   묻고 싶은 값이다 — "이 낡은 파일을 물리면 무엇으로 바뀌는가".
#   반대로 docked/undocked/ghost/nodes 는 **fixture 가 정한 배치**를 본다. 배치는
#   적재 시점에 정해지고 그 뒤 다시 쓰기가 바꾸지 않기 때문이다.
$fixtureRows | Format-Table -AutoSize | Out-String | Write-Host

"editor chrome observation OK — fixtures=$($fixtures.Count), dock nodes=$($dock.data.nodes) leaf=$($dock.data.leafNodes) docked=$($dock.data.dockedWindows) central=$($dock.data.centralNodes), style colors=$($theme.data.colors) differing=$($theme.data.differing) scale=$($theme.data.fontScaleMain), ini entries=$($layout.data.iniEntries) matched=$($layout.data.matched), imgui=$($dock.data.imguiVersionNum), checks=$($script:checks)"
exit 0
