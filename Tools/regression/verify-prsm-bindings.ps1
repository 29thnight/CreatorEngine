# PrSM 엔진 바인딩 게이트 — LightComponent·Entity·Transform 과 값 타입(D-042).
#
# ── 무엇을 잡는가 ──
#
# C# 래퍼 대신 PrSM 이 엔진 클래스를 직접 만진다. 노출은 엔진 header 의
# reflgen::interop("prsm") 과 값 타입 별칭(PrsmValueTypes.h)이 정하고, reflgen 이
# SceneRuntime 빌드 안에서 래퍼·표·카탈로그를 만든다. 에디터는 기동 때
# Assets/Scripts/PrSM 을 prsmc 로 컴파일하고 prsm_jit 로 적재한다. 이 게이트는 그 사슬
# 전체가 실제 엔진 객체에 닿는지 잰다.
#
#   "Directional Light" + PrsmLightProbe
#     Intensity 7     OnInitialized 의 컴포넌트 쓰기(component 수신자)
#     Range 12.5      Entity.Enabled 읽기(entity 수신자)가 참일 때만 쓴다
#     SpotAngle 35    PrePhysics 틱 계약 + 컴포넌트 읽기(Intensity * 5)
#
#   "PrsmProbeTarget"(빈 엔티티 + LightComponent) + PrsmTransformProbe
#     position        기준선 p → (p.x+1, p.y*2, p.z-4)  Vector3 읽기·필드 수정·쓰기
#     scale (2,2,2)   Vector3 생성자로 쓰기
#     rotation        (0, 0.6, 0, 0.8)                 Quaternion 쓰기
#     color           (0.25, 0.5, 1, 1)                Color 쓰기
#     type 2          Directional 을 읽어 비교한 뒤 SpotLight 로 쓰기(enum 왕복)
#
# 기준선도 단정한다 — 대상이 이미 기대값을 들고 있으면 이 게이트는 아무것도 증명하지 않는다.
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
#   pwsh Tools\regression\verify-prsm-bindings.ps1 -Exe Bin\x64-Debug\Editor\CreatorEditor.exe
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
$scenario = Join-Path $repoRoot "scripts\prsm_bindings_probe.txt"
if (-not (Test-Path $scenario)) { "시나리오가 없다: $scenario"; exit 1 }

$outPath = Join-Path $Work "prsm_bindings_probe.out"
$errPath = Join-Path $Work "prsm_bindings_probe.err"
$resultPath = Join-Path $Work "prsm_bindings_probe.results.jsonl"
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
# 시나리오 순서: 기준선(광원 · 대상 Transform · 대상 광원) → 재생 → 같은 셋.
$reads = @($results | Where-Object command -eq 'object.properties' | ForEach-Object { $_.data.values })
if ($reads.Count -ne 6) { throw "object.properties 가 6번이어야 한다: $($reads.Count)" }

$failed = 0
$passed = 0
function Expect([string]$label, $actual, [double]$expected) {
    $value = [double]$actual
    if ([Math]::Abs($value - $expected) -gt 1e-4) {
        "  실패: $label = $value (기대 $expected)"
        $script:failed++
    } else {
        $script:passed++
    }
}

$light = $reads[0]; $transform = $reads[1]; $target = $reads[2]
Expect '기준선 Intensity' $light.m_intencity 1
Expect '기준선 Range' $light.m_range 10
Expect '기준선 SpotAngle' $light.m_spotLightAngle 30
Expect '기준선 rotation.y' $transform.rotation.y 0
Expect '기준선 scale.x' $transform.scale.x 1
Expect '기준선 Type' $target.m_lightType 0
Expect '기준선 color.r' $target.m_color.r 1

$light = $reads[3]; $after = $reads[4]; $targetAfter = $reads[5]
Expect 'Intensity' $light.m_intencity 7
Expect 'Range' $light.m_range 12.5
Expect 'SpotAngle' $light.m_spotLightAngle 35
Expect 'position.x' $after.position.x ([double]$transform.position.x + 1)
Expect 'position.y' $after.position.y ([double]$transform.position.y * 2)
Expect 'position.z' $after.position.z ([double]$transform.position.z - 4)
foreach ($axis in 'x', 'y', 'z') { Expect "scale.$axis" $after.scale.$axis 2 }
Expect 'rotation.x' $after.rotation.x 0
Expect 'rotation.y' $after.rotation.y 0.6
Expect 'rotation.z' $after.rotation.z 0
Expect 'rotation.w' $after.rotation.w 0.8
Expect 'color.r' $targetAfter.m_color.r 0.25
Expect 'color.g' $targetAfter.m_color.g 0.5
Expect 'color.b' $targetAfter.m_color.b 1
Expect 'color.a' $targetAfter.m_color.a 1
Expect 'Type' $targetAfter.m_lightType 2

# 건수를 정확히 못 박는다 — 단정 하나가 조용히 빠져도 통과하지 않도록.
# 내역: 기준선 7 · 광원 3 · position 3 · scale 3 · rotation 4 · color 4 · type 1.
$ExpectedAssertions = 25
if ($failed) { ""; "단정 $failed 건 실패 (통과 $passed 건)"; exit 1 }
if ($passed -ne $ExpectedAssertions) { "단정 $passed 건 — $ExpectedAssertions 건이어야 한다."; exit 1 }
"  통과 $passed 건"
""
"전체 통과 — PrSM 컴포넌트가 생성된 바인딩으로 LightComponent·Entity·Transform 과 값 타입을 읽고 쓴다"
exit 0
