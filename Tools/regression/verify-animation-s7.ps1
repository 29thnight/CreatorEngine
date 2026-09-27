[CmdletBinding()]
param(
    [ValidateSet('Debug','Release')][string]$Configuration = 'Release',
    [switch]$IncludeVisual,
    [string]$Python = 'python',
    [string]$Work = ''
)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$repo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
if (-not $Work) {
    $Work = Join-Path $repo ('Build/Obj/Phase13S7/Regression-' + [guid]::NewGuid().ToString('N'))
}
$Work = [IO.Path]::GetFullPath($Work)
if (Test-Path -LiteralPath $Work) { throw 'Use a new S7 regression output directory.' }
New-Item -ItemType Directory -Path $Work | Out-Null

# The product probe exercises the real 100-actor scene, managed callback
# order/thread, socket world transform, and the recorded S7 HUD task snapshot.
& (Join-Path $PSScriptRoot 'verify-animation-playback.ps1') -Configuration $Configuration
if ($LASTEXITCODE -ne 0) { throw 'Animation playback regression failed.' }
& (Join-Path $PSScriptRoot 'verify-animation-product.ps1') `
    -Configuration $Configuration -Work (Join-Path $Work 'product')
if ($LASTEXITCODE -ne 0) { throw 'Animation product regression failed.' }
& (Join-Path $PSScriptRoot 'measure-animation-budget.ps1') `
    -Configuration $Configuration -Work (Join-Path $Work 'budget')
if ($LASTEXITCODE -ne 0) { throw 'Animation budget regression failed.' }

# Pixel checks deliberately run last, after the functional and budget gates.
if ($IncludeVisual) {
    & (Join-Path $PSScriptRoot 'verify-animation-visual.ps1') `
        -Configuration $Configuration -Python $Python -Work (Join-Path $Work 'visual')
    if ($LASTEXITCODE -ne 0) { throw 'Animation visual regression failed.' }
    & (Join-Path $PSScriptRoot 'verify-animation-ik-visual.ps1') `
        -Configuration $Configuration -Python $Python -Work (Join-Path $Work 'ik-visual')
    if ($LASTEXITCODE -ne 0) { throw 'Animation IK visual regression failed.' }
}
Write-Output "ANIMATION_S7_OK $Work"
