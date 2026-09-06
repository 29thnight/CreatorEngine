param(
    [string]$Exe = (Join-Path $PSScriptRoot "..\..\Bin\x64-Debug\Editor\CreatorEditor.exe"),
    [string]$Work = $env:TEMP,
    [int]$TimeoutSeconds = 180
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'CommandResults.ps1')

if (-not (Test-Path -LiteralPath $Exe -PathType Leaf)) {
    "실행 파일이 없다: $Exe"
    exit 1
}

$root = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$scene = Join-Path $root 'Dynamic_CPP\Assets\Scenes\FT_Primitives.creator'
if (-not (Test-Path -LiteralPath $scene -PathType Leaf)) {
    "FT_Primitives 씬이 없다: $scene"
    exit 1
}

$run = Join-Path $Work ("CE_D2FTPrimitives_" + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $run -Force | Out-Null
$scenario = Join-Path $run 'commands.txt'
$stdout = Join-Path $run 'stdout.txt'
$stderr = Join-Path $run 'stderr.txt'
$resultPath = Join-Path $run 'results.jsonl'
@(
    "scene.switch $($scene.Replace('\', '/'))"
    'wait 120'
    'dx12.scene'
    'quit'
) | Set-Content -LiteralPath $scenario -Encoding UTF8

$process = Start-Process -FilePath $Exe -ArgumentList @('--commandlet-script', ('"'+$scenario+'"'), '--result-file', ('"'+$resultPath+'"')) `
    -WorkingDirectory $root -WindowStyle Hidden `
    -RedirectStandardOutput $stdout -RedirectStandardError $stderr -PassThru
$process.WaitForExit($TimeoutSeconds * 1000) | Out-Null
if (-not $process.HasExited) {
    $process.Kill()
    "TIMEOUT output=$run"
    exit 1
}

$text = if (Test-Path -LiteralPath $stdout) {
    Get-Content -LiteralPath $stdout -Raw
} else { '' }
$errorLines = if (Test-Path -LiteralPath $stderr) {
    @(Get-Content -LiteralPath $stderr)
} else { @() }
$knownLodWarning = 'Mesh::GenerateLODs: MeshOptimizer failed to generate LODs.'
$unexpectedErrors = @($errorLines | Where-Object {
    -not [string]::IsNullOrWhiteSpace($_) -and $_ -ne $knownLodWarning
})
$lodWarnings = @($errorLines | Where-Object { $_ -eq $knownLodWarning }).Count

$data = Get-SucceededCommand (Read-CommandResults $resultPath) 'dx12.scene'
$drawCandidates = [int]$data.drawCandidates
$lights = [int]$data.lights
$draws = [int]$data.draws
$meshUploads = [int]$data.meshUploads

"experiment-ft-primitives exit=$($process.ExitCode) output=$run"
"scene drawCandidates=$drawCandidates lights=$lights draws=$draws meshUploads=$meshUploads lodWarnings=$lodWarnings unexpectedStderr=$($unexpectedErrors.Count)"

$passed = 0 -eq $process.ExitCode -and $drawCandidates -gt 0 -and $lights -gt 0 -and
    $draws -gt 0 -and $meshUploads -gt 0 -and 0 -eq $unexpectedErrors.Count
if (-not $passed) {
    if ($unexpectedErrors.Count -gt 0) {
        '예상하지 않은 stderr:'
        $unexpectedErrors
    }
    exit 1
}

'전체 통과 — FT_Primitives가 catalog GUID 모델/재질을 해석하고 RenderThread proxy delta 뒤 실제 DX12 draw를 만들었다'
exit 0
