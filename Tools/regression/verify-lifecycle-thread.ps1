# 생명주기 스레드 경계 게이트 — 관리 코드가 게임 스레드를 벗어나는 두 자리 (LC5-b·c).
#
# ★ 결함을 증명하려고 붉은 채로 세운다. H는 LC5-b가 착지해 초록이고, J·K는
#   LC5-c가 착지하면 넘어간다.
#
#   재개 축 (LC5-b · await 뒤)
#     H  외부 await 재개가 게임 스레드로 돌아온다        ← RED→GREEN
#     G  지원 경로 재개는 그대로 게임 스레드다            (비회귀)
#     I  워커가 실제로 다른 스레드였다                    (H의 거짓 초록 방지)
#
#   진입 축 (LC5-c · 워커 몸통)                          ← 지금 붉다
#     J  워커에서 부른 엔진 API가 거부된다
#     K  거부된 사실이 기록된다
#
# 두 축을 한 게이트에 두는 이유는 같은 경계의 앞뒤이기 때문이다. 마셜링(H)은
# await **뒤**만 닫는다 — Task.Run 몸통 안은 재개가 아니라 워커 그 자체라
# 컨텍스트가 원리적으로 닿지 못한다. 그 자리를 J가 잡는다.
#
# ── 무엇을 메우는가 ──
#
# verify-lifecycle-failure는 "실패가 어떤 상태를 남기는가"를 굳혔다. 그보다 앞선
# 질문 — 재개된 본문이 애초에 어느 스레드에서 도는가 — 은 어느 게이트에도 없다.
# 관리 훅은 전부 게임 스레드 전용인데(ClrHost.h 규약) await 하나로 그 규약 밖으로
# 나갈 수 있고, 나간 뒤의 코드는 여전히 Transform·Entity를 부른다.
#
# ── 판정 ──
#
#   G 지원 경로 재개
#     `await Scope.Delay` 뒤의 본문이 게임 스레드에서 이어지는가.
#     LC5-b 전에도 초록이었다. 그때는 계약이 아니라 TaskCompletionSource 기본값
#     (인라인 완료)에 얹힌 결과였고, 완료를 일으키는 SimulationScope.Tick이
#     게임 스레드에 있어서 그 자리로 이어졌을 뿐이다. 지금은 컨텍스트가 지킨다.
#
#     ★ 이 판정은 그 기본값이 바뀌는 것을 잡지 못한다 — 변이로 확인했다.
#       TaskCompletionSource에 RunContinuationsAsynchronously를 붙여도 G는
#       초록이었다. 컨텍스트가 재개를 어차피 게임 스레드로 되돌리기 때문이다.
#       그 변이가 깨는 것은 스레드가 아니라 **시점**이고(재개가 그 프레임의
#       배수를 놓친다), 붉어진 것은 verify-lifecycle-baseline의 관리 축이었다
#       (ScriptHostB의 SimulateStart·SimulateResume 소실). 그 축은 저기 있다.
#
#   H 외부 await 재개                                     ← LC5-b가 고칠 자리
#     `await Task.Run(...)` 뒤의 본문이 게임 스레드에서 이어지는가.
#     관리 측에 SynchronizationContext가 없어(실측 0건) await는 포착할 컨텍스트가
#     없고 TaskScheduler.Default — 스레드 풀 — 로 이어진다.
#
#   I 워커 실재 (H의 거짓 초록 방지)
#     워커 몸통이 실제로 게임 스레드가 아닌 곳에서 돌았는가.
#     Task.Run이 어떤 사정으로 인라인되면 H의 두 값이 같아져 **고쳐진 것처럼**
#     보인다. 이 판정이 없으면 그 거짓 초록을 가릴 수 없다.
#
# 기준값은 OnBeginSimulation의 스레드 id다. 그 훅은 ScriptRegistry가 프레임 안에서
# 직접 부르므로 정의상 게임 스레드다. Native 내부의 _gameThreadId를 읽지 않는 이유는
# 그것이 고침의 자기 신고이기 때문이다 — 그것을 기준으로 삼으면 대조가 아니라
# 같은 값을 두 번 읽는 동어반복이 된다.
#
# 사용법: pwsh Tools\regression\verify-lifecycle-thread.ps1

param(
    [string]$Exe = (Join-Path $PSScriptRoot "..\..\Bin\x64-Debug\Editor\CreatorEditor.exe"),
    [string]$Work = $env:TEMP,
    [int]$TimeoutSeconds = 300
)

$ErrorActionPreference = 'Stop'

if (-not (Test-Path $Exe)) { "실행 파일이 없다: $Exe"; exit 1 }
$exeDir = [System.IO.Path]::GetDirectoryName($Exe)

$scenario = Join-Path $PSScriptRoot "lifecycle_thread_probe.txt"
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
    -RedirectStandardOutput (Join-Path $Work "lifecycle_thread.out") `
    -RedirectStandardError  (Join-Path $Work "lifecycle_thread.err") -PassThru

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

$marks = @([regex]::Matches($logText, '\[LC5b\]\s+point=(\w+)\s+tid=(\d+)') |
    ForEach-Object {
        [pscustomobject]@{ Point = $_.Groups[1].Value; Tid = [int]$_.Groups[2].Value }
    })

# LC5-c — 워커 몸통에서 부른 엔진 API가 무엇을 돌려받았나.
$calls = @([regex]::Matches($logText, "\[LC5c\]\s+where=(\w+)\s+tid=(\d+)\s+alive=(\w+)\s+name='([^']*)'") |
    ForEach-Object {
        [pscustomobject]@{
            Where = $_.Groups[1].Value
            Tid   = [int]$_.Groups[2].Value
            Alive = $_.Groups[3].Value
            Name  = $_.Groups[4].Value
        }
    })

if ($marks.Count -eq 0) {
    "픽스처 기록이 0건이다. 스크립트가 붙지 않았거나 재생에 들어가지 못했다."
    "로그: $($editorLog[0].FullName)"
    exit 1
}

""
"─ 스레드 트레이스 ───────────────────────────────────────────────"
foreach ($m in $marks) { "  {0,-10} tid={1}" -f $m.Point, $m.Tid }
""

function Tid-Of([string]$point) {
    $hit = @($marks | Where-Object { $_.Point -eq $point })
    if ($hit.Count -eq 0) { return -1 }
    return $hit[0].Tid
}

$failed = New-Object System.Collections.Generic.List[string]

# 네 표지가 다 있어야 판정이 성립한다. 빠진 것을 0건으로 읽고 "차이 없음"으로
# 통과시키면 아무것도 검증하지 않은 초록이 된다.
$required = @('begin', 'sim', 'delay', 'worker', 'external')
$missing = @($required | Where-Object { (Tid-Of $_) -lt 0 })
if ($missing.Count -gt 0) {
    "필수 표지가 빠졌다: $($missing -join ', ')"
    "  → 루틴이 끝까지 가지 못했다. 판정을 낼 수 없다."
    exit 1
}

$game     = Tid-Of 'begin'
$sim      = Tid-Of 'sim'
$delay    = Tid-Of 'delay'
$worker   = Tid-Of 'worker'
$external = Tid-Of 'external'

"기준 게임 스레드: tid=$game (OnBeginSimulation)"
""

# ── 판정 G: 지원 경로의 재개는 게임 스레드다 ──────────────────────────────────

"판정 G 지원 경로 재개: Scope.Delay 뒤 tid=$delay (기대 $game)"
if ($delay -ne $game) {
    "  → await Scope.Delay가 게임 스레드 밖에서 이어졌다."
    "  → 지원한다고 적어 둔 경로가 규약을 벗어난 것이므로 LC5-b보다 먼저 볼 것."
    $failed.Add('G')
}

# 루틴 시작 자체도 게임 스레드여야 한다. 이것이 어긋나면 위 기준값이 흔들린다.
if ($sim -ne $game) {
    "판정 G 루틴 시작: OnSimulate 진입 tid=$sim (기대 $game)"
    "  → 루틴이 애초에 게임 스레드에서 시작하지 않았다."
    $failed.Add('G(시작)')
}

# ── 판정 I: 워커가 실제로 다른 스레드였다 ─────────────────────────────────────

"판정 I 워커 실재: 워커 몸통 tid=$worker (기대 $game 아님)"
if ($worker -eq $game) {
    "  → Task.Run이 게임 스레드에서 인라인됐다. 이 실행은 외부 경계를 재지 못했다."
    "  → 판정 H가 초록이어도 그것은 고쳐졌다는 뜻이 아니다."
    $failed.Add('I')
}

# ── 판정 H: 외부 await의 재개도 게임 스레드다 ← LC5-b ─────────────────────────

"판정 H 외부 await 재개: Task.Run 뒤 tid=$external (기대 $game)"
if ($external -ne $game) {
    "  → 외부 Task의 완료가 워커에서 그대로 이어졌다."
    "  → 그 뒤의 본문은 여전히 Transform·Entity를 부른다 — 게임 스레드 전용 API다."
    "  → 완료를 프레임 경계로 넘겨야 한다(LC5-b)."
    $failed.Add('H')
}

# ── 판정 J·K: 워커 몸통의 엔진 호출은 거부되고 기록된다 ← LC5-c ───────────────

""
"─ 워커 진입 트레이스 ───────────────────────────────────────────"
if ($calls.Count -eq 0) { "  (없음)" }
else { foreach ($c in $calls) { "  {0,-8} tid={1,-3} alive={2,-5} name='{3}'" -f $c.Where, $c.Tid, $c.Alive, $c.Name } }
""

$onGame = @($calls | Where-Object { $_.Where -eq 'game' })
$onWorker = @($calls | Where-Object { $_.Where -eq 'worker' })

if ($onGame.Count -eq 0 -or $onWorker.Count -eq 0) {
    "판정 J 워커 진입 거부: 표지가 빠졌다 (game $($onGame.Count) 건 · worker $($onWorker.Count) 건)"
    "  → 두 값을 맞대야 판정이 성립한다. 한쪽만으로는 빈 이름이 거부인지 원래 없는 것인지 가릴 수 없다."
    $failed.Add('J(표지)')
}
else {
    $g = $onGame[0]
    $w = $onWorker[0]

    # 게임 스레드 쪽이 정답 노릇을 하려면 그것부터 참이어야 한다.
    if ($g.Alive -ne 'True' -or [string]::IsNullOrEmpty($g.Name)) {
        "판정 J 기준값: 게임 스레드에서 alive=$($g.Alive) name='$($g.Name)'"
        "  → 정답 쪽이 이미 비어 있다. 픽스처가 붙지 않았다 — 하네스를 먼저 볼 것."
        $failed.Add('J(기준)')
    }
    elseif ($w.Tid -eq $g.Tid) {
        "판정 J 워커 실재: 워커 몸통 tid=$($w.Tid) (기대 $($g.Tid) 아님)"
        "  → Task.Run이 인라인됐다. 이 실행은 워커 진입을 재지 못했다."
        $failed.Add('J(워커)')
    }
    else {
        "판정 J 워커 진입 거부: 워커에서 alive=$($w.Alive) name='$($w.Name)' (기대 False / 빈 이름)"
        if ($w.Alive -ne 'False' -or -not [string]::IsNullOrEmpty($w.Name)) {
            "  → 게임 스레드 전용 API가 워커에서 그대로 통과했다."
            "  → 두 호출 다 함수 포인터로 C++에 들어가 씬 그래프를 읽는다 — 게임 스레드가"
            "     같은 순간에 그것을 고치고 있어도 막는 것이 없다(LC5-c)."
            $failed.Add('J')
        }
    }
}

# 거부만 있고 기록이 없으면 조용히 틀린 값을 돌려주는 것이라 더 나쁘다.
# 거부와 보고를 갈라서 세는 이유는 실패 게이트의 판정 E와 같다.
$offThreadReports = @([regex]::Matches($logText, '\[Native\]\s+게임 스레드 밖')).Count
"판정 K 원인 보존: 경계 밖 호출 보고 $offThreadReports 건 (기대 1 이상)"
if ($offThreadReports -lt 1) {
    "  → 거부는 표가 없을 때와 같은 값을 돌려준다. 기록이 없으면 저작자에게는"
    "     '왜 갑자기 빈 값이지'만 남는다."
    $failed.Add('K')
}

""
if ($failed.Count -gt 0) {
    "붉은 판정: $($failed -join ', ')"
    "LC5-c가 착지하기 전까지 J·K는 붉은 것이 정상이다. G나 I가 붉으면 하네스를 먼저 볼 것."
    exit 1
}

"전체 통과 — 재개는 게임 스레드로 돌아오고, 워커의 엔진 진입은 거부되고 기록된다"
exit 0
