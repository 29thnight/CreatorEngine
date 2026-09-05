# 생명주기 자기 제거·배수 재진입 게이트 (LC0 잔여).
#
# ── 무엇을 메우는가 ──
#
# 계획서 LC0의 실패 픽스처 목록에 "자기 제거 · 프레임 경계 재진입"이 남아 있었다.
# 기존 게이트는 실패(LC1/LC2/LC5)와 스레드 경계(LC5-b/c), 재진입 창(LC4)을 덮지만
# **자기 제거**는 어느 것도 보지 않는다.
#
# 게다가 LC5-b가 저작 코드가 도는 자리를 하나 늘렸다 — 배수 지점이다. 새 실행
# 지점을 만들고 그 위에서 무엇이 되는지 재지 않으면, 결함이 생겨도 어느 게이트도
# 보지 않는다. 그것까지 함께 메운다.
#
# ── 판정 ──
#
#   Q 축소 한 번   세 제거 픽스처가 end·removing·uninit 을 정확히 한 번씩 받는다
#   R 순회 무사고  틱 진입점 예외 0건 · 경계 밖 호출 거부 0건
#   S 배수 예산    배수 안의 재게시가 다음 프레임으로 넘어간다
#   T 대조군 온전  이웃이 순회에서 건너뛰어지지 않는다
#
# Q가 본체다. 자기 제거에서 가장 흔하게 깨지는 불변식이 "정확히 한 번"이다 —
# 네이티브 구동과 관리 폴백이 둘 다 부르면 두 번, 둘 다 미루면 0번이 된다.
# 0과 2를 한 판정으로 잡으려면 건수를 **정확히** 봐야 하고, 그래서 "1 이상"이
# 아니라 "1"이다.
#
# S는 건수로 재면 안 된다. 예산이 없어도 열 번은 다 온다 — 한 프레임에 몰릴
# 뿐이다. 가르는 것은 **몇 프레임에 걸쳤는가**다.
#
# R의 거부 0건은 이 시나리오의 모든 저작 코드가 게임 스레드에서 돈다는 뜻이다.
# 0이 아니면 배수나 훅 어딘가가 워커로 새어 나간 것이다.
#
# ── 이빨 확인 (2026-09-05) ──
#
#   Q  TearDown의 TeardownDelivered 가드 제거 → 세 픽스처 전부 2·2·2 로 빨강.
#      나머지 판정은 초록이었다.
#   S  Drain의 예산(진입 시점 건수)을 없앰 → 열 번이 1 프레임에 몰려 S(예산)만
#      빨강. 건수와 완주는 그대로였다 — 건수로 쟀다면 눈먼 초록이었을 자리다.
#
#   R·T는 여기서 변이로 붉히지 않았다. 같은 축을 이미 다른 게이트가 증명했다 —
#   틱 예외는 verify-lifecycle-reentrancy의 판정 L(변이 확인), 경계 밖 거부는
#   verify-lifecycle-thread의 판정 K다. T는 그 게이트의 판정 M과 같은 한계를
#   갖는다: 대조군의 훅을 **집합**으로 세므로 두어 프레임의 중단이 집합을 바꾸지
#   못한다. T는 이웃 격리가 아니라 하네스 생존 확인으로 읽어야 한다.
#
# 사용법: pwsh Tools\regression\verify-lifecycle-selfremove.ps1

param(
    [string]$Exe = (Join-Path $PSScriptRoot "..\..\Bin\x64-Debug\Editor\CreatorEditor.exe"),
    [string]$Work = $env:TEMP,
    [int]$TimeoutSeconds = 300
)

$ErrorActionPreference = 'Stop'

if (-not (Test-Path $Exe)) { "실행 파일이 없다: $Exe"; exit 1 }
$exeDir = [System.IO.Path]::GetDirectoryName($Exe)

$scenario = Join-Path $PSScriptRoot "lifecycle_selfremove_probe.txt"
if (-not (Test-Path $scenario)) { "시나리오가 없다: $scenario"; exit 1 }

"실행 파일: $Exe ($((Get-Item $Exe).LastWriteTime))"
$dll = Join-Path $exeDir "..\Managed\Scripts\GameScripts.dll"
if (Test-Path $dll) { "GameScripts: $((Get-Item $dll).LastWriteTime)" }
else { "GameScripts.dll을 찾지 못했다: $dll" }

# 로그를 고를 기준 시각. "가장 최신"만으로 고르면 이번 실행이 로그를 못 남겼을 때
# 옛 파일을 읽고 조용히 통과한다.
$runStart = Get-Date

$proc = Start-Process -FilePath $Exe -ArgumentList @("--script", $scenario) `
    -WorkingDirectory $exeDir `
    -RedirectStandardOutput (Join-Path $Work "lifecycle_selfremove.out") `
    -RedirectStandardError  (Join-Path $Work "lifecycle_selfremove.err") -PassThru

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

$rows = @([regex]::Matches($logText, '\[LC0\]\s+kind=(\w+)\s+point=(\w+)\s+frame=(\d+)\s+tid=(\d+)') |
    ForEach-Object {
        [pscustomobject]@{
            Kind  = $_.Groups[1].Value
            Point = $_.Groups[2].Value
            Frame = [long]$_.Groups[3].Value
            Tid   = [int]$_.Groups[4].Value
        }
    })

$controlHooks = @([regex]::Matches($logText, '\[Probe\]\s+([A-Za-z]+)\s+\p{Pd}\s+Control') |
    ForEach-Object { $_.Groups[1].Value })

if ($rows.Count -eq 0) {
    "픽스처 기록이 0건이다. 스크립트가 붙지 않았거나 재생에 들어가지 못했다."
    "로그: $($editorLog[0].FullName)"
    exit 1
}

""
"─ 픽스처 트레이스 ───────────────────────────────────────────────"
foreach ($r in $rows) { "  {0,-7} {1,-11} f={2,-5} tid={3}" -f $r.Kind, $r.Point, $r.Frame, $r.Tid }
""
"─ 대조군(Control) ───────────────────────────────────────────────"
if ($controlHooks.Count -eq 0) { "  (없음)" }
else { foreach ($h in $controlHooks) { "  $h" } }
""

function Rows-Of([string]$kind) { return @($rows | Where-Object { $_.Kind -eq $kind }) }
function Count-Of([string]$kind, [string]$point) {
    return @((Rows-Of $kind) | Where-Object { $_.Point -eq $point }).Count
}

$failed = New-Object System.Collections.Generic.List[string]

# 세 픽스처가 실제로 제거를 요청했는지 먼저 본다. 요청이 없었으면 아래 판정은
# 아무것도 시험하지 않은 초록이 된다.
foreach ($kind in @('begin', 'tick', 'resume')) {
    if ((Count-Of $kind 'destroyed') -lt 1) {
        "판정 전제: $kind 픽스처가 제거를 요청하지 못했다"
        "  → 그 자리까지 실행이 닿지 않았다. 이 실행은 자기 제거를 재지 못했다."
        $failed.Add("전제($kind)")
    }
}

# ── 판정 Q: 축소 삼단이 정확히 한 번씩 ────────────────────────────────────────

foreach ($kind in @('begin', 'tick', 'resume')) {
    $e = Count-Of $kind 'end'
    $r = Count-Of $kind 'removing'
    $u = Count-Of $kind 'uninit'
    "판정 Q 축소 한 번 [$kind]: end $e · removing $r · uninit $u (기대 1 / 1 / 1)"
    if ($e -ne 1 -or $r -ne 1 -or $u -ne 1) {
        if ($e -eq 0 -or $r -eq 0 -or $u -eq 0) {
            "  → 축소가 오지 않았다. 자기 제거가 정리 훅을 통째로 건너뛴다."
        }
        else {
            "  → 축소가 두 번 왔다. 네이티브 구동과 관리 폴백이 둘 다 부른 것이다"
            "     (TeardownDelivered 가드가 새는 자리)."
        }
        $failed.Add("Q($kind)")
    }
}

# ── 판정 R: 순회가 무사하고 아무것도 워커로 새지 않았다 ───────────────────────

$tickFaults = @([regex]::Matches($logText, '\[ScriptCore\]\s+(PrePhysicsTick|PostPhysicsTick|FlushRegistrations)\s+처리 중 예외')).Count
$refusals = @([regex]::Matches($logText, '\[Native\]\s+게임 스레드 밖에서\s+(\w+)\s+호출') |
    ForEach-Object { $_.Groups[1].Value })

"판정 R 순회 무사고: 틱 진입점 예외 $tickFaults 건 · 경계 밖 호출 거부 $($refusals.Count) 건 (기대 0 / 0)"
if ($tickFaults -gt 0) {
    "  → 자기 제거가 순회를 깨뜨렸다."
    $failed.Add('R(예외)')
}
if ($refusals.Count -gt 0) {
    "  거부된 API: $(($refusals | Sort-Object -Unique) -join ', ')"
    "  → 이 시나리오의 저작 코드는 전부 게임 스레드에서 돌아야 한다."
    "  → 배수나 훅 어딘가가 워커로 새어 나갔다."
    $failed.Add('R(스레드)')
}

# ── 판정 S: 배수 예산이 재게시를 다음 프레임으로 넘긴다 ───────────────────────

$yields = @((Rows-Of 'drain') | Where-Object { $_.Point -eq 'yield' })
$yieldFrames = @($yields | ForEach-Object { $_.Frame } | Sort-Object -Unique)
$done = Count-Of 'drain' 'done'

"판정 S 배수 예산: Yield 재개 $($yields.Count) 회가 $($yieldFrames.Count) 프레임에 걸쳤다 · 완주 $done (기대 10회 / 2 프레임 이상 / 1)"
if ($yields.Count -ne 10 -or $done -ne 1) {
    "  → 열 번이 다 오지 않았거나 루틴이 끝나지 않았다. 배수가 재게시를 잃는다."
    $failed.Add('S(완주)')
}
elseif ($yieldFrames.Count -lt 2) {
    "  → 열 번이 한 프레임에 몰렸다. 배수가 진입 시점의 건수로 끊지 않고"
    "     그 자리에서 계속 이어붙인다는 뜻이다 — 재게시가 멈추지 않으면 프레임이"
    "     영영 끝나지 않는다."
    $failed.Add('S(예산)')
}

# ── 판정 T: 이웃이 순회에서 건너뛰어지지 않는다 ───────────────────────────────

$required = @('Awake', 'AddedToScene', 'Enable', 'Start', 'SimulateStart',
              'Disable', 'EndSimulation', 'RemovingFromScene', 'Uninitializing')
$missing = @($required | Where-Object { $controlHooks -notcontains $_ })

"판정 T 대조군 온전: 필수 훅 $($required.Count) 종 중 누락 $($missing.Count) 건 (기대 0)"
if ($missing.Count -gt 0) {
    "  누락: $($missing -join ', ')"
    "  → 이웃이 훅을 잃었다. 제거가 순회 인덱스를 흔든 흔적이다."
    $failed.Add('T')
}

""
if ($failed.Count -gt 0) {
    "붉은 판정: $($failed -join ', ')"
    exit 1
}

"전체 통과 — 세 자리의 자기 제거가 축소를 한 번씩 주고, 배수 재진입이 프레임을 넘긴다"
exit 0
