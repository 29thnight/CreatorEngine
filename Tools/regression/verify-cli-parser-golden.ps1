[CmdletBinding()]
param(
    [string]$Exe  = (Join-Path $PSScriptRoot "..\..\Bin\x64-Debug\Editor\CreatorEditor.exe"),
    [string]$Work = (Join-Path $env:TEMP "lc0"),

    # 골든을 현재 관측으로 다시 뜬다. 형상이 **의도적으로** 바뀌는 슬라이스(LC2)에서만 쓴다.
    [switch]$UpdateGolden
)

# LC0 (PHASE 14.5) — tokenizer 형상 골든.
#
# ── 왜 지금 뜨는가 ──────────────────────────────────────────────────────
#
# LC2는 `Split()`의 규칙을 바꾼다(계획 §3.2): quote 안의 `\"`·`\\`만 escape로
# 처리하고 나머지 backslash는 보존, 닫히지 않은 quote는 `InvalidArguments`.
# 지금은 셋 다 다르다 — escape 처리가 아예 없고, 닫히지 않은 quote는 조용히
# 통과하며, `"`는 문자열 어디서든 상태를 뒤집는다(`a"b"c` → `abc`).
#
# 바꾸기 전에 **지금 무엇이 나오는지**를 바이트로 고정해 두지 않으면, LC2가 바꾼
# 것과 실수로 부순 것을 나중에 가를 수 없다. 골든의 값어치는 "옳다"가 아니라
# "무엇이 달라졌는지 한 줄까지 보인다"에 있다.
#
# ── 관측 수단 ───────────────────────────────────────────────────────────
#
# `cli.echo.args`가 토큰을 길이와 함께 되비춘다. 길이를 함께 찍는 이유는 빈
# 토큰과 없는 토큰이 화면에서 같아 보이기 때문이다 — `<<<>>>`가 둘 다이므로
# `len=0`이 있어야 구분된다.

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

if (-not (Test-Path -LiteralPath $Exe -PathType Leaf)) {
    "실행 파일이 없다: $Exe"
    exit 1
}
$exeDir = Split-Path -Parent $Exe
New-Item -ItemType Directory -Force -Path $Work | Out-Null

# 각 항목은 `cli.echo.args` 뒤에 붙일 인자 문자열 그대로다. 시나리오 파일은
# UTF-8로 쓰이고 --script가 줄 단위로 읽는다.
#
# ★ 골든에 넣을 값은 "지금 옳은 것"이 아니라 "지금 일어나는 것"이다. 아래 몇
#   항목은 명백한 결함을 고정한다 — 고의다. 결함이 언제 고쳐졌는지도 diff로 봐야 한다.
$cases = [ordered]@{
    'plain'              = 'alpha beta gamma'
    'quoted-name'        = '"Main Camera"'
    'two-quoted-names'   = '"Big Boss Character" "Main Characters"'
    'empty-quoted'       = '""'
    'empty-then-value'   = '"" value'
    'windows-path'       = 'C:\Users\test\Assets\Scenes\A.creator'
    'windows-path-quoted'= '"C:\Program Files\Creator\A.creator"'
    'backslash-quote'    = 'C:\dir\ "trailing"'
    'escaped-quote-try'  = '"say \"hi\""'
    'unclosed-quote'     = '"never closed'
    'quote-midtoken'     = 'a"b"c'
    'inner-quote-split'  = 'pre"mid post"tail'
    'tabs-and-spaces'    = "alpha`tbeta   gamma"
    'utf8-korean'        = '"메인 카메라" 오브젝트'
    'trailing-space'     = 'alpha '
    'only-spaces'        = '   '
}

$scriptPath = Join-Path $Work 'parser_golden.txt'
$lines = New-Object System.Collections.Generic.List[string]
foreach ($name in $cases.Keys) {
    # 마커를 별도 명령으로 넣는다. echo 출력만으로는 어느 케이스인지 알 수 없고,
    # 케이스 하나가 토큰 0개를 내면 출력이 아예 비어 경계가 사라진다.
    $lines.Add("cli.echo.args CASE $name")
    $lines.Add(("cli.echo.args " + $cases[$name]))
}
$lines.Add('quit')
($lines -join "`n") | Set-Content -LiteralPath $scriptPath -Encoding UTF8

$outPath = Join-Path $Work 'parser_golden.out'
$errPath = Join-Path $Work 'parser_golden.err'
$proc = Start-Process -FilePath $Exe -ArgumentList '--script', $scriptPath `
    -WorkingDirectory $exeDir `
    -RedirectStandardOutput $outPath -RedirectStandardError $errPath -PassThru
$proc.WaitForExit(180000) | Out-Null
if (-not $proc.HasExited) { $proc.Kill(); "TIMEOUT"; exit 1 }

$stdout = Get-Content -LiteralPath $outPath -Encoding UTF8

# `[CLI] cli.echo.args ...` 줄만 남긴다. 나머지 stdout(부팅·셧다운 로그)은
# 실행마다 다르고 tokenizer와 무관하다.
$observed = New-Object System.Collections.Generic.List[string]
foreach ($line in $stdout) {
    if ($line -notmatch '^\[CLI\] cli\.echo\.args ') { continue }
    $observed.Add($line.TrimEnd())
}

if ($observed.Count -eq 0) {
    "cli.echo.args 출력이 하나도 없다 — 명령이 등록되지 않았거나 시나리오가 실행되지 않았다."
    exit 1
}

$goldenPath = Join-Path $PSScriptRoot 'cli_parser_golden.expected'

if ($UpdateGolden -or -not (Test-Path -LiteralPath $goldenPath -PathType Leaf)) {
    ($observed -join "`n") + "`n" | Set-Content -LiteralPath $goldenPath -Encoding UTF8 -NoNewline
    "골든을 갱신했다: $goldenPath ($($observed.Count) 줄)"
    exit 0
}

$expected = @(Get-Content -LiteralPath $goldenPath -Encoding UTF8 | ForEach-Object { $_.TrimEnd() })

$diffs = New-Object System.Collections.Generic.List[string]
$max = [Math]::Max($expected.Count, $observed.Count)
for ($i = 0; $i -lt $max; $i++) {
    $e = if ($i -lt $expected.Count) { $expected[$i] } else { '<없음>' }
    $o = if ($i -lt $observed.Count) { $observed[$i] } else { '<없음>' }
    if ($e -ne $o) { $diffs.Add("  줄 $($i + 1)`n    기대: $e`n    관측: $o") }
}

if ($diffs.Count -gt 0) {
    "tokenizer 형상이 골든과 다르다 ($($diffs.Count) 줄):"
    $diffs | Select-Object -First 20 | ForEach-Object { $_ }
    if ($diffs.Count -gt 20) { "  ... 외 $($diffs.Count - 20) 줄" }
    ""
    "형상을 의도적으로 바꾼 슬라이스(LC2)라면"
    "  pwsh Tools/regression/verify-cli-parser-golden.ps1 -UpdateGolden"
    "로 다시 뜨고, 그 diff를 커밋에 남겨 무엇이 어떻게 바뀌었는지 보이게 한다."
    exit 1
}

"tokenizer 형상 골든 일치 ($($observed.Count) 줄 · 케이스 $($cases.Count) 개)"
exit 0
