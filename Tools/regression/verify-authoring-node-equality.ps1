param(
    [string]$Exe = (Join-Path $PSScriptRoot "..\..\Bin\x64-Debug\Editor\CreatorEditor.exe"),
    [string]$Work = $env:TEMP,
    [int]$TimeoutSeconds = 180
)

# SerializationPlan D3-a-1 — 저작 노드 구조 비교의 판정 규칙.
#
# ★ 이 게이트가 지키는 것은 성능이 아니라 **의미**다. 구조 비교는 이전의
#   `YAML::Dump(a) == YAML::Dump(b)` 동작을 그대로 옮기지 않는다 — 맵 키 순서와
#   emitter 스타일을 무시하는 것이 의도된 차이다. 그래서 검사는 "구조 비교가 옳은
#   답을 내는가"와 "Dump와 갈리는 지점이 예상한 곳뿐인가"를 **함께** 단정한다.
#
# ★ `divergedFromDump=0`이면 실패다. 전부 Dump와 같은 답이면 이 슬라이스가 아무것도
#   바꾸지 않았다는 뜻이고, 그때 통과를 내주면 게이트가 빈 집합을 성공으로 읽는 것이다.

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

if (-not (Test-Path -LiteralPath $Exe -PathType Leaf)) {
    "실행 파일이 없다: $Exe"
    exit 1
}
$Exe = (Resolve-Path -LiteralPath $Exe).Path
$root = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path

$run = Join-Path $Work ("CE_D3a1NodeEqual_" + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $run -Force | Out-Null
$scenario = Join-Path $run 'commands.txt'
$stdout = Join-Path $run 'stdout.txt'
$stderr = Join-Path $run 'stderr.txt'
@('serialize.nodeequal', 'quit') | Set-Content -LiteralPath $scenario -Encoding UTF8

$process = Start-Process -FilePath $Exe -ArgumentList @('--script', $scenario) `
    -WorkingDirectory $root -WindowStyle Hidden `
    -RedirectStandardOutput $stdout -RedirectStandardError $stderr -PassThru
$process.WaitForExit($TimeoutSeconds * 1000) | Out-Null
if (-not $process.HasExited) {
    $process.Kill()
    "TIMEOUT output=$run"
    exit 1
}

$text = if (Test-Path -LiteralPath $stdout) { Get-Content -LiteralPath $stdout -Raw } else { '' }
$failures = New-Object System.Collections.Generic.List[string]

$summary = [regex]::Match($text,
    '\[serialize\.nodeequal\] cases=(\d+) passed=(\d+) failed=(\d+) divergedFromDump=(\d+)')
if (-not $summary.Success) {
    $failures.Add('요약 라인을 찾지 못했다 — serialize.nodeequal이 등록되지 않았거나 낡은 exe다')
} else {
    $cases    = [int]$summary.Groups[1].Value
    $passed   = [int]$summary.Groups[2].Value
    $failed   = [int]$summary.Groups[3].Value
    $diverged = [int]$summary.Groups[4].Value

    if ($cases -le 0)        { $failures.Add('케이스가 0건이다 — 잴 것이 없다') }
    if ($failed -ne 0)       { $failures.Add("케이스 $failed 건 실패") }
    if ($passed -ne $cases)  { $failures.Add("통과 $passed / 전체 $cases — 일부가 실행되지 않았다") }
    if ($diverged -le 0)     { $failures.Add('Dump와 갈리는 케이스가 0건 — 구조 비교가 아무것도 바꾸지 않았다') }

    "cases=$cases passed=$passed failed=$failed divergedFromDump=$diverged"
}

$verdict = [regex]::Match($text, '\[serialize\.nodeequal\] selfcheck=(\w+)')
if (-not $verdict.Success) {
    $failures.Add('selfcheck 판정 라인이 없다')
} elseif ($verdict.Groups[1].Value -ne 'pass') {
    $failures.Add("selfcheck=$($verdict.Groups[1].Value)")
}

$errorLines = if (Test-Path -LiteralPath $stderr) { @(Get-Content -LiteralPath $stderr) } else { @() }
$knownLodWarning = 'Mesh::GenerateLODs: MeshOptimizer failed to generate LODs.'
$unexpected = @($errorLines | Where-Object {
    -not [string]::IsNullOrWhiteSpace($_) -and $_ -ne $knownLodWarning })
if ($unexpected.Count -gt 0) {
    $failures.Add("예상 밖 stderr $($unexpected.Count)줄: $($unexpected[0])")
}

"output=$run"
if ($failures.Count -gt 0) {
    '실패:'
    $failures | ForEach-Object { "  - $_" }
    exit 1
}

'전체 통과 — 구조 비교가 14개 판정 규칙을 만족하고, Dump와 갈리는 3건이 의도된 차이다'
exit 0
