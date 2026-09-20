[CmdletBinding()]
param(
    [ValidateSet('Debug','Release')][string]$Configuration = 'Debug',
    [string]$Model = '',
    [string]$Work = ''
)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$repo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
if (-not $Work) { $Work = Join-Path $repo "Build/Obj/Phase13S0/Product-$Configuration" }
if (-not $Model) { $Model = Join-Path $repo 'Dynamic_CPP/Assets/Models/CreatorRobot.glb' }
$Work = [IO.Path]::GetFullPath($Work)
$Model = [IO.Path]::GetFullPath($Model)
$exe = Join-Path $repo "Bin/x64-$Configuration/Editor/CreatorEditor.exe"
if (Get-Process CreatorEditor -ErrorAction SilentlyContinue) {
    throw 'Close the existing Editor before running the process-scoped animation gate.'
}
if (-not (Test-Path -LiteralPath $exe)) { throw "Build the $Configuration Editor first: $exe" }
if (-not (Test-Path -LiteralPath $Model)) { throw "Animation model fixture missing: $Model" }
New-Item -ItemType Directory -Force -Path $Work | Out-Null
$scenario = Join-Path $Work 'scenario.txt'
$resultPath = Join-Path $Work 'results.jsonl'
@('scene.new AnimationPlaybackGate', 'play', 'wait 2',
    ('animation.playback.probe "' + $Model + '"'), 'play') |
    Set-Content -LiteralPath $scenario -Encoding utf8
if (Test-Path -LiteralPath $resultPath) { Remove-Item -LiteralPath $resultPath }
$proc = Start-Process -FilePath $exe -WorkingDirectory (Split-Path $exe) -WindowStyle Hidden `
    -ArgumentList @('--commandlet-script', ('"' + $scenario + '"'), '--result-file', ('"' + $resultPath + '"')) `
    -RedirectStandardOutput (Join-Path $Work 'editor.out') -RedirectStandardError (Join-Path $Work 'editor.err') -PassThru
try {
    $deadline = (Get-Date).AddSeconds(240)
    while (-not $proc.WaitForExit(1000)) {
        if ((Get-Date) -ge $deadline) { throw 'Animation product gate timed out' }
    }
    $proc.Refresh()
    if (-not (Test-Path -LiteralPath $resultPath)) { throw "Editor exited $($proc.ExitCode) without results: $Work" }
    $results = @(Get-Content -LiteralPath $resultPath | ConvertFrom-Json)
    $results | ConvertTo-Json -Depth 8 | Write-Output
    if ($proc.ExitCode -ne 0 -or $results.Count -ne 5 -or @($results | Where-Object status -ne succeeded).Count -ne 0) {
        throw "Animation product gate failed (exit=$($proc.ExitCode)): $Work"
    }
    $probe = $results[3]
    if (-not $probe.data.passed -or $probe.data.log -notmatch '^ANIMATION_PRODUCT_OK actors=100 rounds=12 checks=\d+ managedThreadErrors=0') {
        throw 'Product probe did not report the complete animation gate'
    }
    Write-Output "$Configuration $($probe.data.log)"
}
finally {
    if (-not $proc.HasExited) { $proc.Kill(); $proc.WaitForExit() }
}
