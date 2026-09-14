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
# ★ 렌더 예열은 프레임 수가 아니라 **시간**을 재야 한다 (2026-09-14).
#   라이브 렌더러의 첫 프레임은 파이프라인 구축과 ShaderMeta 적용으로
#   Debug 에서 13~28초가 걸린다. 예전에는 짧은 wait 로도 통했는데, 그것은
#   BuildPipeline 이 표시 락을 구축 내내 쥐어 게임 스레드까지 멈춰 세운
#   덕이었다 — 프레임이 느려서 수가 곧 시간이었다. 그 정지를 걷어낸 뒤
#   (BuildPipeline 락 축소) 프레임이 제 속도로 돌아 같은 수가 몇 초에
#   지나간다. 그래서 첫 대기만 첫 프레임을 덮는 수로 올린다.
New-Item -ItemType Directory -Path $run -Force | Out-Null
$scenario = Join-Path $run 'commands.txt'
$stdout = Join-Path $run 'stdout.txt'
$stderr = Join-Path $run 'stderr.txt'
$resultPath = Join-Path $run 'results.jsonl'
@(
    "scene.switch $($scene.Replace('\', '/'))"
    # 렌더 예열 — 아래 주석 참고.
    'wait 2000'
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
