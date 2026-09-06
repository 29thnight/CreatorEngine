[CmdletBinding()]
param(
    [string]$Exe  = (Join-Path $PSScriptRoot "..\..\Bin\x64-Debug\Editor\CreatorEditor.exe"),
    [string]$Work = (Join-Path $env:TEMP "lc7"),

    [ValidateRange(5, 900)]
    [int]$BootTimeoutSec = 180,

    # 소스 수정 → 빌드 → reload → invoke 왕복은 dotnet SDK 가 있어야 돈다.
    # 없는 기계에서 **조용히 건너뛰지 않는다** — 건너뛴 사실을 실패로 낸다.
    # 정말로 SDK 없이 나머지만 보고 싶을 때만 이것을 준다.
    [switch]$SkipLiveRoundTrip
)

# PHASE 14.5 LC7 (§10.2) — L3 등급 B: 표식된 메서드 호출과 라이브 코드 교체.
#
# ── 이 게이트가 단정하는 넷 ─────────────────────────────────────────────
#
#   ① 표식된 static 메서드가 호출되고 결과가 **값으로** 돌아온다.
#   ② 표식 **없는** 메서드는 이름을 알아도 호출되지 않는다.
#   ③ 사용자 코드가 던져도 에디터가 죽지 않고, 그것이 명령의 실패로 나온다.
#   ④ **에디터를 끄지 않고** C# 수정 → 빌드 → reload → invoke 가 한 세션에서 닫힌다.
#
# ── ② 를 어떻게 뜻있게 만드는가 ─────────────────────────────────────────
#
# 없는 이름으로 시험하면 확인되는 것은 오타 처리이지 표식 검사가 아니다. 표식
# 검사를 통째로 지워도 그 시험은 초록으로 남는다. 그래서 `EngineCallableProbe`
# 에 **실제로 존재하지만 표식이 없는** `Unmarked()` 를 두고 그것을 부른다. 판정도
# "실패했다" 가 아니라 코드가 `script.invoke_not_callable` 인지로 한다 — 오타·인자
# 오류도 실패라, 실패 여부만 보면 표식 검사가 아닌 것이 통과시킬 수 있다.
#
# ── ④ 를 어떻게 뜻있게 만드는가 ─────────────────────────────────────────
#
# 새 코드가 도는 것을 확인하려면 **반환값이 달라져야** 한다. 그래서 프로브의
# `Ping()` 을 실행마다 새로 만든 토큰으로 바꾸고, 리로드 뒤 그 토큰이 나오는지
# 본다. 상수를 그대로 두고 "리로드가 성공했다" 만 보면, 리로드가 아무 일도 하지
# 않아도 초록이다.
#
# ★ 이 게이트는 **추적되는 소스 파일을 그 자리에서 고친다.** 되돌리기를 해시로
#   확인하고, 되돌린 뒤 다시 빌드해 `Ping()` 이 "pong" 으로 돌아오는 것까지
#   단정한다 — 소스만 되돌리고 DLL 을 두면 다음 실행의 기준선이 깨진다.
#   이 저장소에서 정확히 그 사고를 봤다(되돌리기에 실패한 probe 가 프로젝트
#   설정 둘을 깨뜨린 채 남아 이후 모든 실행이 같은 실패를 재생산했다).

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
if (Get-Process CreatorEditor -ErrorAction SilentlyContinue) { throw 'Close the existing editor before running this isolated gate.' }
$script:ownedEditors = [System.Collections.Generic.List[System.Diagnostics.Process]]::new()

if (-not (Test-Path -LiteralPath $Exe -PathType Leaf)) { "실행 파일이 없다: $Exe"; exit 1 }
$exeDir   = Split-Path -Parent $Exe
$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
New-Item -ItemType Directory -Force -Path $Work | Out-Null

$endpointPath = Join-Path $repoRoot 'Dynamic_CPP\Library\CommandService\endpoint.json'
$probeSource  = Join-Path $repoRoot 'GameScripts\EngineCallableProbe.cs'
$probeProject = Join-Path $repoRoot 'GameScripts\GameScripts.csproj'
if (-not (Test-Path -LiteralPath $probeSource -PathType Leaf)) {
    "프로브 소스가 없다: $probeSource"; exit 1
}

$sourceBackup = Join-Path $Work 'EngineCallableProbe.backup.cs'
Copy-Item -LiteralPath $probeSource -Destination $sourceBackup -Force

# ★ 되돌린 뒤 **수정 시각을 지금으로 올린다.**
#
#   `Copy-Item` 은 원본의 LastWriteTime 을 함께 가져온다. 백업은 이 게이트가
#   시작할 때 뜬 것이라 그 시각이 방금 만든 DLL 보다 **과거**이고, msbuild 는
#   그것을 "최신" 으로 읽어 빌드를 건너뛴다. 그러면 소스는 되돌아왔는데 산출물은
#   토큰이 박힌 채로 남는다 — 실측으로 확인한 실패다(restore-round-trip 이
#   토큰을 그대로 냈다). 바이트는 백업 그대로 두고 시각만 올린다.
function Restore-ProbeSource {
    Copy-Item -LiteralPath $sourceBackup -Destination $probeSource -Force
    (Get-Item -LiteralPath $probeSource).LastWriteTime = Get-Date
}

$failures = New-Object System.Collections.Generic.List[string]
$timings  = [ordered]@{}

function Stop-AllEditors {
    $script:ownedEditors | Where-Object { -not $_.HasExited } | ForEach-Object {
        try { $_.Kill(); $_.WaitForExit(20000) | Out-Null } catch { }
    }
    if (Test-Path -LiteralPath $endpointPath) { Remove-Item -LiteralPath $endpointPath -Force }
}

function Start-Editor {
    param([string[]]$ExtraArgs, [string]$Tag)

    Stop-AllEditors
    $proc = Start-Process -WindowStyle Hidden -FilePath $Exe -ArgumentList (@('--command-service') + $ExtraArgs) `
        -WorkingDirectory $exeDir `
        -RedirectStandardOutput (Join-Path $Work "invoke_$Tag.out") `
        -RedirectStandardError  (Join-Path $Work "invoke_$Tag.err") -PassThru
    $script:ownedEditors.Add($proc)

    $deadline = (Get-Date).AddSeconds($BootTimeoutSec)
    while ((Get-Date) -lt $deadline) {
        if (Test-Path -LiteralPath $endpointPath) {
            try {
                $parsed = Get-Content -LiteralPath $endpointPath -Raw | ConvertFrom-Json
                if ($parsed.pid -eq $proc.Id -and $parsed.port -gt 0) { return $parsed }
            } catch { }
        }
        Start-Sleep -Milliseconds 300
    }
    return $null
}

$dotnet = Get-Command 'dotnet' -ErrorAction SilentlyContinue

# ── 0) 기준선을 이 게이트가 **스스로** 세운다 ───────────────────────────
#
# ★ 실측으로 배운 것이다.
#
#   아래 왕복은 프로브의 `Ping()` 을 토큰으로 바꿨다가 되돌린다. 그 사이에
#   게이트가 죽으면 소스는 되돌아와도 **DLL 은 토큰이 박힌 채** 남는다. 다음
#   실행의 기준선(`Ping() == "pong"`)이 그 DLL 위에서 깨지고, 실패 메시지는
#   "표식된 메서드가 값을 내지 않는다" 라고 말한다 — 원인과 한참 떨어진 설명이다.
#   실제로 이 게이트를 만들면서 그 순서를 그대로 겪었다.
#
#   그러니 저장소에 있던 산출물을 믿지 않고, 시작할 때 소스로부터 한 번 세운다.
#   비용은 빌드 한 번(약 2.5 초)이고, 그 대가로 이 게이트는 지난 실행이 어떻게
#   죽었든 같은 자리에서 시작한다.
if ($null -ne $dotnet -and -not $SkipLiveRoundTrip) {
    & $dotnet.Source build $probeProject -c Debug --nologo -v q *> (Join-Path $Work 'invoke_baseline_build.log')
    if ($LASTEXITCODE -ne 0) {
        "기준선 빌드가 실패했다 — 로그 $(Join-Path $Work 'invoke_baseline_build.log')"; exit 1
    }
}

# ── 1) 사용자 코드는 별도 플래그 없이는 403 이다 (§8) ────────────────────
#
# ★ **먼저** 본다. 뒤로 미루면 아래에서 성공을 확인한 뒤라, 거부를 확인하는
#   실행이 이미 열려 있는 세션을 재사용하고 싶어진다 — 그러면 플래그의 부재가
#   아니라 플래그의 존재를 시험하게 된다. 프로세스를 따로 띄운다.
$denied = Start-Editor -ExtraArgs @() -Tag 'denied'
if ($null -eq $denied) { "서비스가 뜨지 않았다(플래그 없는 실행)"; exit 1 }
try {
    $base = "http://127.0.0.1:$($denied.port)"
    $auth = @{ Authorization = "Bearer $($denied.token)" }
    $r = Invoke-WebRequest ($base + '/command') -Method POST -Headers $auth `
         -ContentType 'application/json' -UseBasicParsing -SkipHttpErrorCheck -TimeoutSec 60 `
         -Body '{"command":"script.invoke","args":["EngineCallableProbe","Ping"],"mode":"sync","timeoutMs":30000}'
    $body = $r.Content | ConvertFrom-Json
    "{0,-26} http={1} code={2}" -f 'user-code-forbidden', $r.StatusCode, $body.code
    if ($r.StatusCode -ne 403) {
        $failures.Add("user-code-forbidden : --allow-user-code 없이 script.invoke 가 통과했다(http=$($r.StatusCode)) — §8 의 사용자 코드 통제가 없다")
    }
    if ($body.code -ne 'service.user_code_forbidden') {
        $failures.Add("user-code-forbidden : 기대한 코드가 아니다(code=$($body.code))")
    }

    # 같은 실행에서 **다른 명령은 그대로 돌아야** 한다. 플래그가 서비스를 통째로
    # 잠그는 것이 아니라 이 한 갈래만 막는다는 것이 요점이다.
    $ok = Invoke-WebRequest ($base + '/command') -Method POST -Headers $auth `
          -ContentType 'application/json' -UseBasicParsing -SkipHttpErrorCheck -TimeoutSec 60 `
          -Body '{"command":"script.status","mode":"sync"}'
    "{0,-26} http={1}" -f 'other-commands-unaffected', $ok.StatusCode
    if ($ok.StatusCode -ne 200) {
        $failures.Add("other-commands-unaffected : 플래그가 없다고 다른 명령까지 막혔다(http=$($ok.StatusCode))")
    }
}
finally { Stop-AllEditors }

# ── 2) 이제 플래그를 주고 본론을 본다 ───────────────────────────────────
$info = Start-Editor -ExtraArgs @('--allow-user-code') -Tag 'allowed'
if ($null -eq $info) { "서비스가 뜨지 않았다(플래그 있는 실행)"; exit 1 }

$base = "http://127.0.0.1:$($info.port)"
$auth = @{ Authorization = "Bearer $($info.token)" }

function Send([string]$Body, [int]$TimeoutSec = 60) {
    $r = Invoke-WebRequest ($base + '/command') -Method POST -Headers $auth `
         -ContentType 'application/json' -Body $Body -UseBasicParsing `
         -SkipHttpErrorCheck -TimeoutSec ($TimeoutSec + 15)
    return ($r.Content | ConvertFrom-Json)
}
function Invoke-Probe {
    # ★ 파라미터 이름을 `$Args` 로 두지 말 것 — PowerShell 의 자동 변수와 겹쳐
    #   바인딩이 조용히 어긋난다. 여기서 한 번 겪었다(인자가 빈 채로 나갔다).
    param([string]$Method, [string[]]$CallArgs = @())

    $list = @("`"EngineCallableProbe`"", "`"$Method`"")
    foreach ($a in $CallArgs) { $list += "`"$a`"" }
    return Send ('{"command":"script.invoke","args":[' + ($list -join ',') + '],"mode":"sync","timeoutMs":60000}')
}

# 실패 응답의 `data` 에는 `returnValue` 가 없다. StrictMode 에서 없는 속성 접근은
# 던지므로, 판정문이 아니라 여기서 흡수한다 — 게이트가 자기 문법으로 죽으면
# 무엇이 실패했는지가 아니라 게이트가 깨졌다는 사실만 남는다.
function Get-Return($Result) {
    if ($null -eq $Result -or $null -eq $Result.data) { return '' }
    $field = $Result.data.PSObject.Properties['returnValue']
    if ($null -eq $field) { return '' }
    return [string]$field.Value
}

try {
    # ── 2a) 표식된 메서드가 값을 돌려준다 ───────────────────────────────
    $ping = Invoke-Probe 'Ping'
    "{0,-26} status={1} return={2}" -f 'marked-invoke', $ping.status, (Get-Return $ping)
    if ($ping.status -ne 'succeeded' -or (Get-Return $ping) -ne 'pong') {
        $failures.Add("marked-invoke : 표식된 메서드가 값을 내지 않는다(status=$($ping.status) code=$($ping.code) return=$((Get-Return $ping)))")
    }

    # 인자 변환(string·int·bool·double)이 왕복하는지. 값 하나가 아니라 **네 타입**을
    # 한 호출에 실어, 변환 표에 구멍이 나면 여기서 잡히게 한다.
    $echo = Invoke-Probe 'Echo' @('ab', '2', 'true', '1.5')
    "{0,-26} status={1} return={2}" -f 'argument-marshalling', $echo.status, (Get-Return $echo)
    if ((Get-Return $echo) -ne 'ABAB|1.5') {
        $failures.Add("argument-marshalling : 인자가 그대로 넘어가지 않았다(return=$((Get-Return $echo)))")
    }

    # ── 2b) 표식 없는 메서드는 거부된다 ─────────────────────────────────
    $unmarked = Invoke-Probe 'Unmarked'
    "{0,-26} status={1} code={2}" -f 'unmarked-rejected', $unmarked.status, $unmarked.code
    if ($unmarked.status -eq 'succeeded') {
        $failures.Add('unmarked-rejected : 표식 없는 메서드가 호출됐다 — L3-B 의 경계가 없다(§10.2)')
    }
    elseif ($unmarked.code -ne 'script.invoke_not_callable') {
        # 여기가 붉으면 대개 프로브의 `Unmarked()` 가 지워졌다는 뜻이다.
        # 그러면 이 검사는 표식이 아니라 오타 처리를 보고 있었던 것이 된다.
        $failures.Add("unmarked-rejected : 거부는 됐는데 사유가 표식이 아니다(code=$($unmarked.code)) — 프로브의 Unmarked() 가 남아 있는지 볼 것")
    }
    if ((Get-Return $unmarked)) {
        $failures.Add("unmarked-rejected : 거부했는데 반환값이 나왔다(return=$((Get-Return $unmarked)))")
    }

    # ── 2c) 사용자 코드의 예외가 에디터를 죽이지 않는다 ─────────────────
    $threw = Invoke-Probe 'Throw'
    "{0,-26} status={1} code={2}" -f 'user-exception-contained', $threw.status, $threw.code
    if ($threw.code -ne 'script.invoke_threw') {
        $failures.Add("user-exception-contained : 사용자 예외가 기대한 결과로 오지 않았다(status=$($threw.status) code=$($threw.code))")
    }

    # ★ 살아 있음을 **다음 호출로** 확인한다. `/health` 만 보면 수신 스레드가
    #   살아 있다는 뜻일 뿐이고, 예외가 관리 계층을 망가뜨렸는지는 알 수 없다.
    $after = Invoke-Probe 'Ping'
    "{0,-26} status={1} return={2}" -f 'alive-after-exception', $after.status, (Get-Return $after)
    if ((Get-Return $after) -ne 'pong') {
        $failures.Add("alive-after-exception : 사용자 예외 뒤에 호출 계층이 망가졌다(status=$($after.status) return=$((Get-Return $after)))")
    }

    # ── 2d) 에디터를 끄지 않고 C# 수정이 반영된다 (§10.2 완료 모습) ─────
    if ($SkipLiveRoundTrip) {
        $failures.Add('live-round-trip : -SkipLiveRoundTrip 로 건너뛰었다 — LC7 의 완료 기준을 확인하지 않았다')
    }
    else {
        if ($null -eq $dotnet) {
            $failures.Add('live-round-trip : dotnet SDK 가 없어 왕복을 태우지 못했다 — 이 실행은 LC7 완료 기준을 확인하지 않았다')
        }
        else {
            # 실행마다 다른 토큰. 지난 실행의 빌드가 남아 있어도 통과하지 않게 한다.
            $token = 'LC7-' + [Guid]::NewGuid().ToString('N').Substring(0, 8)

            $edit = [Diagnostics.Stopwatch]::StartNew()
            $text = [IO.File]::ReadAllText($probeSource)
            $mutated = $text.Replace('public static string Ping() => "pong";',
                                     "public static string Ping() => `"$token`";")
            if ($mutated -eq $text) {
                throw "프로브의 Ping() 정의를 찾지 못했다 — 이 게이트가 무엇을 고칠지 모른다: $probeSource"
            }
            [IO.File]::WriteAllText($probeSource, $mutated)
            $edit.Stop()

            $build = [Diagnostics.Stopwatch]::StartNew()
            $buildLog = Join-Path $Work 'invoke_build.log'
            & $dotnet.Source build $probeProject -c Debug --nologo -v q *> $buildLog
            $buildOk = ($LASTEXITCODE -eq 0)
            $build.Stop()
            if (-not $buildOk) {
                $failures.Add("live-round-trip : GameScripts 빌드가 실패했다 — 로그 $buildLog")
            }

            $reload = [Diagnostics.Stopwatch]::StartNew()
            $reloaded = Send '{"command":"script.reload","mode":"sync","timeoutMs":60000}'
            $reload.Stop()
            if ($reloaded.status -ne 'succeeded') {
                $failures.Add("live-round-trip : 리로드가 실패했다(status=$($reloaded.status) code=$($reloaded.code))")
            }

            $call = [Diagnostics.Stopwatch]::StartNew()
            $fresh = Invoke-Probe 'Ping'
            $call.Stop()

            "{0,-26} status={1} return={2} (기대 {3})" -f `
                'live-round-trip', $fresh.status, (Get-Return $fresh), $token
            if ((Get-Return $fresh) -ne $token) {
                $failures.Add(("live-round-trip : 리로드 뒤에도 옛 코드가 돈다 " +
                    "(return=$((Get-Return $fresh)) 기대=$token) — 에디터를 끄지 않은 코드 교체가 안 된 것이다"))
            }

            $timings['edit_ms']   = [math]::Round($edit.Elapsed.TotalMilliseconds, 1)
            $timings['build_ms']  = [math]::Round($build.Elapsed.TotalMilliseconds, 1)
            $timings['reload_ms'] = [math]::Round($reload.Elapsed.TotalMilliseconds, 1)
            $timings['invoke_ms'] = [math]::Round($call.Elapsed.TotalMilliseconds, 1)
            $timings['total_ms']  = [math]::Round(
                $edit.Elapsed.TotalMilliseconds + $build.Elapsed.TotalMilliseconds +
                $reload.Elapsed.TotalMilliseconds + $call.Elapsed.TotalMilliseconds, 1)

            # ── 2e) 되돌리는 것까지 왕복이다 ────────────────────────────
            #
            # 되돌린 소스로 다시 빌드·리로드해 "pong" 이 돌아오는지 본다. 아래
            # finally 의 되돌리기는 안전망이고, 이 단정이 되돌리기가 **실제로
            # 반영됐는지**를 본다 — 소스만 되돌리고 DLL 을 두면 다음 실행의
            # 기준선이 깨진 채 남는다.
            Restore-ProbeSource
            & $dotnet.Source build $probeProject -c Debug --nologo -v q *> (Join-Path $Work 'invoke_restore_build.log')
            $restoreBuildOk = ($LASTEXITCODE -eq 0)
            $null = Send '{"command":"script.reload","mode":"sync","timeoutMs":60000}'
            $restored = Invoke-Probe 'Ping'
            "{0,-26} status={1} return={2}" -f 'restore-round-trip', $restored.status, (Get-Return $restored)
            if (-not $restoreBuildOk -or (Get-Return $restored) -ne 'pong') {
                $failures.Add(("restore-round-trip : 프로브를 되돌린 빌드가 반영되지 않았다 " +
                    "(build=$restoreBuildOk return=$((Get-Return $restored))) — 다음 실행의 기준선이 깨진 채로 남는다"))
            }
        }
    }
}
finally {
    # 소스를 되돌리고 **해시로 확인한다.** 되돌리기가 실패하면 이후 모든 실행이
    # 이 게이트가 심은 토큰 위에서 돌고, 원인은 여기가 아니라 저 멀리서 보인다.
    try { Restore-ProbeSource } catch { }
    $restoredOk = $false
    try {
        $restoredOk = (Get-FileHash -LiteralPath $probeSource  -Algorithm SHA256).Hash -eq
                      (Get-FileHash -LiteralPath $sourceBackup -Algorithm SHA256).Hash
    } catch { }
    if (-not $restoredOk) {
        Write-Error ("프로브 소스를 되돌리지 못했다: $probeSource — 백업은 $sourceBackup 에 있다. " +
                     "되돌린 뒤 GameScripts 를 다시 빌드할 것.")
    }
    Stop-AllEditors
}

""
if ($timings.Count -gt 0) {
    # ★ 실측을 **출력한다.** §10.2 의 완료 모습이 "그 왕복 시간이 실측으로
    #   기록된다" 이므로, 통과 여부만 내면 완료 기준의 절반이 빈다.
    "왕복 실측 (수정 -> 빌드 -> reload -> invoke):"
    foreach ($k in $timings.Keys) { "  {0,-10} {1,9} ms" -f $k, $timings[$k] }
    ""
}

if ($failures.Count -gt 0) {
    "실패:"
    $failures | ForEach-Object { "  - $_" }
    exit 1
}
"script.invoke 계약 통과 — 표식된 것만 돌고, 에디터를 끄지 않고 C# 변경이 반영된다"
exit 0
