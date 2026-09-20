[CmdletBinding()]
param([ValidateSet('Debug','Release')][string]$Configuration = 'Release', [string]$Work = '')
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$repo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
if (-not $Work) { $Work = Join-Path $repo ("Build/Obj/Phase13S05/Product-$Configuration-" + [guid]::NewGuid().ToString('N')) }
$Work = [IO.Path]::GetFullPath($Work)
if (Get-Process CreatorEditor -ErrorAction SilentlyContinue) { throw 'Close the existing Editor before this isolated gate.' }
if (Test-Path -LiteralPath $Work) { throw 'Use a new result directory.' }
New-Item -ItemType Directory -Path $Work | Out-Null
$exe = Join-Path $repo "Bin/x64-$Configuration/Editor/CreatorEditor.exe"
$scenario = Join-Path $Work 'scenario.txt'
$results = Join-Path $Work 'results.jsonl'
@('scene.new WorkerPoolGate', ('worker.pool.probe "' + (Join-Path $repo 'Dynamic_CPP/Assets/Models/CreatorRobot.glb') + '"')) |
    Set-Content -LiteralPath $scenario -Encoding utf8
$proc = Start-Process -FilePath $exe -WorkingDirectory (Split-Path $exe) -WindowStyle Hidden -PassThru `
    -ArgumentList @('--commandlet-script', ('"' + $scenario + '"'), '--result-file', ('"' + $results + '"')) `
    -RedirectStandardOutput (Join-Path $Work 'editor.out') -RedirectStandardError (Join-Path $Work 'editor.err')
$processHandle = $proc.Handle
if (-not $proc.WaitForExit(180000)) { $proc.Kill(); $proc.WaitForExit(); throw 'Worker product gate timed out' }
$proc.WaitForExit()
$rows = @(Get-Content -LiteralPath $results | ConvertFrom-Json)
$probe = @($rows | Where-Object command -eq 'worker.pool.probe')
if ($proc.ExitCode -ne 0 -or $rows.Count -ne 2 -or @($rows | Where-Object status -ne succeeded).Count -ne 0 -or
    $probe.Count -ne 1 -or $probe[0].data.passed -ne $true -or $probe[0].data.log -notmatch 'WORKER_PRODUCT_OK') {
    throw "Worker product gate failed: $Work"
}
$probe[0].data.log
