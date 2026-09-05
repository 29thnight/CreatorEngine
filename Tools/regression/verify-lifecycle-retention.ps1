# 생명주기 대기 참조 유지 게이트 (LC3).
#
# ★ 결함을 증명하려고 붉은 채로 세웠고, LC3가 착지해 2026-09-05에 초록이 됐다.
#   판정 V 가 50/50 에서 0/50 으로 넘어갔다.
#
# ── 재는 것 ──
#
# SimulationScope.Delay 는 token.Register(...) 의 반환
# CancellationTokenRegistration 을 보관하지도 해제하지도 않는다. 그 등록의 클로저가
# PendingDelay 를 잡고, 그것이 TaskCompletionSource 를, 그것이 완료된 Task 를 잡는다.
# _pending 에서는 빠졌는데 취소 토큰 쪽에서 여전히 도달할 수 있다.
#
# 그래서 오래 사는 스코프가 대기를 반복하면 완료된 Task 와 캡처가 스코프 취소까지
# 쌓인다.
#
# ── 관측 축을 메모리 총량으로 잡지 않는 이유 ──
#
# GC.GetTotalMemory 는 엔진 전체의 잡음을 함께 잰다. 몇십 KB 차이는 묻히고, 묻히지
# 않게 규모를 키우면 시나리오가 길어진다. 대신 **도달 가능성**을 직접 센다 —
# 완료된 Task 마다 약한 참조를 남기고 강한 참조를 버린 뒤 GC 를 돌려 몇 개가
# 살아남는지 본다. 잡음이 0 이고, 살아남은 개수가 곧 누수 개수다.
#
# 이것은 계획서의 완료 조건 문장 그대로다 — "스코프를 살려 둔 채 완료 Task 의
# 외부 참조를 제거하면 회수된다".
#
# ── 판정 ──
#
#   U 생성 확인   50 개를 실제로 만들었다 (전제)
#   V 회수        스코프가 살아 있는 채로 완료 Task 가 전부 회수된다
#   W 축소 온전   참조 해제가 종료 절차를 깨뜨리지 않는다
#
# U 가 없으면 V 가 빈 집합을 보고 초록이 된다 — 0 개를 만들면 0 개가 살아남는다.
#
# W 를 함께 두는 이유는 이 고침이 건드리는 것이 **취소 등록**이기 때문이다.
# 등록을 잘못 놓으면 취소 통지가 끊겨 대기가 영영 안 풀리거나, 반대로 Dispose 가
# 콜백을 기다려 서로 막힌다. 어느 쪽이든 축소 삼단이 오지 않는 것으로 드러난다.
#
# 사용법: pwsh Tools\regression\verify-lifecycle-retention.ps1

param(
    [string]$Exe = (Join-Path $PSScriptRoot "..\..\Bin\x64-Debug\Editor\CreatorEditor.exe"),
    [string]$Work = $env:TEMP,
    [int]$TimeoutSeconds = 300
)

$ErrorActionPreference = 'Stop'

if (-not (Test-Path $Exe)) { "실행 파일이 없다: $Exe"; exit 1 }
$exeDir = [System.IO.Path]::GetDirectoryName($Exe)

$scenario = Join-Path $PSScriptRoot "lifecycle_retention_probe.txt"
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
    -RedirectStandardOutput (Join-Path $Work "lifecycle_retention.out") `
    -RedirectStandardError  (Join-Path $Work "lifecycle_retention.err") -PassThru

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

$points = @([regex]::Matches($logText, '\[LC3\]\s+point=(\S+)\s+frame=(\d+)') |
    ForEach-Object {
        [pscustomobject]@{ Point = $_.Groups[1].Value; Frame = [long]$_.Groups[2].Value }
    })

if ($points.Count -eq 0) {
    "픽스처 기록이 0건이다. 스크립트가 붙지 않았거나 재생에 들어가지 못했다."
    "로그: $($editorLog[0].FullName)"
    exit 1
}

""
"─ 픽스처 트레이스 ───────────────────────────────────────────────"
foreach ($p in $points) { "  {0,-20} f={1}" -f $p.Point, $p.Frame }
""

$failed = New-Object System.Collections.Generic.List[string]

# ── 판정 U: 실제로 만들었다 (전제) ────────────────────────────────────────────

$createdCount = -1
if ($logText -match '\[LC3\]\s+point=created\s+frame=\d+\s+alive=\d+\s+of=(\d+)') { $createdCount = [int]$Matches[1] }

"판정 U 생성 확인: 대기 $createdCount 개를 만들었다 (기대 50)"
if ($createdCount -ne 50) {
    "  → 생성 단계까지 실행이 닿지 않았다. 0개를 만들면 0개가 살아남으므로"
    "     아래 회수 판정이 아무것도 시험하지 않은 초록이 된다."
    $failed.Add('U')
}

# ── 판정 V: 스코프가 살아 있어도 완료 Task 가 회수된다 ← LC3 ──────────────────

$alive = -1
$of = -1
# of= 는 별도 토큰으로 찍히므로 따로 집는다.
if ($logText -match '\[LC3\]\s+point=counted\s+frame=\d+\s+alive=(\d+)\s+of=(\d+)') {
    $alive = [int]$Matches[1]
    $of = [int]$Matches[2]
}

"판정 V 회수: 완료 Task $alive / $of 개가 아직 살아 있다 (기대 0)"
if ($alive -lt 0) {
    "  → 계수 단계까지 실행이 닿지 않았다."
    $failed.Add('V(전제)')
}
elseif ($alive -gt 0) {
    "  → 스코프를 살려 둔 채 강한 참조를 전부 버리고 GC를 돌렸는데도 남았다."
    "  → Delay가 token.Register의 반환 등록을 놓지 않아, 완료된 대기가 취소 토큰"
    "     쪽에서 계속 도달 가능하다(LC3). 오래 사는 스코프에서는 누적된다."
    $failed.Add('V')
}

# ── 판정 W: 참조 해제가 종료 절차를 깨뜨리지 않는다 ───────────────────────────

$teardown = @('end', 'removing', 'uninit')
$counts = @{}
foreach ($t in $teardown) { $counts[$t] = @($points | Where-Object { $_.Point -eq $t }).Count }

"판정 W 축소 온전: end $($counts['end']) · removing $($counts['removing']) · uninit $($counts['uninit']) (기대 1 / 1 / 1)"
if ($counts['end'] -ne 1 -or $counts['removing'] -ne 1 -or $counts['uninit'] -ne 1) {
    "  → 취소 등록을 다루는 변경이 종료 절차를 건드렸다. 등록을 잘못 놓으면"
    "     취소 통지가 끊겨 대기가 안 풀리거나, Dispose가 콜백을 기다려 서로 막힌다."
    $failed.Add('W')
}

""
if ($failed.Count -gt 0) {
    "붉은 판정: $($failed -join ', ')"
    "LC3가 착지하기 전까지 V는 붉은 것이 정상이다. U나 W가 붉으면 하네스를 먼저 볼 것."
    exit 1
}

"전체 통과 — 스코프가 살아 있어도 완료된 대기가 회수된다"
exit 0
