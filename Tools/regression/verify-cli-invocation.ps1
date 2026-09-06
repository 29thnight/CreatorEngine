[CmdletBinding()]
param(
    [string]$Exe  = (Join-Path $PSScriptRoot "..\..\Bin\x64-Debug\Editor\CreatorEditor.exe"),
    [string]$Work = (Join-Path $env:TEMP "lc2")
)

# LC2 (PHASE 14.5) — 소유형 invocation 게이트.
#
# `verify-cli-parser-golden.ps1` 이 tokenizer 의 **형상**을 바이트로 고정한다면,
# 이 게이트는 그 토큰이 **실제 조작까지 온전히 닿는가**를 본다. 둘은 다른 물건이다 —
# 예전 결함은 tokenizer 가 옳게 잘라 놓은 것을 핸들러가 버리고 원문을 다시
# 자르는 데 있었으므로(§3.2), 형상만 보는 골든으로는 잡히지 않았다.
#
# ── 재현하던 결함 ───────────────────────────────────────────────────────
#
#     object.parent "Big Boss Character" "Main Characters"
#
#   `parts.back()` 은 `Main Characters`(따옴표 없음)인데 핸들러가 그것을
#   **원문에서** rfind 해서 앞을 잘랐다. 원문에는 따옴표가 남아 있으니 복원된
#   자식 이름은 `"Big Boss Character"` — 따옴표째다. 씬에서 영영 못 찾는다.
#   그런데 그 실패는 "이름을 못 찾음" 한 줄로만 남아서, 따옴표 문법이 아예
#   동작하지 않는다는 사실이 오래 보이지 않았다.

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

if (-not (Test-Path -LiteralPath $Exe -PathType Leaf)) { "실행 파일이 없다: $Exe"; exit 1 }
$exeDir = Split-Path -Parent $Exe
New-Item -ItemType Directory -Force -Path $Work | Out-Null

$failures = New-Object System.Collections.Generic.List[string]

function Invoke-Script {
    param([string]$Name, [string[]]$Commands)
    $scriptPath = Join-Path $Work "inv_$Name.txt"
    (($Commands + 'quit') -join "`n") | Set-Content -LiteralPath $scriptPath -Encoding UTF8
    $outPath = Join-Path $Work "inv_$Name.out"
    $proc = Start-Process -FilePath $Exe -ArgumentList '--commandlet-script', $scriptPath -WorkingDirectory $exeDir `
        -RedirectStandardOutput $outPath -RedirectStandardError (Join-Path $Work "inv_$Name.err") -PassThru
    $proc.WaitForExit(180000) | Out-Null
    if (-not $proc.HasExited) { $proc.Kill(); return $null }
    [pscustomobject]@{ ExitCode = $proc.ExitCode; Stdout = (Get-Content -LiteralPath $outPath -Raw -ErrorAction SilentlyContinue) }
}

# argv 를 그대로 넘긴다. PowerShell 의 -ArgumentList 문자열 결합을 쓰면 공백
# 있는 인자가 셸 단계에서 갈라져 이 검사가 재려는 것 자체가 사라진다.
function Invoke-Argv {
    param([string]$Name, [string[]]$Arguments)
    $psi = [Diagnostics.ProcessStartInfo]::new()
    $psi.FileName               = $Exe
    $psi.WorkingDirectory       = $exeDir
    $psi.UseShellExecute        = $false
    $psi.RedirectStandardOutput = $true
    $psi.RedirectStandardError  = $true
    $psi.StandardOutputEncoding = [Text.Encoding]::UTF8
    foreach ($a in $Arguments) { $null = $psi.ArgumentList.Add($a) }
    $proc = [Diagnostics.Process]::Start($psi)
    $text = $proc.StandardOutput.ReadToEnd()
    $null = $proc.StandardError.ReadToEnd()
    if (-not $proc.WaitForExit(180000)) { $proc.Kill(); return $null }
    [pscustomobject]@{ ExitCode = $proc.ExitCode; Stdout = $text }
}

function Get-ArgLines([string]$Text) {
    if ([string]::IsNullOrEmpty($Text)) { return @() }
    # line_len 은 뺀다 — 구조화 경로의 진단용 재구성에는 따옴표가 없어 길이가
    # 다르다. 그것은 재구성 문자열의 성질이지 invocation 의 차이가 아니다.
    @($Text -split "`n" |
      Where-Object { $_ -match '^\[CLI\] cli\.echo\.args arg\[' } |
      ForEach-Object { $_.TrimEnd() })
}

# ── 1) 따옴표 두 이름이 실제 씬 조작을 통과한다 ────────────────────────
#
# 판정을 "찾을 수 없음이 없다"로만 두지 않는다. 그것은 명령이 아예 안 돌아도
# 참이다. 계층이 실제로 바뀐 것을 hierarchycheck 의 최상위 수로 확인한다.
$scene = Invoke-Script 'quoted_parent' @(
    'scene.new LC2Probe',
    'object.create "Big Boss Character"',
    'object.create "Main Characters"',
    'object.parent "Big Boss Character" "Main Characters"',
    'scene.hierarchycheck'
)
if ($null -eq $scene) { $failures.Add('quoted-parent : 타임아웃') }
else {
    if ($scene.Stdout -match '찾을 수 없음') {
        $failures.Add('quoted-parent : 오브젝트를 못 찾았다 — 따옴표가 이름에 새고 있다')
    }
    $match = [regex]::Match($scene.Stdout, '\[scene\.hierarchycheck\] 오브젝트 (\d+) · 최상위\(0표기\) (\d+)')
    if (-not $match.Success) { $failures.Add('quoted-parent : hierarchycheck 출력이 없다') }
    else {
        $objects = [int]$match.Groups[1].Value
        $roots   = [int]$match.Groups[2].Value
        "quoted-parent            오브젝트 $objects · 최상위 $roots (기대 3 / 1)"
        # 씬 루트 + 만든 둘 = 3. 부모 지정이 실패했다면 최상위가 2다.
        if ($objects -ne 3 -or $roots -ne 1) {
            $failures.Add("quoted-parent : 계층이 기대와 다르다(오브젝트 $objects · 최상위 $roots) — 부모 지정이 반영되지 않았다")
        }
    }
}

# ── 2) 라인 경로와 구조화 경로가 같은 invocation 을 만든다 ─────────────
$viaLine = Invoke-Script 'equiv_line' @('cli.echo.args "Big Boss Character" "Main Characters"')
$viaArgv = Invoke-Argv  'equiv_argv' @('--exec-args', 'cli.echo.args', 'Big Boss Character', 'Main Characters', '--', '--exec', 'quit')

$lineArgs = Get-ArgLines $(if ($null -ne $viaLine) { $viaLine.Stdout } else { '' })
$argvArgs = Get-ArgLines $(if ($null -ne $viaArgv) { $viaArgv.Stdout } else { '' })

"line-vs-argv             라인 $($lineArgs.Count) 줄 · 구조화 $($argvArgs.Count) 줄"
if ($lineArgs.Count -eq 0) { $failures.Add('line-vs-argv : 라인 경로 출력이 없다') }
elseif ($argvArgs.Count -eq 0) { $failures.Add('line-vs-argv : 구조화 경로 출력이 없다') }
elseif (Compare-Object $lineArgs $argvArgs) {
    $failures.Add('line-vs-argv : 두 입력 경로가 다른 invocation 을 만들었다')
    "  라인 : $($lineArgs -join ' | ')"
    "  구조화: $($argvArgs -join ' | ')"
}

# ── 3) 문법 오류가 오류로 끝난다 ───────────────────────────────────────
$unclosed = Invoke-Script 'unclosed' @('cli.echo.args "never closed')
if ($null -eq $unclosed) { $failures.Add('unclosed-quote : 타임아웃') }
else {
    "unclosed-quote           exit=$($unclosed.ExitCode) (기대 2)"
    if ($unclosed.ExitCode -ne 2) {
        $failures.Add("unclosed-quote : exit $($unclosed.ExitCode) ≠ 2 — 닫히지 않은 따옴표가 조용히 통과한다")
    }
    if ($unclosed.Stdout -notmatch 'parse\.unclosed_quote') {
        $failures.Add('unclosed-quote : 사유 코드가 없다')
    }
}

# ── 4) Windows 경로가 escape 로 소비되지 않는다 ────────────────────────
#
# 따옴표 밖에서도 안에서도 backslash 가 살아야 한다. `\"`·`\\` 만 escape 이고
# 나머지는 보존이라는 규칙이 지켜지는지 본다.
$paths = Invoke-Script 'paths' @(
    'cli.echo.args C:\Users\test\Assets\A.creator',
    'cli.echo.args "C:\Program Files\Creator\A.creator"'
)
if ($null -eq $paths) { $failures.Add('windows-path : 타임아웃') }
else {
    foreach ($expected in @('C:\Users\test\Assets\A.creator', 'C:\Program Files\Creator\A.creator')) {
        if ($paths.Stdout -notmatch [regex]::Escape("<<<$expected>>>")) {
            $failures.Add("windows-path : 경로가 보존되지 않았다: $expected")
        }
    }
    "windows-path             backslash 보존 확인"
}

# ── 5) 따옴표가 값의 공백을 그대로 보존한다 ────────────────────────────
#
# ★ 이것이 자유 형식 payload 의 계약이다.
#
#   `object.property` · `script.set` 은 이름이 아니라 **값**을 받는다. 그 값에
#   공백 연속이 들어갈 수 있는데, 따옴표 없이 여러 토큰으로 주면 `JoinFrom` 이
#   공백 연속을 하나로 접는다. §3.2 의 결정("공백 포함 문자열의 배치 문법은
#   quote")이 그래서 계약이고, 따옴표로 감싼 값은 한 토큰이라 내부 공백이
#   손대지 않은 채로 간다. 그 성질을 여기서 못박는다.
#
#   접히는 쪽(따옴표 없는 여러 토큰)은 오류가 아니라 legacy 형식이고, 엔진이
#   그 사용을 센다(CommandCore::LegacyJoinUseCount). LC9 가 그 수로 제거 시점을 정한다.
$spaces = Invoke-Script 'payload_spaces' @('cli.echo.args "value  with   runs"')
if ($null -eq $spaces) { $failures.Add('payload-spaces : 타임아웃') }
else {
    if ($spaces.Stdout -notmatch [regex]::Escape('<<<value  with   runs>>>')) {
        $failures.Add('payload-spaces : 따옴표 안의 공백 연속이 보존되지 않았다 — 자유 형식 값의 계약이 깨졌다')
    }
    else { "payload-spaces           따옴표 안 공백 연속 보존 확인" }
}

""
if ($failures.Count -gt 0) {
    "실패:"
    $failures | ForEach-Object { "  - $_" }
    exit 1
}
"invocation 전체 통과"
exit 0
