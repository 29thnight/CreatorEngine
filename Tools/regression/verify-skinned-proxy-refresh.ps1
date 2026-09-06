# 스킨 프록시 갱신 — 본 팔레트가 렌더에 도달하는가
#
# 본 팔레트는 프록시가 **다시 만들어질 때만** 렌더로 간다(`ProxyCommand`가
# `Animator::m_FinalTransforms`를 immutable buffer로 복사한다). Transform 트랙
# X8이 프록시 발행을 dirty 게이팅으로 바꾸면서 그 마스크(`ProxyDirty`)에
# **"팔레트가 바뀌었다"에 해당하는 축이 없었다**. 결과는 조용하다:
# 스킨 메시가 정상적으로 그려지되 **첫 포즈에서 굳는다**. 오브젝트를 움직이면
# `Transform` dirty가 올라가 그때만 툭 갱신된다(사용자 보고로 확인).
#
# ★ 이 결함이 왜 기존 게이트를 전부 통과했는가:
#   · 헤드리스(`--commandlet-script`)는 프레임을 완성하지 않아 그림을 못 본다.
#   · 라이브 게이트는 비결정성 때문에 저장 직전 **Animator를 꺼 버린다** —
#     즉 재는 것이 바인드 포즈다.
#   · `experiment.animtick`은 포즈를 **다시 계산**할 뿐 라이브 팔레트가
#     렌더로 가는지는 안 본다.
#   그래서 "포즈 오차 0 · 커버리지 동수 · 업로드 전량 experiment"가 전부
#   초록인 채로 화면이 굳을 수 있었다.
#
# 그림을 못 재므로 대신 **프록시 커밋 누계**를 잰다. 재생 중에 커밋이 안 늘면
# 최신 팔레트가 렌더에 도달하지 않는다 — 그것이 굳음의 직접 원인이다.
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
$template = Join-Path $repoRoot "scripts\skinned_proxy_refresh.txt"
if (-not (Test-Path $template)) { "시나리오가 없다: $template"; exit 1 }

$scenario = Join-Path $Work "skinned_proxy_refresh.txt"
$stdout = Join-Path $Work "skinned_proxy_refresh.out.log"
$stderr = Join-Path $Work "skinned_proxy_refresh.err.log"
(Get-Content $template -Raw).
    Replace('__SCENE__', $scene.Replace('\', '/')).
    Replace('__MODEL_NAME__', [IO.Path]::GetFileNameWithoutExtension($model)).
    Replace('__MODEL__', $model.Replace('\', '/')) |
    Set-Content -LiteralPath $scenario -Encoding UTF8

$proc = Start-Process -FilePath $Exe -ArgumentList @("--commandlet-script", $scenario) `
    -WorkingDirectory $repoRoot -WindowStyle Hidden `
    -RedirectStandardOutput $stdout -RedirectStandardError $stderr -PassThru
if (-not $proc.WaitForExit($TimeoutSec * 1000)) {
    Stop-Process -Id $proc.Id -Force -ErrorAction SilentlyContinue
    "$TimeoutSec 초 내에 끝나지 않았다."
    exit 1
}

$log = Get-Content $stdout -Raw
$fail = @()

# ① 재생이 실제로 돌아야 아래 축이 의미를 가진다(elapsed·팔레트 digest 변화).
$samples = [regex]::Matches($log,
    'animator\.status \S+ path=(\S+) enabled=(\d+) clip=\d+ elapsed=([\d.]+) .* palette=([0-9A-F]{8})')
if ($samples.Count -lt 2) {
    $fail += "1 표본이 2개 미만이다 — 애니메이터를 못 잡았거나 시나리오가 끊겼다"
}
else {
    $first = $samples[0]; $last = $samples[$samples.Count - 1]
    if ($first.Groups[4].Value -eq $last.Groups[4].Value) {
        $fail += "2 포즈가 굳었다 — palette digest가 두 표본에서 같다"
    }
    if ([double]$last.Groups[3].Value -le 0) {
        $fail += "2b 재생 시간이 흐르지 않았다 — elapsed=$($last.Groups[3].Value)"
    }
}

# ② ★ 핵심 — 프록시 커밋이 늘어야 최신 팔레트가 렌더로 간다.
#    수정 전 실측은 두 표본 모두 11로 **고정**이었다(재생 중인데 커밋 0).
$commits = [regex]::Matches($log, 'animator\.status done animators=(\d+) proxyCommitted=(\d+)')
if ($commits.Count -lt 2) {
    $fail += "3 프록시 커밋 표본이 2개 미만이다"
}
else {
    $before = [int]$commits[0].Groups[2].Value
    $after = [int]$commits[$commits.Count - 1].Groups[2].Value
    if ([int]$commits[0].Groups[1].Value -lt 1) {
        $fail += "3b 애니메이터가 0이다 — 잴 대상이 없다"
    }
    if ($after -le $before) {
        $fail += "3 재생 중인데 프록시 커밋이 안 늘었다($before → $after) — 최신 본 팔레트가 렌더에 도달하지 않는다"
    }
}

if ($fail.Count -gt 0) {
    "스킨 프록시 갱신 실패 $($fail.Count)건:"
    $fail | ForEach-Object { "  $_" }
    "로그: $stdout"
    exit 1
}

$grew = "$([int]$commits[0].Groups[2].Value) → $([int]$commits[$commits.Count - 1].Groups[2].Value)"
"스킨 프록시 갱신 통과 — 프록시 커밋 $grew · 팔레트 digest 변화 확인"
exit 0
