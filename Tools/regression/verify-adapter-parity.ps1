param(
    [string]$Exe = (Join-Path $PSScriptRoot "..\..\Bin\x64-Debug\Editor\CreatorEditor.exe"),
    [string]$Work = $env:TEMP,
    [int]$TimeoutSeconds = 600
)

# SerializationPlan D3-b-2b-1b-3a — 어댑터 수준 파리티.
#
# 앞선 두 검사가 증명하지 못하는 축이다:
#   · 파서 파리티(D3-b-0)는 두 파서가 만든 **트리**가 같은지 쟀다.
#   · 스칼라 파리티(D3-b-2b-0)는 **값 변환**이 같은지 쟀다.
#   · 그러나 소비자가 실제로 부르는 것은 `ReadNode`의 연산이고, 그 아홉 가지가
#     두 backend에서 같은 답을 내는지는 여기서만 잰다.
#
# ★ 위험한 것은 backend 비대칭이다. yaml-cpp에서 맵의 키는 **진짜 노드**지만 ryml에서는
#   **자식의 속성**이다. 널도 yaml-cpp는 노드 타입, ryml은 "값이 null 표기"다. 어댑터가
#   그 비대칭을 흡수하는데, 흡수가 맞는지는 같은 문서를 양쪽에 넣어야만 알 수 있다.
#
# ★ 0을 세고 "차이 0"을 통과로 읽지 않는다. 파일 수·노드 수·**맵 항목 수**를 함께
#   단정한다 — 맵을 한 번도 안 돌았다면 키 비대칭을 검사하지 않은 것이다.

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

if (-not (Test-Path -LiteralPath $Exe -PathType Leaf)) {
    "실행 파일이 없다: $Exe"
    exit 1
}
$Exe = (Resolve-Path -LiteralPath $Exe).Path
$root = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path

$run = Join-Path $Work ("CE_D3bAdapterParity_" + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $run -Force | Out-Null
$scenario = Join-Path $run 'commands.txt'
$stdout = Join-Path $run 'stdout.txt'
$stderr = Join-Path $run 'stderr.txt'
@('serialize.adapterparity', 'quit') | Set-Content -LiteralPath $scenario -Encoding UTF8

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
    '\[serialize\.adapterparity\] files=(\d+) nodes=(\d+) mapEntries=(\d+) diverge=(\d+)')
$aux = [regex]::Match($text, '\[serialize\.adapterparity\] skippedBinary=(\d+) parseFailures=(\d+)')
if (-not $summary.Success) {
    $failures.Add('요약 라인이 없다 — 명령 미등록/낡은 exe이거나 프로세스가 죽었다')
} else {
    $files = [int]$summary.Groups[1].Value
    $nodes = [long]$summary.Groups[2].Value
    $mapEntries = [long]$summary.Groups[3].Value
    $diverge = [int]$summary.Groups[4].Value

    # 코퍼스가 통째로 빠지면 "차이 0"이 공허해진다. 기준선은 2026-08-30 실측.
    if ($files -lt 250) { $failures.Add("파일 $files 개 — 코퍼스가 줄었다(기준선 278)") }
    if ($nodes -lt 10000) { $failures.Add("노드 $nodes 개 — 비교 범위가 줄었다(기준선 15339)") }
    if ($mapEntries -lt 10000) { $failures.Add("맵 항목 $mapEntries 개 — 키 비대칭을 검사하지 않았다(기준선 13814)") }
    if ($diverge -ne 0) { $failures.Add("어댑터 연산이 $diverge 건 갈린다") }
}
if ($aux.Success -and [int]$aux.Groups[2].Value -ne 0) {
    $failures.Add("파서 결과가 $($aux.Groups[2].Value) 건 어긋난다 — D3-b-0의 전제가 깨졌다")
}

$first = [regex]::Match($text, '\[serialize\.adapterparity\] first=(.+)')
$selfcheck = [regex]::Match($text, '\[serialize\.adapterparity\] selfcheck=(\w+)(?: reason=(\S+))?')
if (-not $selfcheck.Success) {
    $failures.Add('selfcheck 라인이 없다')
} elseif ($selfcheck.Groups[1].Value -ne 'pass') {
    $failures.Add("selfcheck=fail reason=$($selfcheck.Groups[2].Value)")
}

if ($summary.Success) {
    "files=$($summary.Groups[1].Value) nodes=$($summary.Groups[2].Value) mapEntries=$($summary.Groups[3].Value) diverge=$($summary.Groups[4].Value)"
}
if ($first.Success) { "first=$($first.Groups[1].Value.Trim())" }

if ($failures.Count -gt 0) {
    ''
    '실패:'
    $failures | ForEach-Object { "  - $_" }
    "출력: $run"
    exit 1
}

Remove-Item -LiteralPath $run -Recurse -Force -ErrorAction SilentlyContinue
'전체 통과 — 어댑터 연산이 두 backend에서 같은 답을 낸다'
exit 0
