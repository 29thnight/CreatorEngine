[CmdletBinding()]
param(
    [ValidateSet('Debug','Release')][string]$Configuration = 'Release',
    [string]$Work = '',
    [string]$Python = 'python'
)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$repo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
if (-not $Work) { $Work = Join-Path $repo ('Build/Obj/Phase13S0/Visual-' + $Configuration + '-' + [guid]::NewGuid().ToString('N')) }
$Work = [IO.Path]::GetFullPath($Work)
if (Test-Path -LiteralPath $Work) { throw 'Use a new output directory; captures must be independent.' }
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
    'wait 2', 'editor.viewport game', 'render.live.wait 60'
)
$poses = @('a','a-repeat','b','next','blend0','blendhalf','blend1','layer','layerdisabled','masked','upper','hidden','show')
foreach ($pose in $poses) {
    $action = if ($pose -eq 'a-repeat') {'a'} else {$pose}
    $commands += @("animation.visual.probe $action", 'wait 2',
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
        if ((Get-Date) -ge $deadline) { throw "Visual gate timed out: $Work" }
    }
    $results = @(Get-Content -LiteralPath $resultPath | ConvertFrom-Json)
    $failed = @($results | Where-Object status -ne succeeded)
    if ($proc.ExitCode -ne 0 -or $results.Count -ne $commands.Count -or $failed.Count) {
        $failed | ConvertTo-Json -Depth 8 | Write-Output
        throw "Visual capture failed (exit=$($proc.ExitCode)): $Work"
    }
    & $Python (Join-Path $PSScriptRoot 'animation_visual_pixels.py') $Work
    if ($LASTEXITCODE -ne 0) { throw "Animation pixel regression failed: $Work" }
    Write-Output "ANIMATION_VISUAL_ARTIFACTS $Work"
}
finally {
    if (-not $proc.HasExited) { $proc.Kill(); $proc.WaitForExit() }
}
