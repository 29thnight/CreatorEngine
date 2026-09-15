# 실행 중인 엔진 창을 PNG로 캡처한다(PHASE 3 검증 도구).
#
# 엔진 내부 캡처(DirectX::CaptureTexture)는 게임 스레드에서 죽는 문제로 보류돼 있어,
# 밖에서 창을 찍는다. PrintWindow의 PW_RENDERFULLCONTENT(0x2)는 DWM에게 D3D
# 콘텐츠까지 렌더시키므로 창이 다른 창에 가려져 있어도 스왑체인 내용이 잡힌다.
param(
    [string]$ProcessName = "CreatorEditor",
    [Parameter(Mandatory = $true)][string]$OutFile,
    [int]$TimeoutSec = 30,
    # 창 제목에 이 문자열이 들어갈 때까지 기다린다(빈 값이면 확인하지 않는다).
    #
    # 엔진은 로딩 창을 먼저 띄우고 본 창으로 바꾸므로, 제목을 보면 로딩 화면을
    # 찍는 것을 막을 수 있다. 다만 이 창은 프로세스 API로 제목이 읽히지 않는
    # 경우가 있어(실측) 기본값은 비워 둔다 — 확인하지 못하는 조건을 기본으로
    # 걸면 캡처가 통째로 실패한다. 로딩 화면 문제는 호출부의 대기 시간으로 막는다.
    [string]$TitleContains = ""
)

# ── 먼저 이 프로세스를 DPI 인식으로 선언한다 ─────────────────────────────
#
# 선언하지 않으면 Windows 가 `GetClientRect` 를 **논리 픽셀로 가상화**한다. 그러면
# 아래에서 만드는 비트맵이 실제 클라이언트보다 작아지고, `PrintWindow` 는 창을
# 물리 크기로 그리므로 **좌상단만 남고 잘린다.** 이 저장소는 그 잘림을 두 번
# 제품 결함으로 오독했다 — 배율 1.5 인 기계에서 1400x945 창이 933x630 으로 잡혀
# 오른쪽 패널이 통째로 프레임 밖에 있었다.
#
# 창을 열기 전에 불러야 한다. 실패해도 캡처 자체는 되므로 막지 않되, 크기가
# 가상화됐다는 사실은 남긴다.
Add-Type -Namespace Win32 -Name Dpi -MemberDefinition @'
[DllImport("user32.dll")] public static extern bool SetProcessDpiAwarenessContext(IntPtr value);
'@
# DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2 == -4
if (-not [Win32.Dpi]::SetProcessDpiAwarenessContext([IntPtr](-4))) {
    Write-Host "DPI 인식 선언에 실패했다 — 캡처가 논리 픽셀로 잘릴 수 있다"
}

Add-Type -AssemblyName System.Drawing

Add-Type -Namespace Win32 -Name Capture -MemberDefinition @'
[DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr hwnd, IntPtr hdc, uint flags);
[DllImport("user32.dll")] public static extern bool GetClientRect(IntPtr hwnd, out RECT rect);
[DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr hwnd, out RECT rect);
[DllImport("user32.dll")] public static extern bool ClientToScreen(IntPtr hwnd, ref POINT point);
[DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr hwnd);
public struct RECT { public int Left, Top, Right, Bottom; }
public struct POINT { public int X, Y; }
'@

# 보이는 메인 창이 생길 때까지 기다린다(로딩 창 -> 본 창 전환이 있다).
$deadline = (Get-Date).AddSeconds($TimeoutSec)
$graceDeadline = (Get-Date).AddSeconds([Math]::Max(3, [int]($TimeoutSec / 3)))
$hwnd = [IntPtr]::Zero
while ((Get-Date) -lt $deadline) {
    $proc = Get-Process -Name $ProcessName -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($proc -and $proc.MainWindowHandle -ne 0 -and [Win32.Capture]::IsWindowVisible($proc.MainWindowHandle)) {
        $title = $proc.MainWindowTitle
        $matched = [string]::IsNullOrEmpty($TitleContains) -or ($title -like "*$TitleContains*")

        # 제목을 못 읽는 경우가 있다(프로세스 API가 빈 문자열을 주는 창).
        # 제목으로 거르지 못한다고 캡처 자체를 포기하면 검증이 통째로 멈추므로,
        # 기다릴 만큼 기다린 뒤에는 제목 없이도 받아들이고 그 사실을 알린다.
        if (-not $matched -and [string]::IsNullOrEmpty($title) -and (Get-Date) -gt $graceDeadline) {
            Write-Host "창 제목을 읽지 못해 제목 확인 없이 캡처한다"
            $matched = $true
        }

        if ($matched) {
            $hwnd = $proc.MainWindowHandle
            break
        }
    }
    Start-Sleep -Milliseconds 500
}
if ($hwnd -eq [IntPtr]::Zero) { "창을 찾지 못했다: $ProcessName (제목 조건: `"$TitleContains`")"; exit 1 }

# ── 창을 통째로 찍고 클라이언트만 잘라 낸다 ────────────────
#
# `PrintWindow` 는 **창 전체**(비클라이언트 프레임 포함)를 DC 원점부터 그린다.
# 그런데 여기서 주는 비트맵이 클라이언트 크기면, 왼쪽·위로 프레임 두께만큼 밀린
# 그림이 담기고 오른쪽·아래가 그만큼 잘린다. 크기 조절이 되는 창은 테두리가
# 보이지 않아도 두께가 있어서(WS_THICKFRAME, 배율 1.5 에서 9~10 px) 눈으로는
# 거의 티가 안 난다 — 그러나 화소 좌표를 쓰는 소비자에게는 통째로 어긋난 자다.
#
# 실측으로 확정했다(PHASE 21 W8-2): 제품이 게시한 씬 이미지 오른쪽 끝이 764 인데
# 렌더 배율만 바꿔도 x=773 까지 화소가 변했고, 774 부터는 하나도 변하지 않았다.
$client = New-Object Win32.Capture+RECT
[Win32.Capture]::GetClientRect($hwnd, [ref]$client) | Out-Null
$w = $client.Right - $client.Left
$h = $client.Bottom - $client.Top
if ($w -le 0 -or $h -le 0) { "클라이언트 영역이 비어 있다 (${w}x${h})"; exit 1 }

$window = New-Object Win32.Capture+RECT
[Win32.Capture]::GetWindowRect($hwnd, [ref]$window) | Out-Null
$windowW = $window.Right - $window.Left
$windowH = $window.Bottom - $window.Top
if ($windowW -lt $w -or $windowH -lt $h) { "창이 클라이언트보다 작다 (${windowW}x${windowH})"; exit 1 }

$origin = New-Object Win32.Capture+POINT
[Win32.Capture]::ClientToScreen($hwnd, [ref]$origin) | Out-Null
$offsetX = $origin.X - $window.Left
$offsetY = $origin.Y - $window.Top

$bmp = New-Object System.Drawing.Bitmap($windowW, $windowH)
$gfx = [System.Drawing.Graphics]::FromImage($bmp)
$hdc = $gfx.GetHdc()
# 0x2 = PW_RENDERFULLCONTENT — D3D 스왑체인 내용 포함
$ok = [Win32.Capture]::PrintWindow($hwnd, $hdc, 0x2)
$gfx.ReleaseHdc($hdc)
$gfx.Dispose()

if (-not $ok) { $bmp.Dispose(); "PrintWindow 실패"; exit 1 }

$crop = New-Object System.Drawing.Rectangle $offsetX, $offsetY, $w, $h
$clientBmp = $bmp.Clone($crop, $bmp.PixelFormat)
$bmp.Dispose()
$clientBmp.Save($OutFile, [System.Drawing.Imaging.ImageFormat]::Png)
$clientBmp.Dispose()
"캡처 완료: $OutFile (${w}x${h}, 창 ${windowW}x${windowH} 에서 오프셋 ${offsetX},${offsetY} 로 잘랐다)"
exit 0
