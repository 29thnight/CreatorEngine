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

      · 목록을 **엔진의 런타임 discovery**(`--commandlet list`)에서 뽑는다.
        2026-09-04 이전에는 C++ 소스에서 뽑았는데, 그것도 두 번 틀렸다 —
        등록이 if-else 체인에서 등록 표로 바뀌자 35종을 0종으로 읽었고,
        문자열 리터럴 전수로 바꾼 뒤에도 "소스가 이렇게 생겼다"는 가정이
        남아 있었다. 이제 정본은 프로그램 안에 있다(PHASE 14.5 LC3).
      · 판정을 **사람용 출력에서 읽지 않는다**(2026-09-05, PHASE 14.5 LC9).
        예전에는 `[CLI] <검사> <통과|실패|완료>` 를 정규식으로 긁었다. 진행
        마커(예: `[dx12.scene] [1/4] 씬 입력 확보 완료`)에 '완료'가 들어 있어
        앵커를 걸어야 했고, 어휘가 셋이라는 사실도 그 셋이 언제 바뀌는지도
        이 스크립트는 알 방법이 없었다 — 위 ②가 정확히 그 오독이다.
        이제 엔진이 `--result-format jsonl` 로 terminal 결과를 내고, 그 봉투는
        HTTP 응답과 같은 함수가 만든다(§18 의 schema v1 공유). text fallback 은
        두지 않는다 — 이중 parser 는 새 drift 를 만든다.
      · 검사 앞에서 **라이브 렌더러가 준비될 때까지** 기다린다(`render.live.wait`).

    검사당 프로세스를 하나씩 쓴다. 한 프로세스에 몰면 어서션 모달 하나가
    뒤의 검사를 통째로 막고, 그때 로그에는 아무것도 안 남는다(R2b 에서 25분을
    먹은 함정).

.PARAMETER RenderWaitTimeoutSec
    예열 상한(초). 검사 앞에 `render.live.wait <초>` 를 둔다 — 명령 시점 뒤에
    발행된 프레임을 렌더 스레드가 **끝낼** 때까지 게임 스레드를 세우지 않고 다음
    명령을 미룬다(EnhancedRenderThreadStats::completedFrameId). 상한을 넘기면 그
    검사는 `예열 실패` 로 적히고 검사 자신의 결과로 세지 않는다.
    ★ 2026-09-17 — 프레임 수 예열(`wait 2000`)을 이것으로 바꿨다. 라이브 렌더러의
      첫 프레임은 GBuffer ShaderMeta 반영(slang reflect)에 **Release 에서도 약
      22~24초**를 쓴다. Release 의 2000 프레임은 몇 초 만에 지나가, 같은 결함이
      verify-model-scene-consumption 의 dx12.scene 을 `pending 2 · active 1` 로
      넘어뜨렸다. 아래 WarmupFrames 의 이력이 그 앞 단계다.
    ★ 실측(Release, 28종): 예열 17.2~21.7초(중앙 17.5초). 판정은 `wait 2000` 회차와
      줄 차이 0 이었지만 dx12.scene 의 **사유**가 'RenderThread drain 시간 초과' 에서
      '드로우가 0건이다'(부팅 씬에 메시 없음 — 계획서 기준선의 설계 실패)로 바뀌었다.
      Code 가 둘 다 rendertest.failed 라 -Baseline 대조는 이 차이를 못 본다 —
      사유는 <검사>.out.txt 에서 읽어라.

.PARAMETER NoRenderWait
    준비 대기를 끈다. 워밍업이 없는 자(아래 ③)를 일부러 재고 싶을 때만 쓴다.

.PARAMETER WarmupFrames
    준비 대기 **뒤에** 더 돌릴 프레임 수. 기본 0.
    ★ 기준선(§7.4 의 28 통과)은 워밍업을 갖춘 값이다. 0 으로 재면 27 이 나오고
      그것은 회귀가 아니라 다른 자다.
    ★ 2000 → 0 (2026-09-17). 준비를 프레임 수로 재지 않게 되었으므로 기본을
      내렸다. 준비 뒤에도 프레임이 더 필요한 검사가 드러나면 그때 올린다.
    ★ 240 → 2000 (2026-09-14). 이 수가 재야 하는 것은 프레임 수가 아니라
      **렌더러가 준비되기까지의 시간**이다. 라이브 렌더러의 첫 프레임은
      파이프라인 구축과 ShaderMeta 적용 때문에 Debug 에서 13~28초가 걸린다.
      240 이 그동안 통했던 이유는 BuildPipeline 이 표시 락을 구축 내내 쥐어
      게임 스레드까지 멈춰 세웠기 때문이다 — 240 프레임이 11.8초 걸렸다.
      그 정지를 걷어내자(같은 날 락 축소) 240 프레임이 3.7초에 지나가고
      dx12.scene 이 RenderThread drain 시간 초과로 넘어졌다. 2000 프레임은
      28초로, 첫 프레임을 덮는다. 프레임 수가 시간의 대리 지표라는 사실이
      여기 적혀 있어야 다음에 또 짧게 잡지 않는다.

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
    [string]$TexturePath = (Join-Path $PSScriptRoot "../../Dynamic_CPP/Assets/Materials/Cube_Mat_BaseColor.png"),
    [int]$RenderWaitTimeoutSec = 180,
    [switch]$NoRenderWait,
    [int]$WarmupFrames = 0,
    [int]$TimeoutSec = 300,
    [string[]]$Only = @(),
    [string]$Baseline = ""
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$repoRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
if ([string]::IsNullOrWhiteSpace($Exe)) {
    $Exe = Join-Path $repoRoot "Bin\x64-Debug\Editor\CreatorEditor.exe"
}
if (-not (Test-Path -LiteralPath $Exe -PathType Leaf)) {
    throw "실행 파일이 없다: $Exe (Debug x64 를 먼저 빌드한다)"
}
New-Item -ItemType Directory -Force -Path $OutDir | Out-Null
$OutDir = [IO.Path]::GetFullPath($OutDir)

# ── 검사 목록: 런타임 discovery 에서 ──
#
# ★ 소스 스크래핑을 은퇴한다 (2026-09-04, PHASE 14.5 LC3).
#
#   이 자리는 두 번 틀렸다. 처음에는 도움말을 읽다가 낡은 help 때문에 35종 중
#   26종만 돌렸고, 소스를 읽게 바꿨더니 이번에는 등록 **형태**가 if-else 체인에서
#   등록 표로 바뀌면서 35종을 **0종**으로 읽었다. 형태와 무관하게 문자열 리터럴을
#   뽑는 것으로 또 한 번 막았지만, 그것도 "소스가 이렇게 생겼다"는 가정이다.
#
#   근본 원인은 **명령 목록의 정본이 프로그램 밖에 있었다**는 것이다. 이제
#   엔진이 `--commandlet list` 로 검증 registry 목록을 낸다 — 등록 형태가
#   어떻게 바뀌든 그 출력은 등록된 것 그대로다.
#
# @() 로 감싼다 — 결과가 하나면 파이프라인이 스칼라를 돌려주어 .Count 가 없다.
$discoveryPath = Join-Path $OutDir 'commandlets.jsonl'
if (Test-Path -LiteralPath $discoveryPath) { Remove-Item -LiteralPath $discoveryPath }
$discoverProc = Start-Process -FilePath $Exe -ArgumentList '--commandlet', 'list', '--', '--result-file', ('"'+$discoveryPath+'"') `
    -WorkingDirectory (Split-Path -Parent $Exe) -WindowStyle Hidden `
    -RedirectStandardOutput (Join-Path $OutDir '_discover.out') `
    -RedirectStandardError (Join-Path $OutDir '_discover.err') -PassThru
if (-not $discoverProc.WaitForExit(180000)) { $discoverProc.Kill(); throw 'Commandlet discovery timed out' }
if ($discoverProc.ExitCode -ne 0) { throw "Commandlet discovery failed: $($discoverProc.ExitCode)" }
$discovery = Get-Content -LiteralPath $discoveryPath | ConvertFrom-Json
if ($discovery.status -ne 'succeeded') { throw 'Commandlet discovery did not succeed' }
$tests = @($discovery.data.names | Where-Object { $_ -like 'dx12.*' } | Sort-Object -Unique)
if ($Only.Count -gt 0) {
    $missing = @($Only | Where-Object { $tests -notcontains $_ })
    if ($missing.Count) { throw "Unknown Commandlet(s): $($missing -join ', ')" }
    $tests = @($tests | Where-Object { $Only -contains $_ })
}
if ($tests.Count -eq 0) { throw "검사를 하나도 못 찾았다" }
$warmupLabel = if ($NoRenderWait) { '준비 대기 없음' } else { "render.live.wait(상한 $RenderWaitTimeoutSec 초)" }
Write-Host "검사 $($tests.Count)종 · 예열 $warmupLabel · 추가 $WarmupFrames 프레임 · 출력 $OutDir"

$rows = @()
foreach ($name in $tests) {
    $cmdFile = Join-Path $OutDir "$name.commands.txt"
    $outFile = Join-Path $OutDir "$name.out.txt"
    $errFile = Join-Path $OutDir "$name.err.txt"

    $resultFile = Join-Path $OutDir "$name.result.jsonl"
    if (Test-Path -LiteralPath $resultFile) { Remove-Item -LiteralPath $resultFile -Force }

    $commands = @()
    if (-not $NoRenderWait) { $commands += "render.live.wait $RenderWaitTimeoutSec" }
    if ($WarmupFrames -gt 0) { $commands += "wait $WarmupFrames" }
    $invocation = if ($name -eq "dx12.selftest") { "$name `"$([IO.Path]::GetFullPath($TexturePath))`"" } else { $name }
    $commands += @($invocation, "wait 10", "quit")
    Set-Content -LiteralPath $cmdFile -Value $commands -Encoding UTF8

    # ── LC9: 판정을 사람용 출력이 아니라 결과 스트림에서 읽는다 (§18) ────
    #
    # ★ 예전에는 `^\[CLI\] <검사> (통과|실패|완료)` 를 정규식으로 긁었다.
    #
    #   그 방식이 이 파일에서만 두 번 틀렸다(머리말 ①②) — 목록을 소스에서 뽑다
    #   26/35 만 돌렸고, 판정 어휘를 둘로만 읽어 계측 검사(`완료`)를 실패로 셌다.
    #   어휘가 셋이라는 것도, 그 셋이 언제 바뀌는지도 이 스크립트는 알 방법이
    #   없었다. **사람이 읽으라고 쓴 문자열을 기계가 판정에 쓰는 한 그 종류의
    #   오류는 계속 새로 생긴다.**
    #
    #   이제 엔진이 `--result-format jsonl` 로 terminal 결과를 낸다. 그 봉투는
    #   HTTP 응답과 **같은 함수**가 만들고(§18 의 schema v1 공유), 어휘는
    #   `CommandStatus` 라 한국어가 아니다.
    #
    # ★★ text fallback 을 두지 않는다. 계획이 이름으로 금지한다 — "이관된
    #   consumer 는 text fallback 을 유지하지 않는다. 이중 parser 는 새 drift 를
    #   만든다." 결과 줄이 없으면 그것은 **무판정**이고, 무판정은 통과가 아니다.
    $process = Start-Process -FilePath $Exe `
        -ArgumentList "--commandlet-script", "`"$cmdFile`"", "--result-format", "jsonl", "--result-file", "`"$resultFile`"" `
        -WindowStyle Hidden `
        -PassThru -RedirectStandardOutput $outFile -RedirectStandardError $errFile

    if (-not $process.WaitForExit($TimeoutSec * 1000)) {
        try { $process.Kill($true) } catch {}
        $rows += [pscustomobject]@{
            Test = $name; Verdict = "시간초과"; Exit = ""; ErrBytes = 0; Status = ""; Code = ""
        }
        Write-Host ("  {0,-22} 시간초과" -f $name)
        continue
    }

    $errBytes = if (Test-Path -LiteralPath $errFile) { (Get-Item -LiteralPath $errFile).Length } else { 0 }

    # 이 검사의 줄만 고른다. 시나리오에는 `wait`·`quit` 도 함께 들어 있다.
    $record = $null
    $warmup = $null
    if (Test-Path -LiteralPath $resultFile) {
        foreach ($line in [IO.File]::ReadAllLines($resultFile)) {
            if ([string]::IsNullOrWhiteSpace($line)) { continue }
            try { $parsed = $line | ConvertFrom-Json } catch { continue }
            if ($parsed.command -eq $name) { $record = $parsed }
            if ($parsed.command -eq 'render.live.wait') { $warmup = $parsed }
        }
    }
    # 예열이 서지 않은 검사는 준비되지 않은 렌더러를 잰 것이다 — 그 판정을 검사의
    # 결과로 세지 않는다. 대기 시간은 첫 프레임 비용의 실측이라 CSV 에 남긴다.
    $warmupMs = ''
    $warmupFailed = $false
    if (-not $NoRenderWait) {
        if ($null -eq $warmup -or $warmup.status -ne 'succeeded') { $warmupFailed = $true }
        elseif ($null -ne $warmup.data.PSObject.Properties['waitedMs']) { $warmupMs = [int]$warmup.data.waitedMs }
    }

    if ($null -ne $record) {
        $status  = $record.status
        $code    = $record.code
        # ★ `legacy_unreported` 를 성공으로 세지 않는다.
        #
        #   아직 결과를 내지 않는 핸들러(LC1 이행 전)는 이 상태로 온다. 그것을
        #   "통과" 로 접으면 이행이 끝났는지 아닌지가 집계에서 사라진다 —
        #   §18 의 "모든 command 가 정확히 하나의 terminal CommandResult 를
        #   만든다" 가 얼마나 남았는지를 이 표가 그대로 보여야 한다.
        $verdict = switch ($status) {
            'succeeded'          { '통과' }
            'legacy_unreported'  { '무판정(legacy)' }
            default              { $status }
        }
    } else {
        $status = ''
        $code   = ''
        # 어서션은 stderr 로 나온다(비대화형에서 모달을 끈 뒤로 그 바이트 수가 신호다).
        $verdict = if ($errBytes -gt 0) { "어서션" } else { "무판정" }
    }

    if ($warmupFailed) {
        $warmupStatus = if ($null -eq $warmup) { '결과 없음' } else { "$($warmup.status)/$($warmup.code)" }
        $verdict = "예열 실패($warmupStatus)"
        $status = 'warmup_failed'
    }

    $rows += [pscustomobject]@{
        Test = $name; Verdict = $verdict; Exit = $process.ExitCode
        ErrBytes = $errBytes; Status = $status; Code = $code; WarmupMs = $warmupMs
    }
    Write-Host ("  {0,-22} {1}  (예열 {2} ms)" -f $name, $verdict, $warmupMs)
}

$verdictPath = Join-Path $OutDir "verdicts.csv"
$rows | Export-Csv -LiteralPath $verdictPath -NoTypeInformation -Encoding UTF8

Write-Host ""
$rows | Format-Table Test, Verdict, Status, Code, ErrBytes, WarmupMs -AutoSize | Out-String -Width 200 | Write-Host
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
    # LC9 — 대조 축을 `Line`(사람용 문자열)에서 `Status`/`Code`(계약 값)로 옮겼다.
    #   사람용 문안을 다듬는 것만으로 기준선이 붉어지던 자리다.
    $diff = Compare-Object $before $rows -Property Test, Verdict, Status, Code
    Write-Host ""
    if ($null -eq $diff) {
        Write-Host "기준선 대조: 판정 줄 차이 0"
    } else {
        Write-Host "기준선 대조: 차이 있음"
        $diff | Format-Table -AutoSize | Out-String -Width 200 | Write-Host
        exit 1
    }
}

# A failed/absent terminal record or a failed process must fail the consumer too.
# CSV collection alone used to return exit 0 even when the engine exited 5.
$failed = @($rows | Where-Object { $_.Status -ne 'succeeded' -or $_.Exit -ne 0 })
if ($failed.Count) { Write-Error "DX12 Commandlet failure: $($failed.Test -join ', ')"; exit 1 }
