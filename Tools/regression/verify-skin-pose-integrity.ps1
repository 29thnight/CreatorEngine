# 스킨 포즈 무결성 (I6-B4-pre)
#
# 라이브 스키닝의 **그림**을 헤드리스로 픽셀까지 재는 자가 없다(--script는
# 렌더 0프레임). 기존 라이브 게이트는 비결정성 때문에 저장 직전 Animator를 꺼서
# **바인드 포즈**만 쟀고, 그 공백에서 B4b가 두 번 깨졌다.
#
# 그래서 그림 대신 **그림의 입력**을 잰다: 팔레트 × 정점 스킨을 CPU에서 풀어
#   ① 유한성(NaN/inf) ② 본 인덱스 범위 ③ 바인드 대비 크기 상한 ④ 포즈별 digest
# 를 본다. 결정성은 편집 모드(delta 0) + `experiment.animpose`가 준다.
#
# ★ 크기 비율은 자다(2026-09-02 정정). 첫 판에는 7,115배가 나와 "규약을 못
#   맞춘다"며 관측값으로만 남겼는데, 그것은 팔레트가 **실제로 폭발해 있던 것**
#   이었다 — glTF 임포터가 inverseBind를 전치된 채 게시했다. `dx12.scene`이
#   포즈만 실리면 포화하던 것도 하네스 한계가 아니라 그 폭발을 옳게 그린
#   결과였고, "에디터는 정상"은 legacy(Assimp) 경로를 본 것이었다. 정상 포즈는
#   1배 근방이라 4배 상한이 그 결함 부류를 통째로 잡는다.
#
# 잡는 것: 팔레트가 쓰레기가 되는 변화(NaN), 본 인덱스가 밀리는 변화(범위 밖),
# 팔레트 규약이 어긋나는 변화(크기 상한), 포즈 산술이 달라지는 변화(digest).
param(
    [string]$Exe = "",
    [string]$Work = $env:TEMP,
    [int]$TimeoutSec = 300
)

$ErrorActionPreference = 'Stop'
$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot "..\.."))

if ([string]::IsNullOrEmpty($Exe)) {
    $Exe = Join-Path $repoRoot "Bin\x64-Debug\Editor\CreatorEditor.exe"
}
if (-not (Test-Path $Exe)) { "실행 파일이 없다: $Exe"; exit 1 }
$Exe = (Resolve-Path -LiteralPath $Exe).Path

$scene = Join-Path $repoRoot "Dynamic_CPP\Assets\Scenes\FT_Primitives.creator"
$model = Join-Path $repoRoot "Dynamic_CPP\Assets\Models\Gunner_F_Mythic.glb"
foreach ($required in @($scene, $model)) {
    if (-not (Test-Path $required)) { "자산이 없다: $required"; exit 1 }
}
$template = Join-Path $repoRoot "scripts\skin_pose_integrity.txt"
if (-not (Test-Path $template)) { "시나리오가 없다: $template"; exit 1 }

$scenario = Join-Path $Work "skin_pose_integrity.txt"
$stdout = Join-Path $Work "skin_pose_integrity.out.log"
$stderr = Join-Path $Work "skin_pose_integrity.err.log"
(Get-Content $template -Raw).
    Replace('__SCENE__', $scene.Replace('\', '/')).
    Replace('__MODEL_NAME__', [IO.Path]::GetFileNameWithoutExtension($model)).
    Replace('__SKINNED_MODEL__', $model.Replace('\', '/')) |
    Set-Content -LiteralPath $scenario -Encoding UTF8

$proc = Start-Process -FilePath $Exe -ArgumentList @("--script", $scenario) `
    -WorkingDirectory $repoRoot -WindowStyle Hidden `
    -RedirectStandardOutput $stdout -RedirectStandardError $stderr -PassThru
if (-not $proc.WaitForExit($TimeoutSec * 1000)) {
    Stop-Process -Id $proc.Id -Force -ErrorAction SilentlyContinue
    "$TimeoutSec 초 내에 끝나지 않았다."
    exit 1
}

$log = Get-Content $stdout -Raw
$fail = @()

# 포즈 0.0 / 0.5 / 0.9 의 골든. 값이 바뀌면 포즈 산술이 바뀐 것이다 —
# 의도한 변경이면 왜 바뀌었는지 확인하고 갱신한다.
# 2026-09-02: glTF inverseBind 전치 수정으로 갱신(이전 C3FF0B7A/85CB494C/FAA108C7
# 는 폭발한 팔레트의 값이었다 — 비율 7,115배). 새 값은 비율 1.006/1.010/1.008.
$expected = @('C49D5851', '266E6C06', 'F84B0B3D')

$samples = [regex]::Matches($log,
    'experiment\.skinbounds (\w+) meshes=(\d+) nonFinite=(\d+) outOfRange=(\d+) emptyWeights=\d+ worstRatio=([0-9.]+) .*? digest=([0-9A-F]{8})')
if ($samples.Count -ne 3) {
    $fail += "1 표본이 3개가 아니다($($samples.Count)) — 시나리오가 끊겼거나 대상을 못 잡았다"
}
else {
    for ($i = 0; $i -lt 3; $i++) {
        $verdict = $samples[$i].Groups[1].Value
        $meshes = [int]$samples[$i].Groups[2].Value
        $ratio = [double]$samples[$i].Groups[5].Value
        $digest = $samples[$i].Groups[6].Value
        if ('pass' -ne $verdict) {
            $fail += "2 포즈 $i 판정 실패($verdict) — nonFinite/outOfRange/worstRatio를 확인하라"
        }
        # CLI 자체 상한(4배)과 별개로 여기서도 잰다 — CLI 상한이 느슨해져도
        # 게이트가 같이 느슨해지지 않게. 정상 포즈는 1배 근방이다.
        if ($ratio -gt 4.0) {
            $fail += "2c 포즈 $i 스킨 기하가 바인드 대비 $ratio 배 — 팔레트 규약이 어긋났다"
        }
        if ($meshes -lt 1) {
            $fail += "2b 포즈 $i 에서 스킨 메시를 못 잡았다 — skip을 통과로 읽지 않는다"
        }
        if ($digest -ne $expected[$i]) {
            $fail += "3 포즈 $i digest가 골든과 다르다: $digest (골든 $($expected[$i]))"
        }
    }
    # 세 포즈가 서로 달라야 "포즈가 실제로 기하를 움직인다"가 성립한다.
    # 같으면 팔레트가 굳은 것이고, 그 상태로도 위 단정은 전부 통과한다.
    $unique = ($samples | ForEach-Object { $_.Groups[6].Value } | Sort-Object -Unique).Count
    if ($unique -lt 3) {
        $fail += "4 세 포즈의 digest가 서로 같다($unique 종) — 포즈가 기하를 안 움직인다"
    }
}

if ($fail.Count -gt 0) {
    "스킨 포즈 무결성 실패 $($fail.Count)건:"
    $fail | ForEach-Object { "  $_" }
    "로그: $stdout"
    exit 1
}

"스킨 포즈 무결성 통과 — 포즈 3종 · NaN 0 · 인덱스 범위 밖 0 · 크기 비율 ≤ 4 · digest 골든 일치"
exit 0
