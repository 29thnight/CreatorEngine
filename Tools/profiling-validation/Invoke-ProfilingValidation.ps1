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
    [ValidateSet("Stats", "Workers", "Window", "Gpu", "Build")]
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
    # ★ `profile.record` 를 **씬 교체 앞**에 둔다. 부팅 수집을 걷은 뒤로는
    #   켜지 않으면 캡처가 없고, 켜는 시점이 교체 뒤면 SceneActivated 사건이
    #   기록 밖에서 일어나 §7.3 트랙 1 이 빈다.
    $result = Invoke-EngineScript -Label "profile-workers" -Commands @(
        "profile.record"
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

    # ── §7.3 트랙 1: 길이가 없는 사건 ───────────────────────────────────────
    #
    # 이 축의 자극은 위의 `scene.switch` 다. 씬이 바뀌면 SceneManager 가
    # SceneActivated 를 찍는다.
    #
    # ★ frames 배열을 보지 않는다. 그것은 **최근 여덟 칸**만 담고, 전환은
    #   60 프레임 전에 일어났다 — 거기서 세면 "안 찍혔다" 와 "창 밖이다" 가
    #   같아 보인다. profile.frame 이 캡처 전체를 훑어 따로 내는 목록을 쓴다.
    $instantNames = @()
    if ($null -ne $frame.data.PSObject.Properties['instants']) {
        $instantNames = @($frame.data.instants | ForEach-Object { $_.name })
    }
    $sceneActivated = @($instantNames | Where-Object { $_ -eq 'SceneActivated' }).Count
    Write-Host ("  길이 없는 사건  {0}건 (SceneActivated {1})" -f $instantNames.Count, $sceneActivated)

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
    if ($sceneActivated -le 0)         { $failures.Add("SceneActivated 사건이 없다 - 씬을 갈아 끼웠는데 §7.3 트랙 1 이 비었다") }
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

# 프로파일러 창이 실제로 **그려지는지** 잰다.
#
# ★ 창이 열린 것과 본문이 도는 것은 다르다. 도크 탭으로 겹친 창은 선택돼야
#   본문이 돌고, 그러지 않으면 `editor.window ... open` 이 성공해도 DrawProfilerHUD
#   까지 오지 않는다. 그래서 창 본문에 마커를 하나 걸고 **그 마커가 캡처에
#   나타나는지**로 판정한다 — 프로파일러가 자기 창을 증언한다.
#
# ⚠ 이 축이 재는 것은 "본문이 돌았다" 까지다. 표의 숫자가 맞는지는 코어가
#   판정한다(verify-profile-core.ps1 의 aggregate/ · reader/ 검사). 화면이
#   숫자를 만들지 않으므로 그 둘을 나눌 수 있다.
function Invoke-Window {
    # 창을 열고 → 닫고 → 다시 연다. 중간의 profile.stats 가 완료조건
    # "창을 닫아도 recording 상태가 유지된다" 를 재는 자리다 — 녹화는 서비스가
    # 들고 창은 reader 일 뿐이라는 것이 설계이고, 그 설계가 실제로 그런지 본다.
    # ★ 여기도 `profile.record` 를 명시한다. 이 축의 단정 하나가 "창을 닫아도
    #   state == recording" 인데, 켠 적이 없으면 그 단정은 켜지지 않은 것을
    #   "멈췄다" 로 읽는다.
    $result = Invoke-EngineScript -Label "profile-window" -Commands @(
        "profile.record"
        "editor.window ###Editor.FrameProfiler open"
        "wait $WarmupFrames"
        "editor.window ###Editor.FrameProfiler close"
        "wait 30"
        "profile.stats"
        "editor.window ###Editor.FrameProfiler open"
        "wait 30"
        # ★ 타임라인을 자극하는 두 줄이다.
        #
        #   타임라인은 얼린 캡처가 있어야 그려지는데, 얼린 채로는 마커가 찍히지
        #   않아 그렸다는 증거가 남지 않는다. 얼렸다가 바로 다시 열어야 둘이 동시에 선다.
        #
        # ★ 사이에 wait 를 두지 **않는다.** 얼린 순간을 창이 볼 수 있게 두면,
        #   상태를 보고 집는 낡은 관문도 우연히 통과한다 — 그러면 이 축은 "찰나를
        #   놓쳐도 집는다" 를 재지 못하고 날마다 다른 답을 낸다. 붙여 두면
        #   얼린 구간이 한 프레임보다 짧아지므로 자극이 결정적이 된다.
        "profile.pause"
        "profile.record"
        "wait 30"
        "profile.frame"
        "profile.stats"
        "quit"
    )

    $body = $result.Combined -split "`n"
    $openLine  = $body | Where-Object { $_ -match '"command"\s*:\s*"editor\.window"' } | Select-Object -First 1
    $frameLine = $body | Where-Object { $_ -match '"command"\s*:\s*"profile\.frame"' } | Select-Object -First 1
    $statsLines = @($body | Where-Object { $_ -match '"command"\s*:\s*"profile\.stats"' })
    $closedLine = $statsLines | Select-Object -First 1   # 창을 닫은 뒤
    $statsLine  = $statsLines | Select-Object -Last 1    # 다시 연 뒤
    if (-not $openLine -or -not $frameLine -or -not $statsLine -or -not $closedLine) {
        Write-Host "응답을 찾지 못했다. 전체 출력: $($result.OutFile)" -ForegroundColor Red
        return 1
    }

    try {
        $open  = $openLine.Trim()  | ConvertFrom-Json
        $frame = $frameLine.Trim() | ConvertFrom-Json
        $stats = $statsLine.Trim() | ConvertFrom-Json
        $closed = $closedLine.Trim() | ConvertFrom-Json
    }
    catch {
        Write-Host "응답을 JSON 으로 읽지 못했다: $_" -ForegroundColor Red
        return 1
    }

    # 창 마커가 어느 스레드에 몇 건이나 붙었는지 센다. 창은 프레젠테이션
    # 스레드가 그리므로 귀속까지 봐야 "그 스레드가 실제로 그렸다" 가 된다.
    $windowEvents = 0
    $onPresentation = 0
    $timelineEvents = 0
    foreach ($f in $frame.data.frames) {
        foreach ($t in $f.threads) {
            foreach ($e in $t.events) {
                if ($e.name -eq 'ProfilerTimeline' -and $t.name -eq '[PresentationThread]') {
                    $timelineEvents++
                }
                if ($e.name -ne 'ProfilerWindow') { continue }
                $windowEvents++
                if ($t.name -eq '[PresentationThread]') { $onPresentation++ }
            }
        }
    }

    Write-Host ""
    Write-Host "[profile.window] 프로파일러 창 (###Editor.FrameProfiler)"
    Write-Host ("  창 열기 요청    {0}" -f $open.status)
    Write-Host ("  최근 {0}프레임의 ProfilerWindow  {1}건 (그중 프레젠테이션 스레드 {2})" -f
        $frame.data.frames.Count, $windowEvents, $onPresentation)
    Write-Host ("  같은 구간의 ProfilerTimeline  {0}건 (프레젠테이션 스레드)" -f $timelineEvents)
    Write-Host ("  등록 마커       {0}" -f $stats.data.registeredMarkers)
    Write-Host ("  창을 닫은 뒤 상태  {0}" -f $closed.data.state)

    $failures = New-Object System.Collections.Generic.List[string]
    if ($open.status -ne 'succeeded')  { $failures.Add("창 열기 실패: $($open.status)") }
    if ($stats.status -ne 'succeeded') { $failures.Add("profile.stats status=$($stats.status)") }
    if ($stats.data.malformedScopes -ne 0) {
        $failures.Add("불균형 스코프 $($stats.data.malformedScopes) - 창이 스코프 짝을 깨뜨렸다")
    }
    # ★ 완료조건: 창을 닫아도 녹화가 멈추지 않는다.
    if ($closed.data.state -ne 'recording') {
        $failures.Add("창을 닫았더니 녹화가 '$($closed.data.state)' 가 됐다 - 창이 녹화를 소유하면 안 된다")
    }
    if ($windowEvents -le 0) {
        $failures.Add("ProfilerWindow 가 캡처에 없다 - 창이 열렸다고 했는데 본문이 돌지 않았다")
    }
    if ($onPresentation -le 0) {
        $failures.Add("ProfilerWindow 가 프레젠테이션 스레드에 붙지 않았다")
    }
    # ★ 창이 돌았다는 것과 **타임라인이 그려졌다** 는 것은 다르다. 타임라인은
    #   얼린 캡처가 없으면 한 줄짜리 안내문만 내고 빠져나간다 — 그래도 창은
    #   열려 있고 ProfilerWindow 는 찍힌다. 두 마커를 갈라 세는 이유가 그것이다.
    if ($timelineEvents -le 0) {
        $failures.Add("ProfilerTimeline 이 캡처에 없다 - 창은 돌았으나 타임라인 본문은 돌지 않았다")
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

# ── Gpu ── GPU 수집이 **그 제출의** 기록을 읽는가(P4).
#
# ★ 이 축이 생긴 이유. 패스별 GPU 시간은 렌더 디버그 창에만 있었고, 그
#   숫자가 올바른 제출의 것인지를 물을 수단이 없었다. 착수 전 실측에서
#   수집의 83% 가 남의 제출을 읽고 있었다(§0.5.10) — 눈으로는 틀린 숫자도
#   그럴듯하다.
#
# ⚠ 이 축이 재는 것은 "표가 낡지 않았다" 까지다. 표가 낡았을 때 수집을
#   거절하는 가드 자체를 걷어내는 변이는 이 축이 잡지 못한다 — 그때는
#   다시 조용해진다. 링을 좀히는 변이(제출마다 같은 슬롯)로 가드가 **울린다**는
#   것은 잴다.
function Invoke-Gpu {
    # 라이브 렌더러는 프레임 수로 예열하지 않는다 — 첫 프레임이 slang reflect 로
    # 수십 초를 쓴다. render.live.wait 은 라이브 프레임 완료를 기다린다.
    $result = Invoke-EngineScript -Label "profile-gpu" -Commands @(
        "render.live.wait 300"
        "render.live.wait 400"
        "render.live.wait 400"
        "render.live.wait 400"
        "profile.record"
        "render.live.wait 120"
        "profile.pause"
        "dx12.live status"
        "profile.frame"
        "quit"
    )

    $line = ($result.Combined -split "`n" |
        Where-Object { $_ -match '"command"\s*:\s*"dx12\.live"' } | Select-Object -Last 1)
    if (-not $line) {
        Write-Host "dx12.live 응답을 찾지 못했다. 전체 출력: $($result.OutFile)" -ForegroundColor Red
        return 1
    }

    try { $live = $line.Trim() | ConvertFrom-Json }
    catch {
        Write-Host "응답을 JSON 으로 읽지 못했다: $_" -ForegroundColor Red
        return 1
    }

    $gpu = $live.data.gpu

    # GPU 레인은 dx12.live 가 아니라 **캡처**에서 확인한다. 장부가 초록이어도
    # sink 를 아무도 걸지 않았으면 레인은 조용히 빈다.
    $frameLine = ($result.Combined -split "`n" |
        Where-Object { $_ -match '"command"\s*:\s*"profile\.frame"' } | Select-Object -Last 1)
    $gpuLaneEvents = 0
    $gpuLaneFrames = 0
    $gpuLaneSkewed = 0
    $gpuLaneSeen = $false

    # §7.3 의 귀속과 트랙 순서.
    $gpuNoOrigin = 0        # 제출 번호가 0 인 GPU 구간
    $laneOrderBreaks = 0    # 레인이 트랙 순서를 어긴 자리
    $laneOrderSample = ''
    $lanesScanned = 0
    if ($frameLine) {
        try { $frameData = $frameLine.Trim() | ConvertFrom-Json } catch { $frameData = $null }
        if ($frameData) {
            foreach ($f in $frameData.data.frames) {
                foreach ($th in $f.threads) {
                    if ($th.name -ne '[GPU Graphics]') { continue }
                    $gpuLaneSeen = $true
                    $gpuLaneFrames++
                    foreach ($e in $th.events) {
                        $gpuLaneEvents++
                        # ★ 이 한 줄이 이 조각의 계약이다. 이벤트가 담긴 프레임과
                        #   이벤트가 들고 온 프레임 라벨이 같아야 한다.
                        if ($e.startFrame -ne $f.frame) { $gpuLaneSkewed++ }

                        # 귀속(§7.3 의 tooltip). 제출 번호가 0 이면 그 구간은
                        # 어느 제출의 것인지 말하지 못한다 — 같은 프레임의
                        # 씬뷰와 게임뷰가 이름만 같은 두 줄로 보인다.
                        $submission = 0
                        if ($null -ne $e.PSObject.Properties['submission']) {
                            $submission = [int]$e.submission
                        }
                        if (0 -eq $submission) { $gpuNoOrigin++ }
                    }
                }

                # 레인이 §7.3 의 트랙 순서로 나오는가. CLI 는 코어의
                # track_precedes 로 세워 내므로, 여기서 순서를 다시 적지 않고
                # **오름차순인지만** 묻는다.
                $previous = -1
                $names = @()
                foreach ($th in $f.threads) {
                    $kind = if ($null -ne $th.PSObject.Properties['trackKind']) {
                        [int]$th.trackKind } else { -1 }
                    $names += ($th.name + '(' + $kind + ')')
                    if ($kind -lt $previous) {
                        $laneOrderBreaks++
                        if ('' -eq $laneOrderSample) {
                            $laneOrderSample = 'frame ' + $f.frame + ': ' + ($names -join ' -> ')
                        }
                    }
                    $previous = $kind
                }
                if ($f.threads.Count -ge 2) { $lanesScanned++ }
            }
        }
    }

    Write-Host ""
    Write-Host "[profile.gpu] GPU 수집 장부"
    Write-Host ("  렌더한 프레임   {0}" -f $live.data.framesRendered)
    Write-Host ("  수집           {0}" -f $gpu.collects)
    Write-Host ("  표가 낡아 거절  {0}" -f $gpu.mismatches)
    Write-Host ("  패스           {0}개(이름) · {1}조각(raw) · 버린 조각 {2} · 길이 0 인 조각 {3}" -f
        $gpu.passCount, $gpu.sliceCount, $gpu.droppedSlices, $gpu.zeroLengthSlices)
    Write-Host ("  길이           이름합 {0:N4} · queueSpan {1:N4} · busy {2:N4} ms" -f
        $gpu.ms, $gpu.queueSpanMs, $gpu.busyMs)
    Write-Host ("  귀속           frame {0} · submission {1} · view {2}" -f $gpu.frame, $gpu.submission, $gpu.viewId)
    Write-Host ("  누적 장부      버림 {0} · 길이 0 이 {1} · span 위반 {2} · 조각 부족 {3}" -f
        $gpu.droppedTotal, $gpu.zeroLengthTotal, $gpu.spanViolations, $gpu.sliceUnderflows)
    Write-Host ("  통합 축        표본 {0}회 · GPU {1:N0} Hz · CPU {2:N0} Hz · 못 옮긴 수집 {3}" -f
        $gpu.clockSamples, $gpu.clockGpuHz, $gpu.clockCpuHz, $gpu.unalignedCollects)
    Write-Host ("  표본 어긋남    직전 {0:N4} ms · 최대 {1:N4} ms" -f
        $gpu.clockDriftMs, $gpu.clockMaxDriftMs)
    Write-Host ("  정렬 여유      제출→GPU 시작 최소 {0:N4} ms · GPU 끝→수집 최소 {1:N4} ms · 위반 {2}" -f
        $gpu.minSubmitToBeginMs, $gpu.minEndToCollectMs, $gpu.alignmentViolations)
    Write-Host ("  귀속 지연      제출→수집 최대 {0:N4} ms" -f $gpu.maxSubmitToCollectMs)
    Write-Host ("  레인 귀속      흘린 조각 {0} · 캡처의 GPU 레인 {1}칸 {2}건 · 어긋난 칸 {3}" -f
        $gpu.spansEmitted, $gpuLaneFrames, $gpuLaneEvents, $gpuLaneSkewed)
    Write-Host ("  출처·트랙      제출 번호 0 인 구간 {0} · 트랙 순서 위반 {1} (레인 둘 이상인 프레임 {2})" -f
        $gpuNoOrigin, $laneOrderBreaks, $lanesScanned)
    if ($gpu.clockError) { Write-Host ("  통합 축 사유   {0}" -f $gpu.clockError) }
    if ($gpu.lastError) { Write-Host ("  마지막 사유     {0}" -f $gpu.lastError) }

    $failures = New-Object System.Collections.Generic.List[string]
    if ($live.data.ready -ne $true) { $failures.Add("파이프라인이 준비되지 않았다") }
    if ($gpu.collects -le 0) {
        # 자극이 한 번도 닿지 않았다는 뜻이다. 나머지 단정은 전부 공백에서
        # 초록이 되므로 여기서 막는다.
        $failures.Add("GPU 수집이 0 회 - 자극이 수집 경로에 닿지 않았다")
    }
    if ($gpu.mismatches -ne 0) {
        $failures.Add("표가 낡아 거절한 수집 $($gpu.mismatches)/$($gpu.collects) - 제출마다 기록이 갈라져 있지 않다")
    }
    if ($gpu.passCount -le 0) {
        $failures.Add("수집한 패스가 0 개 - 수치가 비어 있다")
    }
    if ($gpu.frame -le 0) {
        $failures.Add("귀속할 engineFrameId 가 없다 - 숫자가 어느 프레임 것인지 모른다")
    }

    # ── raw 구간이 살아 있는가(§3.3) · 길이 셋의 관계가 성립하는가(§3.4) ──
    #
    # ★ 묻는 대상이 **누적 장부**다. 마지막 한 번의 값은 씬뷰와 게임뷰 중 어느
    #   쪽인지가 실행마다 갈려서, 한쪽에만 있는 패스는 절반의 확률로만 검사를
    #   받는다. 실제로 그랬다 — 길이 0 인 GizmoLine 은 씬뷰에만 있고, 그래서
    #   같은 코드로 초록과 붉음이 번갈아 나왔다.
    if ($gpu.droppedTotal -ne 0) {
        $detail = if ($gpu.droppedSliceName) {
            " (마지막 표본: '$($gpu.droppedSliceName)', $($gpu.droppedSliceDeltaTicks) 틱)"
        } else { "" }
        $failures.Add("뒤집힌 조각 누적 $($gpu.droppedTotal) 개$detail - 끝이 시작보다 앞선다")
    }
    # busy 는 겹침을 한 번만 센 합이므로 queue span 을 넘을 수 없다.
    # 넘으면 합치는 산술이 겹침을 중복으로 센 것이다.
    if ($gpu.spanViolations -ne 0) {
        $failures.Add("busy > queueSpan 인 수집 $($gpu.spanViolations)/$($gpu.collects) - 겹침을 중복으로 센다")
    }
    # 이름으로 묶는 것은 표시용이다. 묶은 것만 남기면 GPU 타임라인을 그릴 수가
    # 없고 조각이 서로 겹치는지도 알 수 없다. 조각 수가 이름 수보다 적을 수는 없다.
    if ($gpu.sliceUnderflows -ne 0) {
        $failures.Add("raw 조각 < 이름 인 수집 $($gpu.sliceUnderflows)/$($gpu.collects) - 묶은 것보다 원본이 적을 수 없다")
    }
    # ★ 분할 패스가 실제로 여러 조각으로 남아 있어야 한다. 같으면 묶기가 원본을
    #   덮어쓴 것과 구별되지 않는다 — 이 씬은 분할 패스를 늘 가지고 있다.
    if ($gpu.sliceCount -le $gpu.passCount) {
        $failures.Add("raw 조각 $($gpu.sliceCount) <= 이름 $($gpu.passCount) - 분할 패스의 조각이 남아 있지 않다")
    }
    if ($gpu.busyMs -le 0) {
        $failures.Add("busy 가 0 ms - 겹치지 않는 실행 구간이 없다")
    }
    # 이름합은 묶은 구간의 합이라 서로 겹칠 수 있고, 그 합집합은 busy 를 덮는다.
    if ($gpu.ms -lt ($gpu.busyMs - 1e-6)) {
        $failures.Add("이름합 $($gpu.ms) < busy $($gpu.busyMs) - 묶은 구간이 원본을 다 덮지 못한다")
    }

    # ── 두 시계가 맞았는가(§5.1) ────────────────────────────────────
    #
    # ★ 판정을 임계값이 아니라 **인과**에 건다. 변환한 GPU 구간은 CPU 가 제출을
    #   연 뒤에 시작해서 수집하기 전에 끝나야 한다 — 하드웨어가 달라도 그대로
    #   서는 관계다. "몇 ms 안쪽" 같은 수를 고르면 그 수가 곧 거짓말의 여유가 된다.
    if ($gpu.clockValid -ne $true) {
        $failures.Add("clock calibration 표본이 없다 - CPU/GPU 통합 축이 꺼져 있다: $($gpu.clockError)")
    }
    # ★ 표본이 한 번뿐이면 주기적 재표본이 죽어 있어도 초록이다. 예열이 몇 초를
    #   지나므로 둘 이상 나와야 한다 — 임계값이 아니라 "다시 뜨는가" 를 묻는다.
    if ($gpu.clockSamples -lt 2) {
        $failures.Add("clock calibration 표본 $($gpu.clockSamples) 회 - 주기적 재표본이 돌지 않았다")
    }
    if ($gpu.unalignedCollects -ne 0) {
        $failures.Add("CPU 축으로 못 옮긴 수집 $($gpu.unalignedCollects)/$($gpu.collects) - 표본이 있는데도 옮기지 않았다")
    }
    # ── GPU 레인이 제 프레임 칸에 앉았는가(§7.3) ──────────────────
    #
    # ★ dx12.live 의 장부가 아니라 **캡처**를 묻는다. 수집이 돌고 정렬이 맞아도
    #   sink 를 아무도 걸지 않았으면 레인은 조용히 빈다 — 생산만 있고 소비가
    #   0 인 파이프라인이 이 저장소에서 여러 번 나왔다.
    if ($gpu.spansEmitted -le 0) {
        $failures.Add("EngineDiagnostics 로 흘린 조각이 0 - 귀속 sink 가 걸리지 않았다")
    }
    if (-not $gpuLaneSeen) {
        $failures.Add("캡처에 [GPU Graphics] 레인이 없다 - 흘렸는데 담기지 않았다")
    }
    if ($gpuLaneEvents -le 0) {
        $failures.Add("GPU 레인에 구간이 0 건 - 레인만 서고 내용이 없다")
    }
    # ★ 임계값이 아니라 동치다. 이벤트가 담긴 칸과 이벤트가 들고 온 라벨이
    #   다르면 늦게 온 구간이 제 프레임으로 돌아가지 못한 것이다.
    if ($gpuLaneSkewed -ne 0) {
        $failures.Add("제 프레임 칸을 벗어난 GPU 구간 $gpuLaneSkewed/$gpuLaneEvents - 늦게 온 것이 수집한 프레임에 담겼다")
    }

    # ── GPU bar 가 자기 출처를 말하는가(§7.3 tooltip) ─────────────
    if ($gpuNoOrigin -ne 0) {
        $failures.Add("제출 번호가 0 인 GPU 구간 $gpuNoOrigin/$gpuLaneEvents - 어느 제출의 것인지 말하지 못한다")
    }

    # ── 레인이 §7.3 의 트랙 순서로 서는가 ─────────────────────────
    #
    # ★ "어긴 자리 0" 은 잴 레인이 없어도 0 이다. 레인이 둘 이상인 프레임을
    #   실제로 훑었는지 먼저 묻는다.
    if ($lanesScanned -le 0) {
        $failures.Add("레인이 둘 이상인 프레임이 없다 - 트랙 순서 단정이 빈 집합을 통과한다")
    }
    if ($laneOrderBreaks -ne 0) {
        $failures.Add("트랙 순서를 어긴 레인 $laneOrderBreaks 자리 ($laneOrderSample) - 창과 CLI 가 다른 순서를 말한다")
    }

    if ($gpu.alignmentViolations -ne 0) {
        $failures.Add("정렬 위반 $($gpu.alignmentViolations)/$($gpu.collects) - 변환한 GPU 구간이 제출과 수집 사이를 벗어난다(제출→시작 최소 $($gpu.minSubmitToBeginMs) ms · 끝→수집 최소 $($gpu.minEndToCollectMs) ms)")
    }

    # ── 레인이 주인 손에서 닫히는가 ────────────────────────────────
    #
    # 스트림의 주인은 **적는 스레드**다. GPU 레인은 OS 스레드가 아니라 큐지만
    # 저장소를 만드는 것은 처음 적은 스레드(렌더 스레드)이고, 그 스레드가
    # unregister_thread 를 불러도 자기 TLS 스트림만 끊어 레인은 남는다.
    # 그러면 종료를 도는 게임 스레드가 남의 저장소를 마주하고 닫지 못한 채
    # 놓아 둔다 — 그 스레드의 꼬리는 어느 캡처에도 남지 않는다.
    #
    # 2026-09-21 실측은 `abandoned=9` 였다(워커 여덟 + [GPU Graphics]).
    #
    # ★ 이 축을 여기 두는 이유는 **자극이 여기에만 있기 때문**이다. GPU 레인은
    #   녹화 중에 라이브 제출의 타임스탬프를 되읽어야 생기고, 위의
    #   `$gpuLaneSeen` 단정이 이 회차에 실제로 생겼음을 이미 보증한다.
    $shutdownLine = (($result.Combined -split "`n" |
        Where-Object { $_ -match '\[profiler\] shutdown ' }) | Select-Object -Last 1)
    if (-not $shutdownLine) {
        $failures.Add("stderr 에 '[profiler] shutdown' 줄이 없다 - 없는 줄에서 '버려진 0' 을 읽을 수는 없다")
    }
    else {
        $shutdownLine = $shutdownLine.Trim()
        Write-Host ("  종료 소유      {0}" -f $shutdownLine)
        $m = [regex]::Match($shutdownLine,
            'abandoned=(?<abandoned>\d+)\s+retained=(?<retained>\d+)\s+foreign=(?<foreign>\d+)')
        if (-not $m.Success) {
            $failures.Add("종료 줄의 모양이 바뀌었다: $shutdownLine - 이 정규식을 함께 고쳐라")
        }
        else {
            if ($m.Groups['abandoned'].Value -ne '0') {
                $failures.Add("종료 때 주인 없이 남은 스트림 $($m.Groups['abandoned'].Value) 개: $shutdownLine - 그 스레드들이 멎기 전에 자기 스트림을 닫지 않았다")
            }
            if ($m.Groups['foreign'].Value -ne '0') {
                $failures.Add("남의 스트림을 만진 횟수 $($m.Groups['foreign'].Value): $shutdownLine")
            }
        }
    }

    if ($result.ExitCode -ne 0) { $failures.Add("종료 코드 $($result.ExitCode)") }

    Write-Host ""
    Write-Host "── 판정 ─────────────────────"
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
    #
    # ★ `render.live.wait` 가 앞뒤로 있는 이유 — **예열은 프레임 수가 아니다.**
    #
    #   `wait N` 은 게임 스레드 프레임 수라 렌더 스레드가 한 프레임에 얼마를
    #   쓰는지와 무관하게 지나간다. 라이브 첫 프레임은 GBuffer ShaderMeta 반영
    #   (slang reflect)에 **22.8 초**를 쓰는데(2026-09-21 Debug 실측), 그동안
    #   게임 프레임은 61 → 3418 로 가고 발행된 것은 전부 latest-wins 로 접힌다.
    #   그래서 `wait 240` 만 쓰던 시절 이 게이트가 본 렌더 소비는 246 중 **4 회**
    #   였고 캡처에 남은 것은 1 건이었다 — 워밍업을 늘려도 나아지지 않는다.
    #   예열이 끝나면 소비는 15 ms 마다 일어난다.
    #
    #   앞의 하나는 그 예열을 통과시키고, 뒤의 하나는 **측정 구간 안에서** 라이브
    #   프레임이 최소 한 번 끝나는 것을 보장한다. 게임 스레드를 세우지 않으므로
    #   (`WaitForResult` 로 판정만 미룬다) 다른 축의 값이 왜곡되지 않는다.
    # ★ `profile.record` 를 **명시한다.** 부팅과 함께 기록을 열던 시절에는
    #   없어도 돌았는데, 그 줄을 걷은 뒤로는 없으면 이 축이 빈 캡처를 읽는다
    #   — 그리고 빈 캡처는 "이벤트 0" 이라 조용히 지나간다.
    $result = Invoke-EngineScript -Label "profile-stats" -Commands @(
        "render.live.wait 300"
        "profile.record"
        "wait $WarmupFrames"
        "render.live.wait 120"
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
    #   RenderThread 는 위의 `render.live.wait` 가 예열을 통과시키고 측정 구간
    #   안의 소비를 보장하므로 **건수를 단정한다.** 그 대기가 없던 시절에는
    #   246 중 4 회만 소비되어 캡처에 1 건이 남았고, 64 프레임 워밍업에서는 0 이라
    #   계측이 멀쩡해도 붉어졌다.
    if (-not $d.captureFrozen) {
        $failures.Add("얼린 캡처가 없다 - profile.frame 이 캡처를 공개하지 못했다")
    }
    foreach ($name in @('[GameThread]', '[PresentationThread]', '[RenderThread]')) {
        $thread = $d.threads | Where-Object { $_.name -eq $name }
        if (-not $thread) {
            $failures.Add("$name 가 스레드 목록에 없다")
        } elseif ($thread.capturedEvents -le 0) {
            $failures.Add("$name 가 등록만 되고 이벤트를 하나도 안 찍었다")
        }
    }
    if (-not ($d.threads | Where-Object { $_.name -eq '[Worker 1]' })) {
        $failures.Add("[Worker 1] 이 스레드 목록에 없다 - 수명 훅이 끊겼다")
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
    "Window"  { exit (Invoke-Window) }
    "Gpu"     { exit (Invoke-Gpu) }
}
