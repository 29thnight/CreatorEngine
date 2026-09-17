[CmdletBinding()]
param(
    [string]$Exe = (Join-Path $PSScriptRoot '../../Bin/x64-Release/Editor/CreatorEditor.exe'),
    [string]$Work = (Join-Path $env:TEMP 'creator-browser-drag')
)
# PHASE 21 W2-B — Content Browser 타일을 **운영체제 마우스 메시지**로 끌어 Hierarchy 에 놓는다.
#
# 끌기 신원(payload = 전체 경로)은 지금까지 소스 축으로만 섰다. 이 검사는 실물 입력으로
# 원천(타일) → payload → 받는 자리까지를 지난다. 받는 자리는 Hierarchy 의 `Texture` 다 —
# 동기로 엔티티를 만들고 SpriteRenderer 에 그 텍스처를 붙여 결과를 CLI 로 볼 수 있다.
# fixture 는 **유형 폴더(Textures) 밖**에 둔다. 예전 소비자는 `Textures\` + 이름으로 경로를
# 다시 지어서, 그 밖의 파일을 끌면 텍스처가 안 올라왔다(스프라이트 경로가 빈다).
#
# 단: 원천 → 놓기(Hierarchy 빈 자리) → 엔티티 +1 · SpriteRenderer 가 그 파일을 올렸다.
#     원천 → 받지 않는 자리(브라우저 본문 빈 곳)에 놓기 → 엔티티 그대로.
#     둘 다 브라우저가 들어 올린 payload 의 유형·경로를 스냅샷으로 먼저 본다(경계 통과).
#
# 자극 경로와 함정은 verify-content-browser-splitter.ps1 과 같다(보내는 스레드의 DPI 인식,
# 숨긴 창의 WM_MOUSELEAVE). 끌어 놓기는 하나가 더 있다.
#   ★ **놓기는 두 프레임을 요구한다.** ImGui 는 직전 프레임에 대상이 payload 를 받아들였고
#     이번 프레임에 버튼이 떼어졌을 때만 전달한다. 이동과 뗌을 같이 보내면 한 프레임에 함께
#     적용돼(좌표 뒤의 버튼은 같은 프레임에 들어간다) 직전 프레임 좌표가 비어 있고 — 떠남이
#     지웠다 — 여섯 번 중 네 번 전달이 안 됐다. `이동 → 가운데 누름 → 가운데 뗌 → 이동 → 왼쪽 뗌`
#     을 한 번에 보낸다. 같은 버튼이 한 프레임에 두 번 바뀌면 뒤엣것이 밀리고 버튼이 바뀐 뒤의
#     좌표도 밀려서, 대상 위 좌표가 두 프레임 이상 산 뒤 왼쪽 뗌이 온다(8/8 전달).
#
# 사용법:
#   pwsh Tools/regression/verify-content-browser-drag.ps1 -Exe Bin/x64-Release/Editor/CreatorEditor.exe
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$Exe = [IO.Path]::GetFullPath($Exe)
$Work = [IO.Path]::GetFullPath($Work)
$repoRoot = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
$assets = Join-Path $repoRoot 'Dynamic_CPP/Assets'
$recentsPath = Join-Path $repoRoot 'Dynamic_CPP/Library/EditorState/ContentBrowserRecents.txt'
if (-not (Test-Path -LiteralPath $Exe)) { throw "Editor executable not found: $Exe" }
if (Get-Process CreatorEditor -ErrorAction SilentlyContinue) {
    throw '이미 떠 있는 에디터를 닫아라 — 이 검사는 남의 에디터를 멈추지 않는다.'
}
New-Item -ItemType Directory -Force -Path $Work | Out-Null

Add-Type -TypeDefinition @'
using System; using System.Runtime.InteropServices; using System.Text;
public static class BrowserDragInput {
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
    static IntPtr Point(int x, int y) { return new IntPtr(((y & 0xFFFF) << 16) | (x & 0xFFFF)); }
    // ★ 보내는 스레드를 모니터별 DPI 인식으로 — 아니면 좌표가 모니터 배율만큼 늘어난다.
    // 한 호출 안에서 틈 없이 보낸다 — PowerShell 호출 사이 간격이 표시 프레임보다 길다.
    public static void Burst(IntPtr h, int x, int y, uint[] messages, int[] states) {
        SetThreadDpiAwarenessContext(new IntPtr(-4));
        for (int i = 0; i < messages.Length; ++i) PostMessage(h, messages[i], new IntPtr(states[i]), Point(x, y));
    }
}
'@

$script:checks = 0
$failures = [Collections.Generic.List[string]]::new()
function Assert([bool]$Condition, [string]$Message) {
    $script:checks++
    if (-not $Condition) { $failures.Add($Message); Write-Host "  FAIL $Message" -ForegroundColor Red }
}

$kMove = [uint32]0x200; $kLeftDown = [uint32]0x201; $kLeftUp = [uint32]0x202; $kMiddleDown = [uint32]0x207; $kMiddleUp = [uint32]0x208
$kLeft = 1; $kMiddle = 0x10

function Invoke-Drag($window, [int]$fromX, [int]$fromY, [int]$toX, [int]$toY) {
    for ($k = 0; $k -lt 40; $k++) { [BrowserDragInput]::Burst($window, $fromX, $fromY, @($kMove), @(0)); Start-Sleep -Milliseconds 10 }
    [BrowserDragInput]::Burst($window, $fromX, $fromY, @($kMove, $kLeftDown), @(0, $kLeft))
    Start-Sleep -Milliseconds 50
    $steps = 60
    for ($k = 1; $k -le $steps; $k++) {
        $x = [int][Math]::Round($fromX + ($toX - $fromX) * $k / $steps)
        $y = [int][Math]::Round($fromY + ($toY - $fromY) * $k / $steps)
        [BrowserDragInput]::Burst($window, $x, $y, @($kMove), @($kLeft)); Start-Sleep -Milliseconds 15
    }
    Start-Sleep -Milliseconds 100
    # ★ 놓기는 두 프레임 — 머리의 설명.
    [BrowserDragInput]::Burst($window, $toX, $toY, @($kMove, $kMiddleDown, $kMiddleUp, $kMove, $kLeftUp),
        @($kLeft, ($kLeft -bor $kMiddle), $kLeft, $kLeft, 0))
    Start-Sleep -Milliseconds 200
}

function Invoke-Case([string]$Name, [string[]]$Lines, [hashtable]$Stimulus) {
    $scriptPath = Join-Path $Work "$Name.txt"
    $resultPath = Join-Path $Work "$Name.jsonl"
    foreach ($p in @($resultPath, "$Work/$Name.out", "$Work/$Name.err")) { Remove-Item -LiteralPath $p -ErrorAction SilentlyContinue }
    foreach ($mark in @($Stimulus.Keys)) { Remove-Item -LiteralPath (Join-Path $Work $mark) -ErrorAction SilentlyContinue }
    Set-Content -LiteralPath $scriptPath -Encoding UTF8 -Value $Lines
    $workspace = Join-Path $Work "$Name-ws"
    Remove-Item -LiteralPath $workspace -Recurse -Force -ErrorAction SilentlyContinue
    New-Item -ItemType Directory -Force -Path $workspace | Out-Null
    $env:CREATOR_EDITOR_WORKSPACE_DIR = $workspace
    $env:CREATOR_EDITOR_LEGACY_INI = Join-Path $workspace 'none.ini'
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
            $window = [BrowserDragInput]::FindEditorWindow([uint32]$process.Id)
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
    [pscustomobject]@{ Rows = $rows; Lines = $Lines; ExitCode = $process.ExitCode }
}
function RowsAfter($case, [string]$prefix) {
    @(for ($i = 0; $i -lt $case.Lines.Count; $i++) { if ($case.Lines[$i] -like "$prefix*") { $case.Rows[$i] } })
}
# 표지 줄 뒤부터 다음 표지 줄 전까지의 표집 행.
function PollsOf($case, [string]$mark) {
    $inside = $false
    @(for ($i = 0; $i -lt $case.Lines.Count; $i++) {
        if ($case.Lines[$i] -like 'scene.save *') { $inside = $case.Lines[$i].EndsWith($mark); continue }
        if ($inside -and $case.Lines[$i] -eq 'editor.browser ') { $case.Rows[$i] }
    })
}

$stem = 'DragCheck_' + [guid]::NewGuid().ToString('N').Substring(0, 8)
$folder = Join-Path $assets $stem
$fixture = Join-Path $PSScriptRoot 'fixtures/browser-thumbnails/Checker256.png'
$recentsBackup = if (Test-Path -LiteralPath $recentsPath) { [Convert]::ToBase64String([IO.File]::ReadAllBytes($recentsPath)) } else { '' }
$relative = "$stem/$stem.png"
$summary = ''
try {
    New-Item -ItemType Directory -Force -Path $folder | Out-Null
    Copy-Item -LiteralPath $fixture -Destination (Join-Path $folder "$stem.png")

    $head = @('window.resize 3400 1900', 'wait 120', 'scene.new BrowserDragCheck', 'wait 60',
        'editor.window ###Editor.ContentBrowser focus', 'wait 120', "editor.browser go $stem", 'wait 240')

    # ── survey: 누를 타일과 놓을 창의 사각형 ────────────────────────────────
    $survey = Invoke-Case 'survey' ($head + @('editor.browser', 'editor.dock', 'scene.dump before', 'quit')) @{}
    $browser = (RowsAfter $survey 'editor.browser')[-1].data   # 마지막 행 = 끝의 `editor.browser`
    $dock = (RowsAfter $survey 'editor.dock')[0].data
    $before = [int](RowsAfter $survey 'scene.dump')[0].data.objects
    $tile = @($browser.tiles | Where-Object { $_.path -eq $relative }) | Select-Object -First 1
    $hierarchy = @($dock.placements | Where-Object { $_.id -eq '###Editor.Hierarchy' }) | Select-Object -First 1
    if ($null -eq $tile) { throw "survey: 그린 타일에 $relative 가 없다([$(@($browser.tiles | ForEach-Object path) -join ', ')])" }
    if ($null -eq $hierarchy -or $hierarchy.rect[2] -le $hierarchy.rect[0]) { throw 'survey: Hierarchy 창 사각형이 없다' }
    $vx = [double]$browser.layout.viewportX; $vy = [double]$browser.layout.viewportY
    $fromX = [int][Math]::Round(($tile.rect[0] + $tile.rect[2]) / 2 - $vx)
    $fromY = [int][Math]::Round(($tile.rect[1] + $tile.rect[3]) / 2 - $vy)
    # Hierarchy 의 놓는 자리는 엔티티 줄 **아래** 남은 영역이다 — 창 높이의 70% 지점.
    $dropX = [int][Math]::Round(($hierarchy.rect[0] + $hierarchy.rect[2]) / 2 - $vx)
    $dropY = [int][Math]::Round($hierarchy.rect[1] + ($hierarchy.rect[3] - $hierarchy.rect[1]) * 0.7 - $vy)
    # 받지 않는 자리: 브라우저 본문의 오른쪽 끝, 타일과 같은 높이.
    $missX = [int][Math]::Round($browser.layout.bodyMaxX - 40 - $vx)
    $missY = $fromY
    Assert ($missX -gt $fromX + 200) "survey: 브라우저 본문에 빈 자리가 없다(타일 $fromX, 끝 $missX)"

    # ── drag: 놓기 한 번, 빗나간 놓기 한 번 ─────────────────────────────────
    $lines = @() + $head + @('editor.browser', "scene.save $(Join-Path $Work '1-drop.mark')")
    for ($i = 0; $i -lt 300; $i++) { $lines += 'wait 20'; $lines += 'editor.browser ' }
    $lines += 'scene.dump dropped'; $lines += "object.properties $stem SpriteRenderer"
    $lines += "scene.save $(Join-Path $Work '2-miss.mark')"
    for ($i = 0; $i -lt 300; $i++) { $lines += 'wait 20'; $lines += 'editor.browser ' }
    $lines += 'scene.dump missed'; $lines += 'editor.browser'; $lines += 'quit'
    $drag = Invoke-Case 'drag' $lines @{
        '1-drop.mark' = { param($window) Invoke-Drag $window $fromX $fromY $dropX $dropY }
        '2-miss.mark' = { param($window) Invoke-Drag $window $fromX $fromY $missX $missY }
    }

    # ★ 접두 매치는 `editor.browser go …` 응답 행을 먼저 집는다 — 그 줄 그대로만.
    $start = @(for ($i = 0; $i -lt $drag.Lines.Count; $i++) { if ($drag.Lines[$i] -eq 'editor.browser') { $drag.Rows[$i] } })[0].data
    Assert (@($start.tiles | Where-Object { $_.path -eq $relative -and [Math]::Abs($_.rect[0] - $tile.rect[0]) -lt 0.5 -and [Math]::Abs($_.rect[1] - $tile.rect[1]) -lt 0.5 }).Count -eq 1) `
        "끌기 회차의 타일 자리가 survey 와 다르다 — 좌표를 믿을 수 없다"

    foreach ($arm in @(@{ Mark = '1-drop.mark'; Name = '놓기' }, @{ Mark = '2-miss.mark'; Name = '빗나간 놓기' })) {
        $polls = PollsOf $drag $arm.Mark
        $lifted = @($polls | Where-Object { $_.data.dragPayloadType })
        Assert ($lifted.Count -gt 0) "$($arm.Name): 끄는 동안 브라우저가 payload 를 한 번도 들지 않았다 — 누름이 타일에 닿지 않았다"
        $types = @($lifted | ForEach-Object { $_.data.dragPayloadType } | Select-Object -Unique)
        $paths = @($lifted | ForEach-Object { $_.data.dragPayloadPath } | Select-Object -Unique)
        Assert ($types.Count -eq 1 -and $types[0] -eq 'Texture') "$($arm.Name): payload 유형이 [$($types -join ', ')] 다(Texture 여야 한다)"
        Assert ($paths.Count -eq 1 -and $paths[0] -eq $relative) "$($arm.Name): payload 경로가 [$($paths -join ', ')] 다($relative 여야 한다 — 이름이 아니라 전체 경로)"
        Assert (-not $polls[-1].data.dragPayloadType) "$($arm.Name): 표집 끝에도 payload 가 들려 있다 — 뗌이 닿지 않았다"
    }

    $dropped = [int](RowsAfter $drag 'scene.dump dropped')[0].data.objects
    $missed = [int](RowsAfter $drag 'scene.dump missed')[0].data.objects
    Assert ($dropped -eq $before + 1) "Hierarchy 에 놓았는데 엔티티가 $before → $dropped 다(+1 이어야 한다) — 놓기가 받는 자리에 전달되지 않았다"
    $properties = (RowsAfter $drag 'object.properties')[0]
    Assert ($properties.status -eq 'succeeded') "놓아 만든 엔티티 $stem 의 SpriteRenderer 를 못 읽었다: $($properties.message)"
    $spritePath = if ($properties.status -eq 'succeeded') { [string]$properties.data.values.m_SpritePath } else { '' }
    # 텍스처가 올라왔을 때만 채워지는 값이다(SetSprite). 예전 소비자는 Textures\ 에서 이름으로 찾아 비었다.
    # 적히는 값은 캐시 신원(Assets 기준 상대 경로)이다 — verify-texture-cache-identity.ps1.
    Assert ($spritePath -eq $relative) "SpriteRenderer 가 끌어 온 텍스처를 올리지 못했다(m_SpritePath='$spritePath', $relative 여야 한다) — 받는 자리가 경로를 다시 지었다"
    Assert ($missed -eq $dropped) "받지 않는 자리(브라우저 본문)에 놓았는데 엔티티가 $dropped → $missed 로 바뀌었다"

    $summary = "놓기: payload Texture·$relative → 엔티티 $before→$dropped · 스프라이트 '$spritePath' | 빗나간 놓기: 엔티티 $missed"
}
finally {
    Remove-Item -LiteralPath $folder -Recurse -Force -ErrorAction SilentlyContinue
    Remove-Item -LiteralPath "$folder.meta" -Force -ErrorAction SilentlyContinue
    if ($recentsBackup) { [IO.File]::WriteAllBytes($recentsPath, [Convert]::FromBase64String($recentsBackup)) }
    elseif (Test-Path -LiteralPath $recentsPath) { Remove-Item -LiteralPath $recentsPath }
}

Write-Host ''
Write-Host "  $summary"
if ($failures.Count -gt 0) { throw "Content Browser 끌어 놓기 검사 실패 $($failures.Count) 건" }
Write-Host "Content Browser 끌어 놓기 검사: 단정 $script:checks · PASS"
