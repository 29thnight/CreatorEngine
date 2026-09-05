# 엔진 API 진입 검사 비용 측정 (LC6).
#
# ★ 이것은 **판정이 아니라 측정**이다. `verify-` 가 아니라 `measure-` 인 이유이고,
#   회귀 세트(run-all)에도 넣지 않는다. 아래 실측이 보이듯 표본 흔들림이 ±25% 라
#   임계값을 걸면 거짓 실패가 난다. 필요할 때 손으로 돌려 수치를 읽는 도구다.
#
# ── 무엇을 재는가 ──
#
# LC5-c 가 엔진 API 206곳에 진입 검사(`Native.Entered`)를 넣었다. 검사 하나는 정적
# bool 읽기 + TLS 스레드 id 읽기 + int 비교인데, 그 비용이 한 번도 측정된 적이
# 없었다. 206곳에 기계적으로 넣었다는 사실만으로는 "싸다"고 말할 수 없다.
#
# 픽스처가 같은 프레임 안에서 두 벌을 잰다 — 검사를 지나는 실제 엔진 API
# (`Native.FrameCount`)와, 검사도 경계도 지나지 않는 관리 측 기준선. 그 차이가
# `delta_ns` 다.
#
# ── 2026-09-06 실측 (Release · 호출 100만 × 표본 15) ──
#
#   검사 있음   중앙값 10.588 ns   범위 8.64 ~ 13.10
#   검사 없음   중앙값  9.648 ns   범위 8.03 ~ 13.02
#
# 두 분포가 거의 완전히 겹친다. 중앙값 차이 0.94 ns 는 표본 흔들림(±2.5 ns)보다
# 작다. **검사의 몫은 이 하네스의 분해능 아래이고**, 말할 수 있는 것은 상한이
# 대략 1 ns/호출이라는 것까지다.
#
# "검사 없음"은 `Entered` 의 TLS 읽기·비교 줄을 `return true;` 로 바꿔 잰 값이다.
# 그 변이는 스레드 검사를 없애므로 `verify-lifecycle-thread` 가 붉어진다 — 측정
# 뒤에는 반드시 되돌려야 한다.
#
# ── 이 수치가 말하는 것 ──
#
# 정작 비싼 것은 검사가 아니라 **경계 크로싱**이다. 엔진 API 호출 하나가 ~10 ns 인데
# 관리 측 필드 읽기는 0.3 ns 다 — 30배. 검사는 그 10 ns 의 10% 안쪽이다.
#
# 첫 판은 호출 10만 × 표본 5로 쟀는데 흔들림(11.4~16.0 ns)이 재려는 차이(~2 ns)보다
# 커서 아무것도 가르지 못했다. 호출을 열 배, 표본을 셋 배로 올린 것이 지금 값이다.
# 그러고도 가르지 못했다는 것이 결론이다.
#
# 사용법: pwsh Tools\regression\measure-entry-guard-cost.ps1

param(
    [string]$Exe = (Join-Path $PSScriptRoot "..\..\Bin\x64-Release\Editor\CreatorEditor.exe"),
    [string]$Work = $env:TEMP,
    [int]$TimeoutSeconds = 300
)

$ErrorActionPreference = 'Stop'

if (-not (Test-Path $Exe)) {
    "실행 파일이 없다: $Exe"
    "  성능 판정은 Release 로만 유효하다 — Debug 는 같은 조건에서 25배 느리고"
    "  규모별 개선 방향까지 뒤집는다. 먼저 Release 를 빌드하라."
    exit 1
}

if ($Exe -notmatch 'Release') {
    "★ Release 가 아닌 바이너리다: $Exe"
    "  이 측정은 Release 에서만 뜻이 있다. 그대로 진행하지만 수치를 믿지 마라."
}

$exeDir = [System.IO.Path]::GetDirectoryName($Exe)
$scenario = Join-Path $PSScriptRoot "lifecycle_entryguard_bench.txt"
if (-not (Test-Path $scenario)) { "시나리오가 없다: $scenario"; exit 1 }

"실행 파일: $Exe ($((Get-Item $Exe).LastWriteTime))"
$dll = Join-Path $exeDir "..\Managed\Scripts\GameScripts.dll"
if (Test-Path $dll) { "GameScripts: $((Get-Item $dll).LastWriteTime)" }

$runStart = Get-Date

$proc = Start-Process -FilePath $Exe -ArgumentList @("--script", $scenario) `
    -WorkingDirectory $exeDir `
    -RedirectStandardOutput (Join-Path $Work "entryguard_bench.out") `
    -RedirectStandardError  (Join-Path $Work "entryguard_bench.err") -PassThru

$proc.WaitForExit($TimeoutSeconds * 1000) | Out-Null
if (-not $proc.HasExited) { $proc.Kill(); "타임아웃 ($TimeoutSeconds 초)."; exit 1 }

$logDir = Join-Path $exeDir "Saved\Log"
$editorLog = @(Get-ChildItem (Join-Path $logDir "Editor_*.html") -ErrorAction SilentlyContinue |
    Where-Object { $_.LastWriteTime -ge $runStart } |
    Sort-Object LastWriteTime -Descending)

if ($editorLog.Count -eq 0) {
    "이번 실행의 에디터 로그가 없다: $logDir\Editor_*.html ($runStart 이후)"
    exit 1
}

$logText = (Get-Content -LiteralPath $editorLog[0].FullName -Raw) -replace '<[^>]+>', ''

$rows = @([regex]::Matches($logText, '\[LC6\] sample=(\d+) calls=(\d+) guarded_ns=([\d.]+) bare_ns=([\d.]+) delta_ns=([\d.]+)') |
    ForEach-Object {
        [pscustomobject]@{
            Sample  = [int]$_.Groups[1].Value
            Calls   = [int]$_.Groups[2].Value
            Guarded = [double]$_.Groups[3].Value
            Bare    = [double]$_.Groups[4].Value
            Delta   = [double]$_.Groups[5].Value
        }
    })

if ($rows.Count -eq 0) {
    "표본이 0건이다. 재생에 들어가지 못했거나 재생 구간이 짧아 표본이 차기 전에 정지했다."
    "로그: $($editorLog[0].FullName)"
    exit 1
}

function Stat($values) {
    $sorted = @($values | Sort-Object)
    return [pscustomobject]@{
        Median = $sorted[[int]($sorted.Count / 2)]
        Min    = $sorted[0]
        Max    = $sorted[-1]
    }
}

$g = Stat (@($rows | ForEach-Object { $_.Guarded }))
$b = Stat (@($rows | ForEach-Object { $_.Bare }))
$d = Stat (@($rows | ForEach-Object { $_.Delta }))

""
"표본 $($rows.Count) 개 · 표본당 호출 $($rows[0].Calls) 회"
""
"                  중앙값        최소        최대"
"  엔진 API      {0,8:0.000}    {1,8:0.000}   {2,8:0.000}  ns/호출" -f $g.Median, $g.Min, $g.Max
"  관리 기준선   {0,8:0.000}    {1,8:0.000}   {2,8:0.000}  ns/호출" -f $b.Median, $b.Min, $b.Max
"  차이          {0,8:0.000}    {1,8:0.000}   {2,8:0.000}  ns/호출" -f $d.Median, $d.Min, $d.Max
""
"  흔들림 폭: {0:0.000} ns (최대 - 최소)" -f ($d.Max - $d.Min)
""
"차이가 곧 경계 크로싱 + 진입 검사의 몫이다. 검사만의 몫을 가르려면 Native.Entered 의"
"TLS 읽기·비교 줄을 지우고 다시 재야 하고, 2026-09-06 실측에서는 그 차이(0.94 ns)가"
"흔들림 안에 묻혀 분리되지 않았다 — 상한 ~1 ns/호출."
exit 0
