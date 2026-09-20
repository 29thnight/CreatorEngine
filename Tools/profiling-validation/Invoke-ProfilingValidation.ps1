#Requires -Version 7.0
<#
.SYNOPSIS
    프레임 프로파일러 수집 코어 검증 진입점 (PHASE 14).

.DESCRIPTION
    에디터를 무인으로 기동해 CPU 프로파일러 특성화 검사를 돌리고 판정한다.
    판정 근거는 화면이 아니라 두 가지다 — 종료 코드와 로그의 고정 마커.

    P0에서 이 스크립트가 재는 것은 "프로파일러가 좋은가"가 아니라
    **지금 무엇이 참인가**다. P2에서 수집 코어를 갈아끼울 때 이 검사가
    계속 통과해야 하고, KNOWN-DEFECT로 남은 항목이 PASS로 바뀌어야 한다.

    PowerShell 5.1로 돌리지 말 것. 이 저장소의 스크립트와 엔진 로그는 UTF-8이고
    5.1은 이를 시스템 코드페이지로 읽어 한글이 깨진 채 정규식 판정이 어긋난다.

.PARAMETER Action
    Stats     라이브 기준선(기본, 교란 없음)
              코어 계약 검사는 Tools/regression/verify-profile-core.ps1 이 맡는다
    Build     Debug|x64 빌드만 수행

.EXAMPLE
    pwsh -NoProfile -File .\Tools\profiling-validation\Invoke-ProfilingValidation.ps1
    pwsh -NoProfile -File .\Tools\profiling-validation\Invoke-ProfilingValidation.ps1 -Action Stats
#>
[CmdletBinding()]
param(
    [ValidateSet("Stats", "Workers", "Build")]
    [string]$Action = "Stats",

    [string]$Exe,

    [string]$OutputRoot,

    [ValidateRange(10, 3600)]
    [int]$TimeoutSec = 300,

    # 검사 전에 엔진이 돌아야 하는 프레임 수. 프로파일러 히스토리(5프레임)가
    # 차기 전에 부르면 검사가 스스로 거부한다.
    [ValidateRange(1, 100000)]
    [int]$WarmupFrames = 60
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$repoRoot = Resolve-Path (Join-Path $PSScriptRoot "..\..")

if (-not $Exe) {
    $Exe = Join-Path $repoRoot "Bin\x64-Debug\Editor\CreatorEditor.exe"
}
if (-not $OutputRoot) {
    $OutputRoot = Join-Path ([IO.Path]::GetTempPath()) "creator-profiling-validation"
}
$null = New-Item -ItemType Directory -Path $OutputRoot -Force

function Find-MSBuild {
    $vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
    if (Test-Path $vswhere) {
        $found = & $vswhere -latest -prerelease -products * `
            -requires Microsoft.Component.MSBuild `
            -find "MSBuild\**\Bin\MSBuild.exe" | Select-Object -First 1
        if ($found) { return $found }
    }
    $fallback = "$env:ProgramFiles\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe"
    if (Test-Path $fallback) { return $fallback }
    throw "MSBuild를 찾지 못했다. vswhere도 폴백 경로도 실패했다."
}

function Invoke-Build {
    $msbuild = Find-MSBuild
    Write-Host "[build] $msbuild"
    & $msbuild (Join-Path $repoRoot "CreatorEngine.sln") `
        /p:Configuration=Debug /p:Platform=x64 /m /v:minimal /nologo /clp:Summary
    if ($LASTEXITCODE -ne 0) {
        throw "빌드 실패 (exit $LASTEXITCODE)"
    }
}

# 엔진을 --commandlet-script 로 무인 기동하고 stdout/stderr 를 합쳐 돌려준다.
function Invoke-EngineScript {
    param([string[]]$Commands, [string]$Label)

    if (-not (Test-Path $Exe)) {
        throw "실행 파일이 없다: $Exe  (-Action Build 로 먼저 빌드할 것)"
    }

    $commandFile = Join-Path $OutputRoot "$Label.txt"
    $outFile = Join-Path $OutputRoot "$Label.out"
    $errFile = Join-Path $OutputRoot "$Label.err"
    Set-Content -Path $commandFile -Value $Commands -Encoding UTF8

    $exeDir = Split-Path -Parent $Exe
    Write-Host "[run] $Label — $Exe --commandlet-script $commandFile"

    $proc = Start-Process -FilePath $Exe -ArgumentList "--commandlet-script", $commandFile `
        -WorkingDirectory $exeDir `
        -RedirectStandardOutput $outFile `
        -RedirectStandardError $errFile `
        -PassThru

    if (-not $proc.WaitForExit($TimeoutSec * 1000)) {
        try { $proc.Kill() } catch { }
        throw "$Label 이(가) ${TimeoutSec}초 안에 끝나지 않았다."
    }

    $stdout = if (Test-Path $outFile) { Get-Content $outFile -Raw -Encoding UTF8 } else { "" }
    $stderr = if (Test-Path $errFile) { Get-Content $errFile -Raw -Encoding UTF8 } else { "" }

    return [pscustomobject]@{
        ExitCode = $proc.ExitCode
        Combined = "$stdout`n$stderr"
        OutFile  = $outFile
    }
}

# Invoke-SelfTest 는 은퇴했다(PHASE 14 P1+P2). 그것은 `profile.selftest` 명령을
# 불렀고, 그 명령은 옛 코어의 특성화 검사였다 — 전역 싱글톤 하나를 공유하는
# 구조라 검사 전용 인스턴스를 세울 수 없어 **라이브 캡처의 프레임 경계를 직접
# 넘겨야** 했고, 그래서 이 검사 직후에 stats 를 재면 예산 100% 포화로 보였다.
#
# 새 코어는 서비스를 인스턴스로 세울 수 있어 엔진을 띄우지 않고 검사한다:
#   pwsh Tools/regression/verify-profile-core.ps1
# 코어만 cl 로 링크해 Debug·Release 각각 초 단위로 돌고, 변이 셋으로 이빨까지
# 증명한다. 이 파일에 남은 축은 **라이브 기준선**(-Action Stats) 하나다.

# 워커 스레드의 구간 계측을 잰다.
#
# Stats 축과 나누는 이유는 **자극이 다르기** 때문이다. Stats 는 기본 씬의 교란
# 없는 기준선을 재고, 이 축은 씬을 갈아 끼워 애니메이션 잡을 돌린다. 한 축에
# 섞으면 기준선 숫자가 fixture 에 딸려 움직인다.
#
# ★ 이 축이 서기 전에는 워커 계측이 통째로 죽어도 아무 게이트도 붉지 않았다.
#   자극이 없으면 등록된 워커의 칸이 빈 채로 초록이기 때문이다.
function Invoke-Workers {
    $fixture = Join-Path $repoRoot "Tools\regression\fixtures\profiling-workers\ProfilingWorkerFixture.creator"

    # fixture 가 없으면 **통과시키지 않는다.** 추적 밖 fixture 를 가진 게이트는
    # 이 기계에서만 돌고 clean checkout 에서는 조용히 빈다.
    if (-not (Test-Path $fixture)) {
        Write-Host ""
        Write-Host "  실패           fixture 가 없다: $fixture" -ForegroundColor Red
        Write-Host "                 (.gitignore 의 profiling-workers 예외를 확인할 것)"
        return 1
    }

    $scenePath = ($fixture -replace '\\', '/')
    $result = Invoke-EngineScript -Label "profile-workers" -Commands @(
        "scene.switch $scenePath"
        "wait $WarmupFrames"
        "profile.frame"
        "profile.stats"
        "quit"
    )

    $body = $result.Combined -split "`n"
    $statsLine = $body | Where-Object { $_ -match '"command"\s*:\s*"profile\.stats"' } | Select-Object -First 1
    $frameLine = $body | Where-Object { $_ -match '"command"\s*:\s*"profile\.frame"' } | Select-Object -First 1
    if (-not $statsLine -or -not $frameLine) {
        Write-Host "profile.stats/profile.frame 응답을 찾지 못했다. 전체 출력: $($result.OutFile)" -ForegroundColor Red
        return 1
    }

    try {
        $stats = $statsLine.Trim() | ConvertFrom-Json
        $frame = $frameLine.Trim() | ConvertFrom-Json
    }
    catch {
        Write-Host "응답을 JSON 으로 읽지 못했다: $_" -ForegroundColor Red
        return 1
    }

    $d = $stats.data
    $workers = @($d.threads | Where-Object { $_.name -like '`[Worker *' })
    $busy = @($workers | Where-Object { $_.capturedEvents -gt 0 })

    # 최근 프레임에서 워커 스레드에 붙은 AnimationJob 을 센다. 건수만 보면
    # "워커가 뭔가를 찍었다" 까지이고, 이름까지 봐야 **그 잡이** 잡혔다가 된다.
    $jobEvents = 0
    foreach ($f in $frame.data.frames) {
        foreach ($t in $f.threads) {
            if ($t.name -notlike '`[Worker *') { continue }
            foreach ($e in $t.events) {
                if ($e.name -eq 'AnimationJob') { $jobEvents++ }
            }
        }
    }

    Write-Host ""
    Write-Host "[profile.workers] 애니메이션 잡 자극 (fixture 씬)"
    Write-Host ("  fixture         {0}" -f (Split-Path $fixture -Leaf))
    Write-Host ("  등록 워커       {0}개 / 이벤트를 찍은 워커 {1}개" -f $workers.Count, $busy.Count)
    foreach ($t in ($workers | Sort-Object -Property name)) {
        Write-Host ("    {0,-24} 이벤트 {1}" -f $t.name, $t.capturedEvents)
    }
    Write-Host ("  최근 {0}프레임의 AnimationJob  {1}건" -f $frame.data.frames.Count, $jobEvents)

    # ── 단정 ────────────────────────────────────────────────────────────────
    #
    # ⚠ 워커 **전부**가 찍기를 요구하지 않는다. 잡을 어느 워커가 집는지는
    #   스케줄러 사정이고, 부하에 따라 한둘은 비는 것이 정상이다. 계측의 생사와
    #   그날의 분배를 가르려면 "여럿이 찍었다" 까지가 맞다.
    $failures = New-Object System.Collections.Generic.List[string]
    if ($stats.status -ne 'succeeded') { $failures.Add("status=$($stats.status)") }
    if (-not $d.captureFrozen)         { $failures.Add("얼린 캡처가 없다") }
    if ($d.malformedScopes -ne 0)      { $failures.Add("불균형 스코프 $($d.malformedScopes)") }
    if ($workers.Count -le 0)          { $failures.Add("워커가 하나도 등록되지 않았다 - 수명 훅이 끊겼다") }
    if ($busy.Count -lt 4)             { $failures.Add("이벤트를 찍은 워커가 $($busy.Count)개뿐이다 - 워커 계측이 끊겼다") }
    if ($jobEvents -le 0)              { $failures.Add("워커 스레드에 AnimationJob 이 하나도 없다") }
    if ($result.ExitCode -ne 0)        { $failures.Add("종료 코드 $($result.ExitCode)") }

    Write-Host ""
    Write-Host "── 판정 ─────────────────────────────"
    if ($failures.Count -eq 0) {
        Write-Host "  결과           통과" -ForegroundColor Green
        return 0
    }
    foreach ($f in $failures) { Write-Host "  실패           $f" -ForegroundColor Red }
    Write-Host ("  전체 출력      {0}" -f $result.OutFile)
    Write-Host "  결과           실패" -ForegroundColor Red
    return 1
}

function Invoke-Stats {
    # profile.frame 을 먼저 부르는 이유는 그 명령이 캡처를 **얼리기** 때문이다.
    # 얼린 캡처가 있어야 profile.stats 가 스레드마다 몇 건을 찍었는지 셀 수 있고,
    # 그 계수라야 "등록만 되고 아무것도 안 찍는 스레드" 를 잡는다. 재는 값 자체는
    # 이미 다 쌓인 뒤라 달라지지 않는다.
    $result = Invoke-EngineScript -Label "profile-stats" -Commands @(
        "wait $WarmupFrames"
        "profile.frame"
        "profile.stats"
        "quit"
    )

    # ★ 9-06(521fa21a)에 이 축이 죽었던 이유를 여기 적어 둔다.
    #
    #   `profile.stats` 가 reg.Legacy 에서 reg.Result 로 옮겨가며 사람이 읽는
    #   "[profile.stats]" 텍스트 블록을 잃었는데, 이 함수는 그 리터럴을 IndexOf 로
    #   찾고 있었다. **명령은 계속 성공하는데 판정기만 붉었다.** 8일·115커밋 동안
    #   아무도 몰랐던 이유는 이 검사가 run-all 세트 밖에 있었기 때문이다.
    #
    #   그래서 텍스트를 되살리지 않는다 — 제품 CLI 의 정본은 JSON(reg.Result)이고
    #   이 저장소에는 ConvertFrom-Json 으로 판정하는 게이트 선례가 여럿 있다.
    $body = $result.Combined
    $line = $body -split "`n" | Where-Object { $_ -match '"command"\s*:\s*"profile\.stats"' } | Select-Object -First 1
    if (-not $line) {
        Write-Host "profile.stats 응답을 찾지 못했다. 전체 출력: $($result.OutFile)" -ForegroundColor Red
        return 1
    }

    try { $json = $line.Trim() | ConvertFrom-Json }
    catch {
        Write-Host "profile.stats 응답을 JSON 으로 읽지 못했다: $_" -ForegroundColor Red
        return 1
    }

    $d = $json.data

    Write-Host ""
    Write-Host "[profile.stats] 라이브 기준선 (측정 뒤 캡처를 얼려 읽는다)"
    Write-Host ("  상태            {0} / 보존 프레임 {1}개 (최신 {2})" -f $d.state, $d.retainedFrames, $d.frameEnd)
    Write-Host ("  이벤트/프레임   마지막 {0} / 최대 {1}" -f $d.lastFrameEvents, $d.peakFrameEvents)
    Write-Host ("  등록 마커       {0}" -f $d.registeredMarkers)
    Write-Host ("  청크 풀         여유 {0} / {1}" -f $d.freeChunks, $d.chunkCount)
    Write-Host ("  캡처 메모리     {0:N2} MiB / {1:N0} MiB" -f ($d.memoryBytes / 1MB), ($d.memoryBudget / 1MB))
    Write-Host ("  누적 누락       이벤트 {0}" -f $d.totalDroppedEvents)
    Write-Host ("  불균형 스코프   {0}" -f $d.malformedScopes)
    Write-Host ("  스레드          {0}개" -f $d.threadCount)
    foreach ($t in $d.threads) {
        Write-Host ("    [{0}] {1,-24} tid={2,-7} 이벤트 {3}" -f
            $t.index, $t.name, $t.threadId, $t.capturedEvents)
    }

    # ── 단정 ────────────────────────────────────────────────────────────────
    #
    # ⚠ 드롭·포화를 단정하지 않는다. 계측 지점이 전부 시스템 단위 고정 지점이라
    #   씬에 무엇을 얹어도 이벤트 수가 상수이므로, 그 단정은 **어떤 변이로도
    #   자극되지 않는 빈 단정**이다. 값은 위에 찍되 판정에서는 뺀다.
    #
    # ⚠ 이벤트 수의 **절대값**은 단정하지 않는다(2026-09-20 실측). 이 축은 남의
    #   임시 계측이 드나들 때마다 움직인다 — 9-15 의 27 이 임시 계측 12곳 때문에
    #   38 이 됐고, 걷으니 다시 27 로 돌아왔다. 절대 숫자를 박으면 멀쩡한 변경이
    #   붉어지고, 우연히 수가 맞으면 마커가 끊긴 변경이 통과한다.
    #
    # ★ 이름 예산 단정은 **대상이 사라져서** 뺐다. 새 코어는 마커 id 만 흘리므로
    #   프레임마다 도는 이름 복사가 없고, 예산도 누락 계수도 개념 자체가 없다.
    $failures = New-Object System.Collections.Generic.List[string]
    if ($json.status -ne 'succeeded') { $failures.Add("status=$($json.status)") }
    if ($d.retainedFrames -le 0)      { $failures.Add("보존 프레임이 0이다 - 수집이 돌지 않았다") }
    if ($d.lastFrameEvents -le 0)     { $failures.Add("이벤트가 0이다 - 계측이 통째로 죽었다") }
    if ($d.registeredMarkers -le 0)   { $failures.Add("등록된 마커가 0이다 - 마커 등록이 끊겼다") }
    if ($d.malformedScopes -ne 0)     { $failures.Add("불균형 스코프 $($d.malformedScopes) - 스코프 짝이 깨졌다") }
    if ($d.freeChunks -le 0)          { $failures.Add("청크 풀이 고갈됐다 - 수집이 막히고 있다") }
    if (-not ($d.threads | Where-Object { $_.name -eq '[GameThread]' })) {
        $failures.Add("[GameThread] 가 캡처에 없다")
    }

    # ★ 등록 표는 프레임 표가 아니다. 스레드가 목록에 있다는 것과 그 스레드가
    #   무언가를 찍었다는 것은 다른 말이고, 실제로 워커 8 개와
    #   PresentationThread 가 "등록은 됐는데 이벤트 0" 인 채로 있던 적이 있다.
    #   그래서 **찍은 건수**를 단정한다.
    #
    # ⚠ 이벤트 건수를 단정하는 스레드는 **이 워밍업 안에서 반드시 도는 것**으로
    #   좁힌다. 그러지 않으면 단정이 재는 것은 계측의 생사가 아니라 그날의 부하다.
    #
    #   - 워커: 애니메이션 잡을 자극하려면 애니메이터가 있는 씬이 필요한데
    #     Dynamic_CPP/Assets/Scenes 는 통째로 .gitignore 라 게이트가 쓸 fixture 가
    #     없다. 씬을 전환해 재면 워커마다 20~40 건이 잡힌다(9-20 실측).
    #   - RenderThread: Debug 는 라이브 발행 246 중 **4 개만 소비한다**(나머지는
    #     latest-wins 로 접힌다). 245 프레임을 돌려도 캡처에 남는 것은 1 건이고
    #     64 프레임 워밍업에서는 0 이다 — 여기서 건수를 단정하면 계측이 멀쩡해도
    #     붉어진다. 등록 여부까지만 본다.
    #
    #   즉 **이 게이트는 워커·RenderThread 의 구간 계측에 눈멀어 있다.**
    #   계획서 §0.5.8 에 그렇게 적어 두었다.
    if (-not $d.captureFrozen) {
        $failures.Add("얼린 캡처가 없다 - profile.frame 이 캡처를 공개하지 못했다")
    }
    foreach ($name in @('[GameThread]', '[PresentationThread]')) {
        $thread = $d.threads | Where-Object { $_.name -eq $name }
        if (-not $thread) {
            $failures.Add("$name 가 스레드 목록에 없다")
        } elseif ($thread.capturedEvents -le 0) {
            $failures.Add("$name 가 등록만 되고 이벤트를 하나도 안 찍었다")
        }
    }
    foreach ($name in @('[RenderThread]', '[Worker 1]')) {
        if (-not ($d.threads | Where-Object { $_.name -eq $name })) {
            $failures.Add("$name 가 스레드 목록에 없다 - 수명 훅이 끊겼다")
        }
    }
    if ($result.ExitCode -ne 0) { $failures.Add("종료 코드 $($result.ExitCode)") }

    Write-Host ""
    Write-Host "── 판정 ─────────────────────────────"
    if ($failures.Count -eq 0) {
        Write-Host "  결과           통과" -ForegroundColor Green
        return 0
    }
    foreach ($f in $failures) { Write-Host "  실패           $f" -ForegroundColor Red }
    Write-Host ("  전체 출력      {0}" -f $result.OutFile)
    Write-Host "  결과           실패" -ForegroundColor Red
    return 1
}

switch ($Action) {
    "Build"   { Invoke-Build; exit 0 }
    "Stats"   { exit (Invoke-Stats) }
    "Workers" { exit (Invoke-Workers) }
}
