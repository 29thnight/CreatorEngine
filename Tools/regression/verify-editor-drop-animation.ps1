# 에디터 드롭 경로의 재생 바인딩 (I6-B4b 후속)
#
# 콘텐츠 브라우저에서 씬으로 모델을 끌어다 놓을 때 도는 로더는 `model.load`가
# 아니라 `DataSystems->LoadCachedModelShared`다(HierarchyWindow·SceneViewWindow·
# TerrainComponent·Foliage). 그 둘이 서로 다른 로더라는 것이 이 게이트 세트의
# 구멍이었다 — 라이브 게이트는 `model.load` 쪽만 태운다.
#
# ★ 이 구멍이 어떻게 드러났는가(기록): D34b가 `LoadModel`을 experiment로
#   이중화하며 주석에 "에디터의 이름 기반 로드"를 적었는데 정작 에디터가
#   부르는 것은 다른 함수였다. B4b가 legacy 재귀 틱을 걷기 전까지는 폴백이
#   덮어 보이지 않았고(애니메이션이 legacy로 돌았다), 틱이 하나가 되자
#   **드롭한 애니메이션 모델이 화면에서 사라지는** 형태로 나왔다.
#   팔레트가 한 번도 안 쓰이면 스킨 정점이 원점으로 접힌다.
#
# 재는 것은 픽셀이 아니라 **바인딩이 서는가**다. 헤드리스는 프레임을 완성하지
# 않으므로(렌더 0프레임) 사라짐 자체는 여기서 못 본다 — 대신 그 직접 원인인
# `[anim.tick] none`과 `animtick skip`을 막는다.
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
$template = Join-Path $repoRoot "scripts\editor_drop_animation.txt"
if (-not (Test-Path $template)) { "시나리오가 없다: $template"; exit 1 }

$scenario = Join-Path $Work "editor_drop_animation.txt"
$stdout = Join-Path $Work "editor_drop_animation.out.log"
$stderr = Join-Path $Work "editor_drop_animation.err.log"
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

# ①② 드롭 경로가 typed generation 인스턴스화를 탄다(MBC9: 유일한 경로 —
#    [model.dual] experiment 로더 관측은 로더와 함께 은퇴했다).
# MBC10: 관측은 읽기 전용 스냅샷(assets.modeldiag)이다 — 제품 stdout 토큰은 없다.
$diag = [regex]::Match($log, '\[CLI\] assets\.modeldiag meshResolveGeneration=(\d+) meshResolveFailed=(\d+) instantiateGeneration=(\d+) instantiateRejected=(\d+) tickGeneration=(\d+) tickNone=(\d+) lastInstantiated=(\S+)')
if (-not $diag.Success) { $fail += "1 assets.modeldiag 스냅샷이 없다" }
elseif ([int]$diag.Groups[3].Value -lt 1 -or $diag.Groups[7].Value -ne 'Gunner_F_Mythic') {
    $fail += "1 드롭한 모델의 인스턴스화가 generation 경로가 아니다 — LoadModelAssetGenerationByPath를 확인하라: $($diag.Value)"
}
# ③ ★ 핵심 — 재생 바인딩이 섰는가. 'none'은 "이 애니메이터는 안 돈다"이고,
#    그 상태가 곧 화면에서 사라지는 원인이다(팔레트 미기록).
$tickNone = if ($diag.Success) { [int]$diag.Groups[6].Value } else { -1 }
if ($tickNone -ne 0) {
    $fail += "3 드롭한 애니메이션 모델의 재생 바인딩이 비었다 — tickNone $tickNone 건"
}
# MBC8/MBC9: UUIDv8 모델의 재생 정본은 typed generation 틱 하나다.
if (-not $diag.Success -or [int]$diag.Groups[5].Value -lt 1) {
    $fail += "3b 드롭한 모델이 generation 틱을 한 번도 안 탔다"
}
# ④ 포즈 산출까지 실제로 성립하는가(바인딩만 서고 표본이 0이면 ③이 공짜다).
if ($log -notmatch 'experiment\.animtick pass animators=[1-9]\d* .* samples=[1-9]\d*') {
    $fail += "4 드롭한 모델에서 포즈 표본이 나오지 않았다 — animtick 출력을 확인하라"
}

if ($fail.Count -gt 0) {
    "에디터 드롭 재생 바인딩 실패 $($fail.Count)건:"
    $fail | ForEach-Object { "  $_" }
    "로그: $stdout"
    exit 1
}

"에디터 드롭 재생 바인딩 통과 — 인스턴스화 generation · tickGeneration ≥ 1 · tickNone 0(읽기 전용 스냅샷)"
exit 0
