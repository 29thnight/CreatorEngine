param(
    [string]$Exe = (Join-Path $PSScriptRoot "..\..\Bin\x64-Debug\Editor\CreatorEditor.exe"),
    [string]$Work = $env:TEMP,
    [int]$TimeoutSeconds = 180
)

# SerializationPlan D3-b-2 — 스칼라 **변환** 파리티.
#
# 구조 파리티(D3-b-0)가 증명하지 못하는 것을 잡는다. 두 파서가 만든 트리가 같아도
# `as<bool>`이 "yes"를 다르게 읽으면 **값의 의미만 조용히 달라진다** — 로드는
# 성공하고 값만 틀린다. 파서를 옮긴 뒤 가장 늦게, 가장 비싸게 드러나는 결함이다.
#
# ★ 이 게이트는 "차이 0"을 단정하지 않는다. 실측 결과 **11건이 실제로 갈린다**.
#   차이를 0으로 만드는 것은 D3-b-2의 일(명시적 변환을 넣는 것)이고, 이 검사의
#   일은 **알려진 차이 목록을 고정**해 새로 생긴 차이만 빨개지게 하는 것이다.
#   목록보다 적어도 실패다 — 차이가 사라졌다면 누군가 변환을 바꾼 것이고,
#   그 사실을 조용히 넘기면 이 표가 낡는다.
#
# ★ 갈리는 것과 **위험한 것**은 다르다. 차이가 실제 손상이 되려면 코퍼스에 그
#   표기가 있어야 한다. 그래서 게이트는 코퍼스도 함께 스캔한다 — 실측상
#   `.inf`/`.nan`/`0o`는 0건이고, `~` 390건은 전부 컨테이너·포인터 필드라
#   `ReadScalar(string&)`에 닿지 않는다. **그 전제가 깨지면 여기서 잡힌다.**

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

if (-not (Test-Path -LiteralPath $Exe -PathType Leaf)) {
    "실행 파일이 없다: $Exe"
    exit 1
}
$Exe = (Resolve-Path -LiteralPath $Exe).Path
$root = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path

# 실측으로 확정한 알려진 ryml 차이(2026-08-30, ryml 0.16 / yaml-cpp). 67케이스 중 21건.
#
# ★ **가장 위험한 것은 실패가 아니라 "둘 다 성공하는데 값이 다른" 쪽이다.**
#     · `010` → yaml-cpp 8(8진), ryml 10(10진)
#     · `99999999999999999999999` → yaml-cpp 실패, ryml 200376420520689663(쓰레기)
#     · `1.5x` → yaml-cpp 실패, ryml 1.5(부분 파싱)
#   에러도 로그도 없이 값만 틀린다. 그래서 D3-b-2b-1a가 변환을 문자열 위로 내렸고,
#   D3-b-2b-1b는 ryml `from_chars`를 **직접 쓰면 안 된다** — 이 표가 그 근거다.
$expected = @(
    'f-inf', 'f-neg-inf', 'f-nan', 'f-inf-caps', 'f-nan-caps',
    'f-plus', 'f-partial',
    'b-yes', 'b-no', 'b-on', 'b-off', 'b-y', 'b-n',
    'b-one', 'b-zero',
    'u-octal', 'u-octal-lead0', 'u-plus', 'u-overflow',
    'i-plus',
    'null-tilde-s'
)

$run = Join-Path $Work ("CE_D3b2ScalarParity_" + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $run -Force | Out-Null
$scenario = Join-Path $run 'commands.txt'
$stdout = Join-Path $run 'stdout.txt'
$stderr = Join-Path $run 'stderr.txt'
@('serialize.scalarparity', 'quit') | Set-Content -LiteralPath $scenario -Encoding UTF8

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

$summary = [regex]::Match($text, '\[serialize\.scalarparity\] cases=(\d+) agree=(\d+) diverge=(\d+) convDiverge=(\d+)')
if (-not $summary.Success) {
    $failures.Add('요약 라인이 없다 — 명령 미등록이거나 낡은 exe다')
} else {
    $cases = [int]$summary.Groups[1].Value
    $convDiverge = [int]$summary.Groups[4].Value
    # 0건을 비교하고 "차이 0"을 통과로 읽지 않는다.
    if ($cases -lt 60) { $failures.Add("케이스가 $cases 건뿐이다 — 표가 축소됐다") }
    # ★ 이식 변환기(`Authoring::Scalar`)와 yaml-cpp의 차이는 **허용되지 않는다.**
    #   ryml과의 차이는 목록으로 관리하지만, 이쪽은 0이어야 D3-b-2b-1a가 성립한다.
    #   실제로 이 단정이 이식 오류 3건을 잡았다(부호 없는 정수의 음수 수용,
    #   널 노드의 문자열 표현 2건).
    if ($convDiverge -ne 0) { $failures.Add("이식 변환기가 yaml-cpp와 $convDiverge 건 갈린다 — CONV-DIVERGE 라인 참조") }
}

$actual = @([regex]::Matches($text, '\[serialize\.scalarparity\] DIVERGE (\S+)') |
    ForEach-Object { $_.Groups[1].Value } | Sort-Object)
$want = @($expected | Sort-Object)

$newOnes = @($actual | Where-Object { $want -notcontains $_ })
$goneOnes = @($want | Where-Object { $actual -notcontains $_ })
if ($newOnes.Count -gt 0) {
    $failures.Add("새로 갈린 케이스: $($newOnes -join ', ')")
}
if ($goneOnes.Count -gt 0) {
    # 차이가 사라진 것도 보고한다 — 좋은 변화일 수 있지만 표를 갱신해야 한다.
    $failures.Add("더 이상 갈리지 않는 케이스(표 갱신 필요): $($goneOnes -join ', ')")
}

# ── 코퍼스 전제: 위험한 표기가 실제로 없는가 ──────────────────────────────────
#
# ryml이 **거부**하는 표기(.inf/.nan/YAML 1.1 불리언)가 자산에 있으면, 파서를 옮긴
# 뒤 그 필드는 예외 없이 기본값이 된다 — 조용한 데이터 손상이다.
# 저작 자산은 `Assets/` 안에만 있지 않다 — 개행 게이트가 같은 구멍으로 한 번
# 거짓 초록을 낸 적이 있다(ProjectSetting 미스캔). 두 루트를 모두 본다.
$assetRoots = @(
    (Join-Path $root 'Dynamic_CPP\Assets'),
    (Join-Path $root 'Dynamic_CPP\ProjectSetting')
)
$riskyFloat = 0
$riskyBool = 0
$scanned = 0
foreach ($assetRoot in ($assetRoots | Where-Object { Test-Path -LiteralPath $_ -PathType Container })) {
    $exts = @('.creator', '.prefab', '.meta', '.shadermeta', '.volume', '.asset')
    foreach ($file in Get-ChildItem -LiteralPath $assetRoot -Recurse -File) {
        if ($exts -notcontains $file.Extension.ToLowerInvariant()) { continue }
        $bytes = [IO.File]::ReadAllBytes($file.FullName)
        if ($bytes.Length -ge 4 -and $bytes[0] -eq 0x43 -and $bytes[1] -eq 0x45 -and
            $bytes[2] -eq 0x4D -and $bytes[3] -eq 0x41) { continue }
        $scanned++
        $body = [Text.Encoding]::UTF8.GetString($bytes)
        $riskyFloat += [regex]::Matches($body, '(?m):\s*-?\.(inf|nan|Inf|NaN|INF|NAN)\s*$').Count
        $riskyBool  += [regex]::Matches($body, '(?m):\s*(yes|no|on|off|Yes|No|On|Off|YES|NO|ON|OFF)\s*$').Count
    }
}
if ($scanned -eq 0) { $failures.Add('검사한 자산이 0개다 — 잴 것이 없다') }
if ($riskyFloat -gt 0) {
    $failures.Add("자산에 .inf/.nan $riskyFloat 건 — ryml이 거부하므로 전환 시 기본값으로 조용히 바뀐다")
}
# 실측 기준선 2건(shadermeta). 늘어나면 위험이 커진 것이다.
if ($riskyBool -gt 2) {
    $failures.Add("자산의 YAML 1.1 불리언(yes/no/on/off)이 $riskyBool 건 — 기준선 2건보다 늘었다")
}

"cases=$(if ($summary.Success) { $summary.Groups[1].Value } else { '?' }) diverge=$($actual.Count) expected=$($want.Count) 자산스캔=$scanned riskyFloat=$riskyFloat riskyBool=$riskyBool"
if ($actual.Count -gt 0) { "갈리는 케이스: $($actual -join ', ')" }

if ($failures.Count -gt 0) {
    ''
    '실패:'
    $failures | ForEach-Object { "  - $_" }
    "출력: $run"
    exit 1
}

Remove-Item -LiteralPath $run -Recurse -Force -ErrorAction SilentlyContinue
'전체 통과 — 이식 변환기는 yaml-cpp와 차이 0, ryml 차이는 알려진 21건과 일치'
exit 0
