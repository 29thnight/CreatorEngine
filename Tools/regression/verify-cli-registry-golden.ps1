[CmdletBinding()]
param(
    [string]$Exe  = (Join-Path $PSScriptRoot "..\..\Bin\x64-Debug\Editor\CreatorEditor.exe"),
    [string]$Work = (Join-Path $env:TEMP "lc6"),

    # 골든을 지금 상태로 다시 기록한다. **의도한 변경일 때만** 쓴다.
    [switch]$Update
)

# PHASE 14.5 LC6 — 명령 표 보존 골든.
#
# ── 왜 이 게이트가 따로 필요한가 ────────────────────────────────────────
#
# LC3 의 discovery 게이트는 registry 의 **자기 일관성**을 본다: 이름 중복,
# help 커버리지, help 고아, 결정성. 그것들은 전부 같은 registry 에서 파생된
# 두 값을 맞대 보는 검사다.
#
# 그래서 명령 하나가 **registry 와 help 양쪽에서 함께 사라지면 그 게이트는
# 초록으로 남는다.** 자기 일관성은 유지되기 때문이다. LC6 은 8,700 줄짜리
# 핸들러 본문을 도메인 파일 일곱 개로 옮긴다 — 등록 한 줄을 빠뜨리는 것이
# 가장 흔한 사고이고, 그 사고가 정확히 이 사각지대로 들어간다.
#
# 이 게이트는 자기 일관성이 아니라 **보존**을 본다. 이동 전에 찍어 둔 골든과
# 지금의 표가 한 글자라도 다르면 붉어진다.
#
# ── 골든을 갱신해야 할 때 ───────────────────────────────────────────────
#
# 명령을 **의도적으로** 더하거나 빼거나 descriptor 를 고쳤으면, 그때는
# `-Update` 로 골든을 다시 기록하고 그 변경을 커밋에 함께 담는다. 갱신이
# 커밋에 보이는 것이 요점이다 — 표가 조용히 움직이지 않게 한다.

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$Work = [IO.Path]::GetFullPath($Work)
$Exe = [IO.Path]::GetFullPath($Exe)

$goldenPath = Join-Path $PSScriptRoot 'cli_registry.golden.tsv'

if (-not (Test-Path -LiteralPath $Exe -PathType Leaf)) { "실행 파일이 없다: $Exe"; exit 1 }
$exeDir = Split-Path -Parent $Exe
New-Item -ItemType Directory -Force -Path $Work | Out-Null

$snapshotPath = Join-Path $Work 'registry_now.tsv'
if (Test-Path -LiteralPath $snapshotPath) { Remove-Item -LiteralPath $snapshotPath -Force }

$scriptPath = Join-Path $Work 'registry_golden_cmd.txt'
Set-Content -LiteralPath $scriptPath -Encoding UTF8 -Value @(
    'commands.list "' + $snapshotPath + '"'
    'quit'
)

$proc = Start-Process -FilePath $Exe -ArgumentList @('--script', ('"'+$scriptPath+'"')) -WindowStyle Hidden -WorkingDirectory $exeDir `
    -RedirectStandardOutput (Join-Path $Work 'registry_golden.out') `
    -RedirectStandardError  (Join-Path $Work 'registry_golden.err') -PassThru
$proc.WaitForExit(180000) | Out-Null
if (-not $proc.HasExited) { $proc.Kill(); "snapshot 생성 타임아웃"; exit 1 }
if (-not (Test-Path -LiteralPath $snapshotPath)) { "snapshot 이 만들어지지 않았다"; exit 1 }

if ($proc.ExitCode -ne 0) { "Registry snapshot process failed: $($proc.ExitCode)"; exit 1 }

if ($Update) {
    Copy-Item -LiteralPath $snapshotPath -Destination $goldenPath -Force
    "골든을 지금 상태로 갱신했다: $goldenPath"
    "  이 변경을 커밋에 함께 담을 것 — 표가 왜 움직였는지가 기록에 남아야 한다."
    exit 0
}

if (-not (Test-Path -LiteralPath $goldenPath)) {
    "골든이 없다: $goldenPath (-Update 로 최초 기록)"
    exit 1
}

# 줄 끝(CRLF/LF)은 .gitattributes 의 text=auto 가 작업본에서 바꿀 수 있다.
# 그것은 표의 내용이 아니므로 비교에서 뺀다.
function Read-Rows([string]$Path) {
    $rows = [ordered]@{}
    $meta = [ordered]@{}
    $header = @()
    foreach ($line in [IO.File]::ReadAllLines($Path)) {
        $line = $line.TrimEnd("`r")
        if ($line -eq '') { continue }
        if ($line.StartsWith('#')) {
            $bits = $line.Split("`t")
            if ($bits.Count -ge 2) { $meta[$bits[0]] = $bits[1] }
            continue
        }
        $name = $line.Split("`t")[0]
        if ($name -eq 'canonical') { $header = $line.Split("`t"); continue }
        $rows[$name] = $line
    }
    return @{ Rows = $rows; Meta = $meta; Header = $header }
}

$golden = Read-Rows $goldenPath
$now    = Read-Rows $snapshotPath

$failures = New-Object System.Collections.Generic.List[string]

$goldNames = @($golden.Rows.Keys)
$nowNames  = @($now.Rows.Keys)

$removed = @($goldNames | Where-Object { -not $now.Rows.Contains($_) })
$added   = @($nowNames  | Where-Object { -not $golden.Rows.Contains($_) })
$changed = @($goldNames | Where-Object { $now.Rows.Contains($_) -and $now.Rows[$_] -ne $golden.Rows[$_] })

"{0,-24} 골든 {1}개 · 현재 {2}개" -f 'registry-golden', $goldNames.Count, $nowNames.Count

# ★ 열 구성이 바뀌면 **위치 비교가 무의미하다.**
#
#   행마다 n 번째 칸을 맞대는 방식이라, 열이 하나 끼어들면 그 뒤가 전부 밀려
#   "result_bearing 이 engine_service 로 바뀌었다" 같은 거짓 설명이 212 행 ×
#   여러 칸 쏟아진다. 진짜 사실 하나(스키마가 바뀌었다)가 잡음에 묻힌다.
#   그러니 먼저 그 하나만 말하고 멈춘다.
$goldHeader = ($golden.Header -join "`t")
$nowHeader  = ($now.Header -join "`t")
if ($goldHeader -ne $nowHeader) {
    ""
    "실패:"
    "  - snapshot 스키마가 바뀌었다(열 구성)"
    "      골든: $goldHeader"
    "      현재: $nowHeader"
    ""
    "열이 바뀌면 행 비교는 뜻이 없다. 의도한 변경이면 -Update 로 골든을 다시"
    "기록하고, 소비자(열 이름으로 읽는 하네스)가 따라오는지 함께 확인할 것."
    exit 1
}

if ($removed.Count -gt 0) {
    $failures.Add("사라진 명령 $($removed.Count)개: $(($removed | Select-Object -First 8) -join ', ')")
}
if ($added.Count -gt 0) {
    $failures.Add("추가된 명령 $($added.Count)개: $(($added | Select-Object -First 8) -join ', ')")
}
foreach ($name in ($changed | Select-Object -First 8)) {
    $g = $golden.Rows[$name].Split("`t")
    $n = $now.Rows[$name].Split("`t")
    # ★ 열 이름을 **파일 머리글에서 읽는다.** 박아 두면 열이 늘어난 순간
    #   진단이 어긋난 이름을 댄다 — LC6 이 class·liveness 를 넣자 실제로
    #   "result_bearing 이 engine_service 로 바뀌었다" 같은 거짓 설명이 나왔다.
    #   틀린 진단은 없는 진단보다 나쁘다.
    $cols = if ($golden.Header.Count -gt 0) { $golden.Header } else { $now.Header }
    for ($i = 0; $i -lt [Math]::Max($g.Count, $n.Count); $i++) {
        $gv = if ($i -lt $g.Count) { $g[$i] } else { '(없음)' }
        $nv = if ($i -lt $n.Count) { $n[$i] } else { '(없음)' }
        if ($gv -ne $nv) {
            $col = if ($i -lt $cols.Count) { $cols[$i] } else { "col$i" }
            $failures.Add("바뀐 필드 ${name}.${col}: '$gv' -> '$nv'")
        }
    }
}
if ($changed.Count -gt 8) {
    $failures.Add("... 바뀐 명령이 $($changed.Count)개다(앞 8개만 표시)")
}

# 머리글의 집계도 함께 본다. 행이 우연히 맞아도 이름(별칭 포함) 수가 어긋나면
# 별칭이 사라진 것이다 — 별칭은 소비자가 부르는 이름이라 사라지면 호출이 깨진다.
foreach ($key in @('# commands', '# names')) {
    $gv = if ($golden.Meta.Contains($key)) { $golden.Meta[$key] } else { '(없음)' }
    $nv = if ($now.Meta.Contains($key))    { $now.Meta[$key] }    else { '(없음)' }
    if ($gv -ne $nv) { $failures.Add("$key 가 $gv 에서 $nv 로 바뀌었다") }
}

""
if ($failures.Count -gt 0) {
    "실패:"
    $failures | ForEach-Object { "  - $_" }
    ""
    "이 변경이 의도한 것이라면 -Update 로 골든을 다시 기록하고 커밋에 함께 담을 것."
    exit 1
}
"명령 표 보존 통과 — 골든과 한 글자도 다르지 않다"
exit 0
