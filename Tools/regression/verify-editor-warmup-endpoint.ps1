# 예열 창구 (PHASE 21 W2-V0 후속)
#
# 왜 필요한가
# ───────────
# 에디터는 창이 뜬 뒤에도 한참 쓸 수 없다. 2026-09-14 실측으로 창은 1.7s 에 뜨고
# 씬뷰 첫 그림은 18.5s 다. 그 구간에 대해 답할 수 있던 것은 "끝났는가" 뿐이었고
# (`render.live.wait`), 어디까지 갔는지는 임시 계측을 붙여야만 보였다.
#
# 진행이 **화면에만** 있었다는 것도 같은 문제다. `BootProgress` 는 로딩창에 퍼센트를
# 그리지만 밖에서 읽을 수단이 없고, 창을 보인 뒤(`Complete()`)로는 아무것도 추적하지
# 않는다 — 정작 긴 구간이 그 뒤에 있다.
#
# `GET /warmup` 이 그 자리를 채운다. 이 게이트가 지키는 것은 그 창구의 계약이다.
#
# ── 판정 항목 ──
#
#   1  창구가 답한다        — 200 · schemaVersion 1 · 단계 여덟
#   2  ★ 목록에 미도달까지 있다 — 도달한 것만 실으면 "무엇이 남았는가" 에 못 답한다
#   3  ★ 예열 **도중에** 답한다 — 완료 전 표본이 있어야 한다. 끝난 뒤에만 답하는
#      창구는 이 작업이 없애려던 상태 그대로다
#   4  ★ 게임 스레드가 막힌 동안에도 답한다 — 예열 구간의 응답이 빠르다.
#      큐를 타면 이 수가 초 단위로 튄다(파이프라인 구축이 표시 락을 쥐는 구간)
#   5  단조 — 연달아 조회한 도달 수가 줄지 않는다
#   6  순서 — 도달한 단계의 시각이 예열 순서를 거스르지 않는다
#   7  완주 — 여덟 단계에 모두 도달하고 complete 가 참이 된다
#   8  엔진이 정상 종료했다
param(
    [string]$Exe = (Join-Path $PSScriptRoot "..\..\Bin\x64-Release\Editor\CreatorEditor.exe"),
    [string]$Work = "",
    # 예열이 끝나기를 기다리는 상한. 이 기계의 Release 실측이 18.8s 다.
    [int]$TimeoutSeconds = 180
)

$ErrorActionPreference = "Stop"

if (-not (Test-Path -LiteralPath $Exe)) { throw "Editor executable is missing: $Exe" }
if ([string]::IsNullOrWhiteSpace($Work)) {
    $Work = Join-Path ([IO.Path]::GetTempPath()) ("CE_Warmup_" + [guid]::NewGuid().ToString("N"))
}
New-Item -ItemType Directory -Force -Path $Work | Out-Null

$repoRoot = Resolve-Path (Join-Path $PSScriptRoot "..\..")
$endpointPath = Join-Path $repoRoot "Dynamic_CPP\Library\CommandService\endpoint.json"

$savedWorkspaceDir = $env:CREATOR_EDITOR_WORKSPACE_DIR
$savedLegacyIni = $env:CREATOR_EDITOR_LEGACY_INI
$env:CREATOR_EDITOR_WORKSPACE_DIR = $Work
$env:CREATOR_EDITOR_LEGACY_INI = Join-Path $Work "legacy.ini"

$failures = New-Object System.Collections.Generic.List[string]
$checks = 0
function Assert([bool]$ok, [string]$what) {
    $script:checks++
    if (-not $ok) { $script:failures.Add($what) }
}

# 예열이 끝나기 전에 엔진이 내려가면 3·7 을 잴 수 없다. 넉넉히 돌린 뒤 quit 한다.
$scriptPath = Join-Path $Work "warmup.txt"
Set-Content -LiteralPath $scriptPath -Encoding UTF8 -Value @('wait 600', 'editor.viewport scene', 'wait 120000', 'quit')

if (Test-Path -LiteralPath $endpointPath) { Remove-Item -LiteralPath $endpointPath -Force }

$kExpectedStages = @(
    'process', 'window_shown', 'first_ui_frame', 'render_pipeline',
    'shader_meta', 'first_live_frame', 'display_texture', 'scene_canvas')

$samples = New-Object System.Collections.Generic.List[object]
$proc = $null
$exitCode = -1
try {
    $proc = Start-Process -FilePath $Exe -WorkingDirectory (Split-Path $Exe) -PassThru `
        -ArgumentList @('--command-service', '--script', ('"' + $scriptPath + '"'))

    $deadline = [datetime]::UtcNow.AddSeconds($TimeoutSeconds)
    while (-not (Test-Path -LiteralPath $endpointPath) -and [datetime]::UtcNow -lt $deadline -and -not $proc.HasExited) {
        Start-Sleep -Milliseconds 150
    }
    if (-not (Test-Path -LiteralPath $endpointPath)) {
        throw "endpoint.json 이 나타나지 않았다 — 서비스가 열리지 않았다 (종료=$($proc.HasExited))"
    }
    $info = Get-Content -LiteralPath $endpointPath -Raw | ConvertFrom-Json
    $base = "http://$($info.host):$($info.port)"
    $headers = @{ Authorization = "Bearer $($info.token)" }

    # 완료까지 폴링하며 표본을 모은다. 응답 지연도 함께 잰다 — 4번이 그 수를 본다.
    while (-not $proc.HasExited -and [datetime]::UtcNow -lt $deadline) {
        $sw = [Diagnostics.Stopwatch]::StartNew()
        try { $w = Invoke-RestMethod -Uri "$base/warmup" -Headers $headers -TimeoutSec 10 }
        catch { Start-Sleep -Milliseconds 300; continue }
        $sw.Stop()
        $samples.Add([pscustomobject]@{
            reached     = [int]$w.reached
            total       = [int]$w.total
            complete    = [bool]$w.complete
            last        = [string]$w.last
            elapsedMs   = [double]$w.elapsedMs
            sinceLastMs = [double]$w.sinceLastMs
            stages      = $w.stages
            replyMs     = $sw.Elapsed.TotalMilliseconds
            schema      = [int]$w.schemaVersion
        })
        if ($w.complete) { break }
        Start-Sleep -Milliseconds 250
    }
}
finally {
    if ($null -ne $proc -and -not $proc.HasExited) {
        # 스크립트의 긴 wait 를 끝까지 기다리지 않는다 — 판정에 필요한 것은
        # 예열 완주까지이고, 그 뒤는 이 게이트의 관심이 아니다.
        $proc.Kill() | Out-Null
        $proc.WaitForExit(15000) | Out-Null
        $exitCode = 0
    }
    elseif ($null -ne $proc) { $exitCode = $proc.ExitCode }
    if ($null -ne $savedWorkspaceDir) { $env:CREATOR_EDITOR_WORKSPACE_DIR = $savedWorkspaceDir }
    else { Remove-Item Env:CREATOR_EDITOR_WORKSPACE_DIR -ErrorAction SilentlyContinue }
    if ($null -ne $savedLegacyIni) { $env:CREATOR_EDITOR_LEGACY_INI = $savedLegacyIni }
    else { Remove-Item Env:CREATOR_EDITOR_LEGACY_INI -ErrorAction SilentlyContinue }
}

Assert ($samples.Count -ge 3) "표본이 3개 미만이다 ($($samples.Count)) — 창구가 답하지 않았다"
if ($samples.Count -eq 0) {
    Write-Host "예열 창구 검사: 표본 0 · FAIL"
    throw "예열 창구가 한 번도 답하지 않았다"
}

$first = $samples[0]
$last = $samples[$samples.Count - 1]

# 1 · 2 — 창구가 답하고, 목록이 미도달까지 싣는다.
Assert ($first.schema -eq 1) "schemaVersion 이 1 이 아니다 ($($first.schema))"
foreach ($s in $samples) {
    Assert ($s.total -eq $kExpectedStages.Count) "단계 수가 $($kExpectedStages.Count) 이 아니다 ($($s.total))"
    Assert ($s.stages.Count -eq $kExpectedStages.Count) "실린 단계가 $($kExpectedStages.Count) 개가 아니다 ($($s.stages.Count))"
}
$names = @($first.stages | ForEach-Object { $_.name })
for ($i = 0; $i -lt $kExpectedStages.Count; $i++) {
    Assert ($names[$i] -eq $kExpectedStages[$i]) "단계 $i 의 이름이 '$($kExpectedStages[$i])' 가 아니다 ('$($names[$i])')"
}
# 도달하지 않은 단계가 목록에 남아 있던 표본이 있어야 한다. 이것이 없으면
# "도달한 것만 싣는" 구현과 구별되지 않는다.
$withPending = @($samples | Where-Object { $_.reached -lt $_.total })
Assert ($withPending.Count -ge 1) "미도달 단계가 목록에 있던 표본이 없다 — '무엇이 남았는가' 를 답할 수 없다"

# 3 — 예열 도중에 답했다.
$midFlight = @($samples | Where-Object { -not $_.complete -and $_.reached -lt $_.total })
Assert ($midFlight.Count -ge 1) "예열 도중 표본이 없다 — 끝난 뒤에만 답하는 창구와 구별되지 않는다"

# 4 — 막힌 동안에도 빠르게 답한다. 큐를 타면 파이프라인 구축 구간에서 초 단위로 튄다.
$worstMid = 0.0
foreach ($s in $midFlight) { if ($s.replyMs -gt $worstMid) { $worstMid = $s.replyMs } }
Assert ($worstMid -lt 2000.0) "예열 도중 응답이 $([math]::Round($worstMid,0)) ms 다 — 창구가 게임 스레드를 기다리고 있다"

# 5 — 도달 수가 줄지 않는다.
for ($i = 1; $i -lt $samples.Count; $i++) {
    Assert ($samples[$i].reached -ge $samples[$i - 1].reached) `
        "도달 수가 줄었다: $($samples[$i-1].reached) → $($samples[$i].reached)"
}

# 5b — ★ 한 번 도달한 단계의 시각은 **변하지 않는다**. 예열은 한 번 지나가는
# 길이고, 뒤의 호출은 재입장이거나 리사이즈 같은 다른 사건이다. 매번 덮어쓰면
# 이 장부는 "언제 처음 됐는가" 가 아니라 "마지막으로 그 코드를 지난 때" 가 된다.
$firstSeen = @{}
foreach ($s in $samples) {
    foreach ($st in $s.stages) {
        if (-not $st.reached) { continue }
        if ($firstSeen.ContainsKey($st.name)) {
            Assert ($firstSeen[$st.name] -eq [double]$st.atMs) `
                "'$($st.name)' 의 시각이 $($firstSeen[$st.name]) → $($st.atMs) ms 로 바뀌었다 — 첫 도달만 남아야 한다"
        }
        else { $firstSeen[$st.name] = [double]$st.atMs }
    }
}

# 5c — `last` 는 **시각이 가장 늦은** 도달 단계여야 한다. 번호가 가장 큰 것을
# 고르면 단계를 건너뛴 실행에서 "지금 무엇을 기다리는가" 가 거꾸로 나온다.
foreach ($s in $samples) {
    $reachedStages = @($s.stages | Where-Object { $_.reached })
    if ($reachedStages.Count -eq 0) { continue }
    # 동률을 허용한다. 같은 프레임에 찍히는 단계가 실제로 있다 —
    # `display_texture` 와 `scene_canvas` 는 씬뷰 본문의 같은 자리에서 연달아
    # 선다. 그때는 어느 쪽을 골라도 "가장 늦게 도달한 것" 이 맞고, 하나만
    # 정답으로 박으면 구현과 검사가 정렬 순서를 놓고 다투게 된다.
    $latestMs = ($reachedStages | Measure-Object -Property atMs -Maximum).Maximum
    $latestNames = @($reachedStages | Where-Object { [double]$_.atMs -eq [double]$latestMs } | ForEach-Object { $_.name })
    Assert ($latestNames -contains $s.last) `
        "last 가 '$($s.last)' 인데 가장 늦게($latestMs ms) 도달한 것은 '$($latestNames -join ", ")' 다"
}

# 6 — 순서. 도달한 단계끼리 시각이 예열 순서를 거스르지 않는다.
$times = @{}
foreach ($s in $last.stages) { if ($s.reached) { $times[$s.name] = [double]$s.atMs } }
for ($i = 1; $i -lt $kExpectedStages.Count; $i++) {
    $prev = $kExpectedStages[$i - 1]; $cur = $kExpectedStages[$i]
    if ($times.ContainsKey($prev) -and $times.ContainsKey($cur)) {
        Assert ($times[$cur] -ge $times[$prev]) `
            "순서가 뒤집혔다: $cur $($times[$cur]) ms 가 $prev $($times[$prev]) ms 보다 이르다"
    }
}
# `process` 는 기준이므로 늘 0 이다.
Assert ($times.ContainsKey('process') -and $times['process'] -eq 0.0) "process 가 0 ms 가 아니다"

# 7 — 완주.
Assert ($last.complete) "예열이 완주하지 않았다 — 마지막 표본 도달 $($last.reached)/$($last.total), 마지막 단계 '$($last.last)'"
Assert ($last.reached -eq $kExpectedStages.Count) "도달 수가 $($kExpectedStages.Count) 이 아니다 ($($last.reached))"
foreach ($name in $kExpectedStages) {
    Assert ($times.ContainsKey($name)) "'$name' 에 도달하지 못했다 — 그 자리를 지나지 않는 구성이거나 지점이 잘못 놓였다"
}

# 8
Assert ($exitCode -eq 0) "엔진이 비정상 종료했다 (exit=$exitCode)"

Write-Host ""
Write-Host "  예열 경과 (표본 $($samples.Count) · 도중 표본 $($midFlight.Count) · 최악 응답 $([math]::Round($worstMid,0)) ms)"
foreach ($s in $last.stages) {
    $mark = if ($s.reached) { "도달" } else { "미도달" }
    Write-Host ("    {0,-18} {1,-6} {2,10:N0} ms" -f $s.name, $mark, $s.atMs)
}
Write-Host ""

if ($failures.Count -gt 0) {
    foreach ($f in $failures) { Write-Host "FAIL $f" }
    throw "예열 창구 검사 실패 $($failures.Count) 건 / 단정 $checks 건"
}
Write-Host "예열 창구 검사: 단정 $checks 건 · 단계 $($kExpectedStages.Count) · PASS"
