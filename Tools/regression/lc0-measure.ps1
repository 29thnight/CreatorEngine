[CmdletBinding()]
param(
    [string]$Exe  = (Join-Path $PSScriptRoot "..\..\Bin\x64-Debug\Editor\CreatorEditor.exe"),
    [string]$Work = (Join-Path $env:TEMP "lc0"),

    [ValidateRange(10, 5000)]
    [int]$Samples = 200,

    [string]$Scene = 'Assets/Scenes/FT_Primitives.creator',

    [ValidateRange(1000, 600000)]
    [int]$StepTimeoutMs = 60000
)

# LC0 (PHASE 14.5) — 지연 실측 하네스.
#
# ── 이 스크립트가 존재하는 이유 ─────────────────────────────────────────
#
# EditorAutomationCLIPlan.md §7.1은 "p50 50ms / p95 150ms / p99 300ms"를 목표로
# 적었는데 **그 예산의 분모가 없다**. 에디터 한 프레임이 몇 ms인지, 명령 하나를
# 밖에서 던져 결과를 받기까지 오늘 몇 ms가 드는지 아무도 재지 않았다. 실측 없이
# "60fps니까 16ms"라고 적으면 LC5의 SLO 게이트가 지어낸 숫자를 지키게 된다.
#
# ── 왜 --script가 아니라 --console + stdin인가 ──────────────────────────
#
# 처음에는 `--script`로 쟀다. 그 값은 쓸 수 없다. --script는 부팅 시점에 명령을
# **전부 한 번에** 큐에 넣으므로, 두 번째 명령의 "큐 대기 시간"에 에디터 부팅
# 전체가 들어간다(실측 5,575ms). 그것은 왕복 지연이 아니라 부팅 시간이다.
#
# 서비스가 대신하려는 것은 "이미 켜져 있는 에디터에 명령 하나를 던지고 결과를
# 받는 일"이다. 오늘 그 일을 할 수 있는 유일한 경로가 `--console`의 stdin이다.
# 그래서 여기서 재는 것은:
#
#     PowerShell write → 파이프 → stdin reader thread → Enqueue → 프레임 경계
#     → Pump → handler → printf → 파이프 → PowerShell read
#
# 이 값이 **바닥값**이다. LC4의 HTTP 서비스는 여기에 accept·파싱·인증·직렬화를
# 더하고, 드레인 예산(LC5)으로 프레임 경계 대기를 줄인다. 바닥값을 모르면
# 그 둘 중 무엇이 지연을 만드는지 영영 못 가른다.
#
# ── 산출 ────────────────────────────────────────────────────────────────
#
#   Artifacts/Tests/Editor/lc0/lc0_roundtrip.tsv          외부 관측 왕복(이 스크립트)
#   Artifacts/Tests/Editor/lc0/lc0_timing.tsv             내부 관측 프레임·큐(엔진)
#   Artifacts/Tests/Editor/lc0/lc0_command_inventory.tsv  등록 표 덤프(엔진)
#
# 두 관측을 나란히 두는 것이 요점이다. 외부 왕복에서 내부 큐 대기를 빼면 파이프와
# 스레드 전달 비용이 남는다 — 그것이 HTTP가 대체할 구간의 크기다.

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

if (-not (Test-Path -LiteralPath $Exe -PathType Leaf)) {
    "실행 파일이 없다: $Exe"
    exit 1
}
$exeDir   = Split-Path -Parent $Exe
$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
New-Item -ItemType Directory -Force -Path $Work | Out-Null

$artifactDir = Join-Path $repoRoot 'Artifacts\Tests\Editor\lc0'
New-Item -ItemType Directory -Force -Path $artifactDir | Out-Null

# ── 프로세스 기동 ────────────────────────────────────────────────────────
#
# EnsureConsole은 파이프로 리다이렉트된 스트림을 다시 열지 않고 stdout 버퍼링도
# 끈다(ConsoleCommandSystem.cpp의 EnsureConsole 주석). 그래서 파이프로 붙어도
# 출력이 콘솔 창으로 새지 않고, 줄 단위로 즉시 도착한다 — 왕복을 재려면 둘 다 필요하다.
$startInfo = [Diagnostics.ProcessStartInfo]::new()
$startInfo.FileName               = $Exe
$startInfo.WorkingDirectory       = $exeDir
$startInfo.UseShellExecute        = $false
$startInfo.RedirectStandardInput  = $true
$startInfo.RedirectStandardOutput = $true
$startInfo.RedirectStandardError  = $true
$startInfo.StandardOutputEncoding = [Text.Encoding]::UTF8
$startInfo.StandardErrorEncoding  = [Text.Encoding]::UTF8
$null = $startInfo.ArgumentList.Add('--console')

$proc = [Diagnostics.Process]::Start($startInfo)
$transcript = New-Object System.Collections.Generic.List[string]

# stderr는 읽지 않으면 파이프가 차서 프로세스가 멈춘다. 값은 안 보고 비우기만 한다.
$drainStderr = $proc.StandardError.ReadToEndAsync()

function Send-Command([string]$Line) {
    $proc.StandardInput.WriteLine($Line)
    $proc.StandardInput.Flush()
}

# 마커가 담긴 줄이 나올 때까지 stdout을 읽는다.
#
# ReadLine을 그냥 부르면 마커가 영영 안 올 때 하네스가 통째로 멈춘다 — 그
# 상태는 "실패"가 아니라 "판정 불가"라 아무 값도 남기지 못한다. 기한을 둔다.
#
# ★ 기한을 넘긴 읽기를 **버리면 안 된다.** StreamReader는 동시에 하나의 읽기만
#   허용해서, 대기 중인 ReadLineAsync를 두고 새로 부르면 다음 호출이
#   "The stream is currently in use by a previous operation" 로 죽는다. 그래서
#   미완료 task를 스크립트 범위에 남겨 두고 다음 대기에서 이어 기다린다.
$script:pendingRead = $null

function Wait-ForMarker([string]$Marker, [int]$TimeoutMs) {
    $deadline = [DateTime]::UtcNow.AddMilliseconds($TimeoutMs)
    while ([DateTime]::UtcNow -lt $deadline) {
        if ($null -eq $script:pendingRead) {
            $script:pendingRead = $proc.StandardOutput.ReadLineAsync()
        }
        $remaining = [int][Math]::Max(1, ($deadline - [DateTime]::UtcNow).TotalMilliseconds)
        if (-not $script:pendingRead.Wait($remaining)) { continue }

        $line = $script:pendingRead.Result
        $script:pendingRead = $null
        if ($null -eq $line) { return $false }   # 스트림 종료 = 프로세스가 죽었다
        $transcript.Add($line)
        if ($line.Contains($Marker)) { return $true }
    }
    return $false
}

function Stop-Harness([string]$Reason) {
    if (-not $proc.HasExited) {
        try { Send-Command 'quit' } catch { }
        if (-not $proc.WaitForExit(30000)) { $proc.Kill() }
    }
    $transcriptPath = Join-Path $Work 'lc0_measure_transcript.txt'
    ($transcript -join "`n") | Set-Content -LiteralPath $transcriptPath -Encoding UTF8
    if ($Reason) {
        $Reason
        "전사: $transcriptPath"
    }
}

function Get-Percentile([double[]]$Sorted, [double]$Fraction) {
    if ($Sorted.Count -eq 0) { return 0.0 }
    # 최근접 순위법. 보간하면 실제로 관측되지 않은 시간이 예산의 분모가 된다.
    $rank = [Math]::Ceiling($Fraction * $Sorted.Count)
    $index = [Math]::Max(0, [Math]::Min($Sorted.Count - 1, [int]$rank - 1))
    return $Sorted[$index]
}

try {
    # ── 부팅 완료 대기 ──────────────────────────────────────────────────
    #
    # "몇 초 자고 시작"은 기계마다 다르고, 느린 기계에서는 부팅 시간이 첫 표본에
    # 섞인다. 명령 하나를 던져 그 응답이 오는 것으로 부팅 완료를 판정한다.
    Send-Command 'cli.echo.args LC0BOOT'
    if (-not (Wait-ForMarker 'LC0BOOT' $StepTimeoutMs)) {
        Stop-Harness "부팅 응답(LC0BOOT)이 ${StepTimeoutMs}ms 안에 오지 않았다."
        exit 1
    }

    # 부팅 구간의 프레임 표본을 버린다. 첫 프레임은 초기화가 통째로 들어 있어
    # (실측 5.5초) 백분위를 통째로 왜곡한다.
    Send-Command 'cli.probe.timing reset'
    if (-not (Wait-ForMarker '표본 초기화' $StepTimeoutMs)) {
        Stop-Harness '표본 초기화 응답이 오지 않았다.'
        exit 1
    }

    # ── 1) 정지 상태 왕복 ───────────────────────────────────────────────
    $idleSamples = New-Object System.Collections.Generic.List[double]
    for ($i = 0; $i -lt $Samples; $i++) {
        $marker = "LC0RT$i"
        $stopwatch = [Diagnostics.Stopwatch]::StartNew()
        Send-Command "cli.echo.args $marker"
        $ok = Wait-ForMarker $marker $StepTimeoutMs
        $stopwatch.Stop()
        if (-not $ok) {
            Stop-Harness "왕복 표본 $i 가 ${StepTimeoutMs}ms 안에 돌아오지 않았다."
            exit 1
        }
        $idleSamples.Add($stopwatch.Elapsed.TotalMilliseconds)
    }

    # ── 2) 씬 로딩 중 왕복 ──────────────────────────────────────────────
    #
    # 계획 §7.3의 "멈춤은 지연이 아니라 상태다"가 겨냥한 구간이다. 오늘은 상태로
    # 응답할 방법이 없어 호출자가 그냥 기다린다 — 그 기다림이 몇 ms인지 잰다.
    $scenePath = Join-Path $repoRoot ('Dynamic_CPP\' + ($Scene -replace '/', '\'))
    $sceneSamples = New-Object System.Collections.Generic.List[double]
    if (Test-Path -LiteralPath $scenePath -PathType Leaf) {
        Send-Command "scene.load $Scene"
        # 로드 직후 곧바로 왕복을 던진다. 로드가 큐를 막으면 그 시간이 여기 잡힌다.
        for ($i = 0; $i -lt 5; $i++) {
            $marker = "LC0SCENE$i"
            $stopwatch = [Diagnostics.Stopwatch]::StartNew()
            Send-Command "cli.echo.args $marker"
            $ok = Wait-ForMarker $marker $StepTimeoutMs
            $stopwatch.Stop()
            if (-not $ok) { Stop-Harness "씬 로딩 중 왕복 $i 실패"; exit 1 }
            $sceneSamples.Add($stopwatch.Elapsed.TotalMilliseconds)
        }
    }
    else {
        "씬이 없어 씬 로딩 구간은 건너뛴다: $scenePath"
    }

    # ── 3) 재생 중 왕복 ─────────────────────────────────────────────────
    $playSamples = New-Object System.Collections.Generic.List[double]
    Send-Command 'play'
    Start-Sleep -Milliseconds 500
    for ($i = 0; $i -lt 30; $i++) {
        $marker = "LC0PLAY$i"
        $stopwatch = [Diagnostics.Stopwatch]::StartNew()
        Send-Command "cli.echo.args $marker"
        $ok = Wait-ForMarker $marker $StepTimeoutMs
        $stopwatch.Stop()
        if (-not $ok) { Stop-Harness "재생 중 왕복 $i 실패"; exit 1 }
        $playSamples.Add($stopwatch.Elapsed.TotalMilliseconds)
    }
    Send-Command 'stop'
    Start-Sleep -Milliseconds 500

    # ── 4) wait 보류 중 왕복 ────────────────────────────────────────────
    #
    # `wait N`은 큐를 정확히 N프레임 멈춘다. 계획 §7.2가 배치 큐의 이 의미를
    # 보존하겠다고 한 바로 그 동작이고, 서비스 세션에서는 금지될 것이다.
    # 금지의 근거가 되려면 "전역 프레임 보류가 다른 요청에 얼마를 물리는가"를
    # 숫자로 알아야 한다.
    $waitSamples = New-Object System.Collections.Generic.List[double]
    Send-Command 'wait 60'
    for ($i = 0; $i -lt 5; $i++) {
        $marker = "LC0WAIT$i"
        $stopwatch = [Diagnostics.Stopwatch]::StartNew()
        Send-Command "cli.echo.args $marker"
        $ok = Wait-ForMarker $marker $StepTimeoutMs
        $stopwatch.Stop()
        if (-not $ok) { Stop-Harness "wait 보류 중 왕복 $i 실패"; exit 1 }
        $waitSamples.Add($stopwatch.Elapsed.TotalMilliseconds)
    }

    # ── 산출물 ──────────────────────────────────────────────────────────
    Send-Command 'commands.dump lc0_command_inventory.tsv'
    if (-not (Wait-ForMarker 'commands.dump 완료' $StepTimeoutMs)) {
        Stop-Harness 'commands.dump 응답이 오지 않았다.'
        exit 1
    }
    Send-Command 'cli.probe.timing lc0_timing.tsv'
    if (-not (Wait-ForMarker 'cli.probe.timing 완료' $StepTimeoutMs)) {
        Stop-Harness 'cli.probe.timing 응답이 오지 않았다.'
        exit 1
    }
}
finally {
    Stop-Harness $null
    $null = $drainStderr
}

# ── 왕복 TSV ────────────────────────────────────────────────────────────
$roundtripPath = Join-Path $artifactDir 'lc0_roundtrip.tsv'
$lines = New-Object System.Collections.Generic.List[string]
$lines.Add('# lc0-roundtrip v1')
$lines.Add("# generated`t" + [DateTime]::UtcNow.ToString('yyyy-MM-ddTHH:mm:ssZ'))
$lines.Add("# transport`tstdin-pipe")
$lines.Add("# note`t외부 프로세스에서 관측한 end-to-end 왕복. LC4의 HTTP가 대체할 구간의 오늘 값.")
$lines.Add("bucket`tsamples`tmin_ms`tp50_ms`tp95_ms`tp99_ms`tmax_ms`tmean_ms")

$buckets = [ordered]@{
    'idle'          = $idleSamples
    'scene-loading' = $sceneSamples
    'playing'       = $playSamples
    'wait-held'     = $waitSamples
}
$summary = New-Object System.Collections.Generic.List[object]
foreach ($name in $buckets.Keys) {
    $values = [double[]]@($buckets[$name])
    if ($values.Count -eq 0) {
        $lines.Add(("{0}`t0`t0`t0`t0`t0`t0`t0" -f $name))
        continue
    }
    $sorted = [double[]]($values | Sort-Object)
    $row = [pscustomobject]@{
        Bucket  = $name
        Samples = $sorted.Count
        Min     = [Math]::Round($sorted[0], 3)
        P50     = [Math]::Round((Get-Percentile $sorted 0.50), 3)
        P95     = [Math]::Round((Get-Percentile $sorted 0.95), 3)
        P99     = [Math]::Round((Get-Percentile $sorted 0.99), 3)
        Max     = [Math]::Round($sorted[$sorted.Count - 1], 3)
        Mean    = [Math]::Round((($sorted | Measure-Object -Average).Average), 3)
    }
    $summary.Add($row)
    $lines.Add(("{0}`t{1}`t{2}`t{3}`t{4}`t{5}`t{6}`t{7}" -f
        $row.Bucket, $row.Samples, $row.Min, $row.P50, $row.P95, $row.P99, $row.Max, $row.Mean))
}

$lines.Add('')
$lines.Add("kind`tbucket`tsequence`troundtrip_ms")
foreach ($name in $buckets.Keys) {
    $values = @($buckets[$name])
    for ($i = 0; $i -lt $values.Count; $i++) {
        $lines.Add(("roundtrip-sample`t{0}`t{1}`t{2:F3}" -f $name, $i, $values[$i]))
    }
}
($lines -join "`n") + "`n" | Set-Content -LiteralPath $roundtripPath -Encoding UTF8 -NoNewline

$summary | Format-Table -AutoSize | Out-String | Write-Host
"왕복: $roundtripPath"
"프레임·큐: " + (Join-Path $artifactDir 'lc0_timing.tsv')
"등록 표: " + (Join-Path $artifactDir 'lc0_command_inventory.tsv')
exit 0
