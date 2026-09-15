# PHASE 21 W8-2 — 3D 렌더 영역을 제외한 chrome crop visual golden.
#
# 계획서 W8: *"3D 씬 렌더 영역을 제외한 chrome crop visual golden을 만든다. 씬뷰
# 전체를 제외하지 않고 W2-V의 툴바·방향 기즈모·HUD를 포함한다."*
#
# ── 왜 픽셀이어야 하는가 ────────────────────────────────────────────────────
#
# 지금까지의 에디터 게이트는 전부 **값**을 본다(도크 노드 수, 사각형, 스타일 색).
# 값이 맞아도 그림이 틀릴 수 있다. W4 에서 실제로 그랬다 — `ImGuizmo::SetRect` 이
# 창 클립과 교차하지 않아 기즈모가 패널 위로 새어 나갔는데, 픽셀·클립 단정이
# 하나도 없어 게이트가 전부 초록이었다.
#
# ── 무엇을 가리는가 ─────────────────────────────────────────────────────────
#
# 3D 그림만 가린다. 씬뷰 **창 전체**를 가리면 계획서가 포함하라고 한 툴바·방향
# 기즈모가 함께 사라진다. 가릴 사각형과 남길 사각형을 **전부 제품이 게시한
# 값에서** 읽는다(`editor.sceneview`) — 이 파일에 좌표를 적으면 자리가 바뀌는
# 순간 게이트가 조용히 낡는다.
#
#   가린다 : image 사각형 (3D 가 놓일 수 있는 자리 전부. clip ⊆ image 이므로
#            image 를 가리면 그림이 어떻게 맞춰지든 덮인다)
#   남긴다 : 툴바 왼쪽 상자 · 툴바 오른쪽 상자 · 방향 기즈모 원반
#
# ── 스크롤한 상태를 반드시 담는다 ───────────────────────────────────────────
#
# W7 의 clipper 는 보폭이 어긋나도 **스크롤해야만** 드러난다. 스크롤 0 인 그림만
# 골든으로 두면 그 결함을 통째로 못 본다. 그래서 회차 둘이다 — 선택 없는 상태와,
# 목록 아래쪽 객체를 선택해 **끌어온** 상태(W8-2 가 그 끌어오기를 세웠다).
#
# ── 판정하지 않고 건너뛰는 자리 ─────────────────────────────────────────────
#
# 픽셀 골든은 기계의 배율·폰트·창 크기에 묶인다. 골든과 환경이 다르면 **틀렸다고
# 말하지 않고 건너뛴다.** 다만 건너뛴 수를 세어 찍는다 — 조용한 건너뜀은 눈먼
# 초록과 구별되지 않는다.
#
# 3D 그림이 실제로 떠 있으면(`imageReady`) 그 회차도 건너뛴다. 툴바 상자가
# 반투명(알파 0.94)이라 뒤의 그림을 6% 섞어 들이고, 그러면 남긴 자리가 3D 를
# 따라 흔들린다. 허용치를 늘려 덮지 않는다 — 판정에서 빼고 수를 남긴다.
[CmdletBinding()]
param(
    [string]$Exe,
    [string]$GoldenDir,
    # 골든을 다시 뜬다. 뜬 그림은 **가린 뒤**의 것이라 사람이 눈으로 검산할 수 있다.
    [switch]$Update,
    [int]$Objects = 300,
    # 목록 아래쪽. 화면에 한 번도 안 보이는 자리라야 "끌어왔다" 가 의미를 갖는다.
    [int]$RevealAt = 260,
    # 접힌 조상 밑에 묻을 사슬의 길이. 마지막 칸을 고르면 조상을 전부 펼쳐야
    # 그 줄이 목록에 **생긴다** — 스크롤만으로는 닿을 수 없는 자리다.
    [int]$NestDepth = 8
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$repo = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
if ([string]::IsNullOrWhiteSpace($Exe)) {
    # 기본값은 **출하 구성**이다. 이 저장소는 기본 -Exe 가 Debug 를 가리켜
    # 게이트가 낡은 바이너리를 재고도 초록이던 일을 겪었다.
    $Exe = Join-Path $repo 'Bin\x64-Release\Editor\CreatorEditor.exe'
}
if ([string]::IsNullOrWhiteSpace($GoldenDir)) {
    $GoldenDir = Join-Path $PSScriptRoot 'golden-editor-chrome'
}
if (-not (Test-Path -LiteralPath $Exe)) { throw "에디터를 찾지 못했다: $Exe" }

$script:checks = 0
$script:failures = @()
function Assert([bool]$condition, [string]$message) {
    $script:checks++
    if (-not $condition) { $script:failures += $message; Write-Host "  [실패] $message" }
}

Add-Type -AssemblyName System.Drawing
Add-Type -Namespace ChromeGolden -Name Win -MemberDefinition @'
[DllImport("user32.dll")] public static extern bool SetProcessDpiAwarenessContext(IntPtr value);
[DllImport("user32.dll")] public static extern bool GetClientRect(IntPtr hwnd, out RECT rect);
[DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr hwnd, out RECT rect);
[DllImport("user32.dll")] public static extern bool ClientToScreen(IntPtr hwnd, ref POINT point);
[DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr hwnd);
[DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr hwnd, IntPtr hdc, uint flags);
public struct RECT { public int Left, Top, Right, Bottom; }
public struct POINT { public int X, Y; }
'@

# 화소를 PowerShell 루프로 훑으면 한 판에 백만 번이 넘는다. 가리기와 대조는
# 컴파일된 쪽에 둔다. 다만 **이미지 형식은 모른다** — 바이트 배열만 받는다.
# .NET 10 에서 System.Drawing 이 갈려 나가 Add-Type 에서 참조하기 까다롭고,
# 읽고 쓰는 일은 PowerShell 쪽이 이미 할 수 있다.
Add-Type -TypeDefinition @'
using System;

public static class ChromePixels
{
    /// outer 안쪽을 지우되 keepBoxes 안이나 keepDisc 안은 그대로 둔다.
    /// 지운 화소 수를 돌려준다 — 0 이면 가릴 것이 없었다는 뜻이고, 그것도 사실이다.
    public static int MaskExcept(byte[] px, int width, int height,
        int[] outer, int[][] keepBoxes, int[] keepDisc)
    {
        int x0 = Math.Max(0, outer[0]), y0 = Math.Max(0, outer[1]);
        int x1 = Math.Min(width, outer[2]), y1 = Math.Min(height, outer[3]);
        int masked = 0;
        for (int y = y0; y < y1; ++y)
        {
            for (int x = x0; x < x1; ++x)
            {
                bool keep = false;
                if (keepBoxes != null)
                {
                    for (int i = 0; i < keepBoxes.Length && !keep; ++i)
                    {
                        int[] k = keepBoxes[i];
                        if (x >= k[0] && x < k[2] && y >= k[1] && y < k[3]) keep = true;
                    }
                }
                if (!keep && keepDisc != null && keepDisc.Length == 3)
                {
                    long dx = x - keepDisc[0], dy = y - keepDisc[1];
                    if (dx * dx + dy * dy <= (long)keepDisc[2] * keepDisc[2]) keep = true;
                }
                if (keep) continue;
                int at = (y * width + x) * 4;
                px[at] = 0; px[at + 1] = 0; px[at + 2] = 0; px[at + 3] = 255;
                ++masked;
            }
        }
        return masked;
    }

    /// {다른 화소 수, minX, maxX, minY, maxY}. 같으면 {0,-1,-1,-1,-1},
    /// 크기가 다르면 {-1,-1,-1,-1,-1}.
    public static int[] Diff(byte[] a, byte[] b, int width, int height)
    {
        if (a.Length != b.Length) return new int[] { -1, -1, -1, -1, -1 };
        int count = 0, minX = int.MaxValue, maxX = -1, minY = int.MaxValue, maxY = -1;
        for (int y = 0; y < height; ++y)
        {
            int row = y * width * 4;
            for (int x = 0; x < width; ++x)
            {
                int at = row + x * 4;
                if (a[at] == b[at] && a[at + 1] == b[at + 1] &&
                    a[at + 2] == b[at + 2] && a[at + 3] == b[at + 3]) continue;
                ++count;
                if (x < minX) minX = x;
                if (x > maxX) maxX = x;
                if (y < minY) minY = y;
                if (y > maxY) maxY = y;
            }
        }
        if (0 == count) return new int[] { 0, -1, -1, -1, -1 };
        return new int[] { count, minX, maxX, minY, maxY };
    }
}
'@

# PNG <-> BGRA 바이트. 한 번의 LockBits/Marshal.Copy 로 옮긴다.
function Read-Shot([string]$path) {
    $source = [System.Drawing.Bitmap]::FromFile($path)
    try {
        $rect = New-Object System.Drawing.Rectangle 0, 0, $source.Width, $source.Height
        $data = $source.LockBits($rect, [System.Drawing.Imaging.ImageLockMode]::ReadOnly,
            [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
        try {
            $bytes = New-Object 'byte[]' ([Math]::Abs($data.Stride) * $source.Height)
            [Runtime.InteropServices.Marshal]::Copy($data.Scan0, $bytes, 0, $bytes.Length)
        }
        finally { $source.UnlockBits($data) }
        return @{ width = $source.Width; height = $source.Height; bytes = $bytes }
    }
    finally { $source.Dispose() }
}

function Write-Shot($shot, [string]$path) {
    $bmp = New-Object System.Drawing.Bitmap($shot.width, $shot.height,
        [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    try {
        $rect = New-Object System.Drawing.Rectangle 0, 0, $shot.width, $shot.height
        $data = $bmp.LockBits($rect, [System.Drawing.Imaging.ImageLockMode]::WriteOnly,
            [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
        try { [Runtime.InteropServices.Marshal]::Copy($shot.bytes, 0, $data.Scan0, $shot.bytes.Length) }
        finally { $bmp.UnlockBits($data) }
        $bmp.Save($path, [System.Drawing.Imaging.ImageFormat]::Png)
    }
    finally { $bmp.Dispose() }
}

# 캡처하는 프로세스가 DPI 를 선언하지 않으면 Windows 가 GetClientRect 를 논리
# 픽셀로 가상화해 좌상단만 담긴다. 이 저장소는 그 잘림을 두 번 제품 결함으로
# 오독했다.
if (-not [ChromeGolden.Win]::SetProcessDpiAwarenessContext([IntPtr](-4))) {
    Write-Host 'DPI 인식 선언에 실패했다 — 캡처가 잘릴 수 있다'
}

$work = Join-Path ([IO.Path]::GetTempPath()) ('chrome-golden-' + [Guid]::NewGuid().ToString('N').Substring(0, 8))
New-Item -ItemType Directory -Force -Path $work | Out-Null

# 회차 정의. 이름이 곧 골든 파일 이름이다.
# 마지막 객체들을 사슬로 엮는다. `object.parent <대상> <새 부모>`.
# 접힘의 기본값은 "씬 루트의 직계만 펼침" 이므로 이 사슬은 통째로 접혀 있고,
# 사슬의 끝은 목록에 **없다**.
$chainTop = $Objects - $NestDepth - 1
$nestLines = @()
for ($step = 1; $step -le $NestDepth; ++$step) {
    $nestLines += ("object.parent W7_" + ($chainTop + $step) + " W7_" + ($chainTop + $step - 1))
}

$rounds = @(
    @{ name = 'base';     select = $null; compareWith = $null; pre = @() }
    @{ name = 'revealed'; select = ("W7_" + $RevealAt); compareWith = $null; pre = @() }
    # 같은 상태를 한 번 더 찍는다. 골든이 **재현 가능한지**를 골든 없이 그 자리에서
    # 재는 축이다 — 두 번 찍은 같은 상태가 다르면 골든 대조는 무엇이 나와도
    # 의미가 없다. 이 회차는 골든 파일을 갖지 않는다.
    @{ name = 'again';    select = $null; compareWith = 'revealed'; pre = @() }
    # 접힌 조상 밑에 묻힌 줄을 고른다. 스크롤은 여기서 힘이 없다 — 그 줄이
    # 목록에 없기 때문이다. 조상을 펼치는 쪽이 서야만 이 그림이 나온다.
    @{ name = 'nested';   select = ("W7_" + ($Objects - 1)); compareWith = $null; pre = $nestLines }
)

# --result-file 은 편집기가 배타로 열어 두므로 도는 동안 읽을 수 없다. 표지는
# **파일이 생기는 것**으로 삼는다 — `scene.save` 는 준 경로에 파일을 만들고,
# 그 시점이 곧 앞 명령들이 끝난 시점이다. 프로젝트 트리 **밖**에 저장한다:
# 안에 쓰면 자산 감시자가 Browser 패널을 갈아 그림이 흔들린다.
#
# 첫 회차 앞의 긴 대기는 **3D 표시 텍스처가 뜰 때까지**다. 이것이 없으면 첫
# 회차는 그림 없이, 뒤 회차는 그림이 뜬 채로 찍혀 두 판의 조건이 갈린다
# (툴바 상자가 반투명이라 뒤의 그림이 비친다).
$lines = @('window.resize 1400 900', 'wait 600', ("scene.populate " + $Objects), 'wait 30000')
foreach ($round in $rounds) {
    foreach ($pre in $round.pre) { $lines += $pre }
    if ($round.pre.Count -gt 0) { $lines += 'wait 900' }
    if ($null -ne $round.select) { $lines += ("scene.select " + $round.select); $lines += 'wait 900' }
    $lines += 'editor.sceneview'
    $lines += 'editor.dock'
    # 회차마다 **같은 파일 이름**을 서로 다른 폴더에 쓴다. `scene.save` 는 씬 이름을
    # 파일 이름으로 바꾸고 그 이름은 Hierarchy 머리 행에 그려진다 — 회차마다 이름이
    # 다르면 그 행 하나 때문에 회차 대조가 저절로 달라져, 스크롤 축을 재려던 단정이
    # 동어반복이 된다.
    $lines += ('scene.save ' + (Join-Path (Join-Path $work $round.name) 'mark.scene'))
    # 표지가 생긴 뒤 캡처할 시간을 준다. 프레임 수라 기계가 빠를수록 짧다.
    $lines += 'wait 12000'
}
$lines += 'quit'

$scriptPath = Join-Path $work 'script.txt'
$lines | Set-Content -LiteralPath $scriptPath -Encoding UTF8
$resultPath = Join-Path $work 'results.jsonl'

function Resolve-EditorWindow($processId) {
    $deadline = (Get-Date).AddSeconds(180)
    while ((Get-Date) -lt $deadline) {
        $fresh = Get-Process -Id $processId -ErrorAction SilentlyContinue
        if ($null -ne $fresh) {
            $handle = $fresh.MainWindowHandle
            if ($handle -ne 0 -and [ChromeGolden.Win]::IsWindowVisible($handle)) {
                $rect = New-Object ChromeGolden.Win+RECT
                [ChromeGolden.Win]::GetClientRect($handle, [ref]$rect) | Out-Null
                # 로딩 창이 먼저 뜬다. 손잡이가 있다고 본 창이 아니다 —
                # 클라이언트가 비어 있지 않은 것까지 봐야 그 창이다.
                if (($rect.Right - $rect.Left) -gt 0 -and ($rect.Bottom - $rect.Top) -gt 0) { return $handle }
            }
        }
        Start-Sleep -Milliseconds 150
    }
    throw '본 창을 찾지 못했다'
}

function Save-ClientCapture($handle, [string]$outFile) {
    # `PrintWindow` 는 **창 전체**(비클라이언트 프레임 포함)를 DC 원점부터 그린다. 클라이언트
    # 크기 비트맵을 주면 프레임 두께만큼 밀린 그림이 담기고 오른쪽·아래가 잘린다. 그러면 ImGui 좌표와 화소 좌표가
    # 어긋나 여기서 계산하는 가리기가 통째로 밀린다 — 씬 이미지 오른쪽 끝이 764 인데 렌더 배율만 바꿔도 x=773 까지 화소가 변했다(실측).
    $client = New-Object ChromeGolden.Win+RECT
    [ChromeGolden.Win]::GetClientRect($handle, [ref]$client) | Out-Null
    $width = $client.Right - $client.Left
    $height = $client.Bottom - $client.Top
    if ($width -le 0 -or $height -le 0) { throw "클라이언트가 비었다: ${width}x${height}" }

    $window = New-Object ChromeGolden.Win+RECT
    [ChromeGolden.Win]::GetWindowRect($handle, [ref]$window) | Out-Null
    $windowWidth = $window.Right - $window.Left
    $windowHeight = $window.Bottom - $window.Top
    if ($windowWidth -lt $width -or $windowHeight -lt $height) {
        throw "창이 클라이언트보다 작다: ${windowWidth}x${windowHeight} < ${width}x${height}"
    }

    $origin = New-Object ChromeGolden.Win+POINT
    [ChromeGolden.Win]::ClientToScreen($handle, [ref]$origin) | Out-Null
    $offsetX = $origin.X - $window.Left
    $offsetY = $origin.Y - $window.Top

    $bmp = New-Object System.Drawing.Bitmap($windowWidth, $windowHeight)
    $gfx = [System.Drawing.Graphics]::FromImage($bmp)
    $hdc = $gfx.GetHdc()
    # 0x2 = PW_RENDERFULLCONTENT — 가려져 있어도 스왑체인 내용까지 그린다.
    $ok = [ChromeGolden.Win]::PrintWindow($handle, $hdc, 0x2)
    $gfx.ReleaseHdc($hdc); $gfx.Dispose()
    if (-not $ok) { $bmp.Dispose(); throw 'PrintWindow 실패' }

    $crop = New-Object System.Drawing.Rectangle $offsetX, $offsetY, $width, $height
    $clientShot = $bmp.Clone($crop, $bmp.PixelFormat)
    $bmp.Dispose()
    $clientShot.Save($outFile, [System.Drawing.Imaging.ImageFormat]::Png)
    $clientShot.Dispose()
}

$env:CREATOR_EDITOR_WORKSPACE_DIR = Join-Path $work 'workspace'
$env:CREATOR_EDITOR_LEGACY_INI = Join-Path $work 'no-legacy.ini'
New-Item -ItemType Directory -Force -Path $env:CREATOR_EDITOR_WORKSPACE_DIR | Out-Null

$exitCode = -1
try {
    $proc = Start-Process -FilePath $Exe -WorkingDirectory (Split-Path $Exe) -PassThru `
        -ArgumentList @('--script', ('"' + $scriptPath + '"'), '--result-format', 'jsonl',
                        '--result-file', ('"' + $resultPath + '"')) `
        -RedirectStandardOutput (Join-Path $work 'stdout.txt') `
        -RedirectStandardError (Join-Path $work 'stderr.txt')

    foreach ($round in $rounds) {
        $mark = Join-Path (Join-Path $work $round.name) 'mark.scene'
        $deadline = (Get-Date).AddSeconds(240)
        while (-not (Test-Path -LiteralPath $mark) -and (Get-Date) -lt $deadline) {
            if ($proc.HasExited) { throw ("에디터가 표지 전에 끝났다: " + $round.name) }
            Start-Sleep -Milliseconds 120
        }
        if (-not (Test-Path -LiteralPath $mark)) { throw ("표지를 못 봤다: " + $round.name) }
        $handle = Resolve-EditorWindow $proc.Id
        Save-ClientCapture $handle (Join-Path $work ($round.name + '.png'))
        Write-Host ("  캡처: " + $round.name)
    }

    if (-not $proc.WaitForExit(240000)) { $proc.Kill(); throw '에디터가 끝나지 않았다' }
    $exitCode = $proc.ExitCode
}
finally {
    Remove-Item Env:CREATOR_EDITOR_WORKSPACE_DIR -ErrorAction SilentlyContinue
    Remove-Item Env:CREATOR_EDITOR_LEGACY_INI -ErrorAction SilentlyContinue
}

Assert (0 -eq $exitCode) "에디터 종료 코드가 0 이 아니다: $exitCode"

$rows = @()
if (Test-Path -LiteralPath $resultPath) {
    $rows = @(Get-Content -LiteralPath $resultPath | Where-Object { $_.Trim() } |
        ForEach-Object { $_ | ConvertFrom-Json })
}
Assert ($rows.Count -gt 0) '결과 줄이 하나도 없다'
$failedRows = @($rows | Where-Object { $_.status -ne 'succeeded' })
Assert (0 -eq $failedRows.Count) ("실패한 명령: " + (($failedRows | ForEach-Object { $_.command }) -join ', '))

$views = @($rows | Where-Object { $_.command -eq 'editor.sceneview' })
$docks = @($rows | Where-Object { $_.command -eq 'editor.dock' })
Assert ($views.Count -eq $rounds.Count) ("editor.sceneview 결과 수가 회차 수와 다르다: " + $views.Count)
Assert ($docks.Count -eq $rounds.Count) ("editor.dock 결과 수가 회차 수와 다르다: " + $docks.Count)
if ($script:failures.Count -gt 0) {
    Write-Host ("FAIL verify-editor-chrome-golden — " + $script:failures.Count + " 건")
    exit 1
}

New-Item -ItemType Directory -Force -Path $GoldenDir | Out-Null
$envPath = Join-Path $GoldenDir 'environment.json'

# ── 환경 기록. 골든이 어떤 자 아래에서 떴는지 함께 적는다 ──────────────────
$firstDock = $docks[0].data
$firstView = $views[0].data
$shot0 = Read-Shot (Join-Path $work ($rounds[0].name + '.png'))
$environment = [ordered]@{
    captureWidth   = $shot0.width
    captureHeight  = $shot0.height
    uiScale        = [double]$firstDock.uiScale
    rootWidth      = [double]$firstDock.rootWidth
    rootHeight     = [double]$firstDock.rootHeight
    imguiVersion   = [string]$firstDock.imguiHeaderVersion
    imageMinX      = [double]$firstView.imageMin[0]
    imageMinY      = [double]$firstView.imageMin[1]
    imageMaxX      = [double]$firstView.imageMax[0]
    imageMaxY      = [double]$firstView.imageMax[1]
    imageReady     = [bool]$firstView.imageReady
    toolbarHeight  = [double]$firstView.toolbarHeight
    gizmoRadius    = [double]$firstView.gizmoRadius
    objects        = $Objects
    revealAt       = $RevealAt
    nestDepth      = $NestDepth
}

function Get-MaskedShot($roundName, $view) {
    $shot = Read-Shot (Join-Path $work ($roundName + '.png'))
    $outer = @(
        [int][Math]::Floor($view.imageMin[0]), [int][Math]::Floor($view.imageMin[1]),
        [int][Math]::Ceiling($view.imageMax[0]), [int][Math]::Ceiling($view.imageMax[1]))
    $keep = New-Object 'int[][]' 2
    $keep[0] = @(
        [int][Math]::Floor($view.left[0]), [int][Math]::Floor($view.left[1]),
        [int][Math]::Ceiling($view.left[0] + $view.leftWidth),
        [int][Math]::Ceiling($view.left[1] + $view.toolbarHeight))
    $keep[1] = @(
        [int][Math]::Floor($view.right[0]), [int][Math]::Floor($view.right[1]),
        [int][Math]::Ceiling($view.right[0] + $view.rightWidth),
        [int][Math]::Ceiling($view.right[1] + $view.toolbarHeight))
    $disc = $null
    if ($view.gizmoVisible) {
        $disc = @([int][Math]::Round($view.gizmoCenter[0]), [int][Math]::Round($view.gizmoCenter[1]),
                  [int][Math]::Ceiling($view.gizmoRadius))
    }
    $masked = [ChromePixels]::MaskExcept($shot.bytes, $shot.width, $shot.height, $outer, $keep, $disc)
    return @{ shot = $shot; masked = $masked }
}

# ── 회차마다 가린 그림을 만든다 ─────────────────────────────────────────────
$skipped = @()
$prepared = @()
for ($at = 0; $at -lt $rounds.Count; ++$at) {
    $name = $rounds[$at].name
    $view = $views[$at].data
    $made = Get-MaskedShot $name $view
    Assert ($made.masked -gt 0) "${name}: 가린 화소가 0 이다 — image 사각형이 화면 밖이거나 비었다"
    $prepared += @{ name = $name; shot = $made.shot; masked = $made.masked; view = $view;
                    compareWith = $rounds[$at].compareWith }
    Write-Host ("  가림: " + $name + " — " + $made.masked + " 화소")
}

if ($Update) {
    foreach ($item in $prepared) {
        if ($null -ne $item.compareWith) { continue }   # 같은 상태를 두 번 찍은 회차
        Write-Shot $item.shot (Join-Path $GoldenDir ($item.name + '.png'))
        Write-Host ("  골든 갱신: " + $item.name + '.png')
    }
    Write-Host ("  건너뛴 축: " + $skipped.Count)
    foreach ($line in $skipped) { Write-Host ("    [건너뜀] " + $line) }
    $environment | ConvertTo-Json | Set-Content -LiteralPath $envPath -Encoding UTF8
    Write-Host ("  골든 환경 갱신: " + $envPath)
    Write-Host '골든을 다시 떴다. 판정은 하지 않는다 — 사람이 눈으로 검산하라.'
    exit 0
}

# ── 환경 대조 ───────────────────────────────────────────────────────────────
$envSkip = $false
if (-not (Test-Path -LiteralPath $envPath)) {
    throw "골든 환경 기록이 없다: $envPath  (-Update 로 먼저 떠라)"
}
$goldenEnv = Get-Content -LiteralPath $envPath -Raw | ConvertFrom-Json
$mismatch = @()
foreach ($key in $environment.Keys) {
    if ($goldenEnv.PSObject.Properties.Name -notcontains $key) { $mismatch += ("$key 없음"); continue }
    $mine = $environment[$key]
    $theirs = $goldenEnv.$key
    if ("$mine" -ne "$theirs") { $mismatch += ("${key}: 골든 $theirs / 지금 $mine") }
}
if ($mismatch.Count -gt 0) {
    $envSkip = $true
    Write-Host '[건너뜀] 골든이 뜬 환경과 지금 환경이 다르다 — 픽셀로 판정하지 않는다:'
    foreach ($line in $mismatch) { Write-Host ("    " + $line) }
}

if (-not $envSkip) {
    foreach ($item in $prepared) {
        if ($null -ne $item.compareWith) { continue }
        $goldenPath = Join-Path $GoldenDir ($item.name + '.png')
        Assert (Test-Path -LiteralPath $goldenPath) "$($item.name): 골든 파일이 없다 — $goldenPath"
        if (-not (Test-Path -LiteralPath $goldenPath)) { continue }
        $golden = Read-Shot $goldenPath
        $diff = [ChromePixels]::Diff($golden.bytes, $item.shot.bytes, $golden.width, $golden.height)
        if (0 -ne $diff[0]) {
            $dump = Join-Path $GoldenDir ($item.name + '.actual.png')
            Write-Shot $item.shot $dump
        }
        Assert (0 -eq $diff[0]) ("$($item.name): 골든과 다르다 — 화소 " + $diff[0] +
            " 개, x " + $diff[1] + ".." + $diff[2] + " y " + $diff[3] + ".." + $diff[4] +
            " (실제 그림을 " + $item.name + ".actual.png 로 남겼다)")
        Write-Host ("  대조: " + $item.name + " — 다른 화소 " + $diff[0])
    }
}

# ── 같은 상태를 두 번 찍으면 같은가 ────────────────────────────────────────
#
# 골든 대조보다 **앞에 서는** 축이다. 한 실행 안에서 같은 상태가 두 번 다르게
# 찍히면 골든이 초록이든 붉든 아무것도 말해 주지 않는다.
foreach ($item in $prepared) {
    if ($null -eq $item.compareWith) { continue }
    $twin = @($prepared | Where-Object { $_.name -eq $item.compareWith })
    Assert (1 -eq $twin.Count) ("$($item.name): 짝을 찾지 못했다 — " + $item.compareWith)
    if (1 -ne $twin.Count) { continue }
    $repeatDiff = [ChromePixels]::Diff($twin[0].shot.bytes, $item.shot.bytes,
        $item.shot.width, $item.shot.height)
    Assert (0 -eq $repeatDiff[0]) ("같은 상태를 두 번 찍었는데 다르다 — 화소 " + $repeatDiff[0] +
        " 개, x " + $repeatDiff[1] + ".." + $repeatDiff[2] + " y " + $repeatDiff[3] + ".." + $repeatDiff[4] +
        " (골든 대조는 이 축이 서기 전에는 아무것도 뜻하지 않는다)")
    Write-Host ("  재현: " + $item.compareWith + " 두 번 — 다른 화소 " + $repeatDiff[0])
}

# ── 스크롤 축이 **자극됐는지** ──────────────────────────────────────────────
#
# 두 회차가 같은 그림이면 골든이 초록이어도 아무것도 증명하지 않는다. 선택이
# 목록을 끌어왔다면 계층 패널이 달라져야 한다.
function Get-Round([string]$name) {
    $hit = @($prepared | Where-Object { $_.name -eq $name })
    if (1 -eq $hit.Count) { return $hit[0] }
    return $null
}

# 무엇과 무엇이 **달라야** 하는가. 같으면 골든이 초록이어도 그 축은 서지 않은 것이다.
$stimulus = @(
    @{ left = 'base';     right = 'revealed'; why = '선택이 목록을 끌어오지 않았다' }
    @{ left = 'revealed'; right = 'nested';   why = '접힌 조상 밑의 선택이 목록을 바꾸지 않았다' }
)
foreach ($pair in $stimulus) {
    $a = Get-Round $pair.left
    $b = Get-Round $pair.right
    if ($null -eq $a -or $null -eq $b) {
        $skipped += ($pair.left + ' ↔ ' + $pair.right + ' 대조 (회차가 서지 못했다)')
        continue
    }
    $crossDiff = [ChromePixels]::Diff($a.shot.bytes, $b.shot.bytes, $a.shot.width, $a.shot.height)
    Assert ($crossDiff[0] -gt 0) ($pair.left + ' 과 ' + $pair.right + ' 의 그림이 같다 — ' + $pair.why)
    if ($crossDiff[0] -le 0) { continue }
    # 차이는 씬 이미지 **바깥**에만 있어야 한다. 안쪽은 가려 놓았으므로 안쪽에서
    # 차이가 나면 가리기가 샌 것이다.
    $imageRight = [int][Math]::Ceiling($a.view.imageMax[0])
    Assert ($crossDiff[1] -ge $imageRight) ($pair.left + ' ↔ ' + $pair.right +
        ' 의 차이가 씬 이미지 열 안쪽에서 시작한다: x ' + $crossDiff[1] + ' < ' + $imageRight + ' — 가리기가 샜다')
    Write-Host ("  자극 확인: " + $pair.left + " ↔ " + $pair.right + " 다른 화소 " + $crossDiff[0] +
        " 개, x " + $crossDiff[1] + ".." + $crossDiff[2] + " (씬 이미지 오른쪽 끝 " + $imageRight + ")")
}

Write-Host ("건너뛴 축: " + $skipped.Count)
foreach ($line in $skipped) { Write-Host ("  [건너뜀] " + $line) }

if ($script:failures.Count -gt 0) {
    Write-Host ("FAIL verify-editor-chrome-golden — " + $script:failures.Count + " 건 / " + $script:checks + " 단정")
    exit 1
}
Remove-Item -LiteralPath $work -Recurse -Force -ErrorAction SilentlyContinue
Write-Host ("PASS verify-editor-chrome-golden — " + $script:checks + " 단정, 회차 " +
    $prepared.Count + " · 건너뜀 " + $skipped.Count)
exit 0
