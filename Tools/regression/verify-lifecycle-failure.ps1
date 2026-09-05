# 생명주기 실패 픽스처 게이트 — 훅·정리·루틴이 실패한 뒤 무엇이 오는가 (LC0 · LC1/LC2/LC5).
#
# ★ 이 게이트는 지금 **붉은 것이 정상이다.** LC1·LC2·LC5가 지목한 결함을 눈에
#   보이게 만들려고 세운 것이고, 고침이 착지하면 초록이 된다. 그때 run-all.ps1에
#   넣는다 — 지금 넣으면 세트가 항상 실패한다.
#
# ── 무엇을 메우는가 ──
#
# verify-lifecycle-baseline의 두 축(네이티브·관리)은 **정상 경로**의 순서를 굳힌다.
# 훅이 실패했을 때 무슨 일이 벌어지는지는 그 골든에 한 줄도 없다. 그래서
# ScriptLifecycleContractHardeningPlan이 지목한 결함들이 어느 게이트로도
# 붉어지지 않았다.
#
# ── 재는 결함 (계획서 §2) ──
#
#   A 실패와 단계 완료가 섞임
#     DispatchLifecycle은 MarkInitialized()를 **먼저** 세우고 훅을 부른다.
#     Invoke는 예외를 잡지만 성공 여부를 호출자에게 돌려주지 않으므로, 초기화가
#     실패해도 "초기화됐다"가 남아 이후 단계가 성공한 초기화를 전제로 진행한다.
#
#   B Begin 실패 뒤 루틴 시작
#     Invoke(OnBeginSimulation) 다음 줄이 StartSimulation이다 — 조건 재검사가 없다.
#     훅이 던져도 OnSimulate가 시작된다.
#
#   C 시작 훅 안의 자기 비활성화가 무시됨
#     같은 자리의 다른 얼굴이다. if (b.Enabled)는 훅 **앞에서** 한 번만 보므로
#     훅 안에서 끈 것이 루틴 시작 결정에 반영되지 않는다. B와 성격이 달라 따로
#     잰다 — 예외는 격리해야 하지만 자기 비활성화는 존중해야 한다.
#
#   E 정리 예외가 종료 절차를 중단 (LC2)
#     Scope.Cancel()은 Invoke로 감싸여 있지 않다. 정리 콜백이 던지면 그 뒤의
#     Invoke(OnEndSimulation)가 통째로 건너뛰어진다. 다만 **형제 콜백은
#     살아남는다** — Cancel()을 인자 없이 부르면 콜백을 전부 돌린 뒤 모아
#     던지기 때문이다. 그 둘은 별개의 문제이므로 갈라서 잰다.
#
#   F 동기/Task 실패 정책 불일치 (LC5)
#     같은 논리적 실패를 발생 형태만 달리해 두 번 낸다. 동기 throw는
#     StartSimulation의 catch가 인스턴스를 끄고, await 뒤 faulted Task는
#     ReportSimulationFault가 로그만 남긴다. 통일 방향은 LC5가 정하고 이
#     판정은 **다르다는 사실**만 잡는다.
#
#   D 대조군
#     정상 인스턴스가 11훅을 그대로 받는가. 이것이 없으면 "실패 픽스처가
#     조용하다"를 고침이 들었다고 읽을지 하네스가 죽었다고 읽을지 가를 수 없다.
#     한 인스턴스의 예외가 이웃의 생명주기를 끊지 않는다는 것도 여기서 본다.
#
# 사용법: pwsh Tools\regression\verify-lifecycle-failure.ps1

param(
    [string]$Exe = (Join-Path $PSScriptRoot "..\..\Bin\x64-Debug\Editor\CreatorEditor.exe"),
    [string]$Work = $env:TEMP,
    [int]$TimeoutSeconds = 300
)

$ErrorActionPreference = 'Stop'

if (-not (Test-Path $Exe)) { "실행 파일이 없다: $Exe"; exit 1 }
$exeDir = [System.IO.Path]::GetDirectoryName($Exe)

$scenario = Join-Path $PSScriptRoot "lifecycle_failure_probe.txt"
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
    -RedirectStandardOutput (Join-Path $Work "lifecycle_failure.out") `
    -RedirectStandardError  (Join-Path $Work "lifecycle_failure.err") -PassThru

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

# 픽스처 트레이스
$rows = @([regex]::Matches($logText, '\[LC1\]\s+kind=(\w+)\s+hook=(\w+)\s+owner=(\w+)') |
    ForEach-Object {
        [pscustomobject]@{
            Kind  = $_.Groups[1].Value
            Hook  = $_.Groups[2].Value
            Owner = $_.Groups[3].Value
        }
    })

# 대조군 트레이스 (LifecycleProbe). 대시는 유니코드 em dash라 문자 종류로 받는다.
$controlHooks = @([regex]::Matches($logText, '\[Probe\]\s+([A-Za-z]+)\s+\p{Pd}\s+Control') |
    ForEach-Object { $_.Groups[1].Value })

if ($rows.Count -eq 0) {
    "픽스처 기록이 0건이다. 스크립트가 붙지 않았거나 재생에 들어가지 못했다."
    "로그: $($editorLog[0].FullName)"
    exit 1
}

# ── 트레이스 전문 (눈으로 훑는 항목) ──────────────────────────────────────────

""
"─ 픽스처 트레이스 ───────────────────────────────────────────────"
foreach ($r in $rows) { "  {0,-14} {1}" -f $r.Kind, $r.Hook }
""
"─ 대조군(Control) ───────────────────────────────────────────────"
if ($controlHooks.Count -eq 0) { "  (없음)" }
else { foreach ($h in $controlHooks) { "  $h" } }
""

$failed = New-Object System.Collections.Generic.List[string]

function Hooks-Of([string]$kind) { return @($rows | Where-Object { $_.Kind -eq $kind } | ForEach-Object { $_.Hook }) }

# ── 판정 A: 초기화가 던진 뒤에는 아무 단계도 오지 않는다 ──────────────────────

$init = Hooks-Of 'failinit'
$afterInit = @($init | Where-Object { $_ -ne 'OnInitialized' })

"판정 A 초기화 실패 격리: OnInitialized 뒤 훅 $($afterInit.Count) 건 (기대 0)"
if ($afterInit.Count -gt 0) {
    "  받은 것: $($afterInit -join ', ')"
    "  → 실패한 초기화가 완료로 남아 이후 단계가 그것을 전제로 진행한다(LC1)."
    $failed.Add('A')
}
if ($init -notcontains 'OnInitialized') {
    "  OnInitialized 자체가 오지 않았다 — 픽스처가 붙지 않았을 수 있다."
    $failed.Add('A(픽스처)')
}

# ── 판정 B: 시작 훅이 던지면 루틴이 시작되지 않는다 ───────────────────────────

$begin = Hooks-Of 'failbegin'
$beginSimulate = @($begin | Where-Object { $_ -eq 'OnSimulate' })

"판정 B 시작 실패 격리: OnBeginSimulation 실패 후 OnSimulate $($beginSimulate.Count) 건 (기대 0)"
if ($beginSimulate.Count -gt 0) {
    "  → Invoke가 예외를 삼킨 뒤 StartSimulation이 조건 재검사 없이 이어진다(LC1)."
    $failed.Add('B')
}
if ($begin -notcontains 'OnBeginSimulation') {
    "  OnBeginSimulation이 오지 않았다 — 픽스처가 시작 단계까지 가지 못했다."
    $failed.Add('B(픽스처)')
}

# ── 판정 C: 시작 훅 안의 자기 비활성화가 존중된다 ─────────────────────────────

$dis = Hooks-Of 'disablebegin'
$disSimulate = @($dis | Where-Object { $_ -eq 'OnSimulate' })

"판정 C 자기 비활성화 존중: 스스로 끈 뒤 OnSimulate $($disSimulate.Count) 건 (기대 0)"
if ($disSimulate.Count -gt 0) {
    "  → if (b.Enabled)를 훅 앞에서 한 번만 보므로 훅 안의 전이가 반영되지 않는다(LC1)."
    $failed.Add('C')
}
if ($dis -notcontains 'OnDisable') {
    "  OnDisable이 오지 않았다 — 자기 비활성화 자체가 전달되지 않았다."
    $failed.Add('C(전파)')
}

# ── 판정 E: 정리 콜백 예외가 종료 절차를 끊지 않는다 (LC2) ────────────────────
#
# 두 가지를 갈라 본다. ① 형제 콜백은 살아남는가 — CancellationTokenSource.Cancel()은
# 인자 없이 부르면 콜백을 전부 돌린 뒤 모아 던지므로 살아남아야 한다. ② 호출자 쪽
# 후속 생명주기는 살아남는가 — Scope.Cancel()이 Invoke로 감싸여 있지 않아 이쪽이
# 끊긴다. 계획서가 "그 둘은 별개의 문제"라고 적어 둔 자리다.

$cleanup = Hooks-Of 'failcleanup'
$siblings = @($cleanup | Where-Object { $_ -eq 'cleanup1' -or $_ -eq 'cleanup3' })
$endAfterCleanupFail = @($cleanup | Where-Object { $_ -eq 'OnEndSimulation' })

"판정 E 정리 예외 격리: 형제 콜백 $($siblings.Count)/2 · 그 뒤 OnEndSimulation $($endAfterCleanupFail.Count) 건 (기대 2 / 1)"
if ($siblings.Count -lt 2) {
    "  형제 콜백이 끊겼다: 받은 것 $($siblings -join ', ')"
    $failed.Add('E(형제)')
}
if ($endAfterCleanupFail.Count -eq 0) {
    "  → Scope.Cancel()이 Invoke 밖이라, 콜백 예외가 그 뒤 OnEndSimulation을 통째로 건너뛴다(LC2)."
    $failed.Add('E')
}

# ── 판정 F: 같은 실패는 같은 상태로 끝난다 (LC5) ──────────────────────────────
#
# 동기 throw는 StartSimulation의 catch가 b.Enabled = false로 끄고, await 뒤의
# faulted Task는 ReportSimulationFault가 로그만 남긴다. 같은 논리적 실패인데
# 남는 활성 상태가 다르다. 어느 쪽으로 통일할지는 LC5가 정하고, 이 판정은
# **다르다는 사실**만 잡는다.
#
# ★ 축을 'OnDisable이 왔는가'로 두면 안 된다 — 처음에 그렇게 짰다가 두 픽스처가
#   나란히 초록이었다. 축소가 ApplyEnabled(b, false)로 **모든** 인스턴스에
#   OnDisable을 주므로(2026-09-05), 재생 중에 꺼진 것과 정지가 끈 것이 한 축에서
#   구분되지 않는다. 그래서 훅의 유무가 아니라 **결과**를 잰다: 실패한 뒤에도
#   틱을 계속 받는가. 두 픽스처가 PostPhysics에서 30틱마다 beat를 남긴다.

$syncHooks  = Hooks-Of 'failsimsync'
$asyncHooks = Hooks-Of 'failsimasync'

# 실패 시점(OnSimulate) 이후의 beat만 센다.
function Beats-After-Simulate($hooks) {
    $i = [array]::IndexOf($hooks, 'OnSimulate')
    if ($i -lt 0) { return -1 }
    return @($hooks[($i + 1)..($hooks.Count - 1)] | Where-Object { $_ -eq 'beat' }).Count
}

$syncBeats  = Beats-After-Simulate $syncHooks
$asyncBeats = Beats-After-Simulate $asyncHooks

"판정 F 실패 정책 일관: 실패 후 틱 — 동기 $syncBeats 회 · Task $asyncBeats 회 (기대: 같음)"
if ($syncBeats -lt 0 -or $asyncBeats -lt 0) {
    "  두 픽스처 중 하나가 루틴에 들어가지 못했다 — 비교가 성립하지 않는다."
    $failed.Add('F(픽스처)')
}
elseif (($syncBeats -eq 0) -ne ($asyncBeats -eq 0)) {
    "  → 발생 형태에 따라 실패 후 살아남는지가 갈린다(LC5). 동기는 꺼지고 Task는 계속 돈다."
    "  → 통일 방향(둘 다 끈다 / 둘 다 로그만)은 LC5가 정한다."
    $failed.Add('F')
}

# ── 판정 D: 대조군은 온전하다 ─────────────────────────────────────────────────
#
# 정상 경로의 11훅. 이웃의 예외가 이 인스턴스의 생명주기를 끊지 않아야 한다.

$expectedControl = @(
    'Awake', 'AddedToScene', 'Enable', 'Start', 'SimulateStart',
    'Disable', 'SimulateCancel', 'EndSimulation', 'RemovingFromScene', 'Uninitializing'
)
$missingControl = @($expectedControl | Where-Object { $controlHooks -notcontains $_ })

"판정 D 대조군 온전: 필수 훅 $($expectedControl.Count) 종 중 누락 $($missingControl.Count) 건 (기대 0)"
if ($missingControl.Count -gt 0) {
    "  누락: $($missingControl -join ', ')"
    "  → 이웃의 예외가 정상 인스턴스의 생명주기를 끊었거나 하네스가 죽었다."
    $failed.Add('D')
}

# ── 판정 ──────────────────────────────────────────────────────────────────────

""
if ($failed.Count -gt 0) {
    "붉은 판정: $($failed -join ', ')"
    "LC1~LC5가 착지하기 전까지 A·B·C·E·F는 붉은 것이 정상이다. D가 붉으면 하네스를 먼저 볼 것."
    exit 1
}

"전체 통과 — 실패한 훅이 뒤 단계를 끌고 가지 않고, 자기 비활성화가 존중되며, 이웃은 온전하다"
exit 0
