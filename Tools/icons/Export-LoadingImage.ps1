[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing

$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$sourcePath = Join-Path $repoRoot 'Resources/Editor/Branding/LoadingBackground.png'
$outputPath = Join-Path $repoRoot 'Resources/Editor/Icons/Loading.bmp'
$width = 666
$height = 390

$source = $null
$bitmap = $null
$graphics = $null
try {
    $source = [Drawing.Bitmap]::new($sourcePath)
    $sourceRatio = $source.Width / $source.Height
    $targetRatio = $width / $height
    if ([Math]::Abs($sourceRatio - $targetRatio) -gt 0.005) {
        throw "Loading background aspect ratio does not match ${width}x${height}: $($source.Width)x$($source.Height)"
    }

    $bitmap = [Drawing.Bitmap]::new($width, $height,
        [Drawing.Imaging.PixelFormat]::Format24bppRgb)
    $graphics = [Drawing.Graphics]::FromImage($bitmap)
    $graphics.InterpolationMode = [Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
    $graphics.PixelOffsetMode = [Drawing.Drawing2D.PixelOffsetMode]::HighQuality
    $graphics.DrawImage($source, [Drawing.Rectangle]::new(0, 0, $width, $height),
        0, 0, $source.Width, $source.Height, [Drawing.GraphicsUnit]::Pixel)
    $bitmap.Save($outputPath, [Drawing.Imaging.ImageFormat]::Bmp)
    Write-Output "Exported $outputPath ($($width)x$($height), 24-bit BMP)"
}
finally {
    if ($graphics) { $graphics.Dispose() }
    if ($bitmap) { $bitmap.Dispose() }
    if ($source) { $source.Dispose() }
}
