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
# scene.switch is queued since 2026-10-09: it returns a request and activates at a
# later frame boundary. Play must start on FT_Primitives, so wait for activation and
# record the first request's state (a fresh process numbers it 1). The Editor
# republishes every authored texture at startup and each publication supersedes
# in-flight scene preparation, so the switch is issued after that settles.
$commands = @(
    'wait 1500', ('scene.switch "' + $scene + '"'), 'wait 600', 'scene.load.status 1', 'window.resize 1280 900',
    'play', 'wait 2', 'play.pause',
    ('animation.visual.probe setup "' + $model + '"'),
    # A cold live frame took 63.4s in the Phase 13 DDOL gate; 60s truncated it.
    'wait 2', 'editor.viewport game', 'render.live.wait 120'
)
# render.live.wait proves CPU packet consumption only. The robot's METEOR material
# programs may still be preparing, which a controlled capture rejects instead of
# waiting. Warm-up captures may fail with exactly that message (as interactive gates
# retry it); the last one must succeed before any pose is captured.
# A cold run (no shader or Scene product cache) needed about 15 attempts.
$warmups = 20
$warmupIndices = @()
foreach ($attempt in 1..$warmups) {
    $commands += @('wait 120')
    $warmupIndices += $commands.Count
    $commands += @('render.pbr.capture "' + (Join-Path $Work "warmup-$attempt") + '" game controlled')
}
$preparing = 'METEOR Scene requested material program is still preparing.'
$poses = @('a','a-repeat','b','next','blend0','blendhalf','blend1','layer','layerdisabled','masked','upper','additive','hidden','show','unobservedA','unobservedB')
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
    $deadline = (Get-Date).AddSeconds(600)
    while (-not $proc.WaitForExit(1000)) {
        if ((Get-Date) -ge $deadline) { throw "Visual gate timed out: $Work" }
    }
    $results = @(Get-Content -LiteralPath $resultPath | ConvertFrom-Json)
    $switch = @($results | Where-Object command -eq 'scene.load.status')
    if (-not $switch.Count -or $switch[0].status -ne 'succeeded' -or -not $switch[0].data.complete -or
        $switch[0].data.state -ne 'Ready') {
        throw "FT_Primitives did not finish activating before Play: $Work"
    }
    # Results follow command order, so warm-up rows are identified by position.
    if ($results.Count -ne $commands.Count) { throw "Visual result count mismatch: $Work" }
    $warmupRows = @($warmupIndices | ForEach-Object { $results[$_] })
    if ($warmupRows[-1].status -ne 'succeeded') {
        throw "METEOR material programs were not ready after $warmups warm-up captures: $Work"
    }
    $failed = @(foreach ($index in 0..($results.Count - 1)) {
        $row = $results[$index]
        if ($row.status -ne 'succeeded' -and -not ($index -in $warmupIndices -and $row.message -eq $preparing)) { $row }
    })
    $tolerated = @($warmupRows | Where-Object status -ne 'succeeded').Count
    $expectedExit = if ($tolerated) { 4 } else { 0 }
    if ($proc.ExitCode -ne $expectedExit -or $results.Count -ne $commands.Count -or $failed.Count) {
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
