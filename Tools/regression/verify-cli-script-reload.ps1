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
"스크립트 리로드 계약 통과 — 실패가 이전 어셈블리를 지우지 않는다"
exit 0
