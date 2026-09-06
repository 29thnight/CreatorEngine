# CameraComponent 래퍼 게이트 (W2).
#
# ── 무엇을 잡는가 ──
#
# 저작 자산에 CameraComponent가 20개 있는데 스크립트가 만질 길이 없었다.
# 있던 것은 핸들을 받지 않는 전역 질의 셋뿐이라 "이 오브젝트의 카메라"라는
# 축이 아예 없었다. 이 게이트가 그 축을 태운다.
#
# ── 왜 축이 하나뿐인가 ──
#
# verify-light-script.ps1은 두 축(경계 왕복 · dirty 사슬)을 본다. 카메라에는
# 두 번째가 없다 — 렌더 프록시를 쓰지 않고(Scene.cpp의 프록시 Kind 열거에
# Camera가 없다) 매 프레임 CaptureFrameSnapshot으로 읽히므로, 값을 넣으면
# 다음 프레임에 그대로 반영된다. 발행할 dirty가 애초에 없다.
#
# 대신 프로브가 Camera.Main과의 일치를 함께 단정한다. 컴포넌트 축과 전역
# 접근점이 같은 카메라를 가리키는지 재는 유일한 자리다.
#
# ── 이 게이트가 못 보는 것 ──
#
# fov를 바꾼 뒤 투영 행렬이 실제로 달라지는지는 보지 못한다. 그것을 보려면
# 렌더가 도는 하네스가 필요한데 --script 헤드리스는 렌더 프레임이 거의 돌지
# 않는다(근거는 verify-light-script.ps1 머리 주석의 실측치).
#
# ── 이빨 확인(2026-09-04) ──
#
# ① Api_Camera_SetFov의 대입을 지우니 "fov 왕복"만 붉어지고 나머지 14건은 통과했다.
# ② Api_Camera_GetPrimaryHandle이 무효 핸들을 돌려주게 하니 "꺼도 Camera.Main은
#    폴백으로 이 카메라"와 "Camera.Main이 있다" 두 건이 붉어졌다. 세 번째("이
#    오브젝트")는 프로브가 null에서 조기 반환해 실행되지 않는다 — 처음에는 3건이
#    붉어질 것으로 적었다가 실측으로 정정했다.
# 둘 다 되돌려 초록을 다시 확인했다.
#
# 사용법:
#   pwsh Tools\regression\verify-camera-script.ps1
param(
    [string]$Exe = (Join-Path $PSScriptRoot "..\..\Bin\x64-Debug\Editor\CreatorEditor.exe"),
    [string]$Work = $env:TEMP,
    [int]$TimeoutSeconds = 300
)

$ErrorActionPreference = 'Stop'
$Work = [IO.Path]::GetFullPath($Work)
. (Join-Path $PSScriptRoot "CommandResults.ps1")
New-Item -ItemType Directory -Path $Work -Force | Out-Null

if (-not (Test-Path $Exe)) { "실행 파일이 없다: $Exe"; exit 1 }
$exeDir = [System.IO.Path]::GetDirectoryName($Exe)

$repoRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot "..\.."))
$scenario = Join-Path $repoRoot "scripts\camera_script_probe.txt"
if (-not (Test-Path $scenario)) { "시나리오가 없다: $scenario"; exit 1 }

$outPath = Join-Path $Work "camera_script_probe.out"
$errPath = Join-Path $Work "camera_script_probe.err"

$resultPath = Join-Path $Work "camera_script_probe.results.jsonl"
if (Test-Path -LiteralPath $resultPath) { Remove-Item -LiteralPath $resultPath }
$proc = Start-Process -FilePath $Exe -ArgumentList @('--script', ('"'+$scenario+'"'), '--result-format', 'jsonl', '--result-file', ('"'+$resultPath+'"')) -WindowStyle Hidden `
    -WorkingDirectory $exeDir `
    -RedirectStandardOutput $outPath `
    -RedirectStandardError $errPath -PassThru

$proc.WaitForExit($TimeoutSeconds * 1000) | Out-Null
if (-not $proc.HasExited) {
    $proc.Kill()
    "타임아웃 ($TimeoutSeconds 초). 시나리오가 끝나지 않았다."
    exit 1
}

if (-not (Test-Path $outPath)) { "표준 출력이 없다: $outPath"; exit 1 }

$results = @(Read-CommandResults $resultPath)
if ($proc.ExitCode -ne 0 -or @($results | Where-Object status -ne 'succeeded').Count) { throw 'Scenario command failed' }
$invocation = Get-SucceededCommand $results 'script.invoke'
$probe = $invocation.returnValue | ConvertFrom-Json
if ($probe.completed -ne 1 -or $probe.failed -ne 0) { throw "Probe incomplete or failed: $($invocation.returnValue)" }
$probeCount = [int]$probe.passed

# 건수를 정확히 못 박는다. "N건 이상"으로 두면 단정 하나가 조용히 빠져도
# 통과한다 — 커버리지가 줄어드는 것을 보는 유일한 자리다.
# 내역: 기본값 4 · 왕복 3 · primary 3 · 전역 접근 4 · 부재 1.
# 프로브에 단정을 더하면 이 값도 함께 고칠 것.
$ExpectedAssertions = 15
if ($probeCount -ne $ExpectedAssertions) {
    "  단정 $probeCount 건 — $ExpectedAssertions 건이어야 한다."
    "  줄었다면 프로브가 조기 반환했고, 늘었다면 이 기대값을 갱신할 것."
    exit 1
}
"  통과 $probeCount 건"

""
"전체 통과 — CameraComponent 래퍼가 경계를 왕복하고 Camera.Main과 일치한다"
exit 0
