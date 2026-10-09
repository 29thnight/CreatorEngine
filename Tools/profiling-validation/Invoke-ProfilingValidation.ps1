#Requires -Version 7.0
<#
.SYNOPSIS
    프레임 프로파일러 라이브 검증 진입점 (PHASE 14).

.DESCRIPTION
    에디터를 무인으로 기동해, 수집 코어가 **살아 있는 엔진에서 실제로 서는지**
    판정한다. 판정 근거는 화면이 아니라 둘이다 — 종료 코드와 응답 JSON.

    경계가 분명하다. 표의 숫자가 맞는지는 코어가 프로세스 밖에서 증명하고
    (Tools/regression/verify-profile-core.ps1), 이 스크립트는 그 숫자가 나올
    자리에 무엇이 실제로 흘렀는지만 본다. 화면이 숫자를 만들지 않으므로 둘을
    나눌 수 있다.

    ★ 모든 라이브 축의 자극에 `profile.record` 를 명시한다(2026-09-23).
      부팅과 함께 기록을 열던 줄을 걷었으므로, 켜지 않으면 캡처가 비고 그러면
      이 게이트들이 **빈 캡처를 성공으로 읽는다.** Workers 는 그것을
      `scene.switch` **앞**에 둬야 한다.

    ⚠ 단정은 "무엇을 찍었는가" 지 "몇 개를 찍었는가" 가 아니다. 기준선 숫자는
      남이 계측을 얹기만 해도 흔들린다.

    PowerShell 5.1로 돌리지 말 것. 이 저장소의 스크립트와 엔진 로그는 UTF-8이고
    5.1은 이를 시스템 코드페이지로 읽어 한글이 깨진 채 정규식 판정이 어긋난다.

.PARAMETER Action
    Stats     기본 씬의 교란 없는 라이브 기준선(기본값). 보존·이벤트·마커·청크와
              [GameThread]/[RenderThread]/[PresentationThread]/[Worker 1] 귀속
    Workers   fixture 씬으로 애니메이션 잡을 돌려 워커 레인의 구간 계측과
              SceneActivated(길이 없는 사건)
    Window    별도 viewer의 인증된 Present 계수, close/reopen과 엔진 녹화 독립성
    Gpu       Scene/Game/동시 뷰·리사이즈·2-inflight·DX12 검증과 GPU 수집 장부
    GpuLoss   Debug 질의 슬롯을 16개로 좁혀 누락이 Collector에 기록되는지 검증
    Providers Resource 모듈 mask와 관리 GC/스크립트 마커의 동일 프레임 귀속
    Build     Debug|x64 빌드만 수행

.EXAMPLE
    pwsh -NoProfile -File .\Tools\profiling-validation\Invoke-ProfilingValidation.ps1 -Action Stats
    pwsh -NoProfile -File .\Tools\profiling-validation\Invoke-ProfilingValidation.ps1 -Action Workers
#>
[CmdletBinding()]
param(
    [ValidateSet("Stats", "Workers", "Window", "Gpu", "GpuLoss", "Providers", "Memory", "Build")]
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
$OutputRoot = [IO.Path]::GetFullPath($OutputRoot)
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
        -WindowStyle Hidden `
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

# Standalone viewer process boundary. This gate is source-only/UNEXECUTED in
# this change. Authenticated presented-frame counters replace engine-local UI
# markers; neither a queued launch nor a process name alone proves presentation.
function Invoke-Window {
    $settleFrames = [Math]::Max($WarmupFrames, 600)
    $result = Invoke-EngineScript -Label "profile-window" -Commands @(
        "profile.record"
        "editor.window ###Editor.FrameProfiler open"
        "wait $settleFrames"
        "profile.stats"
        "editor.window ###Editor.FrameProfiler close"
        "wait $settleFrames"
        "profile.stats"
        "editor.window ###Editor.FrameProfiler open"
        "wait $settleFrames"
        "profile.frame"
        "profile.stats"
        "editor.window ###Editor.FrameProfiler close"
        "wait $settleFrames"
        "profile.stats"
        "quit"
    )
    try {
        $responses = @($result.Combined -split "`n" | Where-Object {
            $_ -match '"command"\s*:\s*"(profile\.stats|profile\.frame)"'
        } | ForEach-Object { $_.Trim() | ConvertFrom-Json })
        $stats = @($responses | Where-Object { $_.command -eq 'profile.stats' })
        $frame = $responses | Where-Object { $_.command -eq 'profile.frame' } | Select-Object -Last 1
        if ($stats.Count -ne 4 -or -not $frame) {
            throw "Expected four viewer status observations and one capture response."
        }
    }
    catch {
        Write-Host "Window gate response failed: $_ ($($result.OutFile))" -ForegroundColor Red
        return 1
    }
    $failures = [Collections.Generic.List[string]]::new()
    foreach ($index in @(0, 2)) {
        $viewer = $stats[$index].data.viewer
        if ($stats[$index].status -ne 'succeeded' -or -not $viewer.running -or
            -not $viewer.connected -or $viewer.pid -eq 0 -or $viewer.presentedFrames -eq 0) {
            $failures.Add("Viewer launch $index did not authenticate and present: $($viewer.message)")
        }
    }
    foreach ($index in @(1, 3)) {
        $closed = $stats[$index]
        if ($closed.status -ne 'succeeded' -or $closed.data.viewer.running -or
            $closed.data.viewer.connected) {
            $failures.Add("Explicit viewer Close did not finish at observation $index")
        }
        if ($closed.data.state -ne 'recording') {
            $failures.Add("Closing the viewer changed engine recording to $($closed.data.state)")
        }
    }
    if ($stats[3].data.viewer.failureRevision -ne $stats[0].data.viewer.failureRevision) {
        $failures.Add('Normal close/reopen was incorrectly reported as a launch or protocol failure')
    }
    if ($stats[2].data.malformedScopes -ne 0) {
        $failures.Add('Engine scope integrity changed while the separate viewer was running')
    }
    if ($frame.status -ne 'succeeded' -or -not $frame.data.hasCapture -or
        @($frame.data.frames).Count -eq 0) {
        $failures.Add('Successful nonempty engine capture is required before asserting viewer markers are absent')
    }
    foreach ($f in $frame.data.frames) {
        foreach ($thread in $f.threads) {
            foreach ($event in $thread.events) {
                if ($event.name -in @('ProfilerWindow', 'ProfilerTimeline', 'ProfilerTelemetry')) {
                    $failures.Add("Viewer analysis/presentation still ran inside engine capture: $($event.name)")
                }
            }
        }
    }
    if ($result.ExitCode -ne 0) {
        $failures.Add("Engine exit code $($result.ExitCode)")
    }
    if ($failures.Count -ne 0) {
        foreach ($failure in $failures) {
            Write-Host "Window gate failed: $failure" -ForegroundColor Red
        }
        Write-Host "Output: $($result.OutFile). Increase WarmupFrames if startup exceeded the observation interval."
        return 1
    }
    Write-Host 'Window process gate passed: authenticated presentation, close/reopen, engine recording independence' -ForegroundColor Green
    Write-Host 'This does not validate pixels, every tab, target-exit recovery or adversarial IPC; see Tools/ProfilerViewer/README.md.'
    return 0
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
        "editor.window ###Editor.GamePreview close"
        "editor.viewport scene"
        "render.live.wait 300"
        "render.live.wait 400"
        "render.live.wait 400"
        "render.live.wait 400"
        "editor.viewport"
        "dx12.live status"
        "editor.viewport game"
        "wait 10"
        "render.live.wait 400"
        "render.live.wait 400"
        "editor.viewport"
        "dx12.live status"
        "editor.viewport scene"
        "editor.window ###Editor.GamePreview open"
        "wait 10"
        "render.live.wait 400"
        "render.live.wait 400"
        "editor.viewport"
        "dx12.live status"
        "profile.record"
        "render.live.wait 120"
        "window.resize 1417 873"
        "render.live.wait 120"
        "render.live.wait 120"
        "render.live.wait 120"
        "render.live.wait 120"
        "render.live.wait 120"
        "render.live.wait 120"
        "render.live.wait 120"
        "render.live.wait 120"
        "render.live.wait 120"
        "render.live.wait 120"
        "profile.pause"
        "dx12.live status"
        "profile.stats"
        "profile.frame gpu"
        "quit"
    )

    $liveLines = @($result.Combined -split "`n" |
        Where-Object { $_ -match '"command"\s*:\s*"dx12\.live"' })
    $line = $liveLines | Select-Object -Last 1
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
    $statsLine = ($result.Combined -split "`n" |
        Where-Object { $_ -match '"command"\s*:\s*"profile\.stats"' } | Select-Object -Last 1)
    $profileStats = if ($statsLine) { $statsLine.Trim() | ConvertFrom-Json } else { $null }
    $singleScene = if ($liveLines.Count -ge 4) {
        $liveLines[0].Trim() | ConvertFrom-Json } else { $null }
    $singleGame = if ($liveLines.Count -ge 4) {
        $liveLines[1].Trim() | ConvertFrom-Json } else { $null }
    $beforeResize = if ($liveLines.Count -ge 4) {
        $liveLines[2].Trim() | ConvertFrom-Json } else { $null }
    $viewportLines = @($result.Combined -split "`n" |
        Where-Object { $_ -match '"command"\s*:\s*"editor\.viewport"' })
    $sceneDemand = if ($viewportLines.Count -ge 6) {
        $viewportLines[1].Trim() | ConvertFrom-Json } else { $null }
    $gameDemand = if ($viewportLines.Count -ge 6) {
        $viewportLines[3].Trim() | ConvertFrom-Json } else { $null }
    $multiDemand = if ($viewportLines.Count -ge 6) {
        $viewportLines[5].Trim() | ConvertFrom-Json } else { $null }
    $beforeResizeGeneration = if ($beforeResize) {
        [int]$beforeResize.data.display.resizeGeneration } else { -1 }
    $singleSceneActive = $null -ne $sceneDemand -and $sceneDemand.data.editorTarget
    $singleSceneHasGame = $null -ne $sceneDemand -and $sceneDemand.data.gameTarget
    $singleGameHasScene = $null -ne $gameDemand -and $gameDemand.data.editorTarget
    $singleGameActive = $null -ne $gameDemand -and $gameDemand.data.gameTarget
    $multiSceneDemand = $null -ne $multiDemand -and $multiDemand.data.editorTarget
    $multiGameDemand = $null -ne $multiDemand -and $multiDemand.data.gameTarget
    $validationModeLine = ($result.Combined -split "`n" |
        Where-Object { $_ -match '\[DX12 검증\]' } | Select-Object -Last 1)
    $validationMessages = @($result.Combined -split "`n" |
        Where-Object { $_ -match '\[dx12\.live 검증\]|\[CORRUPTION\]|\[DRED\]' })

    # GPU 레인은 dx12.live 가 아니라 **캡처**에서 확인한다. 장부가 초록이어도
    # sink 를 아무도 걸지 않았으면 레인은 조용히 빈다.
    $frameLine = ($result.Combined -split "`n" |
        Where-Object { $_ -match '"command"\s*:\s*"profile\.frame"' } | Select-Object -Last 1)
    $gpuLaneEvents = 0
    $gpuLaneFrames = 0
    $gpuLaneSkewed = 0
    $gpuLaneSeen = $false
    $gpuViews = @{}
    $gpuViewFrames = @{}
    $gpuSubmissionViews = @{}
    $gpuSubmissionConflicts = 0

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
                        $view = if ($null -ne $e.PSObject.Properties['view']) {
                            [int]$e.view } else { 0 }
                        if ($view -gt 0) {
                            $gpuViews[$view] = $true
                            $frameKey = [string]$f.frame
                            if (-not $gpuViewFrames.ContainsKey($frameKey)) {
                                $gpuViewFrames[$frameKey] = @{}
                            }
                            if (-not $gpuViewFrames[$frameKey].ContainsKey($view)) {
                                $gpuViewFrames[$frameKey][$view] = 0
                            }
                            $gpuViewFrames[$frameKey][$view] =
                                [int]$gpuViewFrames[$frameKey][$view] + 1
                        }
                        if ($submission -gt 0) {
                            $submissionKey = "$($f.frame):$submission"
                            if ($gpuSubmissionViews.ContainsKey($submissionKey) -and
                                $gpuSubmissionViews[$submissionKey] -ne $view) {
                                $gpuSubmissionConflicts++
                            }
                            else {
                                $gpuSubmissionViews[$submissionKey] = $view
                            }
                        }
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

    $multiViewFrames = @($gpuViewFrames.Values | Where-Object {
        $_.ContainsKey(1) -and $_.ContainsKey(2)
    }).Count
    $differentPassFrames = @($gpuViewFrames.Values | Where-Object {
        $_.ContainsKey(1) -and $_.ContainsKey(2) -and $_[1] -ne $_[2]
    }).Count

    Write-Host ""
    Write-Host "[profile.gpu] GPU 수집 장부"
    Write-Host ("  렌더한 프레임   {0}" -f $live.data.framesRendered)
    Write-Host ("  수집           {0}" -f $gpu.collects)
    Write-Host ("  수집 실패      {0}" -f $gpu.mismatches)
    Write-Host ("  패스           {0}개(이름) · {1}조각(raw) · 버린 조각 {2} · 길이 0 인 조각 {3}" -f
        $gpu.passCount, $gpu.sliceCount, $gpu.droppedSlices, $gpu.zeroLengthSlices)
    Write-Host ("  길이           이름합 {0:N4} · queueSpan {1:N4} · busy {2:N4} ms" -f
        $gpu.ms, $gpu.queueSpanMs, $gpu.busyMs)
    Write-Host ("  귀속           frame {0} · submission {1} · view {2}" -f $gpu.frame, $gpu.submission, $gpu.viewId)
    Write-Host ("  다중 뷰        Scene/Game {0}/{1} · 같은 프레임 {2} (서로 다른 조각 수 {3}) · 제출 충돌 {4} · 최대 인플라이트 {5}" -f
        $live.data.display.scene.ready, $live.data.display.game.ready,
        $multiViewFrames, $differentPassFrames,
        $gpuSubmissionConflicts, $gpu.maxPendingSubmissions)
    Write-Host ("  펜스 대기      인플라이트 상한으로 건너뛴 틱 {0}" -f $live.data.framesInFlight)
    Write-Host ("  뷰 수요        Scene 모드 {0}/{1} · Game 모드 {2}/{3}" -f
        $singleSceneActive, $singleSceneHasGame,
        $singleGameHasScene, $singleGameActive)
    Write-Host ("  리사이즈       generation {0} → {1} · Scene/Game 완료 {2}/{3}" -f
        $beforeResizeGeneration,
        $live.data.display.resizeGeneration,
        $live.data.display.scene.completedResizeGeneration,
        $live.data.display.game.completedResizeGeneration)
    Write-Host ("  DX12 검증      {0} · 오류 메시지 {1}" -f
        [string]$validationModeLine, $validationMessages.Count)
    Write-Host ("  누적 장부      질의 초과 {0} · 버림 {1} · 길이 0 이 {2} · span 위반 {3} · 조각 부족 {4}" -f
        $gpu.queryOverflowPasses, $gpu.droppedTotal, $gpu.zeroLengthTotal,
        $gpu.spanViolations, $gpu.sliceUnderflows)
    if ($profileStats) {
        Write-Host ("  Collector 진단 질의 초과 {0} · 수집 실패 {1} · 마지막 프레임 {2}" -f
            $profileStats.data.gpuQueryOverflowPasses,
            $profileStats.data.gpuCollectFailures,
            $profileStats.data.gpuIssueLastFrame)
    }
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
    if ($null -eq $singleScene -or $null -eq $sceneDemand -or
        $singleScene.data.display.scene.ready -ne $true -or
        $sceneDemand.data.mode -ne 'scene' -or
        -not $singleSceneActive -or $singleSceneHasGame) {
        $failures.Add("Scene 모드의 뷰 수요/GPU 렌더 타깃 검증 실패")
    }
    if ($null -eq $singleGame -or $null -eq $gameDemand -or
        $singleGame.data.display.game.ready -ne $true -or
        $gameDemand.data.mode -ne 'game' -or
        -not $singleGameHasScene -or -not $singleGameActive) {
        $failures.Add("Game 모드의 뷰 수요/GPU 렌더 타깃 검증 실패")
    }
    if (-not $multiSceneDemand -or -not $multiGameDemand) {
        $failures.Add("동시 Scene/Game 뷰 수요가 없다")
    }
    if ($null -eq $beforeResize -or
        $live.data.display.resizeGeneration -le $beforeResizeGeneration -or
        $live.data.display.scene.completedResizeGeneration -ne $live.data.display.resizeGeneration -or
        $live.data.display.game.completedResizeGeneration -ne $live.data.display.resizeGeneration) {
        $failures.Add("리사이즈 전후 두 뷰의 완료 세대가 일치하지 않는다")
    }
    if ($validationModeLine -notmatch 'DebugLayer=on.*DRED=on') {
        $failures.Add("DX12 Debug Layer와 DRED가 활성화되지 않았다: $validationModeLine")
    }
    if ($validationMessages.Count -ne 0) {
        $failures.Add("DX12 검증/DRED 오류 메시지 $($validationMessages.Count)건: $($validationMessages[0])")
    }
    if ($live.data.display.scene.ready -ne $true -or $live.data.display.game.ready -ne $true) {
        $failures.Add("Scene/Game 두 렌더 타깃이 함께 준비되지 않았다")
    }
    if (-not $gpuViews.ContainsKey(1) -or -not $gpuViews.ContainsKey(2) -or $multiViewFrames -le 0) {
        $failures.Add("같은 프레임의 Scene/Game GPU 구간이 캡처에 모두 없다")
    }
    if ($differentPassFrames -le 0) {
        $failures.Add("두 뷰의 조각 수가 다른 프레임이 없어 제출별 기록 혼동을 구분할 자극이 없다")
    }
    if ($gpuSubmissionConflicts -ne 0) {
        $failures.Add("하나의 GPU 제출 번호가 서로 다른 뷰에 귀속됐다")
    }
    if ($gpu.maxPendingSubmissions -ne 2) {
        $failures.Add("GPU 2-inflight 를 확인하지 못했다: 최대 $($gpu.maxPendingSubmissions)")
    }
    if ($live.data.framesInFlight -le 0) {
        $failures.Add("미완료 펜스 때문에 다음 제출을 보류한 틱이 없다")
    }
    if ($gpu.collects -le 0) {
        # 자극이 한 번도 닿지 않았다는 뜻이다. 나머지 단정은 전부 공백에서
        # 초록이 되므로 여기서 막는다.
        $failures.Add("GPU 수집이 0 회 - 자극이 수집 경로에 닿지 않았다")
    }
    if ($gpu.mismatches -ne 0) {
        $failures.Add("GPU 수집 실패 $($gpu.mismatches)/$($gpu.collects): $($gpu.lastError)")
    }
    if ($gpu.queryOverflowPasses -ne 0) {
        $failures.Add("GPU 질의 슬롯 초과로 잃은 패스 $($gpu.queryOverflowPasses)개")
    }
    if ($null -eq $profileStats -or $null -eq $profileStats.data.PSObject.Properties['gpuQueryOverflowPasses'] -or
        $null -eq $profileStats.data.PSObject.Properties['gpuCollectFailures']) {
        $failures.Add("profile.stats에 GPU 손실 진단 필드가 없다")
    }
    elseif ($profileStats.data.gpuQueryOverflowPasses -ne 0 -or
        $profileStats.data.gpuCollectFailures -ne 0) {
        $failures.Add("녹화 Collector에 GPU 손실이 기록됐다")
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

function Invoke-GpuLoss {
    $previousLimit = $env:CREATOR_DX12_GPU_QUERY_LIMIT
    try {
        $env:CREATOR_DX12_GPU_QUERY_LIMIT = '16'
        $result = Invoke-EngineScript -Label 'profile-gpu-loss' -Commands @(
            'editor.window ###Editor.GamePreview open'
            'render.live.wait 300'
            'render.live.wait 400'
            'render.live.wait 400'
            'render.live.wait 400'
            'profile.record'
            'render.live.wait 120'
            'render.live.wait 120'
            'render.live.wait 120'
            'profile.pause'
            'dx12.live status'
            'profile.stats'
            'quit'
        )
    }
    finally {
        $env:CREATOR_DX12_GPU_QUERY_LIMIT = $previousLimit
    }

    $liveLine = $result.Combined -split "`n" |
        Where-Object { $_ -match '"command"\s*:\s*"dx12\.live"' } |
        Select-Object -Last 1
    $statsLine = $result.Combined -split "`n" |
        Where-Object { $_ -match '"command"\s*:\s*"profile\.stats"' } |
        Select-Object -Last 1
    if (-not $liveLine -or -not $statsLine) {
        Write-Host "  실패           GPU 손실 장부 응답이 없다: $($result.OutFile)" -ForegroundColor Red
        return 1
    }
    $live = $liveLine.Trim() | ConvertFrom-Json
    $stats = $statsLine.Trim() | ConvertFrom-Json
    $gpu = $live.data.gpu
    $d = $stats.data

    Write-Host ""
    Write-Host "[profile.gpu-loss] 질의 슬롯 부족 자극"
    Write-Host ("  수집           {0} · 실패 {1} · 렌더러 누락 {2}" -f
        $gpu.collects, $gpu.mismatches, $gpu.queryOverflowPasses)
    Write-Host ("  Collector      누락 {0} · 수집 실패 {1} · 마지막 프레임 {2} · 이유 {3}" -f
        $d.gpuQueryOverflowPasses, $d.gpuCollectFailures,
        $d.gpuIssueLastFrame, $d.gpuIssueLastError)

    $failures = New-Object System.Collections.Generic.List[string]
    if ($result.Combined -notmatch '\[GPU profiler\] query capacity 16') {
        $failures.Add('Debug 질의 슬롯 16개 자극이 적용되지 않았다')
    }
    if ($gpu.collects -le 0 -or $gpu.queryOverflowPasses -le 0) {
        $failures.Add('렌더러가 질의 초과를 관측하지 못했다')
    }
    if ($d.gpuQueryOverflowPasses -le 0 -or $d.gpuIssueLastFrame -le 0 -or
        $d.gpuIssueLastError -notmatch 'GPU query slots exhausted') {
        $failures.Add('녹화 Collector에 누락 건수·프레임·이유가 전달되지 않았다')
    }
    if ($gpu.mismatches -ne 0 -or $d.gpuCollectFailures -ne 0) {
        $failures.Add('질의 초과를 수집 실패로 잘못 계상했다')
    }
    if ($result.ExitCode -ne 0) { $failures.Add("종료 코드 $($result.ExitCode)") }
    if ($failures.Count -eq 0) {
        Write-Host '  결과           통과' -ForegroundColor Green
        return 0
    }
    foreach ($failure in $failures) {
        Write-Host "  실패           $failure" -ForegroundColor Red
    }
    Write-Host "  전체 출력      $($result.OutFile)"
    return 1
}

function Invoke-Providers {
    $result = Invoke-EngineScript -Label "profile-providers" -Commands @(
        "render.live.wait 300"
        "profile.counter-mask resources off"
        "profile.record"
        "play"
        "wait 12"
        "profile.frame"
        "profile.counter-mask resources on"
        "wait 12"
        "gc.collect"
        "wait 3"
        "profile.frame"
        "profile.stats"
        "quit"
    )
    $lines = @($result.Combined -split "`n" | Where-Object { $_ -match '"command"\s*:\s*"profile\.frame"' })
    $collectLine = $result.Combined -split "`n" |
        Where-Object { $_ -match '"command"\s*:\s*"gc\.collect"' } | Select-Object -First 1
    $playLine = $result.Combined -split "`n" |
        Where-Object { $_ -match '"command"\s*:\s*"play"' } | Select-Object -First 1
    $failures = New-Object System.Collections.Generic.List[string]
    if ($lines.Count -ne 2 -or -not $collectLine -or -not $playLine) {
        Write-Host "  실패           제공자 검증 응답이 부족하다: $($result.OutFile)" -ForegroundColor Red
        return 1
    }
    try {
        $off = $lines[0].Trim() | ConvertFrom-Json
        $on = $lines[1].Trim() | ConvertFrom-Json
        $collect = $collectLine.Trim() | ConvertFrom-Json
        $play = $playLine.Trim() | ConvertFrom-Json
    }
    catch {
        Write-Host "  실패           제공자 응답 JSON 오류: $_" -ForegroundColor Red
        return 1
    }
    $offCoverage = @($off.data.counterCoverage)
    $onCoverage = @($on.data.counterCoverage)
    $offGen2 = @($offCoverage | Where-Object { $_.id -eq 14 } | Select-Object -First 1)
    $baselineGen2 = if ($offGen2.Count) { [double]$offGen2[0].lastValue } else { -1.0 }
    $gcFrame = @($on.data.frames | Where-Object {
        $frame = $_
        $gc = @($frame.counters | Where-Object { $_.id -eq 14 -and $_.value -gt $baselineGen2 })
        $markers = @($frame.threads | ForEach-Object { $_.events } |
            Where-Object { $_.name -eq 'ScriptCore.PrePhysicsTick' -or $_.name -eq 'ScriptCore.PostPhysicsTick' })
        $gc.Count -gt 0 -and $markers.Count -gt 0
    } | Select-Object -First 1)
    if ($play.status -ne 'succeeded' -or $collect.status -ne 'succeeded') {
        $failures.Add("Play/GC 자극 실패: $($play.status)/$($collect.status)")
    }
    if (@($offCoverage | Where-Object { $_.id -in 18,19,20,21,25 }).Count -ne 0) {
        $failures.Add('Resource 모듈 off 동안 값이 수집됐다')
    }
    foreach ($id in @(18,19,20,21,25)) {
        if (@($onCoverage | Where-Object { $_.id -eq $id -and $_.samples -gt 0 }).Count -eq 0) {
            $failures.Add("Resource 모듈 on 뒤 counter $id 가 없다")
        }
    }
    if ($gcFrame.Count -eq 0) {
        $failures.Add('강제 GC 뒤 증가한 Gen2 값과 ScriptCore 마커가 같은 프레임에 없다')
    }
    if (@($on.data.counterDescriptors | Where-Object { $_.id -eq 25 -and $_.name -eq 'Resource provider cost' }).Count -eq 0) {
        $failures.Add('캡처가 Resource 제공자 어휘를 보존하지 않았다')
    }
    if ($off.data.droppedCounters -ne 0 -or $on.data.droppedCounters -ne 0) {
        $failures.Add("제공자 캡처에서 counter 누락: off=$($off.data.droppedCounters), on=$($on.data.droppedCounters)")
    }
    if ($result.ExitCode -ne 0) { $failures.Add("종료 코드 $($result.ExitCode)") }
    Write-Host "[profile.providers] Resource off/on · GC/스크립트 프레임"
    Write-Host ("  Resource 표본  off={0} on={1} · GC 동시 프레임={2}" -f
        @($offCoverage | Where-Object { $_.id -eq 25 }).Count,
        @($onCoverage | Where-Object { $_.id -eq 25 }).Count,
        $(if ($gcFrame.Count) { $gcFrame[0].frame } else { '없음' }))
    if ($failures.Count -eq 0) {
        Write-Host '  결과           통과' -ForegroundColor Green
        return 0
    }
    foreach ($failure in $failures) { Write-Host "  실패           $failure" -ForegroundColor Red }
    Write-Host "  전체 출력      $($result.OutFile)"
    return 1
}

function Invoke-Memory {
    $model = Join-Path $repoRoot "Dynamic_CPP\Assets\Models\Prim_Cube.glb"
    if (-not (Test-Path $model)) {
        Write-Host "[memory] 모델 fixture 가 없다: $model" -ForegroundColor Red
        return 1
    }
    $modelPath = ($model -replace '\\', '/')
    $result = Invoke-EngineScript -Label "memory-snapshots" -Commands @(
        "model.loadcached $modelPath"
        "wait $WarmupFrames"
        "memory.capture"
        "wait 4"
        "memory.snapshot"
        "memory.capture"
        "wait 4"
        "memory.snapshot"
        "quit"
    )
    $lines = @($result.Combined -split "`n" |
        Where-Object { $_ -match '"command"\s*:\s*"memory\.snapshot"' })
    $modelLine = $result.Combined -split "`n" |
        Where-Object { $_ -match '"command"\s*:\s*"model\.loadcached"' } | Select-Object -First 1
    if ($lines.Count -ne 2) {
        Write-Host "[memory] 스냅샷 응답 2개가 없다: $($result.OutFile)" -ForegroundColor Red
        return 1
    }
    try { $first = $lines[0].Trim() | ConvertFrom-Json; $second = $lines[1].Trim() | ConvertFrom-Json }
    catch {
        Write-Host "[memory] JSON 오류: $_" -ForegroundColor Red
        return 1
    }
    $failures = New-Object System.Collections.Generic.List[string]
    try { $modelResult = $modelLine.Trim() | ConvertFrom-Json }
    catch { $modelResult = $null }
    if (-not $modelResult -or $modelResult.status -ne 'succeeded') {
        $failures.Add('모델 fixture 로드 실패')
    }
    foreach ($snapshot in @($first, $second)) {
        if ($snapshot.status -ne 'succeeded' -or -not $snapshot.data.available -or $snapshot.data.pending) {
            $failures.Add('스냅샷이 다음 GameThread 경계에서 완성되지 않았다')
        }
        if (-not $snapshot.data.processValid -or $snapshot.data.workingSetBytes -le 0 -or
            $snapshot.data.privateCommitBytes -le 0) {
            $failures.Add('OS 프로세스 메모리 표본이 없다')
        }
        if (-not $snapshot.data.crtHeapValid -or $snapshot.data.crtLiveBytes -le 0 -or
            $snapshot.data.crtLiveBlocks -le 0) {
            $failures.Add('Debug CRT live heap 표본이 없다')
        }
        if ($snapshot.data.regions -le 0 -or $snapshot.data.committedPrivateBytes -le 0) {
            $failures.Add('가상 메모리 영역 표본이 없다')
        }
        if ($snapshot.data.objects -le 0) { $failures.Add('씬을 연 뒤 자산 객체가 없다') }
        if ($snapshot.data.captureMs -le 0) { $failures.Add('수집 비용 표본이 없다') }
    }
    if ($second.data.serial -le $first.data.serial -or $second.data.frame -lt $first.data.frame) {
        $failures.Add('두 스냅샷의 순서가 어긋났다')
    }
    if ($result.ExitCode -ne 0) { $failures.Add("종료 코드 $($result.ExitCode)") }
    Write-Host ("[memory] frame {0} → {1} · 객체 {2} → {3} · 영역 {4} → {5} · 수집 {6:N2}/{7:N2} ms" -f
        $first.data.frame, $second.data.frame, $first.data.objects, $second.data.objects,
        $first.data.regions, $second.data.regions, $first.data.captureMs, $second.data.captureMs)
    if ($failures.Count -eq 0) {
        Write-Host '  결과           통과' -ForegroundColor Green
        return 0
    }
    foreach ($failure in $failures) { Write-Host "  실패           $failure" -ForegroundColor Red }
    Write-Host "  전체 출력      $($result.OutFile)"
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
    Write-Host ("  카운터 누락     {0}" -f $d.droppedCounters)
    Write-Host ("  불균형 스코프   {0}" -f $d.malformedScopes)
    Write-Host ("  잘못된 페이지   {0}" -f $d.malformedPages)
    Write-Host ("  수집 스레드     tid={0} / 대기 프레임 {1} / 버린 경계 {2}" -f
        $d.collectorThreadId, $d.collectorQueuedFrames, $d.collectorDroppedFrames)
    $collectorMs = 1000.0 / [double]$d.ticksPerSecond
    Write-Host ("  수집 비용       페이지 {0:N3} / 프레임 {1:N3} / 캡처 {2:N3} ms" -f
        ($d.collectorPageIngestTicks * $collectorMs),
        ($d.collectorFrameCloseTicks * $collectorMs),
        ($d.collectorSnapshotTicks * $collectorMs))
    Write-Host ("  대기/지연       신호 {0:N3} / 제출 큐 {1:N3} ms" -f
        ($d.collectorWaitTicks * $collectorMs),
        ($d.collectorQueueDelayTicks * $collectorMs))
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
    if ($d.malformedPages -ne 0)      { $failures.Add("잘못된 수집 페이지 $($d.malformedPages)") }
    if ($d.freeChunks -le 0)          { $failures.Add("청크 풀이 고갈됐다 - 수집이 막히고 있다") }
    if ($d.collectorThreadId -le 0)   { $failures.Add("전용 수집 스레드 ID 가 없다") }
    $gameThread = $d.threads | Where-Object { $_.name -eq '[GameThread]' } | Select-Object -First 1
    if ($gameThread -and $d.collectorThreadId -eq $gameThread.threadId) {
        $failures.Add("수집기가 GameThread 에서 돌고 있다")
    }

    if ($d.collectorQueuedFrames -gt 256) { $failures.Add("수집 대기 프레임 상한을 넘었다") }
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
    "GpuLoss" { exit (Invoke-GpuLoss) }
    "Providers" { exit (Invoke-Providers) }
    "Memory" { exit (Invoke-Memory) }
}
