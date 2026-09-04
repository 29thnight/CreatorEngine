# script.add 이중 초기화 게이트 (트랙 C · C2-2).
#
# ── 무엇을 잡는가 ──
#
# Editor/EngineGUIWindow/InspectorWindow.cpp의 AttachManagedScript와
# Editor/EngineEntry/ConsoleCommandSystem.cpp의 script.add는 예전에 같은 결함을 갖고
# 있었다 — AddComponentAllowMultiple 직후 script->OnInitialized()를 수동으로
# 불렀는데, AttachComponentLifecycle이 이미 그 컴포넌트를 PendingAwake 큐에
# 넣어 둔 뒤라(State_AwakeCalled 비트는 서지 않은 채) 다음 프레임의
# Scene::RegistryDrainAwakeAndStart가 같은 컴포넌트를 또 한 번 깨웠다 —
# OnInitialized 이중 호출.
#
# ── 왜 lifecycle.trace 건수가 아니라 로그 줄 수를 세는가 ──
#
# OnInitialized 단계 LIFECYCLE_TRACE 호출은 Scene.cpp의 자동 드레인 지점
# (RegistryDrainAwakeAndStart) 한 곳에만 있다. script.add/AttachManagedScript의
# (예전) 수동 호출부에는 트레이스 매크로가 없었다 — 그래서 결함이 있던 옛
# 코드에서도 트레이스에는 자동 드레인 쪽 호출 단 한 번만 찍히고 수동 호출은
# 아예 기록되지 않는다. 트레이스 건수만으로는 수정 전/후가 똑같이 1건으로
# 나와 이중 호출을 구분하지 못한다.
#
# 그리고 ScriptComponent::OnInitialized의 `if (HasInstance()) return;` 가드가
# 정상 스크립트 타입에서는 두 번째 호출을 조용히 삼킨다 — 그래서 정상 타입으로
# 시험하면 이중 호출이 있어도 관측 가능한 부작용이 아예 없다(우연히 멱등).
#
# 그래서 시나리오(script_add_awake_once.txt)는 **등록되지 않은 스크립트
# 타입**을 붙인다. CreateBehaviour가 항상 실패해 HasInstance()가 절대
# true가 되지 않으므로 그 가드가 절대 두 번째 호출을 막지 못하고, 호출될
# 때마다 로그 한 줄을 남긴다 — 그 줄 수가 곧 OnInitialized 호출 횟수다.
#
#   0회 = 명령 자체가 실행되지 않았다(오브젝트/씬 문제 등) — 판정 불가로 실패
#   1회 = 정상(수정 후 — scene->DrainPendingLifecycle() 동기 호출 경로)
#   2회 = 결함 재현(수정 전 — 수동 호출 + 다음 프레임 자동 드레인)
#   3회+ = 예상 밖의 추가 호출 — 그 자체로 조사 대상
#
# PROBE_TYPE은 시나리오 파일과 이 스크립트가 공유하는 리터럴 상수다.
# 바꾸려면 두 파일 모두 고칠 것.
#
# 사용법:
#   pwsh Tools\regression\verify-script-add-awake-once.ps1
param(
    [string]$Exe = (Join-Path $PSScriptRoot "..\..\Bin\x64-Debug\Editor\CreatorEditor.exe"),
    [string]$Work = $env:TEMP,
    [int]$TimeoutSeconds = 300
)

$ProbeType = "NoSuchScriptType_C2_2_Regression"

$exeDir = [System.IO.Path]::GetDirectoryName($Exe)
if (-not (Test-Path $Exe)) { "실행 파일이 없다: $Exe"; exit 1 }

$script = Join-Path $PSScriptRoot "..\..\scripts\script_add_awake_once.txt"
$script = [System.IO.Path]::GetFullPath($script)
if (-not (Test-Path $script)) { "시나리오가 없다: $script"; exit 1 }

$outPath = Join-Path $Work "script_add_awake_once.out"
$errPath = Join-Path $Work "script_add_awake_once.err"
$producedTrace = Join-Path $PSScriptRoot "..\..\Artifacts\Tests\Editor\Traces\script_add_awake_once_trace.tsv"
if (Test-Path $producedTrace) { Remove-Item $producedTrace -Force }

$proc = Start-Process -FilePath $Exe -ArgumentList "--script", $script `
    -WorkingDirectory $exeDir `
    -RedirectStandardOutput $outPath `
    -RedirectStandardError $errPath -PassThru

$proc.WaitForExit($TimeoutSeconds * 1000) | Out-Null
if (-not $proc.HasExited) {
    $proc.Kill()
    "타임아웃 ($TimeoutSeconds 초). 시나리오가 끝나지 않았다."
    exit 1
}

if (-not (Test-Path $outPath)) { "표준 출력이 없다: $outPath"; exit 1 }

# ── 로그 줄 수 — 주 판정 ──
#
# Debug->LogWarning/LogError는 stdout에 안 나간다(인메모리·HTML 로그 싱크뿐 —
# verify-asan-lifecycle.ps1의 같은 주석 참고). Editor_*.html에서 읽는다.
$logDir = Join-Path $exeDir "Saved\Log"
$editorLog = Get-ChildItem (Join-Path $logDir "Editor_*.html") -ErrorAction SilentlyContinue |
             Sort-Object LastWriteTime -Descending | Select-Object -First 1

if (-not $editorLog) {
    "에디터 로그를 찾지 못했다: $logDir\Editor_*.html"
    exit 1
}

$logText = (Get-Content -LiteralPath $editorLog.FullName -Raw) -replace '<[^>]+>', ''

$escapedType = [regex]::Escape($ProbeType)
$pattern = "(?:인스턴스 생성 실패 — 등록되지 않은 타입: $escapedType|CLR이 준비되지 않아 $escapedType 을 만들지 못했습니다)"
$hits = [regex]::Matches($logText, $pattern)
$callCount = $hits.Count

# ── 보조 신호: lifecycle.trace ──
#
# 위에서 설명한 이유로 이 신호 하나로는 이중 호출을 구분할 수 없다 — 드레인이
# 실제로 한 번은 돌았다는 것만 확인한다(트레이스는 자동 드레인 경로에서만
# 기록되므로, 이 값은 결함 유무와 무관하게 1이면 정상이다).
$traceCount = 0
if (Test-Path $producedTrace) {
    $traceLines = Get-Content -LiteralPath $producedTrace |
        Where-Object { $_ -notmatch '^\s*#' -and $_.Trim() -ne '' }
    $traceCount = @($traceLines | Where-Object {
        $_ -match "^\d+\tOnInitialized\tScriptComponent\tScriptAddRegressionTarget\t"
    }).Count
}

$stdoutText = Get-Content -LiteralPath $outPath -Raw
$attachFailedSeen = ($logText -match [regex]::Escape("부착 실패 — 타입=$ProbeType")) `
    -or ($stdoutText -match [regex]::Escape("스크립트 부착 실패 (타입=$ProbeType)"))

"명령 실행 확인 — 부착 실패 로그 관측: $attachFailedSeen (프로브 타입은 항상 실패하므로 참이어야 한다)"
"드레인 확인(보조) — OnInitialized 트레이스 $traceCount 건 (0이면 드레인 자체가 안 돈 것)"
"OnInitialized 호출 흔적 — 로그 $callCount 줄 (기대: 1 — CreateBehaviour/CLR 실패 로그가 호출마다 한 줄씩 남는다)"
""

$failed = @()

if (-not $attachFailedSeen) {
    $failed += "부착 실패 로그를 못 찾았다 — script.add 명령 자체가 실행되지 않았을 수 있다(오브젝트 생성 확인)"
}

if ($traceCount -eq 0) {
    $failed += "OnInitialized 트레이스가 0건이다 — 드레인 경로 자체가 안 돌았다(scene->DrainPendingLifecycle 호출 여부를 의심할 것)"
}

if ($callCount -eq 0) {
    $failed += "OnInitialized 호출 흔적이 0건이다 — 판정 불가(로그 파싱 또는 명령 실행 자체를 의심할 것)"
} elseif ($callCount -eq 2) {
    $failed += "OnInitialized가 2회 호출됐다 — 이중 초기화 결함이 재현됐다(수동 OnInitialized() 호출이 남아 있는지 확인할 것)"
} elseif ($callCount -gt 2) {
    $failed += "OnInitialized 호출 흔적이 $callCount 건이다 — 예상 밖의 추가 호출(별도 조사 대상)"
}

# ── 종료 코드: 이 시나리오는 **일부러 실패한다** ──────────────────────────
#
# 이 검사는 존재하지 않는 프로브 타입을 붙여 "OnInitialized가 정확히 한 번"을 본다.
# 부착은 반드시 실패하고, 그것이 시나리오의 전제다(위 attachFailedSeen 단정).
#
# 예전에는 `script.add`가 legacy void 핸들러라 실패해도 종료 코드가 0이었고, 그래서
# 여기서 0을 기대했다. LC7이 그것을 결과형으로 옮기면서 실패가 session을 거쳐
# 종료 코드에 닿는다(Failed → 4). 그것이 LC1이 세운 규약이다.
#
# 그래서 기대값을 4로 **올린다** — 느슨하게 "0이 아니어도 통과"로 두지 않는다.
# 여기서 0이 돌아오면 `script.add`가 실패를 다시 삼키기 시작했다는 뜻이고,
# 그것은 이 시나리오가 아니라 명령 계약이 깨진 것이다.
$expectedExit = 4
if ($proc.ExitCode -ne $expectedExit) {
    $failed += ("종료 코드 {0} 을 기대했는데 0x{1:X8} 이다 — " -f $expectedExit, $proc.ExitCode) +
               "0 이면 script.add 가 부착 실패를 보고하지 않는다는 뜻이다(LC1 규약 위반)"
}

if ($failed.Count -gt 0) {
    "실패 $($failed.Count)건:"
    $failed | ForEach-Object { "  $_" }
    exit 1
}

"전체 통과 — OnInitialized 정확히 1회"
exit 0
