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

Add-Type -AssemblyName System.Drawing

Add-Type -Namespace Win32 -Name Capture -MemberDefinition @'
[DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr hwnd, IntPtr hdc, uint flags);
[DllImport("user32.dll")] public static extern bool GetClientRect(IntPtr hwnd, out RECT rect);
[DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr hwnd);
public struct RECT { public int Left, Top, Right, Bottom; }
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
