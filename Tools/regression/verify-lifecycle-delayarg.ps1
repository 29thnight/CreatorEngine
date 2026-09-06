# 생명주기 대기 인자 게이트 (LC7-b).
#
# ── 무엇을 메우는가 ──
#
# `Scope.Delay(seconds)`는 저작 표면의 관용구인데 인자의 뜻이 적힌 적이 없다.
# 0은? 음수는? 계산이 어긋나 NaN이 들어오면? 무한대는? 연쇄로 걸면 같은 프레임에서
# 무한히 재개되지는 않는가(LC4가 여기로 넘긴 항목).
#
# ── 착수 시 실측 ──
#
#   Delay(0)      159 → 159   0 프레임
#   Delay(-1)     159 → 160   1 프레임
#   Delay(NaN)    160 → 161   1 프레임 · **완료됨**
#   연쇄 ×5       161 → 166   5 프레임
#   Delay(∞)      취소로만 풀림
#
# 둘이 결함이다.
#
# ① NaN 이 조용히 삼켜진다. `Remaining -= dt` 뒤 `Remaining > 0f` 로 판정하는데
#    `NaN > 0f` 는 거짓이라 0초와 똑같이 완료된다. 저작자의 계산 실수(0으로 나눔,
#    미초기화 값)가 "한 프레임 대기"가 되어 아무 신호도 남기지 않는다.
#
# ② 같은 `Delay(0f)` 가 첫 호출에서는 0 프레임, 이후에는 1 프레임을 쓴다. 갈림은
#    등록 시점이 그 프레임의 `Scope.Tick` 보다 앞이냐 뒤냐이고, 그것은 네이티브가
#    OnBeginSimulation 드레인을 프레임의 어느 지점에서 부르느냐에 달렸다 —
#    저작자가 볼 수도 제어할 수도 없는 우연이다. 계약이 될 수 없다.
#
# ── 판정 ──
#
#   AA 0초 계약     Delay(0)이 호출 위치와 무관하게 정확히 한 프레임을 쓴다
#   BB 음수 동치    음수는 0과 같게 다뤄진다(따로 뜻을 주지 않는다)
#   CC NaN 거부     NaN 은 예외로 거부되고 프레임을 쓰지 않는다
#   DD 연쇄 진행    연쇄 대기가 프레임당 한 칸씩 나아간다(같은 프레임 무한 재개 없음)
#   EE 무한 대기    무한대는 스스로 완료되지 않고 스코프 취소로만 풀린다
#
# AA·BB 를 나눠 둔 이유: 둘을 "1 프레임"으로 합치면 음수가 0과 다르게 다뤄지도록
# 바뀌었을 때 어느 쪽이 어긋났는지 알 수 없다.
#
# ── 변이 기록 ──
#
#   R3  Tick 의 `CreatedFrame == frame` 가드 제거   → AA 만 붉음
#   R4  Delay 의 NaN 거부 제거                      → CC 만 붉음
#   R5  음수를 즉시 완료(Task.CompletedTask)로 변경 → BB 만 붉음
#
# DD 와 EE 는 변이로 붉히지 못했다.
#
# DD — R3(가드 제거)에도 연쇄는 그대로 5 프레임이었고, 착수 시 실측(가드가 없던
# 상태)도 5 프레임이었다. 즉 연쇄를 프레임당 한 칸으로 묶고 있는 것은 이 가드가
# 아니라 LC5-b 의 컨텍스트 마샬링이다 — await 재개가 Post 되어 다음 프레임의
# 배수에서 돌기 때문에, 새 대기는 그 프레임의 Tick 이 이미 지난 뒤에 등록된다.
# 그것을 끄는 변이(Install 제거)는 이 게이트가 아니라 `verify-lifecycle-thread`
# 가 먼저 붉으므로 여기서 축을 가르지 못한다. DD 는 그 성질이 유지되는지를
# 지키는 못(pin)으로 둔다.
#
# EE — 무한대가 스스로 완료되게 만들려면 만기 판정 자체를 뒤집어야 하고, 그러면
# AA·BB 가 먼저 붉는다. 단독으로 붉힐 변이가 없다.
#
# 사용법: pwsh Tools\regression\verify-lifecycle-delayarg.ps1

param(
    [string]$Exe = (Join-Path $PSScriptRoot "..\..\Bin\x64-Debug\Editor\CreatorEditor.exe"),
    [string]$Work = $env:TEMP,
    [int]$TimeoutSeconds = 300
)

$ErrorActionPreference = 'Stop'

if (-not (Test-Path $Exe)) { "실행 파일이 없다: $Exe"; exit 1 }
$exeDir = [System.IO.Path]::GetDirectoryName($Exe)

$scenario = Join-Path $PSScriptRoot "lifecycle_delayarg_probe.txt"
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
    -RedirectStandardOutput (Join-Path $Work "lifecycle_delayarg.out") `
    -RedirectStandardError  (Join-Path $Work "lifecycle_delayarg.err") -PassThru

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

# case=X [outcome=Y] start=N end=M  — outcome 은 nan·infinite 에만 있다.
$rows = @([regex]::Matches($logText, '\[LC7b\]\s+case=(\w+)(?:\s+outcome=(\w+))?\s+start=(\d+)\s+end=(\d+)') |
    ForEach-Object {
        [pscustomobject]@{
            Case    = $_.Groups[1].Value
            Outcome = $_.Groups[2].Value
            Start   = [long]$_.Groups[3].Value
            End     = [long]$_.Groups[4].Value
            Frames  = [long]$_.Groups[4].Value - [long]$_.Groups[3].Value
        }
    })

if ($rows.Count -eq 0) {
    "픽스처 기록이 0건이다. 스크립트가 붙지 않았거나 재생에 들어가지 못했다."
    "로그: $($editorLog[0].FullName)"
    exit 1
}

""
"─ 픽스처 트레이스 ───────────────────────────────────────────────"
foreach ($r in $rows) {
    "  case={0,-9} outcome={1,-10} {2} → {3}  ({4} 프레임)" -f $r.Case, ($(if ($r.Outcome) { $r.Outcome } else { '-' })), $r.Start, $r.End, $r.Frames
}
""

$failed = New-Object System.Collections.Generic.List[string]

function Row([string]$case) { return @($rows | Where-Object { $_.Case -eq $case })[0] }

# ── 판정 AA: Delay(0)은 호출 위치와 무관하게 한 프레임을 쓴다 ─────────────────

$zero = Row 'zero'
if (-not $zero) {
    "판정 AA 0초 계약: 표지 없음 — 첫 대기가 재개되지 못했다"
    $failed.Add('AA(표지없음)')
}
else {
    "판정 AA 0초 계약: Delay(0) 가 $($zero.Frames) 프레임을 썼다 (기대 1)"
    if ($zero.Frames -ne 1) {
        if ($zero.Frames -eq 0) {
            "  → 등록한 프레임 안에서 완료됐다. 이 대기가 Scope.Tick 보다 앞에서 등록됐다는"
            "     뜻이고, 그 순서는 네이티브가 훅을 프레임의 어느 지점에서 부르는지에 달렸다 —"
            "     저작자가 볼 수 없는 우연이 대기 길이를 정한다."
        }
        else {
            "  → 0초 대기가 한 프레임보다 오래 걸렸다."
        }
        $failed.Add('AA')
    }
}

# ── 판정 BB: 음수는 0과 같다 ──────────────────────────────────────────────────

$neg = Row 'negative'
if (-not $neg) {
    "판정 BB 음수 동치: 표지 없음"
    $failed.Add('BB(표지없음)')
}
else {
    "판정 BB 음수 동치: Delay(-1) 이 $($neg.Frames) 프레임을 썼다 (기대 1 — 0초와 같게)"
    if ($neg.Frames -ne 1) {
        "  → 이미 지난 시각을 0초와 다르게 다룬다. 둘 중 하나는 저작자를 놀래킨다."
        $failed.Add('BB')
    }
}

# ── 판정 CC: NaN은 거부된다 ───────────────────────────────────────────────────

$nan = Row 'nan'
if (-not $nan) {
    "판정 CC NaN 거부: 표지 없음 — 대기가 영영 풀리지 않았다"
    $failed.Add('CC(표지없음)')
}
else {
    "판정 CC NaN 거부: outcome=$($nan.Outcome) · $($nan.Frames) 프레임 (기대 rejected · 0)"
    if ($nan.Outcome -ne 'rejected') {
        "  → NaN 이 그대로 대기가 됐다. `Remaining > 0f` 는 NaN 에 거짓이라 0초와 똑같이"
        "     완료된다 — 저작자의 계산 실수가 '한 프레임 대기'로 삼켜지고 아무 신호도"
        "     남지 않는다."
        $failed.Add('CC')
    }
    elseif ($nan.Frames -ne 0) {
        "  → 거부는 됐는데 프레임을 썼다. 거부는 그 자리에서 던져야 한다."
        $failed.Add('CC(프레임)')
    }
}

# ── 판정 DD: 연쇄가 프레임당 한 칸씩 나아간다 ─────────────────────────────────
#
# LC4 가 여기로 넘긴 "무한 같은 프레임 재개 방지"의 실측이다. 완료 continuation 이
# 같은 스코프에 새 대기를 거는데, 그것이 같은 프레임에서 계속 이어지면 프레임이
# 하나도 흐르지 않고 루프가 갇힌다.

$chainSteps = 5
$chain = Row 'chain'
if (-not $chain) {
    "판정 DD 연쇄 진행: 표지 없음 — 연쇄가 끝나지 않았다(같은 프레임 무한 재개일 수 있다)"
    $failed.Add('DD(표지없음)')
}
else {
    "판정 DD 연쇄 진행: 연쇄 $chainSteps 칸이 $($chain.Frames) 프레임을 썼다 (기대 $chainSteps)"
    if ($chain.Frames -lt $chainSteps) {
        "  → 한 프레임에 두 칸 이상 나아갔다. 완료 continuation 이 건 새 대기가 같은"
        "     틱에서 또 완료된다 — 조건이 오래 참인 루프가 프레임을 넘기지 못하고 갇힌다."
        $failed.Add('DD')
    }
    elseif ($chain.Frames -gt $chainSteps) {
        "  → 칸당 한 프레임보다 오래 걸렸다."
        $failed.Add('DD')
    }
}

# ── 판정 EE: 무한대는 취소로만 풀린다 ─────────────────────────────────────────

$inf = Row 'infinite'
if (-not $inf) {
    "판정 EE 무한 대기: 표지 없음 — 취소가 대기를 풀지 못했다"
    $failed.Add('EE(표지없음)')
}
else {
    "판정 EE 무한 대기: outcome=$($inf.Outcome) · $($inf.Frames) 프레임 (기대 cancelled)"
    if ($inf.Outcome -ne 'cancelled') {
        "  → 무한대 대기가 스스로 완료됐다. `Infinity - dt` 는 여전히 Infinity 라 완료될"
        "     수 없어야 한다 — 완료됐다면 만기 판정이 바뀐 것이다."
        $failed.Add('EE')
    }
}

""
if ($failed.Count -gt 0) {
    "붉은 판정: $($failed -join ', ')"
    exit 1
}

"전체 통과 — 대기 인자 다섯 종이 각각 적힌 뜻대로 움직인다"
exit 0
