[CmdletBinding()]
param(
    [string]$Exe = (Join-Path $PSScriptRoot '../../Bin/x64-Release/Editor/CreatorEditor.exe'),
    [string]$Work = (Join-Path $env:TEMP 'creator-browser-splitter'),
    # ★ 배열이 아니라 문자열이다 — `pwsh -File` 은 배열 인자를 한 문자열로 넘긴다.
    [string]$Scales = '1,1.25,1.5,2'
)
# PHASE 21 W2-B1 — Content Browser 분할선을 **운영체제 마우스 메시지**로 끌고, 사용자 배율마다
# 트리·분할선·본문의 같은 프레임 배치를 잰다.
#
# 자극 경로: `PostMessage(WM_MOUSEMOVE/WM_LBUTTONDOWN/UP)` → 창 프로시저 → `WinProcProxy` →
# 표시 스레드의 `ImGui_ImplWin32_WndProcHandler` → `DirectorySplitter`. `editor.nav pointer`
# 주입(ImGui 입력 상태를 직접 덮는다)과 달리 백엔드를 지난다. 2026-09-12 의 추출 탐침과
# 자동화 끌기는 이 경로를 못 밟았다.
#
# ★ 이 검사가 밟은 함정 둘.
#   ① **보내는 스레드의 DPI 인식.** PowerShell 은 DPI 를 모르는 프로세스라 150% 모니터에서
#      (476,1224) 로 보낸 좌표를 ImGui 가 (714,1836) 으로 받았다 — 정확히 1.5 배. 보내기 전에
#      스레드를 모니터별 인식(-4)으로 바꾼다. 9-12 기록의 "끌기에서 폭 변화를 못 봤다" 가
#      이것이었을 수 있다.
#   ② **숨긴 창의 WM_MOUSELEAVE.** 백엔드는 이동마다 `TrackMouseEvent` 를 거는데 실제 커서가
#      창 밖이라 곧바로 떠남이 와서 좌표가 -FLT_MAX 가 된다. 누름을 따로 보내면 떠남 뒤의
#      허공을 누른다. 이동과 누름·뗌을 **틈 없이 연달아** 보내 같은 묶음에 넣는다(ImGui 의
#      입력 흘리기가 누름을 다음 프레임으로 미루므로 그 프레임의 좌표는 유효하다).
#
# 경계 통과는 결과가 아니라 **ImGui 가 본 포인터**(`layout.mouseX/Y/mouseDown`)로 센다 —
# 폭이 바뀌지 않았을 때 메시지가 닿지 않았는지, 닿았는데 위젯이 못 받았는지를 가른다.
#
# 회차는 배율마다 셋이다.
#   survey  — 배치 불변식(트리 끝 = 분할선 시작, 분할선 8u, 본문 = 분할선 끝 + 10u, 적용 폭 =
#             clamp(선호×u, 140u, 가용 − 358u)), 좁은 창에서 본문 최소 340u, 560u 아래에서
#             트리 접힘, 창 크기 100 회 왕복 뒤 배치 그대로. 여기서 얻은 좌표를 다음 회차가 쓴다.
#   drag    — 두 번 눌러 초기화(220) → 방향키 두 번(+16) → 끌기(+40 논리 px). 선호 폭은
#             논리 px 로 저장되므로 배율과 무관한 값이 나와야 한다.
#   persist — drag 의 작업 공간으로 다시 띄워 끈 폭이 돌아오는지, 프로젝트 설정 파일은
#             건드리지 않았는지.
#
# 모니터 배율은 바꿀 수 없으므로 `imguiScale` 로 민다(verify-editor-theme 과 같은 방법).
# 실제 배율 u 는 사용자 배율 × 모니터 DPI 다 — 이 기계(150%)에서는 1.5·1.875·2.25·3.0.
#
# 사용법:
#   pwsh Tools/regression/verify-content-browser-splitter.ps1 -Exe Bin/x64-Release/Editor/CreatorEditor.exe
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
New-Item -ItemType Directory -Force -Path $Work | Out-Null

Add-Type -TypeDefinition @'
using System; using System.Runtime.InteropServices; using System.Text;
public static class BrowserSplitterInput {
    delegate bool EnumProc(IntPtr h, IntPtr l);
    [DllImport("user32.dll")] static extern bool EnumWindows(EnumProc cb, IntPtr l);
    [DllImport("user32.dll")] static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] static extern int GetClassName(IntPtr h, StringBuilder s, int n);
    [DllImport("user32.dll")] static extern bool PostMessage(IntPtr h, uint m, IntPtr w, IntPtr l);
    [DllImport("user32.dll")] static extern IntPtr SetThreadDpiAwarenessContext(IntPtr c);
    public static IntPtr FindEditorWindow(uint pid) {
        IntPtr found = IntPtr.Zero;
        EnumWindows((h, l) => {
            uint p; GetWindowThreadProcessId(h, out p);
            if (p != pid) return true;
            var name = new StringBuilder(64); GetClassName(h, name, 64);
            if (name.ToString() == "CoreWindowApp") { found = h; return false; }
            return true;
        }, IntPtr.Zero);
        return found;
    }
    // ★ 보내는 스레드를 모니터별 DPI 인식으로 — 아니면 좌표가 모니터 배율만큼 늘어난다.
    public static bool Post(IntPtr h, uint message, int buttons, int x, int y) {
        SetThreadDpiAwarenessContext(new IntPtr(-4));
        return PostMessage(h, message, new IntPtr(buttons), new IntPtr(((y & 0xFFFF) << 16) | (x & 0xFFFF)));
    }
}
'@

$script:checks = 0
$failures = [Collections.Generic.List[string]]::new()
function Assert([bool]$Condition, [string]$Message) {
    $script:checks++
    if (-not $Condition) { $failures.Add($Message); Write-Host "  FAIL $Message" -ForegroundColor Red }
}
function Near([double]$a, [double]$b, [double]$tolerance) { [Math]::Abs($a - $b) -le $tolerance }

$kMove = 0x200; $kDown = 0x201; $kUp = 0x202; $kLeft = 1
$kWidth = 3400; $kHeight = 1900
$kSettingsTreeWidth = 300.0
$kDragLogical = 40.0

$settingsBytes = [IO.File]::ReadAllBytes($settingsPath)
$settingsText = [IO.File]::ReadAllText($settingsPath)
$scalePattern = '(?m)(^imguiScale: )[^\r\n]+'
$widthPattern = '(?m)(^contentTreeWidth: )[^\r\n]+'
if ([regex]::Matches($settingsText, $scalePattern).Count -ne 1) { throw 'imguiScale 설정이 하나가 아니다' }
if ([regex]::Matches($settingsText, $widthPattern).Count -ne 1) { throw 'contentTreeWidth 설정이 하나가 아니다' }
$utf8 = [Text.UTF8Encoding]::new($false)

# 한 회차. `$Stimulus` 는 표지 파일 이름 → 그 파일이 생기면 부를 스크립트 블록.
function Invoke-Case([string]$Name, [string[]]$Lines, [string]$Workspace, [hashtable]$Stimulus) {
    $scriptPath = Join-Path $Work "$Name.txt"
    $resultPath = Join-Path $Work "$Name.jsonl"
    foreach ($p in @($resultPath, "$Work/$Name.out", "$Work/$Name.err")) { Remove-Item -LiteralPath $p -ErrorAction SilentlyContinue }
    foreach ($mark in @($Stimulus.Keys)) { Remove-Item -LiteralPath (Join-Path $Work $mark) -ErrorAction SilentlyContinue }
    Set-Content -LiteralPath $scriptPath -Encoding UTF8 -Value $Lines
    New-Item -ItemType Directory -Force -Path $Workspace | Out-Null
    $env:CREATOR_EDITOR_WORKSPACE_DIR = $Workspace
    $env:CREATOR_EDITOR_LEGACY_INI = Join-Path $Workspace 'none.ini'
    $process = Start-Process -FilePath $Exe -WorkingDirectory (Split-Path $Exe) -WindowStyle Hidden -PassThru `
        -ArgumentList @('--script', ('"' + $scriptPath + '"'), '--result-format', 'jsonl', '--result-file', ('"' + $resultPath + '"')) `
        -RedirectStandardOutput "$Work/$Name.out" -RedirectStandardError "$Work/$Name.err"
    try {
        $clock = [Diagnostics.Stopwatch]::StartNew()
        foreach ($mark in @($Stimulus.Keys | Sort-Object)) {
            $markPath = Join-Path $Work $mark
            while (-not (Test-Path -LiteralPath $markPath)) {
                if ($process.HasExited) { throw "${Name}: 표지 $mark 전에 에디터가 끝났다(exit $($process.ExitCode))" }
                if ($clock.Elapsed.TotalSeconds -gt 180) { throw "${Name}: 표지 $mark 가 180 초 안에 안 생겼다" }
                Start-Sleep -Milliseconds 20
            }
            $window = [BrowserSplitterInput]::FindEditorWindow([uint32]$process.Id)
            if ([IntPtr]::Zero -eq $window) { throw "${Name}: 에디터 창(CoreWindowApp)을 못 찾았다" }
            & $Stimulus[$mark] $window
        }
        if (-not $process.WaitForExit(300000)) { $process.Kill(); throw "${Name}: 에디터가 제때 끝나지 않았다" }
    }
    finally {
        if (-not $process.HasExited) { $process.Kill() }
        Remove-Item Env:CREATOR_EDITOR_WORKSPACE_DIR -ErrorAction SilentlyContinue
        Remove-Item Env:CREATOR_EDITOR_LEGACY_INI -ErrorAction SilentlyContinue
    }
    $rows = @(Get-Content -LiteralPath $resultPath | Where-Object { $_.Trim() } | ForEach-Object { $_ | ConvertFrom-Json })
    if ($rows.Count -ne $Lines.Count) { throw "${Name}: 결과 행 $($rows.Count) 이 줄 수 $($Lines.Count) 와 다르다" }
    # 번호 붙은 표본은 끝에 공백이 없는 `editor.browser` 줄, 표집은 공백이 붙은 줄.
    $samples = @(); $polls = @{}
    for ($i = 0; $i -lt $Lines.Count; $i++) {
        if ($Lines[$i] -eq 'editor.browser') {
            if ($rows[$i].status -ne 'succeeded') { throw "${Name}: 표본 #$($samples.Count) 가 실패했다: $($rows[$i].message)" }
            $samples += $rows[$i].data.layout
        }
    }
    $block = ''
    for ($i = 0; $i -lt $Lines.Count; $i++) {
        if ($Lines[$i] -like 'scene.save *') { $block = Split-Path $Lines[$i].Substring(11) -Leaf; $polls[$block] = @() }
        elseif ($Lines[$i] -eq 'editor.browser ' -and $rows[$i].status -eq 'succeeded') { $polls[$block] += $rows[$i].data.layout }
    }
    [pscustomobject]@{ Samples = $samples; Polls = $polls; ExitCode = $process.ExitCode }
}

function Header([string[]]$extra) {
    @("window.resize $kWidth $kHeight", 'wait 120', 'editor.window ###Editor.ContentBrowser focus', 'wait 240') + $extra
}
function PollBlock([string]$mark, [int]$count) {
    $block = @("scene.save $(Join-Path $Work $mark)")
    for ($i = 0; $i -lt $count; $i++) { $block += 'wait 20'; $block += 'editor.browser ' }
    $block
}

# ── 자극 ────────────────────────────────────────────────────────────────────
function Hover($window, [int]$x, [int]$y, [int]$times) {
    for ($i = 0; $i -lt $times; $i++) { [void][BrowserSplitterInput]::Post($window, $kMove, 0, $x, $y); Start-Sleep -Milliseconds 10 }
}
function DoubleClick($window, [int]$x, [int]$y) {
    Hover $window $x $y 60
    # ★ 틈 없이 — 떠남 메시지가 사이에 끼면 두 번째 누름이 허공을 누른다.
    foreach ($m in @($kMove, $kDown, $kUp, $kMove, $kDown, $kUp)) {
        [void][BrowserSplitterInput]::Post($window, $m, $(if ($m -eq $kDown) { $kLeft } else { 0 }), $x, $y)
    }
    Start-Sleep -Milliseconds 200
}
function Drag($window, [int]$x, [int]$y, [int]$dx) {
    Hover $window $x $y 60
    [void][BrowserSplitterInput]::Post($window, $kMove, 0, $x, $y)
    [void][BrowserSplitterInput]::Post($window, $kDown, $kLeft, $x, $y)
    $steps = 40
    for ($i = 1; $i -le $steps; $i++) {
        [void][BrowserSplitterInput]::Post($window, $kMove, $kLeft, $x + [int][Math]::Round($dx * $i / $steps), $y)
        Start-Sleep -Milliseconds 15
    }
    [void][BrowserSplitterInput]::Post($window, $kMove, $kLeft, $x + $dx, $y)
    [void][BrowserSplitterInput]::Post($window, $kUp, 0, $x + $dx, $y)
    Start-Sleep -Milliseconds 200
}

function Assert-Geometry($layout, [string]$where) {
    $u = [double]$layout.uiScale
    if (-not $layout.treeVisible) {
        Assert ($layout.availableWidth -lt 560 * $u + 0.5) "${where}: 트리가 접혔는데 가용 폭 $($layout.availableWidth) 이 560u=$(560 * $u) 이상이다"
        return
    }
    Assert ($layout.availableWidth -ge 560 * $u - 0.5) "${where}: 가용 폭 $($layout.availableWidth) < 560u 인데 트리가 보인다"
    $upper = $layout.availableWidth - 358 * $u
    $expected = [Math]::Min([Math]::Max($layout.preferredTreeWidth * $u, 140 * $u), $upper)
    Assert (Near $layout.appliedTreeWidth $expected 1.01) "${where}: 적용 폭 $($layout.appliedTreeWidth) ≠ clamp(선호 $($layout.preferredTreeWidth)×$u, 140u, 가용−358u) = $expected"
    Assert (Near ($layout.treeMaxX - $layout.treeMinX) $layout.appliedTreeWidth 1.01) "${where}: 트리 사각형 폭 $($layout.treeMaxX - $layout.treeMinX) ≠ 적용 폭 $($layout.appliedTreeWidth)"
    Assert (Near $layout.treeMaxX $layout.splitterMinX 1.01) "${where}: 트리 끝 $($layout.treeMaxX) ≠ 분할선 시작 $($layout.splitterMinX)"
    Assert (Near ($layout.splitterMaxX - $layout.splitterMinX) (8 * $u) 1.01) "${where}: 분할선 폭 $($layout.splitterMaxX - $layout.splitterMinX) ≠ 8u=$(8 * $u)"
    Assert (Near $layout.bodyMinX ($layout.splitterMaxX + 10 * $u) 1.01) "${where}: 본문 시작 $($layout.bodyMinX) ≠ 분할선 끝 + 10u = $($layout.splitterMaxX + 10 * $u)"
    Assert (($layout.bodyMaxX - $layout.bodyMinX) -ge 340 * $u - 2.01) "${where}: 본문 폭 $($layout.bodyMaxX - $layout.bodyMinX) < 340u=$(340 * $u)"
}

$summary = [Collections.Generic.List[string]]::new()
$dpi = $null
try {
    foreach ($userScale in @($Scales -split '[,;\s]+' | Where-Object { $_ } | ForEach-Object { [double]$_ })) {
        $tag = 's' + $userScale.ToString('0.00', [Globalization.CultureInfo]::InvariantCulture).Replace('.', '')
        # ★ 에디터가 저장할 때 쓰는 모양(`1`·`1.25`)으로 적는다. `1.0` 으로 적으면 에디터가 `1` 로
        #   다시 써서, 폭을 건드리지 않았는데도 바이트 비교가 붉었다.
        $scaleText = $userScale.ToString('0.###', [Globalization.CultureInfo]::InvariantCulture)
        $configured = [regex]::Replace($settingsText, $scalePattern, '${1}' + $scaleText)
        $configured = [regex]::Replace($configured, $widthPattern, '${1}' + $kSettingsTreeWidth.ToString([Globalization.CultureInfo]::InvariantCulture))
        [IO.File]::WriteAllText($settingsPath, $configured, $utf8)
        $configuredBytes = [IO.File]::ReadAllBytes($settingsPath)
        Write-Host "── 사용자 배율 $scaleText"

        # ── survey ──────────────────────────────────────────────────────────
        $lines = Header @('editor.browser')                                      # #0 넓은 창
        for ($i = 0; $i -lt 50; $i++) { $lines += 'window.resize 1500 1000'; $lines += 'wait 2'; $lines += "window.resize $kWidth $kHeight"; $lines += 'wait 2' }
        $lines += 'wait 240'; $lines += 'editor.browser'                         # #1 100 회 왕복 뒤
        $lines += 'quit'
        $survey = Invoke-Case "$tag-survey" $lines (Join-Path $Work "$tag-survey-ws") @{}
        $s0 = $survey.Samples[0]
        $u = [double]$s0.uiScale
        if ($null -eq $dpi) { $dpi = $u / $userScale }
        Assert (Near ($u / $userScale) $dpi 0.01) "${tag}: 실제 배율/사용자 배율 $($u / $userScale) 이 첫 배율의 $dpi 와 다르다 — 배율이 섞였다"
        Assert ($s0.treeVisible) "${tag}: 넓은 창($kWidth)에서 트리가 안 보인다 — 끌기 회차가 설 수 없다"
        Assert (Near $s0.preferredTreeWidth $kSettingsTreeWidth 0.01) "${tag}: 시작 선호 폭 $($s0.preferredTreeWidth) 이 설정의 $kSettingsTreeWidth 가 아니다"
        Assert-Geometry $s0 "$tag 넓은 창"
        $s1 = $survey.Samples[1]
        foreach ($field in @('appliedTreeWidth', 'treeMinX', 'treeMaxX', 'splitterMinX', 'splitterMaxX', 'bodyMinX', 'bodyMaxX', 'preferredTreeWidth')) {
            Assert (Near $s1.$field $s0.$field 0.01) "${tag}: 창 크기 100 회 왕복 뒤 $field 가 $($s0.$field) → $($s1.$field)"
        }

        # ── narrow ──────────────────────────────────────────────────────────
        # ★ 창 폭 목록을 고정하면 배율마다 잘림 구간(가용 560u~658u)을 건너뛴다 — 오른쪽 도크는
        #   창을 좁혀도 절대 폭을 지키므로 가용 폭 = 창 폭 − 도크 몫이다. 그 몫을 넓은 창에서
        #   재어 가용 800u(여유)·600u(잘림)·400u(접힘)가 되는 창 폭을 고른다.
        $dockShare = $kWidth - $s0.availableWidth
        $targets = @(800, 600, 400)
        $lines = Header @()
        foreach ($t in $targets) { $lines += "window.resize $([int][Math]::Round($dockShare + $t * $u)) 1100"; $lines += 'wait 180'; $lines += 'editor.browser' }
        $lines += 'quit'
        $narrowCase = Invoke-Case "$tag-narrow" $lines (Join-Path $Work "$tag-narrow-ws") @{}
        $clamped = 0; $hidden = 0
        for ($k = 0; $k -lt $targets.Count; $k++) {
            $layout = $narrowCase.Samples[$k]
            Assert-Geometry $layout "$tag 가용 $($targets[$k])u"
            if (-not $layout.treeVisible) { $hidden++ }
            elseif ($layout.appliedTreeWidth -lt $layout.preferredTreeWidth * $u - 1.01) { $clamped++ }
            Assert (Near $layout.preferredTreeWidth $kSettingsTreeWidth 0.01) "$tag 가용 $($targets[$k])u: 좁히기만 했는데 선호 폭이 $($layout.preferredTreeWidth) 로 바뀌었다"
        }
        Assert ($clamped -eq 1) "${tag}: 가용 600u 에서 본문 최소 폭에 잘려야 하는데 잘린 경우가 $clamped 다"
        Assert ($hidden -eq 1) "${tag}: 가용 400u 에서만 트리가 접혀야 하는데 접힌 경우가 $hidden 다"

        # ── drag ────────────────────────────────────────────────────────────
        # 좌표는 클라이언트 기준 = ImGui 화면 좌표 − 주 뷰포트 원점.
        $splitY = [int][Math]::Round(($s0.splitterMinY + $s0.splitterMaxY) / 2 - $s0.viewportY)
        $x300 = [int][Math]::Round(($s0.splitterMinX + $s0.splitterMaxX) / 2 - $s0.viewportX)
        $x220 = [int][Math]::Round($s0.treeMinX + 220 * $u + 4 * $u - $s0.viewportX)
        $x236 = [int][Math]::Round($s0.treeMinX + 236 * $u + 4 * $u - $s0.viewportX)
        $dragPixels = [int][Math]::Round($kDragLogical * $u)
        $lines = Header @('editor.browser')                                      # #0
        $lines += PollBlock '1-reset.mark' 300
        $lines += 'editor.browser'                                               # #1 두 번 눌러 초기화 뒤
        $lines += 'editor.nav key right right'; $lines += 'wait 60'
        $lines += 'editor.browser'                                               # #2 방향키 두 번 뒤
        $lines += PollBlock '2-drag.mark' 300
        $lines += 'editor.browser'                                               # #3 끈 뒤
        $lines += 'quit'
        $dragWorkspace = Join-Path $Work "$tag-drag-ws"
        Remove-Item -LiteralPath $dragWorkspace -Recurse -Force -ErrorAction SilentlyContinue
        $drag = Invoke-Case "$tag-drag" $lines $dragWorkspace @{
            '2-drag.mark' = { param($window) Drag $window $x236 $splitY $dragPixels }
            '1-reset.mark' = { param($window) DoubleClick $window $x300 $splitY }
        }
        $d = $drag.Samples
        Assert (Near $d[0].appliedTreeWidth $s0.appliedTreeWidth 0.01) "$tag 끌기 회차의 시작 배치가 survey 와 다르다($($d[0].appliedTreeWidth) vs $($s0.appliedTreeWidth)) — 좌표를 믿을 수 없다"
        # 두 번 누르기는 틈 없이 지나가 표집이 누름 프레임을 못 잡는다 — 경계 통과는 그 앞의
        # hover 이동이 ImGui 좌표로 도착했는지로 센다(좌표가 DPI 배율만큼 늘면 여기서 붉다).
        $resetSeen = @(@($drag.Polls['1-reset.mark']) + @($drag.Polls['2-drag.mark']) | Where-Object { ((Near $_.mouseX ($x300 + $s0.viewportX) 0.5) -or (Near $_.mouseX ($x236 + $s0.viewportX) 0.5)) -and (Near $_.mouseY ($splitY + $s0.viewportY) 0.5) }).Count
        Assert ($resetSeen -gt 0) "${tag}: 두 번 누르는 동안 ImGui 가 ($x300,$splitY) 좌표(또는 끌기 시작점 $x236)를 한 번도 못 봤다 — 메시지가 백엔드를 못 지났거나 좌표가 틀어졌다"
        Assert (Near $d[1].preferredTreeWidth 220 0.01) "${tag}: 분할선을 두 번 눌렀는데 선호 폭이 $($d[1].preferredTreeWidth) 이다(220 이어야 한다)"
        Assert (Near $d[2].preferredTreeWidth 236 0.01) "${tag}: 방향키 오른쪽 두 번 뒤 선호 폭이 $($d[2].preferredTreeWidth) 이다(236 이어야 한다)"
        Assert-Geometry $d[2] "$tag 방향키 뒤"
        $activeSeen = @($drag.Polls['2-drag.mark'] | Where-Object { $_.splitterActive -and $_.mouseDown }).Count
        Assert ($activeSeen -gt 0) "${tag}: 끄는 동안 분할선이 한 번도 활성이 아니었다"
        $expectedDrag = $d[2].appliedTreeWidth / $u + $dragPixels / $u
        Assert (Near $d[3].preferredTreeWidth $expectedDrag 0.05) "${tag}: $dragPixels px 끈 뒤 선호 폭 $($d[3].preferredTreeWidth) ≠ $expectedDrag (논리 +$($dragPixels / $u))"
        Assert (-not $d[3].splitterActive -and -not $d[3].mouseDown) "${tag}: 뗀 뒤에도 분할선이 활성이거나 버튼이 눌려 있다"
        Assert-Geometry $d[3] "$tag 끈 뒤"
        Assert ([Convert]::ToBase64String([IO.File]::ReadAllBytes($settingsPath)) -ceq [Convert]::ToBase64String($configuredBytes)) `
            "${tag}: 끌기가 프로젝트 설정 파일을 고쳤다 — 폭은 개인 작업 공간의 것이다"

        # ── persist ─────────────────────────────────────────────────────────
        $persist = Invoke-Case "$tag-persist" (Header @('editor.browser', 'quit')) $dragWorkspace @{}
        Assert (Near $persist.Samples[0].preferredTreeWidth $d[3].preferredTreeWidth 0.01) `
            "${tag}: 다시 띄운 선호 폭 $($persist.Samples[0].preferredTreeWidth) 이 끈 폭 $($d[3].preferredTreeWidth) 가 아니다"
        Assert-Geometry $persist.Samples[0] "$tag 다시 띄운 뒤"

        $summary.Add(('  사용자 {0} · 실제 {1:0.###} | 시작 {2} px · 초기화 {3} · 방향키 {4} · 끌기 +{5} px → {6:0.###} · 다시 띄움 {7:0.###} | 잘림 {8} · 접힘 {9} · 좌표 관측 {10} · 활성 관측 {11}' -f `
            $scaleText, $u, $s0.appliedTreeWidth, $d[1].preferredTreeWidth, $d[2].preferredTreeWidth, $dragPixels,
            $d[3].preferredTreeWidth, $persist.Samples[0].preferredTreeWidth, $clamped, $hidden, $resetSeen, $activeSeen))
    }
}
finally {
    [IO.File]::WriteAllBytes($settingsPath, $settingsBytes)
}

Write-Host ''
$summary | ForEach-Object { Write-Host $_ }
Write-Host ("  모니터 DPI 배수 {0:0.###}" -f $dpi)
if ($failures.Count -gt 0) { throw "Content Browser 분할선 검사 실패 $($failures.Count) 건" }
Write-Host "Content Browser 분할선 검사: 단정 $script:checks · 배율 $(@($Scales -split '[,;\s]+' | Where-Object { $_ }).Count) · PASS"
