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
    SelfTest  프로파일러 특성화 검사(기본)
    Stats     프로파일러 자체 비용만 출력(교란 없음)
    Build     Debug|x64 빌드만 수행

.EXAMPLE
    pwsh -NoProfile -File .\Tools\profiling-validation\Invoke-ProfilingValidation.ps1
    pwsh -NoProfile -File .\Tools\profiling-validation\Invoke-ProfilingValidation.ps1 -Action Stats
#>
[CmdletBinding()]
param(
    [ValidateSet("SelfTest", "Stats", "Build")]
    [string]$Action = "SelfTest",

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

function Invoke-SelfTest {
    $result = Invoke-EngineScript -Label "profile-selftest" -Commands @(
        "# PHASE 14 P0 — CPU 프로파일러 특성화 검사"
        "wait $WarmupFrames"
        # profile.stats 를 따로 부르지 않는다 — RunProfilerSelfTest 가 자기 리포트 끝에
        # GetProfilerStatsReport() 를 붙인다. 제품 명령이 JSON 으로 바뀐 뒤(9-06) 이 줄은
        # 아무 텍스트도 내지 않으면서 selftest 리포트에 실린 **교란된** 값이 그 자리를
        # 메워 왔다. 라이브 기준선은 -Action Stats 단독으로만 잰다.
        "profile.selftest"
        "quit"
    )

    # 검사 본문을 그대로 보여준다. 통과/실패보다 항목별 실측값이 쓸모 있다.
    $body = $result.Combined
    $start = $body.IndexOf("[profile.selftest]")
    if ($start -ge 0) {
        Write-Host ""
        Write-Host $body.Substring($start)
    }

    $ok = $body -match "PROFILE_SELFTEST_OK=true"
    $crashed = $body -match "미처리 예외"
    $exitOk = ($result.ExitCode -eq 0)

    Write-Host ""
    Write-Host "── 판정 ─────────────────────────────"
    Write-Host ("  종료 코드      {0}" -f $(if ($exitOk) { "0 (정상)" } else { "$($result.ExitCode) (비정상)" }))
    Write-Host ("  성공 마커      {0}" -f $(if ($ok) { "PROFILE_SELFTEST_OK=true" } else { "없음" }))
    Write-Host ("  크래시         {0}" -f $(if ($crashed) { "미처리 예외 발견" } else { "없음" }))
    Write-Host ("  전체 출력      {0}" -f $result.OutFile)

    if ($ok -and $exitOk -and -not $crashed) {
        Write-Host "  결과           통과" -ForegroundColor Green
        return 0
    }

    Write-Host "  결과           실패" -ForegroundColor Red
    return 1
}

function Invoke-Stats {
    $result = Invoke-EngineScript -Label "profile-stats" -Commands @(
        "wait $WarmupFrames"
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
    $tps = [double]$d.ticksPerSecond
    $toMs = if ($tps -gt 0) { 1000.0 / $tps } else { 0.0 }

    Write-Host ""
    Write-Host "[profile.stats] 라이브 기준선 (교란 없음)"
    Write-Host ("  상태            {0} / 보존 프레임 [{1}, {2})" -f $(if ($d.paused) { "일시정지" } else { "기록 중" }), $d.frameBegin, $d.frameEnd)
    Write-Host ("  Tick 비용       평균 {0:N1}us / 최대 {1:N1}us  ({2}회)" -f `
        $(if ($d.tickCount -gt 0) { $d.totalTickTicks * $toMs * 1000.0 / $d.tickCount } else { 0 }), `
        ($d.peakTickTicks * $toMs * 1000.0), $d.tickCount)
    Write-Host ("  이벤트/프레임   마지막 {0} / 최대 {1} / 상한 {2}" -f $d.lastFrameEvents, $d.peakFrameEvents, $d.eventCapacity)
    Write-Host ("  이름 바이트     마지막 {0} / 최대 {1} / 상한 {2}" -f $d.lastFrameNameBytes, $d.peakFrameNameBytes, $d.nameCapacity)
    Write-Host ("  누적 누락       이벤트 {0} / 이름 {1}" -f $d.totalDroppedEvents, $d.totalDroppedNames)
    Write-Host ("  불균형 스코프   {0}" -f $d.malformedScopes)
    Write-Host ("  스레드 슬롯     {0}개" -f $d.threads.Count)
    foreach ($t in $d.threads) {
        Write-Host ("    [{0}] {1,-24} tid={2}{3}" -f $t.index, $t.name, $t.threadId, $(if ($t.retired) { " (은퇴)" } else { "" }))
    }

    # ── 단정 ────────────────────────────────────────────────────────────────
    #
    # ⚠ 드롭·포화를 단정하지 않는다. 실측(9-15)에서 편집 27 / 재생 34 이벤트로
    #   상한 1024 의 3% 였고 이름도 525B/16384B 였다. 계측 지점 39곳이 전부 시스템
    #   단위 고정 지점이라 씬에 무엇을 얹어도 상수이므로, 그 단정은 **어떤 변이로도
    #   자극되지 않는 빈 단정**이다. 값은 위에 찍되 판정에서는 뺀다.
    #
    # ⚠ 이벤트 수의 **정확한 값**도 아직 단정하지 않는다. PHASE 14 임시 계측이
    #   들어가 있는 동안에는 값이 유동적이다. 임시 계측을 정리하고 새 수집 코어로
    #   넘어갈 때 정확값(편집/재생 두 축)을 여기에 못 박는다 — 계획서 §10 P1b.
    $failures = New-Object System.Collections.Generic.List[string]
    if ($json.status -ne 'succeeded') { $failures.Add("status=$($json.status)") }
    if ($d.frameEnd -le $d.frameBegin) { $failures.Add("보존 프레임이 비었다 [$($d.frameBegin), $($d.frameEnd))") }
    if ($d.lastFrameEvents -le 0)      { $failures.Add("이벤트가 0이다 — 계측이 통째로 죽었다") }
    if ($d.malformedScopes -ne 0)      { $failures.Add("불균형 스코프 $($d.malformedScopes) — Begin/End 짝이 깨졌다") }
    if (-not ($d.threads | Where-Object { $_.name -eq '[GameThread]' -and -not $_.retired })) {
        $failures.Add("[GameThread] 슬롯이 살아 있지 않다")
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
    "Build" { Invoke-Build; exit 0 }
    "Stats" { exit (Invoke-Stats) }
    "SelfTest" { exit (Invoke-SelfTest) }
}
