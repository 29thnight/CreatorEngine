[CmdletBinding()]
param(
    [string]$Exe  = (Join-Path $PSScriptRoot "..\..\Bin\x64-Debug\Editor\CreatorEditor.exe"),
    [string]$Work = (Join-Path $env:TEMP "lc5"),

    [ValidateRange(20, 1000)]
    [int]$Samples = 100,

    [ValidateRange(5, 600)]
    [int]$BootTimeoutSec = 120
)

# LC5 (PHASE 14.5) — 드레인 예산 · operation · 지연 SLO.
#
# ── SLO 를 실측 위에 세운다 ─────────────────────────────────────────────
#
# §7.1 은 p50 50ms / p95 150ms / p99 300ms 를 적었는데, LC0 이 잰 바닥값은
# p50 2.5~3.0ms 였다. 그 목표를 그대로 게이트로 쓰면 **20배 퇴행이 초록으로
# 통과한다.** 목표가 아니라 실측 + 여유가 게이트여야 한다.
#
# 여기 쓰는 값은 LC5 실측(HTTP 왕복 p50 6.5ms · p95 9.1ms · p99 10.5ms)에
# 약 3배 여유를 얹은 것이다. Debug 빌드와 느린 기계를 감안한 여유이고,
# 그래도 §7.1 의 원래 목표보다 2배 이상 엄하다.
#
# ── 게이트가 자기 이빨을 확인한다 (§14.7) ───────────────────────────────
#
# 드레인 예산을 0 으로 만들면 서비스 큐가 돌지 않는다. 그 상태에서 이 게이트가
# 붉어지지 않으면, 이 게이트는 아무것도 지키고 있지 않은 것이다.

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

# 실측 + 여유. 값을 바꿀 때는 무엇을 재서 그렇게 정했는지 함께 적는다.
$SloP50Ms = 25
$SloP95Ms = 60
$SloP99Ms = 120

if (-not (Test-Path -LiteralPath $Exe -PathType Leaf)) { "실행 파일이 없다: $Exe"; exit 1 }
$exeDir   = Split-Path -Parent $Exe
$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
New-Item -ItemType Directory -Force -Path $Work | Out-Null

$endpointPath = Join-Path $repoRoot 'Dynamic_CPP\Library\CommandService\endpoint.json'
$failures = New-Object System.Collections.Generic.List[string]

if (Get-Process CreatorEditor -ErrorAction SilentlyContinue) { throw 'Close the existing editor before running this isolated gate.' }
$script:gateProcesses = [System.Collections.Generic.List[System.Diagnostics.Process]]::new()
function Stop-AllEditors {
    foreach ($owned in $script:gateProcesses) {
        try { if (-not $owned.HasExited) { $owned.Kill(); $owned.WaitForExit(15000) | Out-Null } } catch { }
    }
    $script:gateProcesses.Clear()
}

# 에디터를 띄우고 endpoint 가 우리 프로세스의 것으로 확정될 때까지 기다린다.
# `--console` 없이도 띄울 수 있어야 한다 — 냉시작 검사가 배치 입력이 하나도
# 없는 실행을 재현하기 때문이다.
function Start-Editor {
    param([string]$Tag, [switch]$WithConsole)

    Stop-AllEditors
    $argumentList = if ($WithConsole) { @('--command-service', '--console') }
                    else             { @('--command-service') }

    $started = Start-Process -FilePath $Exe -ArgumentList $argumentList `
        -WorkingDirectory $exeDir -WindowStyle Hidden `
        -RedirectStandardOutput (Join-Path $Work "$Tag.out") `
        -RedirectStandardError  (Join-Path $Work "$Tag.err") -PassThru
    $script:gateProcesses.Add($started)

    $waitUntil = (Get-Date).AddSeconds($BootTimeoutSec)
    while ((Get-Date) -lt $waitUntil) {
        if (Test-Path -LiteralPath $endpointPath) {
            try {
                $parsed = Get-Content -LiteralPath $endpointPath -Raw | ConvertFrom-Json
                if ($parsed.pid -eq $started.Id -and $parsed.port -gt 0) {
                    return [pscustomobject]@{
                        Process = $started
                        Base    = "http://127.0.0.1:$($parsed.port)"
                        Auth    = @{ Authorization = "Bearer $($parsed.token)" }
                    }
                }
            } catch { }
        }
        Start-Sleep -Milliseconds 300
    }
    try { $started.Kill() } catch { }
    return $null
}

# ── 0) 냉시작: 첫 요청이 Long 이어도 202 여야 한다 ──────────────────────
#
# ★ **표가 비어 있는 실행이 있다.**
#
#   registry 를 채우는 것은 `GetTable()` 이고 그것은 `ExecuteParsed` 에서만
#   불린다. `--command-service` 만 준 실행에는 배치 입력이 없어서, 첫 HTTP
#   요청이 오기 전까지 명령이 하나도 안 돈다 = 표가 비어 있다. 그 상태에서
#   `cost` 조회가 빗나가면 `game.pak` 이 **동기로** 돌고, 호출자는 5초 뒤
#   `timed_out` 을 받는다 — LC5 가 존재하는 이유가 그 순간 사라진다.
#
#   그래서 이 검사는 **첫 요청**이어야 한다. 아래 본 검사는 `wait` 를 먼저
#   부르는데, 그 한 번이 표를 채워 버려서 이 결함을 영영 못 본다.
$cold = Start-Editor -Tag 'cold'
if ($null -eq $cold) { "냉시작: 서비스가 뜨지 않았다"; exit 1 }
try {
    $r = Invoke-WebRequest ($cold.Base + "/command") -Method POST -Headers $cold.Auth `
         -ContentType 'application/json' -Body '{"command":"game.pak","mode":"auto"}' `
         -UseBasicParsing -SkipHttpErrorCheck -TimeoutSec 20
    $j = $r.Content | ConvertFrom-Json
    "{0,-24} HTTP {1} status={2} (첫 요청 · 표가 비어 있는 실행)" -f 'cold-long-async', $r.StatusCode, $j.status
    if ($r.StatusCode -ne 202) {
        $failures.Add("cold-long-async : 냉시작 첫 요청의 cost=Long 이 202 가 아니라 HTTP $($r.StatusCode) — 등록이 안 된 채 서비스가 열렸다")
    }
} finally { Stop-AllEditors }

$session = Start-Editor -Tag 'drain' -WithConsole
if ($null -eq $session) { "서비스가 뜨지 않았다"; exit 1 }
$proc = $session.Process
$base = $session.Base
$auth = $session.Auth

function Post([string]$Body, [int]$TimeoutSec = 20) {
    Invoke-WebRequest ($base + "/command") -Method POST -Headers $auth `
        -ContentType 'application/json' -Body $Body -UseBasicParsing `
        -SkipHttpErrorCheck -TimeoutSec $TimeoutSec
}
function Get-Op([string]$Path, [int]$TimeoutSec = 20) {
    Invoke-WebRequest ($base + $Path) -Headers $auth -UseBasicParsing `
        -SkipHttpErrorCheck -TimeoutSec $TimeoutSec
}
# 최근접 순위법. 보간하지 않는다 — 예산의 분모로 쓸 값은 실제로 관측된 값이어야 한다.
function Get-Pct {
    param([Parameter(Mandatory)][AllowEmptyCollection()][double[]]$Values,
          [Parameter(Mandatory)][double]$Fraction)
    if ($Values.Length -eq 0) { return [double]0 }
    $sorted = [double[]]($Values | Sort-Object)
    $rank   = [int][Math]::Ceiling($Fraction * $sorted.Length)
    $index  = [Math]::Max(0, [Math]::Min($sorted.Length - 1, $rank - 1))
    return [double]$sorted[$index]
}

try {
    # ── 1) `wait` 는 서비스 세션에서 금지 (§7.2) ────────────────────────
    #
    # 전역 프레임 보류는 자기 요청만 늦추는 것이 아니라 **다른 요청 전부**의
    # 지연이 된다. 배치 문법을 서비스로 들이면 안 되는 이유다.
    $r = Post '{"command":"wait","args":["5"]}'
    $j = $r.Content | ConvertFrom-Json
    "{0,-24} HTTP {1} code={2}" -f 'wait-forbidden', $r.StatusCode, $j.code
    if ($r.StatusCode -ne 400 -or $j.code -ne 'service.wait_forbidden') {
        $failures.Add("wait-forbidden : HTTP $($r.StatusCode) code=$($j.code)")
    }

    # ── 2) cost=Long 은 동기 응답을 막지 않는다 (§6.2 · §5.2) ───────────
    #
    # LC3 이 붙인 `cost` 를 보고 서비스가 **판정한다**. 추측하면 초 단위
    # 명령을 동기로 기다리게 되고 지연 계약이 그 순간 거짓말이 된다.
    $r = Post '{"command":"game.pak","mode":"auto"}'
    $j = $r.Content | ConvertFrom-Json
    "{0,-24} HTTP {1} operationId={2}" -f 'long-auto-202', $r.StatusCode, $j.operationId
    if ($r.StatusCode -ne 202 -or [string]::IsNullOrEmpty($j.operationId)) {
        $failures.Add("long-auto-202 : cost=Long 이 202 로 승격되지 않았다 (HTTP $($r.StatusCode))")
    }
    $longOp = $j.operationId

    # 그 긴 명령이 도는 동안 짧은 명령이 답해야 한다.
    $sw = [Diagnostics.Stopwatch]::StartNew()
    $r = Post '{"command":"help"}'
    $sw.Stop()
    "{0,-24} HTTP {1} · {2:F0}ms (긴 명령이 도는 동안)" -f 'short-not-blocked', $r.StatusCode, $sw.Elapsed.TotalMilliseconds
    if ($r.StatusCode -ne 200) {
        $failures.Add("short-not-blocked : 긴 명령 중 짧은 명령이 $($r.StatusCode)")
    }

    # ── 3) operation 폴링·취소·스트림 (§5.2 · §7.4) ─────────────────────
    $r = Post '{"command":"help","mode":"async"}'
    $op = ($r.Content | ConvertFrom-Json).operationId
    if ([string]::IsNullOrEmpty($op)) { $failures.Add('async : operationId 가 없다') }
    else {
        Start-Sleep -Milliseconds 800
        $r = Get-Op ("/operations/" + $op)
        $j = $r.Content | ConvertFrom-Json
        "{0,-24} HTTP {1} state={2} status={3}" -f 'operation-poll', $r.StatusCode, $j.state, $j.status
        if ($r.StatusCode -ne 200 -or $j.state -ne 'completed') {
            $failures.Add("operation-poll : state=$($j.state)")
        }
        if ($null -eq $j.timing -or $null -eq $j.timing.queuedMs) {
            $failures.Add('operation-poll : timing 이 없다')
        }

        # 취소 지점을 가진 명령이 없으므로 409 다. 끊는 시늉을 하면 안 된다.
        $r = Invoke-WebRequest ($base + "/operations/" + $op + "/cancel") -Method POST `
             -Headers $auth -UseBasicParsing -SkipHttpErrorCheck -TimeoutSec 20
        "{0,-24} HTTP {1} (기대 409)" -f 'operation-cancel', $r.StatusCode
        if ($r.StatusCode -ne 409) { $failures.Add("operation-cancel : HTTP $($r.StatusCode) ≠ 409") }

        # 스트림은 수명 전이를 흘리고 끝난다.
        $r = Get-Op ("/operations/" + $op + "/stream") 30
        $events = @(($r.Content -split "`n") | Where-Object { $_ -like 'event: *' })
        "{0,-24} HTTP {1} · 이벤트 {2}개" -f 'operation-stream', $r.StatusCode, $events.Count
        if ($r.StatusCode -ne 200 -or $events.Count -eq 0) {
            $failures.Add("operation-stream : 이벤트가 없다 (HTTP $($r.StatusCode))")
        }
    }

    $r = Get-Op "/operations/op-does-not-exist"
    if ($r.StatusCode -ne 404) { $failures.Add("operation-unknown : HTTP $($r.StatusCode) ≠ 404") }

    # ── 4) 지연 SLO ─────────────────────────────────────────────────────
    # ★ 지역 변수 이름을 매개변수와 겹치지 않게 둔다.
    #
    #   처음에는 `$samples` 였는데, PowerShell 변수는 **대소문자를 가리지 않아서**
    #   `[int]$Samples` 매개변수를 List 로 덮어썼다. 그 뒤 `$n -lt $Samples` 가
    #   "List 를 Int32 로 못 바꾼다"로 죽었다 — 원인과 증상이 멀어 한참 헤맸다.
    $latencies = New-Object System.Collections.Generic.List[double]
    for ($n = 0; $n -lt $Samples; $n++) {
        $sw = [Diagnostics.Stopwatch]::StartNew()
        $r = Post '{"command":"help"}'
        $sw.Stop()
        if ($r.StatusCode -ne 200) { $failures.Add("slo : 표본 $n 이 HTTP $($r.StatusCode)"); break }
        $latencies.Add($sw.Elapsed.TotalMilliseconds)
    }
    $values = [double[]]$latencies.ToArray()
    $p50 = Get-Pct -Values $values -Fraction 0.50
    $p95 = Get-Pct -Values $values -Fraction 0.95
    $p99 = Get-Pct -Values $values -Fraction 0.99
    "{0,-24} p50={1:F2} p95={2:F2} p99={3:F2} (상한 {4}/{5}/{6})" -f `
        'slo', $p50, $p95, $p99, $SloP50Ms, $SloP95Ms, $SloP99Ms
    if ($p50 -gt $SloP50Ms) { $failures.Add("slo : p50 $([Math]::Round($p50,2))ms > $SloP50Ms") }
    if ($p95 -gt $SloP95Ms) { $failures.Add("slo : p95 $([Math]::Round($p95,2))ms > $SloP95Ms") }
    if ($p99 -gt $SloP99Ms) { $failures.Add("slo : p99 $([Math]::Round($p99,2))ms > $SloP99Ms") }

    # ── 5) 변이: 예산 0 이면 SLO 가 붉어진다 (§14.7) ────────────────────
    #
    # 게이트가 처음부터 초록이면 이빨을 확인한다. 예산 0 에서도 지연이
    # 상한 안이라면, 위의 SLO 검사는 아무것도 지키고 있지 않은 것이다.
    # ★ **깨끗한 큐에서 재야 한다.**
    #
    #   처음에는 429 검사 뒤에 뒀는데, 그때는 큐가 이미 가득 차 있어 요청이
    #   느려지는 대신 429 로 **즉시** 거절됐다 — p50 4ms 가 나와서 "이빨이 없다"는
    #   잘못된 판정이 났다. 재는 것이 무엇인지에 맞는 상태에서 재야 한다.
    $null = Post '{"command":"cli.drain.budget","args":["0","0"]}'
    $mutant = New-Object System.Collections.Generic.List[double]
    for ($n = 0; $n -lt 5; $n++) {
        $sw = [Diagnostics.Stopwatch]::StartNew()
        $mr = Post '{"command":"help","timeoutMs":1500}' 12
        $sw.Stop()
        # 200 이 아니면(429 등) 지연 판정에 섞지 않는다 — 그것은 다른 사건이다.
        if ($mr.StatusCode -eq 200) { $mutant.Add($sw.Elapsed.TotalMilliseconds) }
    }
    $mutantP50 = Get-Pct -Values ([double[]]$mutant.ToArray()) -Fraction 0.50
    "{0,-24} 예산 0 에서 p50={1:F0}ms (상한 {2}ms 를 넘어야 한다)" -f 'slo-mutation', $mutantP50, $SloP50Ms
    if ($mutantP50 -le $SloP50Ms) {
        $failures.Add("slo-mutation : 드레인 예산을 0 으로 해도 p50 이 $([Math]::Round($mutantP50))ms 다 — SLO 검사에 이빨이 없다")
    }

    # 예산을 되돌리고 큐가 비기를 기다린다. 다음 검사는 정상 상태에서 재야 한다.
    $null = Post '{"command":"cli.drain.budget","args":["2","8"]}' 12
    Start-Sleep -Milliseconds 1500

    # ── 6) 큐 상한 → 429 (§7.3) ─────────────────────────────────────────
    #
    # 예산을 0 으로 만들어 큐가 비지 않게 한 뒤 async 로 밀어 넣는다.
    # 무한 적재는 지연을 숨기는 가장 흔한 방법이라, 상한이 실제로 있는지 본다.
    $null = Post '{"command":"cli.drain.budget","args":["0","0"]}'
    $sawQueueFull = $false
    for ($n = 0; $n -lt 90; $n++) {
        $r = Post '{"command":"help","mode":"async"}' 8
        if ($r.StatusCode -eq 429) { $sawQueueFull = $true; break }
    }
    "{0,-24} {1}" -f 'queue-429', $(if ($sawQueueFull) { '큐 상한에서 429' } else { '429 를 못 봤다' })
    if (-not $sawQueueFull) { $failures.Add('queue-429 : 큐가 무한히 쌓인다 — 상한이 없다') }

    # ── 7) `/health` 가 **서비스 큐**를 낸다 (§7.3) ─────────────────────
    #
    # ★ 큐를 둘로 쪼갠 뒤 창구가 한쪽만 보고 있었다.
    #
    #   여기 도달한 시점의 서비스 큐는 상한까지 차 있다(바로 위에서 429 를
    #   받았다). 그런데 `/health` 는 **배치 큐**를 내고 있었고, 이 실행에는
    #   배치 입력이 없어 그 값은 언제나 0 이다 — 클라이언트는 429 를 받기
    #   직전까지 `queueDepth 0` 인 한가한 서버를 본다. 적체를 숨기지 않으려고
    #   만든 창구가 정작 새로 생긴 줄에 대해서만 눈을 감고 있었다.
    $r = Get-Op "/health"
    $h = $r.Content | ConvertFrom-Json
    "{0,-24} queueDepth={1} batchQueueDepth={2} state={3}" -f `
        'health-service-queue', $h.queueDepth, $h.batchQueueDepth, $h.state
    if ($r.StatusCode -ne 200) {
        $failures.Add("health-service-queue : HTTP $($r.StatusCode)")
    }
    elseif ($h.queueDepth -le 0) {
        $failures.Add("health-service-queue : 서비스 큐가 상한까지 찼는데 /health 가 queueDepth=$($h.queueDepth) 를 낸다 — 적체가 안 보인다")
    }

    # 예산을 되돌린다. 서비스 큐가 안 도니 배치 큐(stdin)로 복구할 수도 있지만,
    # 이 게이트는 어차피 프로세스를 내린다.
}
finally { Stop-AllEditors }

# ── 8) 스트림이 열려 있어도 종료가 늦지 않는다 ──────────────────────────
#
# ★ **깨우는 신호가 닿지 않는 대기가 하나 있었다.**
#
#   `Stop()` 은 작업 스레드를 `ShutdownSocket` 으로 깨운 뒤 join 한다. 그것이
#   통하는 이유는 그 스레드들이 `recv` 에서 막혀 있기 때문이다. 그런데 스트림
#   핸들러는 소켓을 만지지 않고 표를 보며 sleep 한다 — shutdown 이 아무 효과도
#   없다. 스트림을 연 클라이언트가 **하나만 있어도** 에디터 종료가
#   `streamTimeoutMs`(기본 60초)만큼 통째로 늦어진다. LC4 가 없앤 30초 정지를
#   이름만 바꿔 되살린 셈이었다.
#
#   재현 조건을 만든다: 드레인 예산을 0 으로 두면 async 로 넣은 operation 이
#   영원히 `queued` 로 남고, 그 위의 스트림은 완료를 못 본다. 그 상태에서
#   창을 닫고 프로세스가 실제로 언제 사라지는지 잰다.
# CloseMainWindow ignores hidden windows. Target only this gate's PID and engine window class.
Add-Type -TypeDefinition @"
using System;
using System.Text;
using System.Runtime.InteropServices;
public static class CommandGateWindow {
    private delegate bool EnumProc(IntPtr window, IntPtr parameter);
    [DllImport("user32.dll")] private static extern bool EnumWindows(EnumProc callback, IntPtr parameter);
    [DllImport("user32.dll")] private static extern uint GetWindowThreadProcessId(IntPtr window, out uint processId);
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] private static extern int GetClassName(IntPtr window, StringBuilder text, int count);
    [DllImport("user32.dll", SetLastError=true)] private static extern bool PostMessage(IntPtr window, uint message, IntPtr wparam, IntPtr lparam);
    public static bool Close(int processId) {
        IntPtr target = IntPtr.Zero;
        EnumWindows((window, parameter) => {
            uint owner; GetWindowThreadProcessId(window, out owner);
            if (owner != (uint)processId) return true;
            var name = new StringBuilder(128); GetClassName(window, name, name.Capacity);
            if (name.ToString() != "CoreWindowApp") return true;
            target = window; return false;
        }, IntPtr.Zero);
        return target != IntPtr.Zero && PostMessage(target, 0x0010, IntPtr.Zero, IntPtr.Zero);
    }
}
"@
$ShutdownBudgetSec = 25
$life = Start-Editor -Tag 'shutdown' -WithConsole
if ($null -eq $life) {
    $failures.Add('shutdown-with-stream : 서비스가 뜨지 않았다')
}
else {
    $http = $null
    try {
        $lifeProc = $life.Process
        $null = Invoke-WebRequest ($life.Base + "/command") -Method POST -Headers $life.Auth `
                -ContentType 'application/json' -Body '{"command":"cli.drain.budget","args":["0","0"]}' `
                -UseBasicParsing -SkipHttpErrorCheck -TimeoutSec 20

        $r = Invoke-WebRequest ($life.Base + "/command") -Method POST -Headers $life.Auth `
             -ContentType 'application/json' -Body '{"command":"help","mode":"async"}' `
             -UseBasicParsing -SkipHttpErrorCheck -TimeoutSec 20
        $stuckOp = ($r.Content | ConvertFrom-Json).operationId

        if ([string]::IsNullOrEmpty($stuckOp)) {
            $failures.Add('shutdown-with-stream : 매달아 둘 operation 을 못 만들었다')
        }
        else {
            # 스트림을 열어 두고 **기다리지 않는다.** 이 프로세스가 연결을 잡고
            # 있는 동안 저쪽 작업 스레드는 완료를 기다리며 도는 중이다.
            $http = [System.Net.Http.HttpClient]::new()
            $http.Timeout = [TimeSpan]::FromSeconds(90)
            $http.DefaultRequestHeaders.Add('Authorization', $life.Auth.Authorization)
            $streamTask = $http.GetAsync($life.Base + "/operations/$stuckOp/stream")
            Start-Sleep -Milliseconds 700   # 작업 스레드가 루프에 들어갈 시간

            $sw = [Diagnostics.Stopwatch]::StartNew()
            $asked = [CommandGateWindow]::Close($lifeProc.Id)
            if (-not $asked) {
                # 종료 경로를 못 잡았으면 **통과로 처리하지 않는다.** 여기서
                # 조용히 넘어가면 이 검사는 아무것도 지키지 않는 채 초록이 된다.
                $failures.Add('shutdown-with-stream : 창을 닫지 못해 종료 경로를 재지 못했다')
            }
            else {
                $exited = $lifeProc.WaitForExit($ShutdownBudgetSec * 1000)
                $sw.Stop()
                "{0,-24} {1:F1}s (상한 {2}s · 스트림을 연 채 종료)" -f `
                    'shutdown-with-stream', $sw.Elapsed.TotalSeconds, $ShutdownBudgetSec
                if (-not $exited) {
                    $failures.Add("shutdown-with-stream : 스트림이 열려 있으면 종료가 ${ShutdownBudgetSec}s 안에 끝나지 않는다 — 종료 신호가 스트림 루프에 닿지 않는다")
                }
            }
            $streamTask = $null
        }
    }
    finally {
        if ($null -ne $http) { $http.Dispose() }
        Stop-AllEditors
    }
}

""
if ($failures.Count -gt 0) {
    "실패:"
    $failures | ForEach-Object { "  - $_" }
    exit 1
}
"드레인·operation·SLO 전체 통과"
exit 0
