# PrSM 첫 엔진 도메인 게이트 — LightComponent(Intensity·Range·SpotAngle) + Entity(Enabled).
#
# ── 무엇을 잡는가 ──
#
# C# 래퍼 대신 PrSM 이 엔진 클래스를 직접 만진다. 노출은 엔진 header 의
# reflgen::interop("prsm") 이 정하고, reflgen 이 SceneRuntime 빌드 안에서 래퍼·표·카탈로그를
# 만든다. 에디터는 기동 때 Assets/Scripts/PrSM 을 prsmc 로 컴파일하고 prsm_jit 로 적재한다.
# 이 게이트는 그 사슬 전체가 실제 엔진 객체에 닿는지 잰다.
#
#   Intensity 7    OnInitialized 의 컴포넌트 쓰기(component 수신자)
#   Range 12.5     Entity.Enabled 읽기(entity 수신자)가 참일 때만 쓴다
#   SpotAngle 35   PrePhysics 틱 계약 + 컴포넌트 읽기(Intensity * 5)
#
# 셋 중 하나라도 기본값(1·10·30)이면 그 축이 끊긴 것이다. 기준선 값도 함께 단정한다 —
# 기본 씬 광원이 이미 기대값을 들고 있으면 이 게이트는 아무것도 증명하지 않는다.
#
# ── 이 게이트가 못 보는 것 ──
#
# 렌더 프록시 반영은 보지 않는다(--script 헤드리스는 렌더 프레임이 거의 돌지 않는다 —
# verify-light-script.ps1 머리 주석). writer 가 dirty 를 내는 것은 C# 경로와 같은 setter 라
# 그 게이트가 덮는다.
#
# ── 이빨 확인(2026-10-20) ──
#
# PrsmScriptComponent::OnAddedToScene 의 Enable 전달을 빼면 SpotAngle 만 30 으로 붉어진다
# (엔진은 켜진 채 붙은 컴포넌트에 OnEnable 을 부르지 않아 틱이 서지 않는다 — 이 게이트가
# 처음 잡은 결함이다).
#
# 사용법:
#   pwsh Tools\regression\verify-prsm-light.ps1 -Exe Bin\x64-Debug\Editor\CreatorEditor.exe
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
$exeDir = [System.IO.Path]::GetDirectoryName([IO.Path]::GetFullPath($Exe))
$toolDir = [IO.Path]::GetFullPath((Join-Path $exeDir "..\PrSM"))
foreach ($tool in 'prsmc.exe', 'prsm_jit.dll', 'LLVM-C.dll', 'SceneRuntime.bindings.json') {
    if (-not (Test-Path (Join-Path $toolDir $tool))) { "PrSM 도구가 없다: $toolDir\$tool (SceneRuntime 빌드가 배치한다)"; exit 1 }
}

$repoRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot "..\.."))
$scenario = Join-Path $repoRoot "scripts\prsm_light_probe.txt"
if (-not (Test-Path $scenario)) { "시나리오가 없다: $scenario"; exit 1 }

$outPath = Join-Path $Work "prsm_light_probe.out"
$errPath = Join-Path $Work "prsm_light_probe.err"
$resultPath = Join-Path $Work "prsm_light_probe.results.jsonl"
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

$results = @(Read-CommandResults $resultPath)
if ($proc.ExitCode -ne 0 -or @($results | Where-Object status -ne 'succeeded').Count) { throw 'Scenario command failed' }
$reads = @($results | Where-Object command -eq 'object.properties')
if ($reads.Count -ne 2) { throw "object.properties 가 2번이어야 한다: $($reads.Count)" }

$failed = 0
function Expect([string]$label, $values, [string]$field, [double]$expected) {
    $actual = [double]$values.$field
    if ([Math]::Abs($actual - $expected) -gt 1e-4) {
        "  실패: $label $field = $actual (기대 $expected)"
        $script:failed++
    } else {
        "  통과: $label $field = $actual"
    }
}

$before = $reads[0].data.values
$after = $reads[1].data.values
Expect '기준선' $before 'm_intencity' 1
Expect '기준선' $before 'm_range' 10
Expect '기준선' $before 'm_spotLightAngle' 30
Expect '재생 후' $after 'm_intencity' 7
Expect '재생 후' $after 'm_range' 12.5
Expect '재생 후' $after 'm_spotLightAngle' 35

if ($failed) { ""; "단정 $failed 건 실패"; exit 1 }
""
"전체 통과 — PrSM 컴포넌트가 생성된 바인딩으로 LightComponent·Entity 를 읽고 쓴다"
exit 0
