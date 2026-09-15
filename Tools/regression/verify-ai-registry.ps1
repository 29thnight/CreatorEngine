# AI 레지스트리 회귀 — 최초 등록 · DDOL Scene handle 재등록 · 파괴 해지.
#
# ★ 판정은 사람용 로그 줄이 아니라 명령 결과 계약(jsonl)으로 한다(2026-09-15).
#   예전에는 `[AI 레지스트리] object=... registered=1 total=1` printf 줄 세 개를 셌는데,
#   9-06 이후 ai.status 는 JSON 결과만 내서 이 게이트가 "status 줄 0개" 로 붉은 채
#   아홉 날을 보냈다. 로그 줄은 진단이고, 계약은 결과 파일이다.
param(
    [string]$Exe = (Join-Path $PSScriptRoot "..\..\Bin\x64-Debug\Editor\CreatorEditor.exe"),
    [string]$Work = $env:TEMP,
    [int]$TimeoutSeconds = 300
)

. (Join-Path $PSScriptRoot "CommandResults.ps1")

$exeDir = [System.IO.Path]::GetDirectoryName($Exe)
if (-not (Test-Path $Exe)) { "실행 파일이 없다: $Exe"; exit 1 }

$repoRoot = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
$scenePath = Join-Path $repoRoot "Dynamic_CPP\Assets\Scenes\E5AiRegistryDest.creator"
if (Test-Path $scenePath) { Remove-Item -LiteralPath $scenePath -Force }

$scenario = Join-Path $Work "ai_registry_resolved.txt"
(Get-Content (Join-Path $PSScriptRoot "ai_registry_probe.txt") -Raw) `
    -replace '\{\{SCENE_DEST\}\}', ($scenePath -replace '\\', '/') |
    Set-Content $scenario -Encoding UTF8

$outPath = Join-Path $Work "ai_registry.out"
$errPath = Join-Path $Work "ai_registry.err"
$resultPath = Join-Path $Work "ai_registry.results.jsonl"
if (Test-Path -LiteralPath $resultPath) { Remove-Item -LiteralPath $resultPath -Force }

# lifecycle.stress 는 commandlet 표에만 있으므로 --commandlet-script 가 필요하다.
$proc = Start-Process -FilePath $Exe `
    -ArgumentList @('--commandlet-script', ('"'+$scenario+'"'), '--result-format', 'jsonl', '--result-file', ('"'+$resultPath+'"')) `
    -WorkingDirectory $exeDir -RedirectStandardOutput $outPath `
    -RedirectStandardError $errPath -PassThru
$proc.WaitForExit($TimeoutSeconds * 1000) | Out-Null
if (-not $proc.HasExited) { $proc.Kill(); "TIMEOUT"; exit 1 }

$failed = @()
if ($proc.ExitCode -ne 0) { $failed += ("종료 코드 비정상: 0x{0:X8}" -f $proc.ExitCode) }

$results = @()
try { $results = @(Read-CommandResults $resultPath) }
catch { $failed += "결과 파일을 읽지 못했다: $($_.Exception.Message)" }

$notOk = @($results | Where-Object status -ne 'succeeded')
foreach ($row in $notOk) { $failed += "$($row.command) $($row.status) ($($row.code)): $($row.message)" }

# 시나리오가 ai.status 를 정확히 세 번 부른다 — 최초 등록, DDOL 재등록, 파괴 후.
# 건수를 못 박는다. "1건 이상" 으로 두면 단정 하나가 조용히 빠져도 초록이다.
$status = @($results | Where-Object command -eq 'ai.status')
if ($status.Count -ne 3) { $failed += "ai.status 결과 $($status.Count)건 (기대 3)" }

function Test-Registered($row) {
    return ($row.data.total -eq 1) -and ($row.data.registered -eq $true) -and ($row.data.hasComponent -eq $true)
}
if ($status.Count -ge 1 -and -not (Test-Registered $status[0])) {
    $failed += "최초 등록 실패: $($status[0].data | ConvertTo-Json -Compress)"
}
if ($status.Count -ge 2 -and -not (Test-Registered $status[1])) {
    $failed += "DDOL 재등록 실패: $($status[1].data | ConvertTo-Json -Compress)"
}
if ($status.Count -ge 3 -and $status[2].data.total -ne 0) {
    $failed += "파괴 후 해지 실패: $($status[2].data | ConvertTo-Json -Compress)"
}

# 해지의 원인이 실제로 stress 였는지 — 파괴 표시 0건이면 세 번째 total=0 은 다른 이유다.
$stress = @($results | Where-Object command -eq 'lifecycle.stress')
if ($stress.Count -ne 1) { $failed += "lifecycle.stress 결과 $($stress.Count)건 (기대 1)" }
elseif ($stress[0].data.marked -ne 1) { $failed += "lifecycle.stress 파괴 표시 $($stress[0].data.marked)건 (기대 1)" }

$status | ForEach-Object { "ai.status " + ($_.data | ConvertTo-Json -Compress) }
if ($failed.Count -gt 0) {
    "실패 $($failed.Count)건:"; $failed | ForEach-Object { "  $_" }; exit 1
}
"전체 통과 — AI registry가 최초 등록, DDOL Scene handle 재등록, 파괴 해지를 지킨다"
exit 0
