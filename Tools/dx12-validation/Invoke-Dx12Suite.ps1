#Requires -Version 7
<#
.SYNOPSIS
    dx12.* 자가 검증 전수 스윕. 판정을 CSV 로 남기고 기준선과 대조한다.

.DESCRIPTION
    RhiBoundaryPlan §7.2.7 이 지적한 것을 닫는 스크립트다 — 이 하네스가
    저장소에 없어서 세션마다 새로 짰고, 그때마다 자가 달라져 같은 부류의 오독이
    세 번 났다:

      ① 검사 목록을 CLI 도움말에서 뽑아 35 중 26 만 돌았다(도움말은 손으로
         유지하는 것이라 코드보다 낡는다).
      ② 판정 어휘를 통과/실패 둘로만 읽어 계측 검사(`완료`)를 실패로 셌다.
      ③ 워밍업 없이 불러 dx12.gizmoscene 이 점등 0 으로 실패했다 — 그 검사는
         에디터 씬의 살아 있는 카메라를 쓴다(§6.2).

    그래서 이 스크립트는 셋을 구조로 막는다:

      · 목록을 **소스**(ConsoleCommandSystem.cpp 의 `cmd == "dx12.*"`)에서 뽑는다.
      · 판정은 `[CLI] <검사> <통과|실패|완료>` 형태로만 읽는다. 진행 마커
        (예: `[dx12.scene] [1/4] 씬 입력 확보 완료`)에 '완료'가 들어 있어서,
        문자열 검색으로 세면 그것이 판정으로 잡힌다.
      · 워밍업 프레임을 기본으로 준다.

    검사당 프로세스를 하나씩 쓴다. 한 프로세스에 몰면 어서션 모달 하나가
    뒤의 검사를 통째로 막고, 그때 로그에는 아무것도 안 남는다(R2b 에서 25분을
    먹은 함정).

.PARAMETER WarmupFrames
    검사 앞에 돌릴 프레임 수. 0 이면 워밍업 없음.
    ★ 기준선(§7.4 의 28 통과)은 워밍업을 갖춘 값이다. 0 으로 재면 27 이 나오고
      그것은 회귀가 아니라 다른 자다.

.PARAMETER Baseline
    이전 실행의 verdicts.csv 경로. 주면 판정 줄을 대조해 차이만 출력한다.

.EXAMPLE
    pwsh -File Tools/dx12-validation/Invoke-Dx12Suite.ps1 -OutDir artifacts/suite-after
    pwsh -File Tools/dx12-validation/Invoke-Dx12Suite.ps1 -OutDir artifacts/suite-after -Baseline artifacts/suite-before/verdicts.csv

.NOTES
    반드시 pwsh(7+)로 실행한다. Windows PowerShell 5.1 은 이 파일의 한글을
    잘못 읽어 파싱이 무너진다.
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory)]
    [string]$OutDir,

    [string]$Exe = "",
    [string]$CommandSource = "",
    [int]$WarmupFrames = 240,
    [int]$TimeoutSec = 300,
    [string[]]$Only = @(),
    [string]$Baseline = ""
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$repoRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
if ([string]::IsNullOrWhiteSpace($Exe)) {
    $Exe = Join-Path $repoRoot "x64\Debug\CreatorEditor.exe"
}
if ([string]::IsNullOrWhiteSpace($CommandSource)) {
    $CommandSource = Join-Path $repoRoot "EngineEntry\ConsoleCommandSystem.cpp"
}
if (-not (Test-Path -LiteralPath $Exe -PathType Leaf)) {
    throw "실행 파일이 없다: $Exe (Debug x64 를 먼저 빌드한다)"
}
if (-not (Test-Path -LiteralPath $CommandSource -PathType Leaf)) {
    throw "명령 소스가 없다: $CommandSource"
}

New-Item -ItemType Directory -Force -Path $OutDir | Out-Null
$OutDir = [IO.Path]::GetFullPath($OutDir)

# ── 검사 목록: 도움말이 아니라 소스에서 ──
#
# ★ 등록 **형태**를 파싱하지 않는다 (2026-08-20 교정).
#   원래는 `cmd == "dx12.x"` 를 찾았는데, 명령 등록이 if-else 체인에서
#   `reg({ "dx12.x" }, &Cmd_...)` 등록 테이블로 바뀌면서 그 패턴이 소스에서
#   사라졌다 — 이 스크립트는 35종을 0종으로 읽었다. 도움말 대신 소스를 보게
#   만들어 막으려던 바로 그 부패가 소스 쪽에서 재발한 것이다.
#   그래서 지금은 형태와 무관하게 `"dx12.<이름>"` **문자열 리터럴 전부**를 뽑는다.
#   교정 시점 실측: 리터럴 전수 35종 = reg 등록 35종, 차집합 0 (오탐 없음).
#
# @() 로 감싼다 — 결과가 하나면 파이프라인이 스칼라를 돌려주어 .Count 가 없다.
$source = Get-Content -LiteralPath $CommandSource -Raw
$tests = @([regex]::Matches($source, '"(dx12\.[a-z0-9]+)"') |
    ForEach-Object { $_.Groups[1].Value } |
    Sort-Object -Unique)
if ($Only.Count -gt 0) {
    $tests = @($tests | Where-Object { $Only -contains $_ })
}
if ($tests.Count -eq 0) { throw "검사를 하나도 못 찾았다" }
Write-Host "검사 $($tests.Count)종 · 워밍업 $WarmupFrames 프레임 · 출력 $OutDir"

$rows = @()
foreach ($name in $tests) {
    $cmdFile = Join-Path $OutDir "$name.commands.txt"
    $outFile = Join-Path $OutDir "$name.out.txt"
    $errFile = Join-Path $OutDir "$name.err.txt"

    $commands = @()
    if ($WarmupFrames -gt 0) { $commands += "wait $WarmupFrames" }
    $commands += @($name, "wait 10", "quit")
    Set-Content -LiteralPath $cmdFile -Value $commands -Encoding UTF8

    $process = Start-Process -FilePath $Exe -ArgumentList "--script", "`"$cmdFile`"" `
        -PassThru -NoNewWindow -RedirectStandardOutput $outFile -RedirectStandardError $errFile

    if (-not $process.WaitForExit($TimeoutSec * 1000)) {
        try { $process.Kill($true) } catch {}
        $rows += [pscustomobject]@{
            Test = $name; Verdict = "시간초과"; Exit = ""; ErrBytes = 0; Line = ""
        }
        Write-Host ("  {0,-22} 시간초과" -f $name)
        continue
    }

    $out = if (Test-Path -LiteralPath $outFile) { Get-Content -LiteralPath $outFile -Raw } else { "" }
    $errBytes = if (Test-Path -LiteralPath $errFile) { (Get-Item -LiteralPath $errFile).Length } else { 0 }

    # 판정 줄은 이 형태 하나뿐이다. 진행 마커를 세지 않으려고 앵커를 건다.
    $matches = [regex]::Matches($out, "^\[CLI\] $([regex]::Escape($name)) (통과|실패|완료).*$", "Multiline")
    if ($matches.Count -gt 0) {
        $last = $matches[$matches.Count - 1]
        $line = $last.Value.Trim()
        $verdict = $last.Groups[1].Value
    } else {
        $line = ""
        # 어서션은 stderr 로 나온다(비대화형에서 모달을 끈 뒤로 그 바이트 수가 신호다).
        $verdict = if ($errBytes -gt 0) { "어서션" } else { "무판정" }
    }

    $rows += [pscustomobject]@{
        Test = $name; Verdict = $verdict; Exit = $process.ExitCode
        ErrBytes = $errBytes; Line = $line
    }
    Write-Host ("  {0,-22} {1}" -f $name, $verdict)
}

$verdictPath = Join-Path $OutDir "verdicts.csv"
$rows | Export-Csv -LiteralPath $verdictPath -NoTypeInformation -Encoding UTF8

Write-Host ""
$rows | Format-Table Test, Verdict, ErrBytes, Line -AutoSize | Out-String -Width 200 | Write-Host
$summary = $rows | Group-Object Verdict | Sort-Object Name |
    ForEach-Object { "$($_.Name)=$($_.Count)" }
Write-Host ("집계: " + ($summary -join " · ") + "  → $verdictPath")

if (-not [string]::IsNullOrWhiteSpace($Baseline)) {
    if (-not (Test-Path -LiteralPath $Baseline -PathType Leaf)) {
        throw "기준선 CSV 가 없다: $Baseline"
    }
    $before = Import-Csv -LiteralPath $Baseline
    # 계측 검사(*scale·bench)는 시간이 매번 다르므로 판정과 검사 이름만 견준다.
    # 수치 대조가 필요하면 out.txt 를 직접 Compare-Object 한다.
    $diff = Compare-Object $before $rows -Property Test, Verdict, Line
    Write-Host ""
    if ($null -eq $diff) {
        Write-Host "기준선 대조: 판정 줄 차이 0"
    } else {
        Write-Host "기준선 대조: 차이 있음"
        $diff | Format-Table -AutoSize | Out-String -Width 200 | Write-Host
        exit 1
    }
}
