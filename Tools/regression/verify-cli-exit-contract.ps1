[CmdletBinding()]
param(
    [string]$Exe  = (Join-Path $PSScriptRoot "..\..\Bin\x64-Debug\Editor\CreatorEditor.exe"),
    [string]$Work = (Join-Path $env:TEMP "lc0"),

    # 관측값을 baseline에 다시 쓴다. LC1이 exit spine을 세운 뒤 한 번 쓴다.
    [switch]$UpdateBaseline
)

# LC0 (PHASE 14.5) — false-green canary.
#
# ── 이 검사는 "통과해야 할 계약"을 재지 않는다. "지금 무엇이 거짓 초록인가"를 잰다 ──
#
# EditorAutomationCLIPlan.md §3.1이 적은 결함은 이렇다: 명령이 실패를 **출력**해도
# 프로세스는 0으로 끝난다. session 결과 정본이 없고, Execute()는 unknown command에
# printf 한 줄만 내고 그냥 return한다. 그래서 오타 하나가 조용히 성공이다.
#
# 그 결함을 게이트로 바로 못 바꾸는 이유가 있다. 계약값(§5.4)을 지금 요구하면 이
# 검사는 첫날부터 붉고, 붉은 검사는 며칠이면 아무도 안 본다. 그래서 **관측값**을
# baseline에 적어 두고 관측이 그것과 달라질 때 붉어지게 한다.
#
#   · 오늘      — 관측 = baseline. 초록. 회귀 세트에 넣어도 세트가 붉지 않다.
#   · LC1 착수 후 — 관측이 계약값으로 바뀌면서 붉어진다. 그때 -UpdateBaseline로
#                   한 번 갱신하면 observed와 contract가 같아지고, 이 파일은
#                   그 시점부터 진짜 계약 게이트가 된다.
#   · 그 사이 누가 exit 처리를 되돌리면 — 역시 붉어진다.
#
# 즉 이것은 **한 방향으로만 도는 래칫**이다. 계획 §13 LC0의 "canary가 현 결함에
# 실제 발화"는 아래 요약이 `계약 위반 N건`을 0이 아닌 수로 찍는 것으로 확인한다.

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

if (-not (Test-Path -LiteralPath $Exe -PathType Leaf)) {
    "실행 파일이 없다: $Exe"
    exit 1
}
$exeDir = Split-Path -Parent $Exe
New-Item -ItemType Directory -Force -Path $Work | Out-Null

$baselinePath = Join-Path $PSScriptRoot 'cli_exit_contract.baseline.json'
if (-not (Test-Path -LiteralPath $baselinePath -PathType Leaf)) {
    "baseline이 없다: $baselinePath"
    exit 1
}
$baseline = Get-Content -LiteralPath $baselinePath -Raw | ConvertFrom-Json

function Invoke-Case {
    param([string]$Name, [string[]]$Commands)

    # quit을 반드시 붙인다. 무인 실행에서 quit 없는 시나리오는 하네스 타임아웃까지
    # 산다 — 그 상태는 "실패"가 아니라 "판정 불가"라 baseline에 적을 값이 없다.
    $scriptPath = Join-Path $Work "exit_contract_$Name.txt"
    (($Commands + 'quit') -join "`n") | Set-Content -LiteralPath $scriptPath -Encoding UTF8

    $outPath = Join-Path $Work "exit_contract_$Name.out"
    $errPath = Join-Path $Work "exit_contract_$Name.err"

    $proc = Start-Process -FilePath $Exe -ArgumentList '--script', $scriptPath `
        -WorkingDirectory $exeDir `
        -RedirectStandardOutput $outPath -RedirectStandardError $errPath -PassThru
    $proc.WaitForExit(180000) | Out-Null
    if (-not $proc.HasExited) {
        $proc.Kill()
        return [pscustomobject]@{ ExitCode = $null; Stdout = ''; TimedOut = $true }
    }

    $stdout = if (Test-Path -LiteralPath $outPath) { Get-Content -LiteralPath $outPath -Raw } else { '' }
    [pscustomobject]@{ ExitCode = $proc.ExitCode; Stdout = $stdout; TimedOut = $false }
}

$rows            = New-Object System.Collections.Generic.List[object]
$baselineDrift   = New-Object System.Collections.Generic.List[string]
$contractGap     = 0
$missingEvidence = New-Object System.Collections.Generic.List[string]

foreach ($case in $baseline.cases) {
    $result = Invoke-Case -Name $case.name -Commands $case.commands

    if ($result.TimedOut) {
        $baselineDrift.Add("$($case.name): 타임아웃 — 종료 코드를 관측하지 못했다")
        continue
    }

    # 종료 코드만 보면 "실패를 감지조차 못 한 것"과 "실패를 알리고도 0으로 끝난 것"이
    # 구분되지 않는다. 후자가 §3.1의 결함이므로, 실패 문안이 실제로 나왔는지도 본다.
    $sawEvidence = $result.Stdout -match [regex]::Escape($case.expectStdout)
    if (-not $sawEvidence) {
        $missingEvidence.Add("$($case.name): 기대한 문안('$($case.expectStdout)')이 출력에 없다")
    }

    if ($result.ExitCode -ne $case.observed) {
        $baselineDrift.Add(("{0}: 관측 {1} ≠ baseline {2}" -f $case.name, $result.ExitCode, $case.observed))
    }
    if ($case.observed -ne $case.contract) { $contractGap++ }

    $rows.Add([pscustomobject]@{
        Case     = $case.name
        Observed = $result.ExitCode
        Contract = $case.contract
        Evidence = if ($sawEvidence) { 'yes' } else { 'NO' }
        Verdict  = if ($result.ExitCode -eq $case.contract) { '계약 충족' } else { 'false-green' }
    })

    if ($UpdateBaseline) { $case.observed = $result.ExitCode }
}

$rows | Format-Table -AutoSize | Out-String | Write-Host

if ($UpdateBaseline) {
    $baseline | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $baselinePath -Encoding UTF8
    "baseline을 관측값으로 갱신했다: $baselinePath"
}

"계약 위반(false-green) $contractGap 건 / 전체 $($baseline.cases.Count) 건"

if ($missingEvidence.Count -gt 0) {
    "실패 문안이 관측되지 않은 항목:"
    $missingEvidence | ForEach-Object { "  - $_" }
}
if ($baselineDrift.Count -gt 0) {
    "baseline과 어긋난 항목:"
    $baselineDrift | ForEach-Object { "  - $_" }
    ""
    "종료 코드 거동이 바뀌었다. LC1(exit spine)을 착수한 결과라면"
    "  pwsh Tools/regression/verify-cli-exit-contract.ps1 -UpdateBaseline"
    "로 한 번 갱신하고, 그 diff를 커밋에 함께 남긴다."
    exit 1
}

# 문안이 안 나오는 것은 baseline 문제가 아니라 검사가 아무것도 안 본 것이다.
if ($missingEvidence.Count -gt 0) { exit 1 }

if ($contractGap -gt 0) {
    "현 결함이 그대로다(예상된 상태). LC1이 닫는다."
}
exit 0
