param(
    [string]$Exe = (Join-Path $PSScriptRoot "..\..\Bin\x64-Debug\Editor\CreatorEditor.exe"),
    [string]$Work = $env:TEMP,
    [int]$TimeoutSeconds = 180
)

# SerializationPlan D3-b-1 — ryml 에러가 abort가 아니라 예외인가.
#
# ★ **이 게이트의 이빨은 종료 코드가 아니라 크래시다.** ryml의 기본 에러 처리는
#   예외도 반환값도 아니라 프로세스 abort다. 정책 콜백이 빠지거나 ryml이 나눈 세
#   채널(basic/parse/visit) 중 하나를 놓치면, 이 명령은 "fail"을 찍는 것이 아니라
#   **프로세스가 그 자리에서 죽는다.** 그래서 아래 검사는 종료 코드와 요약 라인의
#   존재를 함께 본다 — 요약이 없으면 그것이 곧 abort의 증거다.
#
# ★ 재현은 지어내지 말고 재라. 이 게이트를 처음 만들 때 "CRLF"와 "멀티라인 스칼라
#   키"를 트리거로 썼는데 **ryml이 둘 다 조용히 받아들였다** — 게이트가 초록인데
#   아무것도 증명하지 않는 상태였다. 14종을 실제로 태워 보고서야 알았다:
#     · **CRLF는 ryml 0.16이 정상 파싱한다**(단순 맵·시퀀스·블록 스칼라·주석 모두).
#       D3-b-0 프로브가 정규화 사본을 넣은 근거였던 "CRLF는 abort"는 과잉 일반화였다.
#     · 실제 트리거는 **홀로 선 CR**(basic 채널)과 **탭 들여쓰기**(parse 채널)다.
#   그래서 아래는 그 둘을 쓰고, CRLF는 오히려 **통과해야 하는 대조군**으로 둔다.
#
# ★ 채널이 갈리는지도 단정한다. 한 채널만 설치해도 두 입력이 모두 던질 수 있으므로,
#   예외 메시지의 채널 태그가 서로 다른지 봐야 "여러 채널을 덮었다"에 가까워진다.

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

if (-not (Test-Path -LiteralPath $Exe -PathType Leaf)) {
    "실행 파일이 없다: $Exe"
    exit 1
}
$Exe = (Resolve-Path -LiteralPath $Exe).Path
$root = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path

$run = Join-Path $Work ("CE_D3b1RymlError_" + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $run -Force | Out-Null
$scenario = Join-Path $run 'commands.txt'
$stdout = Join-Path $run 'stdout.txt'
$stderr = Join-Path $run 'stderr.txt'
@('serialize.rymlerror', 'quit') | Set-Content -LiteralPath $scenario -Encoding UTF8

$process = Start-Process -FilePath $Exe -ArgumentList @('--script', $scenario) `
    -WorkingDirectory $root -WindowStyle Hidden `
    -RedirectStandardOutput $stdout -RedirectStandardError $stderr -PassThru
$process.WaitForExit($TimeoutSeconds * 1000) | Out-Null
if (-not $process.HasExited) {
    $process.Kill()
    "TIMEOUT output=$run"
    exit 1
}
$exitCode = $process.ExitCode

$text = if (Test-Path -LiteralPath $stdout) { Get-Content -LiteralPath $stdout -Raw } else { '' }
$failures = New-Object System.Collections.Generic.List[string]

$detail = [regex]::Match($text,
    '\[serialize\.rymlerror\] loneCr=(\d+) tabIndent=(\d+) distinctChannels=(\d+)')
$controls = [regex]::Match($text,
    '\[serialize\.rymlerror\] validParsed=(\d+) crlfParsed=(\d+)')
if (-not $detail.Success) {
    # 여기가 abort를 잡는 자리다. 명령이 등록됐는데 요약이 없다면 프로세스가
    # 그 명령 도중에 죽었다는 뜻이다.
    $failures.Add('결과 라인이 없다 — 명령 미등록/낡은 exe이거나, ryml이 abort해 프로세스가 죽었다')
} else {
    if ([int]$detail.Groups[1].Value -ne 1) { $failures.Add('홀로 선 CR에서 예외가 오지 않았다 (basic 채널 미설치)') }
    if ([int]$detail.Groups[2].Value -ne 1) { $failures.Add('탭 들여쓰기에서 예외가 오지 않았다 (parse 채널 미설치)') }
    # 두 입력이 같은 채널을 탔다면 나머지 채널은 여전히 abort한다.
    if ([int]$detail.Groups[3].Value -ne 1) { $failures.Add('두 실패가 같은 채널이다 — 한 채널만 덮인 상태를 통과로 읽지 않는다') }
}
if (-not $controls.Success) {
    $failures.Add('대조군 라인이 없다')
} else {
    if ([int]$controls.Groups[1].Value -ne 1) { $failures.Add('정상 문서가 거부됐다 — 정책이 파싱 자체를 막고 있다') }
    # CRLF가 거부되면 파싱 전 정규화 사본이 다시 필요해지고 D3-b의 성능 계산이 바뀐다.
    if ([int]$controls.Groups[2].Value -ne 1) { $failures.Add('CRLF 문서가 거부됐다 — ryml 개행 처리가 바뀌었다(정규화 재도입 필요)') }
}

$message = [regex]::Match($text, '\[serialize\.rymlerror\] firstMessage=(.+)')
if (-not $message.Success) {
    $failures.Add('메시지 라인이 없다')
} elseif ($message.Groups[1].Value.Trim() -eq '(none)') {
    # 예외는 왔는데 what()이 비면 실제 실패에서 원인을 못 읽는다.
    $failures.Add('예외 메시지가 비었다 — 진단 불가')
} elseif ($message.Groups[1].Value.Trim() -notmatch '^\[(basic|parse|visit)\]') {
    # 채널 태그가 없으면 distinctChannels 판정 자체가 성립하지 않는다.
    $failures.Add('예외 메시지에 채널 태그가 없다')
}

$selfcheck = [regex]::Match($text, '\[serialize\.rymlerror\] selfcheck=(\w+)(?: reason=(\S+))?')
if (-not $selfcheck.Success) {
    $failures.Add('selfcheck 라인이 없다')
} elseif ($selfcheck.Groups[1].Value -ne 'pass') {
    $failures.Add("selfcheck=fail reason=$($selfcheck.Groups[2].Value)")
}

# abort는 종료 코드로도 드러난다(0xC0000409 등). 종료 코드를 검사하지 않으면
# "요약은 찍혔는데 그 뒤에 죽은" 경우를 놓친다.
if ($exitCode -ne 0) {
    $failures.Add("종료 코드 $exitCode — 정상 종료가 아니다")
}

$errText = if (Test-Path -LiteralPath $stderr) { (Get-Content -LiteralPath $stderr -Raw) } else { '' }
if ($errText -match 'check failed|assert|abort') {
    $failures.Add('stderr에 ryml assert/abort 흔적이 있다')
}

if ($detail.Success -and $controls.Success) {
    "loneCr=$([int]$detail.Groups[1].Value) tabIndent=$([int]$detail.Groups[2].Value) distinctChannels=$([int]$detail.Groups[3].Value) validParsed=$([int]$controls.Groups[1].Value) crlfParsed=$([int]$controls.Groups[2].Value) exit=$exitCode"
} else {
    "결과 없음 exit=$exitCode"
}
if ($message.Success) { "firstMessage=$($message.Groups[1].Value.Trim())" }

if ($failures.Count -gt 0) {
    ''
    '실패:'
    $failures | ForEach-Object { "  - $_" }
    "출력: $run"
    exit 1
}

Remove-Item -LiteralPath $run -Recurse -Force -ErrorAction SilentlyContinue
'전체 통과 — ryml 에러가 abort 대신 예외로 온다 (basic·parse 두 채널, 정상·CRLF 대조군 통과)'
exit 0
