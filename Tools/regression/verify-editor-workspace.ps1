[CmdletBinding()]
param(
    [string]$Exe = (Join-Path $PSScriptRoot '../../Bin/x64-Debug/Editor/CreatorEditor.exe'),
    [string]$Work = (Join-Path $env:TEMP 'creator-editor-workspace')
)
# PHASE 21 W0 전반 관측 + W3 workspace 저장·이주·복구 판정.
#
# 선언 배선 게이트(`verify-editor-declaration-wiring.ps1`)와 **파일을 나눈 이유**는
# 뜨는 상태가 다르기 때문이다. 그쪽은 개발자의 평소 상태에서 한 번 띄워 선언 표를
# 본다. 이쪽은 ini fixture 여섯 벌과 재시작·손상본을 태우므로 준비된 상태로
# 에디터를 여러 번 띄운다. 한 파일에 섞으면 선언 검사가 남의 이유로 에디터를
# 다시 띄우게 된다.
#
# ★ **W3 이후로 이 게이트는 개발자의 상태를 빌리지 않는다.** `EditorWorkspaceStore`
#   가 `CREATOR_EDITOR_WORKSPACE_DIR` 와 `CREATOR_EDITOR_LEGACY_INI` 를 읽으므로
#   실행마다 $Work 아래 빈 폴더를 하나 주고 그 안에서만 논다. 이전 판은 개발자의
#   `Saved/Config/imgui.ini` 를 백업했다 되돌리는 식이었고, 그 사이에 게이트가
#   죽으면 손상본이 개발자 자리에 남았다.
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
# ③ `editor.layout` — 저장 파일의 창 항목을 선언 표와 맞댄다. 같은 창 이름이 두 번
#    있는 것이 §1.4 가 기록한 사고의 꼴이다(공백 하나가 달라 Content Browser
#    항목이 둘로 갈렸다).
#
# 그 위에 W3 판정 여섯을 얹는다 — 기본 저장 · 재시작 왕복 · 패널 여닫기 영속 ·
# Reset · legacy 이주 넷 · 손상 복구 셋.
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

# 시나리오 하나 = 빈 폴더 하나. workspace 파일도 legacy ini 도 이 안에만 생긴다.
function New-Scenario([string]$Name) {
    $dir = Join-Path $Work "scenario/$Name"
    if (Test-Path -LiteralPath $dir) { Remove-Item -LiteralPath $dir -Recurse -Force }
    New-Item -ItemType Directory -Force -Path $dir | Out-Null
    return [IO.Path]::GetFullPath($dir)
}

function Invoke-EditorBatch([string[]]$Commands, [string]$Tag, [string]$Scenario) {
    $scriptPath = Join-Path $Work "$Tag.txt"
    $resultPath = Join-Path $Work "$Tag.jsonl"
    $stdoutPath = Join-Path $Work "$Tag.out"
    $stderrPath = Join-Path $Work "$Tag.err"
    foreach ($file in @($resultPath, $stdoutPath, $stderrPath)) {
        if (Test-Path -LiteralPath $file) { Remove-Item -LiteralPath $file }
    }
    Set-Content -LiteralPath $scriptPath -Encoding UTF8 -Value $Commands

    # 자식은 부모의 환경을 물려받는다. 시나리오 폴더를 주지 않는 호출은 없다 —
    # legacy 는 그 폴더 안의 `legacy.ini` 로 고정하고, 이주를 태우고 싶은
    # 시나리오만 그 자리에 fixture 를 미리 복사해 둔다. 없으면 없는 것이다.
    $priorWorkspace = $env:CREATOR_EDITOR_WORKSPACE_DIR
    $priorLegacy = $env:CREATOR_EDITOR_LEGACY_INI
    $env:CREATOR_EDITOR_WORKSPACE_DIR = $Scenario
    $env:CREATOR_EDITOR_LEGACY_INI = (Join-Path $Scenario 'legacy.ini')
    try {
        $proc = Start-Process -FilePath $Exe -WorkingDirectory (Split-Path $Exe) -WindowStyle Hidden -PassThru `
            -ArgumentList @('--script', ('"' + $scriptPath + '"'), '--result-format', 'jsonl',
                            '--result-file', ('"' + $resultPath + '"')) `
            -RedirectStandardOutput $stdoutPath -RedirectStandardError $stderrPath
        if (-not $proc.WaitForExit(300000)) {
            $proc.Kill()
            throw "Editor did not exit within 300s while running the workspace gate ($Tag)."
        }
    }
    finally {
        $env:CREATOR_EDITOR_WORKSPACE_DIR = $priorWorkspace
        $env:CREATOR_EDITOR_LEGACY_INI = $priorLegacy
    }

    $results = @{}
    if (Test-Path -LiteralPath $resultPath) {
        foreach ($line in @(Get-Content -LiteralPath $resultPath | Where-Object { $_.Trim().Length -gt 0 })) {
            $parsed = $line | ConvertFrom-Json
            $results[$parsed.command] = $parsed
        }
    }
    return [pscustomobject]@{
        Tag      = $Tag
        ExitCode = $proc.ExitCode
        Result   = $resultPath
        Stdout   = $stdoutPath
        Data     = $results
    }
}

# JSON 키에 `#` 가 들어 있어 점 표기로는 못 읽는다(`###Editor.Hierarchy`).
function Get-Panel($Status, [string]$Id) {
    $property = $Status.data.panels.psobject.Properties[$Id]
    if ($null -eq $property) { throw "Workspace status carries no panel entry for $Id" }
    return $property.Value
}

$workspaceFileName = 'active.workspace'
$fixtureDir = Join-Path $PSScriptRoot 'fixtures/imgui-ini'
Assert (Test-Path -LiteralPath $fixtureDir) "ini fixture directory is missing: $fixtureDir"

# ── 기본 workspace (관측 ①②③ + W3-①) ──────────────────────────────────
#
# 빈 폴더에서 시작한다. workspace 도 legacy 도 없으므로 기본 배치가 서고 그것이
# 저장돼야 한다. 이전 판은 개발자의 ini 가 없으면 워밍업으로 한 번 더 돌렸는데,
# 이제 저장의 주인이 스토어라 첫 프레임 뒤에 곧바로 생긴다.
$home1 = New-Scenario 'home'
$run = Invoke-EditorBatch @('wait 45', 'editor.dock', 'editor.theme', 'editor.layout', 'editor.workspace', 'quit') 'workspace' $home1

# ★ 종료 코드 단정은 맨 뒤다(선언 게이트와 같은 이유). 배치 러너는 명령 하나가
#   실패하면 4로 나가는데, 앞에서 그것을 보면 아래 단정이 한 번도 돌지 않아
#   "exited 4" 한 줄만 남는다. 그러면 붉은 줄이 고칠 곳을 가리키지 못한다.
Assert (Test-Path -LiteralPath $run.Result) "No result file produced at $($run.Result) (batch exited $($run.ExitCode); see $($run.Stdout))"
$results = $run.Data

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

# W4 가 중앙 노드를 세웠으므로 이제 **존재까지** 단정한다. 세우기 전에는 실측이
# 0 이었고, 그 사실이 `DockBuilderGetCentralNode` 를 널로 만들어 중앙을 특별히
# 다루는 것들(탭 바 억제 · PassthruCentralNode 투명 배경)이 조용히 닿지 않았다.
Assert ($dock.data.centralNodes -eq 1) "Expected exactly one central dock node, found $($dock.data.centralNodes)"

# 빈 트리 위에서 도는 부재 단정을 막는다. 개수를 못 박지는 않고 하한만 둔다.
Assert ($dock.data.nodes -ge 3) "Only $($dock.data.nodes) dock nodes; the probe likely read nothing"
Assert ($dock.data.leafNodes -ge 2) "Only $($dock.data.leafNodes) leaf dock nodes; the layout did not split"
# W4 전에는 이 수가 중앙 창 둘만 셌다 — 감사가 패널 전체를 도크 면제로 두고
# 있었기 때문이다(빌더는 면제하지 않는다). 면제를 걷어 실제 도크된 창을 센다.
Assert ($dock.data.dockedWindows -ge 4) "Only $($dock.data.dockedWindows) windows are docked; the gate would be vacuous"

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
Assert ($theme.data.dpiMatches -eq $true) `
    "DPI source diverged: OS=$($theme.data.windowDpiScale) viewport=$($theme.data.viewportDpiScale) font=$($theme.data.fontScaleDpi)"
Assert ($theme.data.geometryMatches -eq $true) 'Theme geometry differs from user scale times OS DPI'
Assert ($theme.data.themeMappingMatches -eq $true) 'Applied ImGui colors differ from semantic theme tokens'

# 선언된 창 라벨의 아이콘이 폰트 블롭에 실제로 있는가. EditorIcons.h 에
# 정의만 있고 블롭에 글리프가 없으면 네모 한 칸이 조용히 그려진다.
Assert ($theme.data.missingGlyphLabels -eq 0) `
    "$($theme.data.missingGlyphLabels) window label(s) reference a glyph missing from the icon font; they draw as boxes. See the [AUDIT] lines for which window, and check the semantic role mapping and bundled Material Symbols subset"

# ── 폰트 (PHASE 21 W1) ───────────────────────────────────────────────────
#
# 폰트 파일이 없으면 에디터가 죽었다. 죽은 자리는 본문 폰트가 아니라 그 다음의
# 아이콘 폰트였다 — `MergeMode` 가 빈 폰트 목록의 끝을 읽었다(2026-09-11
# ACCESS_VIOLATION 실측, `EditorFontResources.h` 머리에 역추적 전체가 있다).
#
# 그래서 여기서 셋을 본다. ① 본문 폰트가 실제로 섰는가 ② 아이콘이 그 폰트에
# 병합됐는가 ③ 후보가 하나도 없을 때 해상이 빈 것을 돌려주는가.
#
# ③ 이 필요한 이유: 실제 시스템 폰트를 지울 수 없어 fallback 경로 전체를 도는
# 세트에서 재현할 수 없다. 해상 함수의 negative 경로만이라도 살아 있는 에디터에서
# 태운다 — 이 저장소는 "게이트가 도는 세트에 없으면 없는 것" 으로 두 번 데었다.
Assert ($theme.data.fonts -ge 1) `
    "No font load was reported; the probe or editor::fonts bookkeeping is unwired"
Assert ($theme.data.bodyFontPresent -eq $true) `
    "The body font did not stand up. Text renders with nothing; see the fontRole table above"
Assert ($theme.data.iconFontMerged -eq $true) `
    "The icon font was not merged into the body font; icon labels draw as boxes"
Assert ($theme.data.fontFallbackProbeOk -eq $true) `
    "resolve_font_path returned a path for candidates that do not exist; the fallback chain is broken"
if ($theme.data.bodyFontUsedFallback -eq $true) {
    # 틀린 것이 아니다 — 후보가 다 없어 ImGui 기본 폰트로 내려간 것이다.
    # 붉히지 않고 적는다. 이 기계에서 이 줄이 보이면 후보 목록을 늘려야 한다.
    Write-Host "  [NOTE] 본문 폰트가 ImGui 기본 폰트로 내려갔다 — 후보가 하나도 없다"
}

Assert ($theme.data.colors -ge 40) "Only $($theme.data.colors) style colors captured; the probe likely read nothing"
Assert ($theme.data.scalars -ge 10) "Only $($theme.data.scalars) style scalars captured; the probe likely read nothing"
Assert ($theme.data.differing -ge 10) "Only $($theme.data.differing) colors differ from the ImGui default"

# ── ③ 레이아웃과 저장 파일 ──────────────────────────────────────────────
Assert ($results.ContainsKey('editor.layout')) 'editor.layout produced no result line'
$layout = $results['editor.layout']
Assert ($layout.status -eq 'succeeded') "editor.layout failed: $($layout.message)"
Assert ($layout.data.clean -eq $true) "Layout audit is dirty: $($layout.message)"
Assert ($layout.data.duplicateEntries -eq 0) `
    "Same window name appears twice in the workspace layout: $($layout.data.duplicateEntries)"
Assert ($layout.data.orphanEntries -eq 0) `
    "The saved layout holds $($layout.data.orphanEntries) entr(ies) no declaration claims"

# 저장의 주인이 바뀌었다. W3 이후 창 배치는 `imgui.ini` 가 아니라 versioned
# workspace 파일에 들어가고 ImGui backend 의 자체 저장은 꺼져 있다
# (`EditorWorkspaceStore` 생성자의 `IniFilename=nullptr`). 여기가 어긋나면
# 배치가 조용히 다른 파일로 저장되고, 사용자는 레이아웃이 저장되지 않는 것으로 본다.
Assert ($layout.data.iniPath -like "*$workspaceFileName") `
    "The editor is not reporting the workspace file as its layout store: $($layout.data.iniPath)"
Assert ($layout.data.iniPath.StartsWith($home1, [StringComparison]::OrdinalIgnoreCase)) `
    "CREATOR_EDITOR_WORKSPACE_DIR did not take; the gate is writing outside its scenario folder: $($layout.data.iniPath)"

Assert ($layout.data.iniExists -eq $true) `
    "The workspace file does not exist after the first run: $($layout.data.iniPath)"
Assert ($layout.data.iniEntries -ge 5) "Only $($layout.data.iniEntries) window entries in the workspace; the parse likely broke"
Assert ($layout.data.matched -ge 3) "Only $($layout.data.matched) entries match a declared window; the name contract likely broke"

# ── W3-① 기본 배치가 저장됐는가 ─────────────────────────────────────────
Assert ($results.ContainsKey('editor.workspace')) 'editor.workspace produced no result line'
$workspace = $results['editor.workspace']
Assert ($workspace.status -eq 'succeeded') "editor.workspace failed: $($workspace.message)"
Assert ($workspace.data.ready -eq $true) "The workspace never completed a save: $($workspace.data.error)"
Assert ($workspace.data.error -eq '') "Workspace reported an error on a clean start: $($workspace.data.error)"
Assert ($workspace.data.migrated -eq $false) 'A clean scenario folder reported a legacy migration'
Assert ($workspace.data.recovered -eq $false) 'A clean scenario folder reported a recovery'

$homeFile = Join-Path $home1 $workspaceFileName
Assert (Test-Path -LiteralPath $homeFile) "No workspace file was written at $homeFile"
$homeBytes = [IO.File]::ReadAllText($homeFile)
Assert ($homeBytes.StartsWith('CreatorWorkspace 1')) 'The workspace file does not carry the versioned header'
Assert ($homeBytes.Contains('--ini--')) 'The workspace file carries no layout section'

# 종료 코드는 여기서 본다 — 위의 단정이 전부 돌고 난 뒤다.
Assert ($run.ExitCode -eq 0) "Workspace gate batch exited $($run.ExitCode) though every check passed; see $($run.Stdout)"

# ── W3-② save → restart → load 가 동일한가 ─────────────────────────────
#
# 계획서 §W3 판정의 두 번째 문장이다. 같은 폴더로 한 번 더 띄운다. 스토어는
# 바이트가 같으면 쓰지 않으므로(`Save` 의 `bytes!=m_lastBytes`), 왕복이 손실
# 없이 돌았으면 파일이 한 자리도 바뀌지 않는다.
$restart = Invoke-EditorBatch @('wait 45', 'editor.dock', 'editor.workspace', 'quit') 'restart' $home1
Assert ($restart.Data.ContainsKey('editor.workspace')) 'Restart run produced no editor.workspace line'
$restartStatus = $restart.Data['editor.workspace']
Assert ($restartStatus.data.recovered -eq $false) "Restart rejected the file this gate just wrote: $($restartStatus.data.error)"
Assert ($restartStatus.data.migrated -eq $false) 'Restart re-ran the legacy migration on a file it already owns'
$restartDock = $restart.Data['editor.dock']
Assert ($restartDock.data.nodes -eq $dock.data.nodes) `
    "Dock node count changed across restart: $($dock.data.nodes) -> $($restartDock.data.nodes)"
Assert ($restartDock.data.leafNodes -eq $dock.data.leafNodes) `
    "Leaf node count changed across restart: $($dock.data.leafNodes) -> $($restartDock.data.leafNodes)"
Assert ($restartDock.data.dockedWindows -eq $dock.data.dockedWindows) `
    "Docked window count changed across restart: $($dock.data.dockedWindows) -> $($restartDock.data.dockedWindows)"
Assert ($restartDock.data.undockedSlots -eq 0) "Restart lost a dock slot: $($restartDock.data.undockedSlots)"
$restartBytes = [IO.File]::ReadAllText($homeFile)
Assert ($restartBytes -eq $homeBytes) `
    "save -> restart -> load did not round-trip; the workspace file changed with no user action. Compare $homeFile against $($restart.Stdout)"
Assert ($restart.ExitCode -eq 0) "Restart batch exited $($restart.ExitCode); see $($restart.Stdout)"

# ── W3-③ 패널 여닫기가 재시작을 넘는가 ─────────────────────────────────
$panelId = '###Editor.Hierarchy'
$closed = Invoke-EditorBatch @('wait 30', "editor.workspace close $panelId", 'wait 30', 'editor.workspace', 'quit') 'panel-close' $home1
$closedStatus = $closed.Data['editor.workspace']
Assert ($closedStatus.status -eq 'succeeded') "editor.workspace close failed: $($closedStatus.message)"
Assert ((Get-Panel $closedStatus $panelId) -eq $false) "Closing $panelId did not take effect in the same session"
Assert ($closed.ExitCode -eq 0) "Panel close batch exited $($closed.ExitCode); see $($closed.Stdout)"

$reopened = Invoke-EditorBatch @('wait 45', 'editor.workspace', "editor.workspace open $panelId", 'wait 30', 'editor.workspace', 'quit') 'panel-reopen' $home1
$reopenLines = @(Get-Content -LiteralPath $reopened.Result | Where-Object { $_.Trim().Length -gt 0 } | ForEach-Object { $_ | ConvertFrom-Json } | Where-Object { $_.command -eq 'editor.workspace' })
Assert ($reopenLines.Count -ge 3) "Expected three editor.workspace lines in the reopen run, found $($reopenLines.Count)"
Assert ((Get-Panel $reopenLines[0] $panelId) -eq $false) `
    "A closed panel came back open after restart; the panel state is not persisted with the layout"
Assert ((Get-Panel $reopenLines[2] $panelId) -eq $true) "Reopening $panelId did not take effect"
Assert ($reopened.ExitCode -eq 0) "Panel reopen batch exited $($reopened.ExitCode); see $($reopened.Stdout)"

# ── W3-④ Reset Layout 은 되돌리기 전에 백업한다 ────────────────────────
$reset = Invoke-EditorBatch @('wait 30', 'editor.workspace reset', 'wait 30', 'editor.workspace', 'editor.dock', 'quit') 'reset' $home1
$resetStatus = $reset.Data['editor.workspace']
Assert ($resetStatus.status -eq 'succeeded') "editor.workspace reset failed: $($resetStatus.message)"
Assert ($resetStatus.data.error -eq '') "Reset reported an error: $($resetStatus.data.error)"
$resetBackups = @(Get-ChildItem -LiteralPath $home1 -Filter "$workspaceFileName.before-reset*" -ErrorAction SilentlyContinue)
Assert ($resetBackups.Count -ge 1) "Reset Layout replaced the workspace without leaving a backup in $home1"
Assert ((Get-Panel $resetStatus $panelId) -eq $true) 'Reset did not restore the default panel set'
$resetDock = $reset.Data['editor.dock']
Assert ($resetDock.data.undockedSlots -eq 0) "Reset left a window without a dock slot: $($resetDock.data.undockedSlots)"
Assert ($resetDock.data.ghostTabs -eq 0) "Reset left ghost tabs: $($resetDock.data.ghostTabs)"
Assert ($reset.ExitCode -eq 0) "Reset batch exited $($reset.ExitCode); see $($reset.Stdout)"

# ── W4-① 중앙 Host 는 늫을 수 없고 모드를 든다 ────────────────
#
# 계획서 W4 판정의 첫 번째 문장이다. Host 를 늫거나 ToolPanel 로 대신할 수 없어야
# 한다 — 늫히면 다시 여는 자리가 없다(§4.2). 여기는 언제나 거짓인 `closable` 을 소스로
# 도 볼 수 있지만, 살아 있는 에디티의 close 경로가 그 것을 실제로 거절하는지를 태운다.
$hostId = '###Editor.Viewport'
$previewId = '###Editor.GamePreview'
$viewportDir = New-Scenario 'viewport'
$vp = Invoke-EditorBatch @('wait 45', 'editor.viewport', "editor.workspace close $hostId",
    'wait 25', 'editor.workspace', 'editor.dock', 'quit') 'viewport-host' $viewportDir
Assert ($vp.Data.ContainsKey('editor.viewport')) "editor.viewport produced no result line (exited $($vp.ExitCode); see $($vp.Stdout))"
$central = $vp.Data['editor.viewport']
Assert ($central.status -eq 'succeeded') "editor.viewport failed: $($central.message)"
Assert ($central.data.hostPresent -eq $true) 'The central viewport host never drew a frame'
Assert ($central.data.mode -eq 'scene') "A fresh workspace should start in scene mode, got $($central.data.mode)"
# 거절은 명령의 실패가 아니라 workspace 상태의 `error` 로 나온다 — W3 가 세운
# 출구다(reset 경로가 같은 자리를 봈러 말한다). 바뀜 것은 없는지까지 함께 본다.
# 그리고 그 `error` 는 close 명령 **자신의** 줄에 없다 — 명령은 우정함에 넣고
# 그 직전의 상태를 돌려주며, 거절은 다음 표시 프레임의 `BeforeFrame` 이 한다. 뒤에 잎는
# `editor.workspace` 한 줄이 답을 들고, 해시표는 같은 이름의 마지리 줄을 들기에 그것을 본다.
$closeAttempt = $vp.Data['editor.workspace']
Assert ($closeAttempt.data.error -ne '') 'Closing the central viewport host was accepted; it must be refused'
Assert ((Get-Panel $closeAttempt $hostId) -eq $true) 'The central viewport host was actually closed'
$vpDock = $vp.Data['editor.dock']
Assert ($vpDock.data.centralNodes -eq 1) "The central node did not survive the close attempt ($($vpDock.data.centralNodes))"
Assert ($vpDock.data.undockedSlots -eq 0) "A window lost its dock slot: $($vpDock.data.undockedSlots)"
Assert ($vpDock.data.ghostTabs -eq 0) "The close attempt left ghost tabs: $($vpDock.data.ghostTabs)"
Assert ($vp.ExitCode -eq 0) "Viewport host batch exited $($vp.ExitCode); see $($vp.Stdout)"

# ── W4-② 보이지 않는 표시 타깃은 만들지 않는다 ────────────────
#
# 판정의 두 번째 문장. Scene 모드이고 Game Preview 가 늫혀 있으면 게임 뷰는 밀봉되지
# 않아야 하고, **그 사실이 밖에서 세어지는가**로 재다. 전엔 "지금 무엇이 보이는가" 에
# 답할 자리가 없어 제작자가 더 따질 것 없이 둘 다 밀봉했다.
Assert ($central.data.gameTarget -eq $false) 'Scene mode with no preview still demands the game target'
Assert ($central.data.suppressedGameViews -gt 0) `
    "The game view was sealed on every frame even though nothing displays it (suppressed=$($central.data.suppressedGameViews))"
Assert ($central.data.publishedFrames -gt 0) 'The host published no demand frames'
Assert ($central.data.sceneModeFrames -gt 0) 'No frame was published as scene mode'
Assert ($central.data.gamePreviewFrames -eq 0) `
    "Game Preview was never opened yet drew $($central.data.gamePreviewFrames) frame(s)"
# 에디터 타깃은 대칭으로 끄지 않았다 — 타깃의 생성·회수를 두 백엔드에서
# 검증한 뒤로 밀렸기 때문이다(ViewportHostWindow.cpp ★). 그 사실을 여기 박아도어,
# 나중에 대칭으로 거듀면 이 줄이 붉어지고 그랬면 결정이 바뀐 것을 적으면 된다.
Assert ($central.data.editorTarget -eq $true) 'Scene mode does not demand the editor target'
Assert ($central.data.suppressedEditorViews -eq 0) `
    "The editor view was skipped $($central.data.suppressedEditorViews) time(s); W4 deliberately keeps it"

$preview = Invoke-EditorBatch @('wait 30', "editor.workspace open $previewId", 'wait 45',
    'editor.viewport', 'quit') 'viewport-preview' $viewportDir
Assert ($preview.Data.ContainsKey('editor.viewport')) "Preview run produced no viewport line; see $($preview.Stdout)"
$withPreview = $preview.Data['editor.viewport']
Assert ($withPreview.data.gamePreviewFrames -gt 0) 'Game Preview was opened but never drew'
Assert ($withPreview.data.mode -eq 'scene') "Opening the preview changed the host mode to $($withPreview.data.mode)"
Assert ($withPreview.data.gameTarget -eq $true) 'An open Game Preview does not demand the game target'
Assert ($preview.ExitCode -eq 0) "Preview batch exited $($preview.ExitCode); see $($preview.Stdout)"

# ── W4-③ 표시 모드가 재시작을 넘는다 ─────────────────────────
#
# 모드는 workspace 의 `viewport` 필드가 든다. 상태도 바뀌었지만 스키마는 그대로다 —
# 예전에는 그 값이 도클 노드의 역 탭 이름이었고 지금은 Host 의 모드 토큰이다.
# 옆 파일의 `###Editor.Game` 이 그대로 Game 모드로 읽힌다.
#
# 모드 전환을 CLI 가 할 수 있게 만든 이유가 이것이다. 클릭만이 모드를 바꿀 수
# 있으면 이 줄은 기본값을 두 번 읽고 초록이 되었을 것이다.
$toGame = Invoke-EditorBatch @('wait 30', 'editor.viewport game', 'wait 30', 'editor.viewport',
    'wait 20', 'quit') 'viewport-to-game' $viewportDir
$gameMode = $toGame.Data['editor.viewport']
Assert ($gameMode.data.mode -eq 'game') "editor.viewport game did not switch the host mode (still $($gameMode.data.mode))"
Assert ($gameMode.data.gameTarget -eq $true) 'Game mode does not demand the game target'
Assert ($gameMode.data.gameModeFrames -gt 0) 'No frame was published as game mode'
Assert ($toGame.ExitCode -eq 0) "Mode switch batch exited $($toGame.ExitCode); see $($toGame.Stdout)"

$modeFile = [IO.File]::ReadAllText((Join-Path $viewportDir $workspaceFileName))
Assert ($modeFile -match 'viewport "###Editor\.Game"') `
    "The workspace file did not record the game mode; the viewport line is missing or still Scene"

$afterRestart = Invoke-EditorBatch @('wait 45', 'editor.viewport', 'editor.dock', 'quit') 'viewport-restart' $viewportDir
$restored = $afterRestart.Data['editor.viewport']
Assert ($restored.data.mode -eq 'game') `
    "The display mode did not survive a restart; came back as $($restored.data.mode)"
Assert ($restored.data.sceneModeFrames -eq 0) `
    "The restored mode flipped to scene for $($restored.data.sceneModeFrames) frame(s) before settling"
Assert ($afterRestart.Data['editor.dock'].data.centralNodes -eq 1) `
    'The restored game mode lost the central dock node'
Assert ($afterRestart.ExitCode -eq 0) "Restart batch exited $($afterRestart.ExitCode); see $($afterRestart.Stdout)"

# ── W3-⑤ legacy ini 이주 (계획서 §1.4 의 실물 넷) ───────────────────────
#
# 실물 fixture 를 시나리오 폴더의 `legacy.ini` 자리에 놓고 빈 workspace 로
# 띄운다. W0 후반에는 여기서 `undocked`/`ghost` 를 **기록만** 했다 — 그것을
# 0 으로 만드는 것이 W3 의 이주였고, 이제 단정한다.
$realFixtures = @(Get-ChildItem -LiteralPath $fixtureDir -Filter '*.ini' |
    Where-Object { -not $_.Name.StartsWith('damaged-') } | Sort-Object Name)
$damagedFixtures = @(Get-ChildItem -LiteralPath $fixtureDir -Filter 'damaged-*.ini' | Sort-Object Name)
Assert ($realFixtures.Count -ge 4) "Expected at least 4 real imgui.ini fixtures, found $($realFixtures.Count)"
Assert ($damagedFixtures.Count -ge 2) "Expected at least 2 damaged fixtures, found $($damagedFixtures.Count)"

$fixtureRows = @()
foreach ($fixture in $realFixtures) {
    $name = [IO.Path]::GetFileNameWithoutExtension($fixture.Name)
    $dir = New-Scenario "legacy-$name"
    $legacy = Join-Path $dir 'legacy.ini'
    Copy-Item -LiteralPath $fixture.FullName -Destination $legacy -Force
    $original = [IO.File]::ReadAllBytes($legacy)

    $fx = Invoke-EditorBatch @('wait 45', 'editor.dock', 'editor.layout', 'editor.theme', 'editor.workspace', 'quit') "legacy-$name" $dir
    Assert ($fx.Data.ContainsKey('editor.workspace')) "Fixture $($fixture.Name): the editor produced no workspace line (exited $($fx.ExitCode)); see $($fx.Stdout)"
    $fxWorkspace = $fx.Data['editor.workspace']
    $fxDock = $fx.Data['editor.dock']
    $fxLayout = $fx.Data['editor.layout']
    $fxTheme = $fx.Data['editor.theme']

    Assert ($fxDock.code -ne 'editor.dock.no_frame') `
        "Fixture $($fixture.Name): no display frame ran, so the chrome snapshot is empty. The editor started but never presented."
    Assert ($fxWorkspace.data.migrated -eq $true) `
        "Fixture $($fixture.Name): a legacy ini was present but no migration was reported ($($fxWorkspace.message))"
    Assert ($fxWorkspace.data.recovered -eq $false) `
        "Fixture $($fixture.Name): migration fell through to recovery: $($fxWorkspace.data.error)"

    # 사용자 파일은 잃지 않는다 — 원본이 그대로고, 그 사본이 따로 남는다.
    Assert ([Linq.Enumerable]::SequenceEqual($original, [IO.File]::ReadAllBytes($legacy))) `
        "Fixture $($fixture.Name): the migration rewrote the user's own ini"
    $backups = @(Get-ChildItem -LiteralPath $dir -Filter 'legacy.ini.pre-workspace-v1*' -ErrorAction SilentlyContinue)
    Assert ($backups.Count -ge 1) "Fixture $($fixture.Name): migration left no pre-workspace-v1 backup"
    Assert ([Linq.Enumerable]::SequenceEqual($original, [IO.File]::ReadAllBytes($backups[0].FullName))) `
        "Fixture $($fixture.Name): the backup does not hold the original bytes"
    Assert (Test-Path -LiteralPath (Join-Path $dir $workspaceFileName)) `
        "Fixture $($fixture.Name): migration produced no workspace file"

    # W3 의 이주가 값을 갚는 자리. 옛 이름이 남아 있으면 여기가 붉어진다.
    Assert ($fxDock.data.undockedSlots -eq 0) `
        "Fixture $($fixture.Name): $($fxDock.data.undockedSlots) window(s) are drawn without a dock slot after migration"
    Assert ($fxDock.data.ghostTabs -eq 0) `
        "Fixture $($fixture.Name): $($fxDock.data.ghostTabs) dock tab(s) still carry undeclared legacy names"
    Assert ($fxLayout.data.duplicateEntries -eq 0) `
        "Fixture $($fixture.Name): the migrated layout still holds $($fxLayout.data.duplicateEntries) duplicate window entries"
    # 옛 이름이 한 줄이라도 남았는지는 **여기서만** 붉어진다. 살아 있는 도크 탭이
    # 아니라 아무도 만들지 않는 항목으로 남기 때문에 ghost/undocked 로는 안 잡힌다 —
    # alias 하나를 빼는 변이가 그 셋을 모두 통과했고 이 줄만 붉었다.
    Assert ($fxLayout.data.orphanEntries -eq 0) `
        "Fixture $($fixture.Name): migration left $($fxLayout.data.orphanEntries) entr(ies) the declaration does not know; a legacy name survived"
    # ★ 이 줄이 제품 결함 하나를 잡았다. W4 가 중앙 표시를 도클 바더 안에 두었고 바더는
    #   ini 가 없을 때만 돌다. `CentralNode` 는 ini 에 저장되는 local flag 라 W4 전에
    #   쓰인 파일에는 그 뱄트가 없고, 이주는 없는 뱄트를 지어낼 수 없다 — 여기 legacy fixture
    #   넷이 붉어지기 전에는 모를 수 없었다. 본 배지만 보는 위의 같은 단정은 초록이었다.
    Assert ($fxDock.data.centralNodes -eq 1) `
        "Fixture $($fixture.Name): expected one central dock node, found $($fxDock.data.centralNodes)"
    Assert ($fxTheme.data.styleApplied -eq $true) "Fixture $($fixture.Name): the editor skin was not applied"
    Assert ($fxTheme.data.missingGlyphLabels -eq 0) `
        "Fixture $($fixture.Name): $($fxTheme.data.missingGlyphLabels) window label(s) draw as boxes"
    Assert ($fxLayout.data.iniEntries -ge 1) `
        "Fixture $($fixture.Name): the migrated layout parsed to zero window entries"
    Assert ($fx.ExitCode -eq 0) "Fixture $($fixture.Name): batch exited $($fx.ExitCode); see $($fx.Stdout)"

    $fixtureRows += [pscustomobject]@{
        fixture  = $fixture.Name
        state    = 'migrated'
        entries  = $fxLayout.data.iniEntries
        matched  = $fxLayout.data.matched
        docked   = $fxDock.data.dockedWindows
        undocked = $fxDock.data.undockedSlots
        ghost    = $fxDock.data.ghostTabs
        nodes    = $fxDock.data.nodes
    }
}

# ── W3-⑥ 손상 복구 — 사용자 파일을 잃지 않고 기본 preset 으로 ──────────
foreach ($fixture in $damagedFixtures) {
    $name = [IO.Path]::GetFileNameWithoutExtension($fixture.Name)
    $dir = New-Scenario "damaged-$name"
    $legacy = Join-Path $dir 'legacy.ini'
    Copy-Item -LiteralPath $fixture.FullName -Destination $legacy -Force
    $original = [IO.File]::ReadAllBytes($legacy)

    $fx = Invoke-EditorBatch @('wait 45', 'editor.dock', 'editor.layout', 'editor.theme', 'editor.workspace', 'quit') "damaged-$name" $dir
    Assert ($fx.Data.ContainsKey('editor.workspace')) "Fixture $($fixture.Name): the editor produced no workspace line (exited $($fx.ExitCode)); see $($fx.Stdout)"
    $fxWorkspace = $fx.Data['editor.workspace']
    $fxDock = $fx.Data['editor.dock']
    $fxTheme = $fx.Data['editor.theme']

    Assert ($fxDock.code -ne 'editor.dock.no_frame') `
        "Fixture $($fixture.Name): a damaged layout stopped the editor from presenting a frame"
    Assert ($fxWorkspace.data.recovered -eq $true) `
        "Fixture $($fixture.Name): a damaged layout was accepted instead of being rejected ($($fxWorkspace.message))"
    # 이유는 `error` 가 아니라 `message` 가 든다. `error` 는 마지막 조작의 결과라
    # 복구 직후의 첫 저장이 지운다 — 그 자리에 단정을 걸면 게이트가 타이밍을 잰다.
    Assert ($fxWorkspace.message -like '*restored default layout*') `
        "Fixture $($fixture.Name): recovery reported no reason ($($fxWorkspace.message))"
    Assert ([Linq.Enumerable]::SequenceEqual($original, [IO.File]::ReadAllBytes($legacy))) `
        "Fixture $($fixture.Name): recovery rewrote the user's own file"
    $backups = @(Get-ChildItem -LiteralPath $dir -Filter 'legacy.ini.pre-workspace-v1*' -ErrorAction SilentlyContinue)
    Assert ($backups.Count -ge 1) "Fixture $($fixture.Name): recovery left no backup of the damaged file"

    # 기본 preset 이 실제로 섰는가. 복구가 "아무것도 안 함" 이면 여기가 붉다.
    Assert ($fxDock.data.nodes -ge 3) "Fixture $($fixture.Name): recovery produced only $($fxDock.data.nodes) dock nodes"
    Assert ($fxDock.data.leafNodes -ge 2) "Fixture $($fixture.Name): recovery did not split the layout"
    Assert ($fxDock.data.undockedSlots -eq 0) "Fixture $($fixture.Name): recovery left $($fxDock.data.undockedSlots) undocked window(s)"
    Assert ($fxDock.data.ghostTabs -eq 0) "Fixture $($fixture.Name): recovery left $($fxDock.data.ghostTabs) ghost tab(s)"
    Assert ($fxTheme.data.styleApplied -eq $true) "Fixture $($fixture.Name): the editor skin was not applied"
    Assert (Test-Path -LiteralPath (Join-Path $dir $workspaceFileName)) `
        "Fixture $($fixture.Name): recovery never wrote a replacement workspace"
    Assert ($fx.ExitCode -eq 0) "Fixture $($fixture.Name): batch exited $($fx.ExitCode); see $($fx.Stdout)"

    $fixtureRows += [pscustomobject]@{
        fixture  = $fixture.Name
        state    = 'recovered'
        entries  = $fx.Data['editor.layout'].data.iniEntries
        matched  = $fx.Data['editor.layout'].data.matched
        docked   = $fxDock.data.dockedWindows
        undocked = $fxDock.data.undockedSlots
        ghost    = $fxDock.data.ghostTabs
        nodes    = $fxDock.data.nodes
    }
}

# ── W3-⑦ 손상된 workspace 파일 자체 ────────────────────────────────────
#
# legacy 이주가 아니라 **이미 v1 로 저장된 파일이 깨진** 경우다. 여기가 "사용자
# 파일을 잃지 않는다" 를 가장 곧바로 재는 자리다 — 스토어는 거부한 바이트를
# `.rejected` 로 남긴 뒤에야 기본 배치를 덮어쓴다.
$corruptDir = New-Scenario 'corrupt-workspace'
$corruptFile = Join-Path $corruptDir $workspaceFileName
$corruptBytes = $homeBytes.Substring(0, [Math]::Min(64, $homeBytes.Length)) + "`nwrecked by the gate`n"
[IO.File]::WriteAllText($corruptFile, $corruptBytes)
$corrupt = Invoke-EditorBatch @('wait 45', 'editor.dock', 'editor.workspace', 'quit') 'corrupt-workspace' $corruptDir
$corruptStatus = $corrupt.Data['editor.workspace']
Assert ($corruptStatus.data.recovered -eq $true) `
    "A corrupt workspace file was accepted instead of being rejected ($($corruptStatus.message))"
Assert ($corruptStatus.message -like '*restored default layout*') `
    "Workspace recovery reported no reason ($($corruptStatus.message))"
$rejected = @(Get-ChildItem -LiteralPath $corruptDir -Filter "$workspaceFileName.rejected*" -ErrorAction SilentlyContinue)
Assert ($rejected.Count -ge 1) "Recovery discarded the corrupt workspace without preserving it in $corruptDir"
Assert ([IO.File]::ReadAllText($rejected[0].FullName) -eq $corruptBytes) `
    'The preserved copy does not hold the rejected bytes'
Assert ([IO.File]::ReadAllText($corruptFile) -ne $corruptBytes) 'Recovery never replaced the corrupt workspace file'
Assert ([IO.File]::ReadAllText($corruptFile).StartsWith('CreatorWorkspace 1')) `
    'The replacement workspace is not a valid versioned file'
$corruptDock = $corrupt.Data['editor.dock']
Assert ($corruptDock.data.nodes -ge 3) "Recovery produced only $($corruptDock.data.nodes) dock nodes"
Assert ($corruptDock.data.undockedSlots -eq 0) "Recovery left $($corruptDock.data.undockedSlots) undocked window(s)"
Assert ($corruptDock.data.ghostTabs -eq 0) "Recovery left $($corruptDock.data.ghostTabs) ghost tab(s)"
Assert ($corrupt.ExitCode -eq 0) "Corrupt workspace batch exited $($corrupt.ExitCode); see $($corrupt.Stdout)"

$fixtureRows += [pscustomobject]@{
    fixture  = 'corrupt active.workspace'
    state    = 'recovered'
    entries  = 0
    matched  = 0
    docked   = $corruptDock.data.dockedWindows
    undocked = $corruptDock.data.undockedSlots
    ghost    = $corruptDock.data.ghostTabs
    nodes    = $corruptDock.data.nodes
}

Write-Host ''
Write-Host 'workspace fixture 판정 (W3 — undocked/ghost 는 이제 기록이 아니라 단정이다):'
$fixtureRows | Format-Table -AutoSize | Out-String | Write-Host

"editor workspace OK — fixtures=$($realFixtures.Count + $damagedFixtures.Count), dock nodes=$($dock.data.nodes) leaf=$($dock.data.leafNodes) docked=$($dock.data.dockedWindows) central=$($dock.data.centralNodes), style colors=$($theme.data.colors) differing=$($theme.data.differing) scale=$($theme.data.fontScaleMain), layout entries=$($layout.data.iniEntries) matched=$($layout.data.matched), imgui=$($dock.data.imguiVersionNum), checks=$($script:checks)"
exit 0
