[CmdletBinding()]
param(
    [string]$Exe  = (Join-Path $PSScriptRoot "..\..\Bin\x64-Debug\Editor\CreatorEditor.exe"),
    [string]$Work = (Join-Path $env:TEMP "lc7"),

    [ValidateRange(5, 900)]
    [int]$BootTimeoutSec = 180
)

# PHASE 14.5 LC7 (§10.2) — 리로드 실패가 반쯤 교체된 상태를 남기지 않는다.
#
# ── 무엇이 잘못돼 있었나 (실측) ─────────────────────────────────────────
#
# 관리 쪽 `ScriptAssemblyLoader.Reload()` 가 `Unload(); Load();` 였다. 그래서 새
# 어셈블리가 깨져 있으면 이전 것은 **이미 사라진 뒤**였고, 에디터에는 스크립트가
# 하나도 남지 않았다. 실측으로 확인했다:
#
#   리로드 전:  script.add Bobber  ->  부착 완료(id=1)
#   리로드 실패
#   리로드 후:  script.add Bobber  ->  부착 실패(타입=Bobber)
#
# 복구 방법은 성공적 리로드나 프로세스 재시작뿐이었다. 그런데 빌드가 깨진 채로
# 리로드를 부르는 것은 드문 일이 아니라 **가장 흔한 일**이다 — 그때마다 에디터가
# 못 쓰는 상태가 되면 라이브 코드 교체라고 부를 수 없다.
#
# LC7 이 갈아 끼우기 **전에** 버리는 컨텍스트에서 새 어셈블리를 검증하도록 고쳤다.
#
# ── 둘째 결함 (2026-09-06 실측) — 실패가 인스턴스를 두 벌로 만든다 ─────────
#
# 위 고침이 "이전 어셈블리를 지킨다"를 세운 뒤, CLI 쪽이 그 전제를 따라가지
# 않았다. Cmd_script_reload 는 리로드 **전에** 전 스크립트의 인스턴스 id 를 끊어
# 두는데(PrepareForReload — "관리 측이 통째로 내려가므로"), 검증 실패로 관리 측이
# 내려가지 **않으면** 옛 인스턴스는 그대로 살아 있고, 복원이 그 옆에 새 것을 하나
# 더 만들었다. 재생 중이면 그때부터 틱이 두 벌 돈다 — 입력이 두 번 처리되고
# 코루틴이 두 벌 돈다. 옛 것은 아무도 거두지 않는다(소유자가 살아 있어
# SweepOrphans 도 못 잡는다).
#
# 이 게이트가 그것을 못 본 이유: "실패했는가 · 이전 어셈블리를 지켰다고 보고하는가
# · 실패 뒤에도 붙는가"만 보고, **붙어 있던 것이 몇 벌인지**는 세지 않았다.
# 관측은 이미 있었다 — script.status 가 컴포넌트마다 instanceId 를 내고 재생 중에는
# activeScripts(관리 측 _active.Count)를 낸다. 쓰지 않았을 뿐이다.
#
# 그래서 두 축을 더한다:
#   identity-after-failure   편집 모드 — 실패 뒤 같은 컴포넌트의 instanceId 가 그대로다
#   single-after-failure     재생 중   — 실패 뒤 activeScripts 와 identity 가 그대로다
#
# 편집 모드에서는 activeScripts 가 갱신되지 않는다(FlushRegistrations 가
# TickSimulationFrame 안에 있다). 그래서 편집 축은 identity 로, 재생 축은 둘 다로 잰다.
#
# ── 이 게이트가 스스로를 검사한다 ───────────────────────────────────────
#
# 핵심 단정은 "리로드 실패 뒤에도 스크립트가 붙는다" 이고, 그것이 뜻을 가지려면
# **리로드가 실제로 실패했어야** 한다. 손상이 먹히지 않아 리로드가 성공해 버리면
# 이 검사는 아무것도 확인하지 않은 채 초록이 된다. 그래서 실패 자체를 먼저 단정한다.

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
if (Get-Process CreatorEditor -ErrorAction SilentlyContinue) { throw 'Close the existing editor before running this isolated gate.' }
$script:ownedEditors = [System.Collections.Generic.List[System.Diagnostics.Process]]::new()

if (-not (Test-Path -LiteralPath $Exe -PathType Leaf)) { "실행 파일이 없다: $Exe"; exit 1 }
$exeDir   = Split-Path -Parent $Exe
$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
New-Item -ItemType Directory -Force -Path $Work | Out-Null

$endpointPath = Join-Path $repoRoot 'Dynamic_CPP\Library\CommandService\endpoint.json'
$scriptDll    = Join-Path (Split-Path (Split-Path $Exe -Parent) -Parent) 'Managed\Scripts\GameScripts.dll'
if (-not (Test-Path -LiteralPath $scriptDll)) { "스크립트 어셈블리가 없다: $scriptDll"; exit 1 }

$backup = Join-Path $Work 'GameScripts.backup.dll'
Copy-Item -LiteralPath $scriptDll -Destination $backup -Force

function Stop-AllEditors {
    $script:ownedEditors | Where-Object { -not $_.HasExited } | ForEach-Object {
        try { $_.Kill(); $_.WaitForExit(20000) | Out-Null } catch { }
    }
    if (Test-Path -LiteralPath $endpointPath) { Remove-Item -LiteralPath $endpointPath -Force }
}

$failures = New-Object System.Collections.Generic.List[string]

try {
    Stop-AllEditors
    $proc = Start-Process -WindowStyle Hidden -FilePath $Exe -ArgumentList '--command-service' -WorkingDirectory $exeDir `
        -RedirectStandardOutput (Join-Path $Work 'reload.out') `
        -RedirectStandardError  (Join-Path $Work 'reload.err') -PassThru
    $script:ownedEditors.Add($proc)

    $deadline = (Get-Date).AddSeconds($BootTimeoutSec)
    $info = $null
    while ((Get-Date) -lt $deadline) {
        if (Test-Path -LiteralPath $endpointPath) {
            try {
                $parsed = Get-Content -LiteralPath $endpointPath -Raw | ConvertFrom-Json
                if ($parsed.pid -eq $proc.Id -and $parsed.port -gt 0) { $info = $parsed; break }
            } catch { }
        }
        Start-Sleep -Milliseconds 300
    }
    if ($null -eq $info) { "서비스가 뜨지 않았다"; exit 1 }

    $base = "http://127.0.0.1:$($info.port)"
    $auth = @{ Authorization = "Bearer $($info.token)" }

    function Send([string]$Body, [int]$TimeoutSec = 60) {
        $r = Invoke-WebRequest ($base + '/command') -Method POST -Headers $auth `
             -ContentType 'application/json' -Body $Body -UseBasicParsing `
             -SkipHttpErrorCheck -TimeoutSec ($TimeoutSec + 15)
        return ($r.Content | ConvertFrom-Json)
    }
    function Attach([string]$ObjectName) {
        $null = Send ('{"command":"object.create","args":["' + $ObjectName + '"],"mode":"sync"}')
        return Send ('{"command":"script.add","args":["' + $ObjectName + '","Bobber"],"mode":"sync"}')
    }

    # ── 0) 프레임 루프가 살아 있는지 먼저 확인한다 ──────────────────────
    #
    #   endpoint.json 이 생긴 것은 서비스 스레드가 떴다는 뜻이지 게임 스레드가
    #   프레임을 돌린다는 뜻이 아니다. Debug 에디터는 첫 프레임까지 몇 초가
    #   걸리고(셰이더·자산 로드), 그 사이의 sync 명령은 waitedFrames=0 으로
    #   5000ms 타임아웃을 낸다 — 2026-09-06 실측: object.create 와 script.add 가
    #   그렇게 죽고 script.status 는 3프레임 뒤 성공했다. 그러면 baseline-attach 는
    #   "붙지 않았다"가 아니라 "묻기 전에 물었다"인데, 아래 판정은 그 둘을 못 가른다.
    #
    #   그래서 첫 명령 전에 값싼 명령이 succeeded 로 돌아올 때까지 기다린다.
    $ready = $false
    $readyDeadline = (Get-Date).AddSeconds($BootTimeoutSec)
    while ((Get-Date) -lt $readyDeadline) {
        $probe = Send '{"command":"script.status","mode":"sync","timeoutMs":5000}'
        if ($probe.status -eq 'succeeded') { $ready = $true; break }
        Start-Sleep -Milliseconds 500
    }
    "{0,-26} ready={1}" -f 'frame-loop', $ready
    if (-not $ready) {
        $failures.Add("frame-loop : $BootTimeoutSec 초 안에 게임 스레드가 명령을 실행하지 못했다 — 아래 검사가 전부 무의미하다")
        throw "프레임 루프가 서지 않았다"
    }

    # ── 1) 기준: 손대기 전에는 붙는다 ───────────────────────────────────
    $before = Attach 'ReloadProbeBefore'
    "{0,-26} status={1} id={2}" -f 'baseline-attach', $before.status, $before.data.instanceId
    if ($before.status -ne 'succeeded') {
        $failures.Add("baseline-attach : 손대기 전인데 부착이 안 됐다(status=$($before.status)) — 아래 검사가 전부 무의미해진다")
        throw "기준이 서지 않았다"
    }

    # ── 2) 어셈블리를 깨고 리로드 ───────────────────────────────────────
    Set-Content -LiteralPath $scriptDll -Value 'not a managed assembly' -Encoding ASCII
    $reload = Send '{"command":"script.reload","mode":"sync","timeoutMs":50000}'
    "{0,-26} status={1} code={2} kept={3}" -f `
        'reload-fails', $reload.status, $reload.code, $reload.data.previousAssemblyKept

    # ★ 실패 자체를 먼저 단정한다. 손상이 안 먹혀 성공해 버리면 아래 단정이 공허하다.
    if ($reload.status -eq 'succeeded') {
        $failures.Add('reload-fails : 깨진 어셈블리로 리로드가 성공했다 — 손상이 먹히지 않았고 아래 검사는 무의미하다')
    }
    elseif ($reload.code -ne 'script.reload_failed') {
        $failures.Add("reload-fails : 기대한 코드가 아니다(code=$($reload.code))")
    }
    if ($true -ne $reload.data.previousAssemblyKept) {
        $failures.Add('reload-fails : previousAssemblyKept 가 참이 아니다 — 이전 어셈블리를 지켰다고 보고하지 않는다')
    }

    # ── 2b) 실패가 살아 있던 인스턴스를 건드리지 않는다 (identity) ─────────
    #
    #   가르는 축은 identity 다. 실패 뒤 같은 컴포넌트의 instanceId 가 리로드 전과
    #   같아야 한다. 달라졌다면 새 인스턴스가 만들어진 것이고 — 이전 어셈블리는
    #   그대로인데 — 옛 것은 관리 측에 산 채로 남는다.
    $st1 = Send '{"command":"script.status","mode":"sync"}'
    $probe1 = @($st1.data.components | Where-Object owner -eq 'ReloadProbeBefore')
    $probe1Id = if ($probe1.Count -ge 1) { $probe1[0].instanceId } else { '(없음)' }
    "{0,-26} count={1} id={2} (before={3})" -f 'identity-after-failure', $probe1.Count, $probe1Id, $before.data.instanceId
    if ($probe1.Count -ne 1) {
        $failures.Add("identity-after-failure : ReloadProbeBefore 의 ScriptComponent 가 $($probe1.Count) 개다(기대 1)")
    }
    elseif ($probe1[0].instanceId -ne $before.data.instanceId) {
        $failures.Add(("identity-after-failure : 실패한 리로드가 인스턴스를 바꿨다 " +
            "(before=$($before.data.instanceId) after=$($probe1[0].instanceId)) — 이전 어셈블리는 그대로인데 " +
            "새 인스턴스를 만들었고, 옛 것은 관리 측에 산 채로 남는다"))
    }

    # ── 3) 핵심: 실패 뒤에도 이전 어셈블리로 스크립트가 붙는다 ──────────
    $after = Attach 'ReloadProbeAfter'
    "{0,-26} status={1} id={2}" -f 'attach-after-failure', $after.status, $after.data.instanceId
    if ($after.status -ne 'succeeded') {
        $failures.Add(("attach-after-failure : 리로드 실패가 이전 어셈블리를 지우고 갔다 " +
            "(status=$($after.status) code=$($after.code)) — 반쯤 교체된 상태다(§10.2)"))
    }

    # ── 4) 되돌리면 정상 리로드가 된다 ──────────────────────────────────
    Copy-Item -LiteralPath $backup -Destination $scriptDll -Force
    $good = Send '{"command":"script.reload","mode":"sync","timeoutMs":50000}'
    "{0,-26} status={1} restored={2}/{3}" -f `
        'reload-recovers', $good.status, $good.data.restored, $good.data.total
    if ($good.status -ne 'succeeded') {
        $failures.Add("reload-recovers : 정상 어셈블리로도 리로드가 안 된다(status=$($good.status) code=$($good.code))")
    }

    # ── 4b) 재생 중 실패 — 틱 목록이 두 벌이 되지 않는다 ────────────────
    #
    #   편집 모드의 identity 판정은 "새 인스턴스가 섰다"까지만 본다. 재생 중이면
    #   그 새 인스턴스가 진입 단계를 다 받고 틱 목록에 오르므로 **활성 스크립트
    #   수가 곧 벌 수**다 — activeScripts 는 관리 측 _active.Count 이고 Pre/Post
    #   틱이 그 목록을 돈다. 두 벌이면 입력이 두 번 처리되고 코루틴이 두 벌 돈다.
    #
    #   activeScripts 는 재생 중에만 갱신된다. 그래서 이 판정은 재생 안에서만 뜻이
    #   있고, 편집 모드는 위 2b 의 identity 축이 맡는다.
    $playResult = Send '{"command":"play","mode":"sync","timeoutMs":60000}' 60
    "{0,-26} status={1}" -f 'play', $playResult.status
    if ($playResult.status -ne 'succeeded') {
        $failures.Add("play : 재생에 들어가지 못했다(status=$($playResult.status) code=$($playResult.code)) — 4b 전체가 무의미하다")
    }
    else {
        Start-Sleep -Milliseconds 1500
        $st2 = Send '{"command":"script.status","mode":"sync"}'
        $activeBefore = [int]$st2.data.activeScripts
        $idsBefore = @($st2.data.components | ForEach-Object { "$($_.owner)=$($_.instanceId)" } | Sort-Object)
        "{0,-26} active={1} ids={2}" -f 'play-baseline', $activeBefore, ($idsBefore -join ',')
        if ($activeBefore -lt 1) {
            $failures.Add("play-baseline : 재생 중인데 활성 스크립트가 $activeBefore 개다 — 아래 판정이 무의미하다")
        }

        Set-Content -LiteralPath $scriptDll -Value 'not a managed assembly' -Encoding ASCII
        $reload2 = Send '{"command":"script.reload","mode":"sync","timeoutMs":50000}'
        "{0,-26} status={1} code={2} kept={3}" -f 'reload-fails-in-play', $reload2.status, $reload2.code, $reload2.data.previousAssemblyKept
        if ($reload2.status -eq 'succeeded') {
            $failures.Add('reload-fails-in-play : 깨진 어셈블리로 리로드가 성공했다 — 손상이 먹히지 않았고 아래 판정은 무의미하다')
        }

        Start-Sleep -Milliseconds 1500
        $st3 = Send '{"command":"script.status","mode":"sync"}'
        $activeAfter = [int]$st3.data.activeScripts
        $idsAfter = @($st3.data.components | ForEach-Object { "$($_.owner)=$($_.instanceId)" } | Sort-Object)
        "{0,-26} active={1} ids={2}" -f 'single-after-failure', $activeAfter, ($idsAfter -join ',')
        if ($activeAfter -ne $activeBefore) {
            $failures.Add(("single-after-failure : 실패한 리로드 뒤 활성 스크립트가 $activeBefore -> $activeAfter 다 — " +
                "옛 인스턴스가 살아 있는 채로 새 인스턴스가 틱 목록에 올랐다. 스크립트가 두 벌 돈다"))
        }
        if (($idsAfter -join ',') -ne ($idsBefore -join ',')) {
            $failures.Add("single-after-failure : 재생 중 실패한 리로드가 인스턴스 identity 를 바꿨다 ($($idsBefore -join ',') -> $($idsAfter -join ','))")
        }

        # 되돌리면 재생 중에도 정상 리로드가 된다(진입 단계 복원은 verify-lifecycle-reload 의 QQ 가 잰다).
        Copy-Item -LiteralPath $backup -Destination $scriptDll -Force
        $good2 = Send '{"command":"script.reload","mode":"sync","timeoutMs":50000}'
        "{0,-26} status={1} restored={2}/{3}" -f 'reload-recovers-in-play', $good2.status, $good2.data.restored, $good2.data.total
        if ($good2.status -ne 'succeeded') {
            $failures.Add("reload-recovers-in-play : 재생 중 정상 어셈블리로도 리로드가 안 된다(status=$($good2.status) code=$($good2.code))")
        }
        $stopResult = Send '{"command":"stop","mode":"sync","timeoutMs":60000}' 60
        if ($stopResult.status -ne 'succeeded') {
            $failures.Add("stop : 재생을 끝내지 못했다(status=$($stopResult.status))")
        }
    }

    # ── 5) 이전 컨텍스트 잔존은 script.status 가 답한다 ─────────────────
    # 리로드 직후의 값은 뜻이 없다(호출 스택이 살아 있어 항상 잔존). 몇 프레임
    # 지난 뒤 물어야 판정이 된다 — 그래서 이 값을 reload 가 아니라 status 가 낸다.
    Start-Sleep -Milliseconds 1500
    $st = Send '{"command":"script.status","mode":"sync"}'
    "{0,-26} ready={1} previousContextAlive={2}" -f `
        'status-reports-stale', $st.data.ready, $st.data.previousContextAlive
    if ($st.status -ne 'succeeded') {
        $failures.Add("status-reports-stale : script.status 가 값을 내지 않는다(status=$($st.status))")
    }
}
finally {
    # ★ 되돌린 것을 **확인한다.**
    #
    #   이 게이트는 저장소의 빌드 산출물을 그 자리에서 훼손했다가 되돌린다.
    #   되돌리기가 실패하면 이후의 모든 검사가 깨진 어셈블리 위에서 돌고, 원인은
    #   여기가 아니라 저 멀리서 보인다. 오늘 같은 저장소에서 정확히 그 사고를
    #   봤다 — 자산을 뒤집었다 되돌리는 probe 가 되돌리기 전에 죽어 프로젝트
    #   설정 두 개가 깨진 채로 남았고, 그 뒤 모든 실행이 같은 실패를 재생산했다.
    #
    #   되돌린 결과를 해시로 대조하고, 다르면 **소리 내어** 실패한다.
    Copy-Item -LiteralPath $backup -Destination $scriptDll -Force -ErrorAction SilentlyContinue
    $restoredOk = $false
    try {
        $restoredOk = (Get-FileHash -LiteralPath $scriptDll -Algorithm SHA256).Hash -eq
                      (Get-FileHash -LiteralPath $backup   -Algorithm SHA256).Hash
    } catch { }
    if (-not $restoredOk) {
        Write-Error ("스크립트 어셈블리를 되돌리지 못했다: $scriptDll — " +
                     "백업은 $backup 에 있다. 이후 검사가 전부 깨진 어셈블리 위에서 돈다.")
    }
    Stop-AllEditors
}

""
if ($failures.Count -gt 0) {
    "실패:"
    $failures | ForEach-Object { "  - $_" }
    exit 1
}
"스크립트 리로드 계약 통과 — 실패가 이전 어셈블리를 지우지 않고, 살아 있던 인스턴스를 두 벌로 만들지도 않는다"
exit 0
