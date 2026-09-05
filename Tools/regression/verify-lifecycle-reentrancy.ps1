# 생명주기 재진입 게이트 — Tick 순회 중에 저작 코드가 도는 창 (LC4).
#
# ── 무엇을 메우는가 ──
#
# §2.1의 대역 시험은 완료 continuation이 내부 Cancel()을 불러 _pending이 비면
# 다음 인덱스 접근이 ArgumentOutOfRangeException이 되는 것을 재현했다. 그 시험은
# **내부 메서드를 직접 불렀다** — 저작 표면에서 같은 일이 되는지는 재지 않았고,
# 계획서도 그것을 "강제 재현"이라고 적어 두었다.
#
# ── 실측이 뒤집은 것 (2026-09-05) ──
#
# 창이 열리려면 재개가 Tick 루프 안에서, 게임 스레드에서 일어나야 한다. 그런데
# 저작 표면에는 그럴 수단이 없다:
#
#   컨텍스트를 탄다        → 재개가 프레임 경계로 (LC5-b)
#   ConfigureAwait(false)  → 재개가 스레드 풀로   (실측: 게임 2 vs 재개 4·6)
#
# 어느 쪽도 Tick 안이 아니다. ConfigureAwait(false)가 인라인으로 돌 것이라 보고
# 픽스처를 짰는데 실측이 그것을 부정했다 — 그래서 이 게이트의 축을 뒤집었다.
# 이제 재는 것은 "창을 열었더니 무사한가"가 아니라 **"창이 열리지 않는가"**다.
#
# ── 판정 ──
#
#   P 창 부재      재개 스레드가 게임 스레드와 다르다
#   O 거부 완결    워커의 Enabled 변경이 관리 폴백으로도 새지 않는다
#   L 순회 무사고  Tick이 던지지 않았다
#   M 대조군 온전  이웃이 훅을 잃지 않았다
#   N 대기 생존    남은 대기가 재개됐다
#
# P가 이 게이트의 본체다. 언젠가 재개가 게임 스레드로 인라인되면 §2.1의 창이
# 저작 표면에서 열리고 LC4를 실제로 닫아야 한다 — 그때 이 판정이 붉어진다.
# 지금의 초록은 "결함이 없다"가 아니라 **"경로가 없다"**는 기록이다.
#
# O는 이 픽스처가 물어 온 결함이다. 재개가 워커라 ScriptSetEnabled가 거부되는데,
# setter가 그 거짓을 "전달 실패"로 읽고 관리 폴백을 타 워커에서 OnDisable을
# 돌리고 있었다. 거부가 막으려던 일의 축소판을 관리 측에 다시 만든 것이다.
#
# ★ M의 한계 (변이로 확인) — Tick이 던지게 만드는 변이를 심었더니 L·N은
#   붉어졌지만 M은 초록이었다. 대조군의 훅을 **집합**으로 세기 때문에 두어
#   프레임의 중단이 집합을 바꾸지 못한다. M은 이웃 격리가 아니라 하네스가
#   살아 있다는 확인으로 읽어야 한다. 프레임 단위 손실을 재려면 틱 횟수 축이
#   따로 필요한데, L이 예외를 직접 보고하므로 그것까지 두지 않았다.
#
# 사용법: pwsh Tools\regression\verify-lifecycle-reentrancy.ps1

param(
    [string]$Exe = (Join-Path $PSScriptRoot "..\..\Bin\x64-Debug\Editor\CreatorEditor.exe"),
    [string]$Work = $env:TEMP,
    [int]$TimeoutSeconds = 300
)

$ErrorActionPreference = 'Stop'

if (-not (Test-Path $Exe)) { "실행 파일이 없다: $Exe"; exit 1 }
$exeDir = [System.IO.Path]::GetDirectoryName($Exe)

$scenario = Join-Path $PSScriptRoot "lifecycle_reentrancy_probe.txt"
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
    -RedirectStandardOutput (Join-Path $Work "lifecycle_reentrancy.out") `
    -RedirectStandardError  (Join-Path $Work "lifecycle_reentrancy.err") -PassThru

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

$rows = @([regex]::Matches($logText, '\[LC4\]\s+kind=(\w+)\s+point=(\w+)\s+tid=(\d+)') |
    ForEach-Object {
        [pscustomobject]@{
            Kind  = $_.Groups[1].Value
            Point = $_.Groups[2].Value
            Tid   = [int]$_.Groups[3].Value
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
foreach ($r in $rows) { "  {0,-9} {1,-9} tid={2}" -f $r.Kind, $r.Point, $r.Tid }
""
"─ 대조군(Control) ───────────────────────────────────────────────"
if ($controlHooks.Count -eq 0) { "  (없음)" }
else { foreach ($h in $controlHooks) { "  $h" } }
""

function Rows-Of([string]$kind) { return @($rows | Where-Object { $_.Kind -eq $kind }) }
function Points-Of([string]$kind) { return @((Rows-Of $kind) | ForEach-Object { $_.Point }) }

function Tid-Of([string]$kind, [string]$point) {
    $hit = @((Rows-Of $kind) | Where-Object { $_.Point -eq $point })
    if ($hit.Count -eq 0) { return -1 }
    return $hit[0].Tid
}

$failed = New-Object System.Collections.Generic.List[string]

# 게임 스레드 기준값. OnBeginSimulation은 ScriptRegistry가 프레임 안에서 직접
# 부르므로 정의상 게임 스레드다 — Native 내부의 _gameThreadId를 읽지 않는다.
# 그것은 고침의 자기 신고라 대조가 아니라 동어반복이 된다.
$game = Tid-Of 'destroy' 'begin'
if ($game -lt 0) {
    "게임 스레드 기준값을 얻지 못했다 (destroy/begin 표지 없음)."
    exit 1
}
"기준 게임 스레드: tid=$game (OnBeginSimulation)"
""

# 루틴이 재개까지 갔는지 먼저 본다. 표지가 없으면 아래 판정은 아무것도
# 시험하지 않은 초록이 된다.
foreach ($kind in @('destroy', 'disable')) {
    $p = Points-Of $kind
    foreach ($need in @('inline', 'acted')) {
        if ($p -notcontains $need) {
            "판정 전제: $kind 픽스처에 '$need' 표지가 없다 (표지: $($p -join ', '))"
            "  → 루틴이 재개까지 가지 못했다. 이 실행은 아무것도 재지 못했다."
            $failed.Add("전제($kind)")
        }
    }
}

# ── 판정 P: 재개가 Tick 루프 안으로 들어오지 않는다 ← LC4의 본체 ──────────────

foreach ($kind in @('destroy', 'disable')) {
    $t = Tid-Of $kind 'inline'
    if ($t -lt 0) { continue }
    "판정 P 창 부재 [$kind]: ConfigureAwait(false) 재개 tid=$t (기대 $game 아님)"
    if ($t -eq $game) {
        "  → 재개가 게임 스레드에서, 곧 Tick 루프 한복판에서 일어났다."
        "  → §2.1의 창이 저작 표면에서 열린다는 뜻이다. LC4를 실제로 닫아야 한다:"
        "     완료 후보를 수집하는 단계와 continuation을 실행하는 단계를 나눈다."
        $failed.Add("P($kind)")
    }
}

# ── 판정 O: 거부가 관리 폴백으로 새지 않는다 ──────────────────────────────────

# ★ 훅 건수로 세면 안 된다. 정지 시점 축소가 모든 인스턴스에 OnDisable을
#   하나씩 주므로(ApplyEnabled(b,false)), 건수 축은 고침 전에도 후에도 1이다 —
#   이유만 다르다(고침 전에는 워커가 먼저 껐고, 그래서 축소 때는 전이가 아니라
#   훅이 없었다). 가르는 것은 **어느 스레드에서 왔는가**뿐이다.
$leaked = @((Rows-Of 'disable') | Where-Object { $_.Point -eq 'disable' -and $_.Tid -ne $game }).Count

"판정 O 거부 완결: 게임 스레드 밖에서 온 OnDisable $leaked 건 (기대 0)"
if ($leaked -gt 0) {
    "  → ScriptSetEnabled는 거부됐는데 setter가 그것을 '전달 실패'로 읽고"
    "     관리 폴백을 탔다. 워커에서 관리 상태를 바꾸고 사용자 훅을 돌린 것이다."
    "  → 거부가 막으려던 일의 축소판을 관리 측에 다시 만들었다."
    $failed.Add('O')
}

# 거부 자체는 일어나야 한다. 보고가 없으면 애초에 워커 호출이 없었다는 뜻이라
# 위 판정이 빈 집합을 보고 초록이 된다.
$refusals = @([regex]::Matches($logText, '\[Native\]\s+게임 스레드 밖에서\s+(\w+)\s+호출') |
    ForEach-Object { $_.Groups[1].Value })
"판정 O 거부 발생: 경계 밖 호출 거부 $($refusals.Count) 건 — $(($refusals | Sort-Object -Unique) -join ', ') (기대 1 이상)"
if ($refusals.Count -lt 1) {
    "  → 워커에서 엔진 API를 부른 적이 없다. 판정 O가 아무것도 시험하지 않았다."
    $failed.Add('O(전제)')
}

# ── 판정 L: 순회가 던지지 않았다 ──────────────────────────────────────────────

$tickFaults = @([regex]::Matches($logText, '\[ScriptCore\]\s+(PrePhysicsTick|PostPhysicsTick|FlushRegistrations)\s+처리 중 예외')).Count

"판정 L 순회 무사고: 틱 진입점 예외 보고 $tickFaults 건 (기대 0)"
if ($tickFaults -gt 0) {
    "  → Tick이 순회 중에 던졌다. 그 프레임의 모든 인스턴스가 훅과 배수를 잃는다."
    $failed.Add('L')
}

# ── 판정 M: 하네스가 살아 있다 (한계는 위 머리말 참고) ────────────────────────

$required = @('Awake', 'AddedToScene', 'Enable', 'Start', 'SimulateStart',
              'Disable', 'EndSimulation', 'RemovingFromScene', 'Uninitializing')
$missing = @($required | Where-Object { $controlHooks -notcontains $_ })

"판정 M 대조군 온전: 필수 훅 $($required.Count) 종 중 누락 $($missing.Count) 건 (기대 0)"
if ($missing.Count -gt 0) {
    "  누락: $($missing -join ', ')"
    $failed.Add('M')
}

# ── 판정 N: 남은 대기가 재개된다 ──────────────────────────────────────────────

foreach ($kind in @('destroy', 'disable')) {
    $resumed = @((Points-Of $kind) | Where-Object { $_ -eq 'resumed' }).Count
    "판정 N 대기 생존 [$kind]: 재개 $resumed 건 (기대 1)"
    if ($resumed -lt 1) {
        "  → 남은 Scope.Delay가 완료되지 않았다. 순회가 그것을 잃었거나"
        "     스코프가 잘못 취소됐다."
        $failed.Add("N($kind)")
    }
}

""
if ($failed.Count -gt 0) {
    "붉은 판정: $($failed -join ', ')"
    exit 1
}

"전체 통과 — 저작 표면에 Tick 루프 안으로 재개하는 길이 없고, 거부가 관리 측으로 새지 않는다"
exit 0
