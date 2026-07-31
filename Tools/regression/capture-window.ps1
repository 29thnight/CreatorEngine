# 실행 중인 엔진 창을 PNG로 캡처한다(PHASE 3 검증 도구).
#
# 엔진 내부 캡처(DirectX::CaptureTexture)는 게임 스레드에서 죽는 문제로 보류돼 있어,
# 밖에서 창을 찍는다. PrintWindow의 PW_RENDERFULLCONTENT(0x2)는 DWM에게 D3D
# 콘텐츠까지 렌더시키므로 창이 다른 창에 가려져 있어도 스왑체인 내용이 잡힌다.
param(
    [string]$ProcessName = "Academy_4Q",
    [Parameter(Mandatory = $true)][string]$OutFile,
    [int]$TimeoutSec = 30
)

Add-Type -AssemblyName System.Drawing

Add-Type -Namespace Win32 -Name Capture -MemberDefinition @'
[DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr hwnd, IntPtr hdc, uint flags);
[DllImport("user32.dll")] public static extern bool GetClientRect(IntPtr hwnd, out RECT rect);
[DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr hwnd);
public struct RECT { public int Left, Top, Right, Bottom; }
'@

# 보이는 메인 창이 생길 때까지 기다린다(로딩 창 -> 본 창 전환이 있다).
$deadline = (Get-Date).AddSeconds($TimeoutSec)
$hwnd = [IntPtr]::Zero
while ((Get-Date) -lt $deadline) {
    $proc = Get-Process -Name $ProcessName -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($proc -and $proc.MainWindowHandle -ne 0 -and [Win32.Capture]::IsWindowVisible($proc.MainWindowHandle)) {
        $hwnd = $proc.MainWindowHandle
        break
    }
    Start-Sleep -Milliseconds 500
}
if ($hwnd -eq [IntPtr]::Zero) { "창을 찾지 못했다: $ProcessName"; exit 1 }

$rect = New-Object Win32.Capture+RECT
[Win32.Capture]::GetClientRect($hwnd, [ref]$rect) | Out-Null
$w = $rect.Right - $rect.Left
$h = $rect.Bottom - $rect.Top
if ($w -le 0 -or $h -le 0) { "클라이언트 영역이 비어 있다 (${w}x${h})"; exit 1 }

$bmp = New-Object System.Drawing.Bitmap($w, $h)
$gfx = [System.Drawing.Graphics]::FromImage($bmp)
$hdc = $gfx.GetHdc()
# 0x2 = PW_RENDERFULLCONTENT — D3D 스왑체인 내용 포함
$ok = [Win32.Capture]::PrintWindow($hwnd, $hdc, 0x2)
$gfx.ReleaseHdc($hdc)
$gfx.Dispose()

if (-not $ok) { $bmp.Dispose(); "PrintWindow 실패"; exit 1 }

$bmp.Save($OutFile, [System.Drawing.Imaging.ImageFormat]::Png)
$bmp.Dispose()
"캡처 완료: $OutFile (${w}x${h})"
exit 0
