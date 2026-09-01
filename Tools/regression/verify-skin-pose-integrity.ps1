# 스킨 포즈 무결성 (I6-B4-pre)
#
# 라이브 스키닝의 **그림**을 재는 자가 이 저장소에 없다. 기존 라이브 게이트는
# 비결정성 때문에 저장 직전 Animator를 꺼서 **바인드 포즈**만 재고,
# `dx12.scene`은 팔레트를 옳게 받고도(digest 일치 실측) 포즈가 실리면 커버리지가
# 포화한다 — 오프라인 하네스의 한계다(에디터 화면은 정상임을 사용자가 확인).
# 그 공백에서 B4b가 두 번 깨졌다.
#
# 그래서 그림 대신 **그림의 입력**을 잰다: 팔레트 × 정점 스킨을 CPU에서 풀어
#   ① 유한성(NaN/inf) ② 본 인덱스 범위 ③ 포즈별 digest(골든)
# 를 본다. 결정성은 편집 모드(delta 0) + `experiment.animpose`가 준다.
#
# ★ 무엇을 단정하지 **않는가**: 결과 기하의 **절대 크기**. 곱 순서 두 가지를
#   다 시험했는데 바인드 대비 7,115배와 22,020배가 나왔다 — 밖에서 팔레트
#   규약을 맞출 수 없다는 뜻이고, 재구현 대조군을 자로 쓰면 안 된다는 이
#   저장소의 교훈 그대로다. 크기 비율은 로그에 관측값으로만 남긴다.
#
# 잡는 것: 팔레트가 쓰레기가 되는 변화(NaN), 본 인덱스가 밀리는 변화(범위 밖),
# 포즈 산술이 달라지는 변화(digest). B4b의 실패 양식이 그 셋이다.
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
$expected = @('C3FF0B7A', '85CB494C', 'FAA108C7')

$samples = [regex]::Matches($log,
    'experiment\.skinbounds (\w+) meshes=(\d+) nonFinite=(\d+) outOfRange=(\d+) .*? digest=([0-9A-F]{8})')
if ($samples.Count -ne 3) {
    $fail += "1 표본이 3개가 아니다($($samples.Count)) — 시나리오가 끊겼거나 대상을 못 잡았다"
}
else {
    for ($i = 0; $i -lt 3; $i++) {
        $verdict = $samples[$i].Groups[1].Value
        $meshes = [int]$samples[$i].Groups[2].Value
        $digest = $samples[$i].Groups[5].Value
        if ('pass' -ne $verdict) {
            $fail += "2 포즈 $i 판정 실패($verdict) — nonFinite/outOfRange를 확인하라"
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
    $unique = ($samples | ForEach-Object { $_.Groups[5].Value } | Sort-Object -Unique).Count
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

"스킨 포즈 무결성 통과 — 포즈 3종 · NaN 0 · 인덱스 범위 밖 0 · digest 골든 일치"
exit 0
