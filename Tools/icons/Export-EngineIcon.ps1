[CmdletBinding()]
param()
$ErrorActionPreference = 'Stop'
$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
Add-Type -AssemblyName System.Drawing
# Reuse the application resource artwork; no separate title-bar logo is authored.
$icon = [Drawing.Icon]::new((Join-Path $repoRoot 'Editor/EngineEntry/Academy_4Q.ico'), 32, 32)
$bitmap = $null
try {
    $bitmap = $icon.ToBitmap()
    $bitmap.Save((Join-Path $repoRoot 'Resources/Editor/Icons/Engine.png'), [Drawing.Imaging.ImageFormat]::Png)
}
finally {
    if ($bitmap) { $bitmap.Dispose() }
    $icon.Dispose()
}
