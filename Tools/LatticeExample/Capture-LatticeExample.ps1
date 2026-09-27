param(
    [string]$OutputPath = (Join-Path $PSScriptRoot '..\..\Build\LatticeExample\LatticeExampleCapture.png')
)

$ErrorActionPreference = 'Stop'
Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
public static class LatticeWindowCapture {
    [StructLayout(LayoutKind.Sequential)]
    public struct Rect { public int Left, Top, Right, Bottom; }
    [DllImport("user32.dll")]
    public static extern bool SetProcessDPIAware();
    [DllImport("user32.dll")]
    public static extern bool GetWindowRect(IntPtr window, out Rect rect);
    [DllImport("dwmapi.dll")]
    public static extern int DwmGetWindowAttribute(IntPtr window, int attribute, out Rect rect, int size);
}
'@
Add-Type -AssemblyName System.Drawing

# A DPI-unaware capture process receives virtualized window coordinates but
# CopyFromScreen consumes physical pixels. Opt in before querying the rectangle.
[LatticeWindowCapture]::SetProcessDPIAware() | Out-Null
$process = Get-Process LatticeExample -ErrorAction Stop | Select-Object -First 1
if ($process.MainWindowHandle -eq [IntPtr]::Zero) { throw 'Lattice Example window is not open.' }
$rect = New-Object LatticeWindowCapture+Rect
# DWMWA_EXTENDED_FRAME_BOUNDS (9) excludes the invisible resize border that
# GetWindowRect includes on modern Windows. It is the visible screenshot area.
$result = [LatticeWindowCapture]::DwmGetWindowAttribute(
    $process.MainWindowHandle, 9, [ref]$rect, 16)
if ($result -ne 0) {
    throw "Could not read visible DWM frame bounds (HRESULT $result)."
}
$width = $rect.Right - $rect.Left
$height = $rect.Bottom - $rect.Top
if ($width -le 0 -or $height -le 0) { throw 'Invalid Lattice Example window rectangle.' }
$bitmap = [System.Drawing.Bitmap]::new($width, $height)
try {
    $graphics = [System.Drawing.Graphics]::FromImage($bitmap)
    try {
        $graphics.CopyFromScreen($rect.Left, $rect.Top, 0, 0, [System.Drawing.Size]::new($width, $height))
    } finally { $graphics.Dispose() }
    $bitmap.Save($OutputPath)
} finally { $bitmap.Dispose() }
Write-Output "Captured $width x $height to $OutputPath"
