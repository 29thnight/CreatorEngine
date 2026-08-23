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
    $Exe = Join-Path $repoRoot "x64\Debug\CreatorEditor.exe"
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

# 엔진을 --script 로 무인 기동하고 stdout/stderr 를 합쳐 돌려준다.
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
    Write-Host "[run] $Label — $Exe --script $commandFile"

    $proc = Start-Process -FilePath $Exe -ArgumentList "--script", $commandFile `
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
        "profile.stats"
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

    $body = $result.Combined
    $start = $body.IndexOf("[profile.stats]")
    if ($start -lt 0) {
        Write-Host "프로파일러 통계를 출력하지 못했다. 전체 출력: $($result.OutFile)" -ForegroundColor Red
        return 1
    }

    Write-Host ""
    Write-Host $body.Substring($start)
    return $(if ($result.ExitCode -eq 0) { 0 } else { 1 })
}

switch ($Action) {
    "Build" { Invoke-Build; exit 0 }
    "Stats" { exit (Invoke-Stats) }
    "SelfTest" { exit (Invoke-SelfTest) }
}
