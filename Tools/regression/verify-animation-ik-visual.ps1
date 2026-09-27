[CmdletBinding()]
param(
    [ValidateSet('Debug','Release')][string]$Configuration = 'Release',
    [string]$Work = '',
    [string]$Python = 'python'
)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$repo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
if (-not $Work) { $Work = Join-Path $repo ('Build/Obj/Phase13S4/IKVisual-' + [guid]::NewGuid().ToString('N')) }
$Work = [IO.Path]::GetFullPath($Work)
if (Test-Path -LiteralPath $Work) { throw 'Use a new output directory for independent IK captures.' }
if (Get-Process CreatorEditor -ErrorAction SilentlyContinue) { throw 'Close the existing Editor before this isolated gate.' }
& $Python -c 'import numpy, PIL'
if ($LASTEXITCODE -ne 0) { throw 'Pass -Python with a Python runtime containing NumPy and Pillow.' }
New-Item -ItemType Directory -Path $Work | Out-Null
$exe = Join-Path $repo "Bin/x64-$Configuration/Editor/CreatorEditor.exe"
$model = Join-Path $repo 'Dynamic_CPP/Assets/Models/CreatorRobot.glb'
$scene = Join-Path $repo 'Dynamic_CPP/Assets/Scenes/FT_Primitives.creator'
$commands = @(
    ('scene.switch "' + $scene + '"'), 'window.resize 1280 900',
    'play', 'wait 2', 'play.pause',
    ('animation.visual.probe setup "' + $model + '"'),
    'wait 2', 'editor.viewport game', 'render.live.wait 120'
)
$poses = @('ik-base','ik-full','ik-down1','ik-down2','ik-down3','ik-up1','ik-up2','ik-up3')
foreach ($pose in $poses) {
    $commands += @("animation.visual.probe $pose", 'wait 2',
        ('render.pbr.capture "' + (Join-Path $Work $pose) + '" game controlled'))
}
$commands += @('stop')
$scenario = Join-Path $Work 'scenario.txt'
$resultPath = Join-Path $Work 'results.jsonl'
$commands | Set-Content -LiteralPath $scenario -Encoding utf8
$proc = Start-Process -FilePath $exe -WorkingDirectory (Split-Path $exe) -WindowStyle Hidden `
    -ArgumentList @('--commandlet-script', ('"' + $scenario + '"'), '--result-file', ('"' + $resultPath + '"')) `
    -RedirectStandardOutput (Join-Path $Work 'editor.out') -RedirectStandardError (Join-Path $Work 'editor.err') -PassThru
try {
    $deadline = (Get-Date).AddSeconds(360)
    while (-not $proc.WaitForExit(1000)) {
        if ((Get-Date) -ge $deadline) { throw "IK visual gate timed out: $Work" }
    }
    $proc.Refresh()
    if (-not (Test-Path -LiteralPath $resultPath)) { throw "Editor exited without results: $Work" }
    $results = @(Get-Content -LiteralPath $resultPath | ConvertFrom-Json)
    $failed = @($results | Where-Object status -ne succeeded)
    if ($proc.ExitCode -ne 0 -or $results.Count -ne $commands.Count -or $failed.Count) {
        $failed | ConvertTo-Json -Depth 8 | Write-Output
        throw "IK visual capture failed (exit=$($proc.ExitCode)): $Work"
    }
    & $Python (Join-Path $PSScriptRoot 'animation_ik_visual_pixels.py') $Work
    if ($LASTEXITCODE -ne 0) { throw "IK pixel transition failed: $Work" }
    Write-Output "ANIMATION_IK_VISUAL_ARTIFACTS $Work"
}
finally {
    if (-not $proc.HasExited) { $proc.Kill(); $proc.WaitForExit() }
}
