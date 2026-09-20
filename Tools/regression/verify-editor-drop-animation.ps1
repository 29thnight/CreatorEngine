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
# generation 틱과 Play 중 팔레트 갱신을 확인한다.
param(
    [string]$Exe = "",
    [string]$Work = $env:TEMP,
    [int]$TimeoutSec = 300
)

$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'CommandResults.ps1')
$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot "..\.."))

if ([string]::IsNullOrEmpty($Exe)) {
    $Exe = Join-Path $repoRoot "Bin\x64-Debug\Editor\CreatorEditor.exe"
}
if (-not (Test-Path $Exe)) { "실행 파일이 없다: $Exe"; exit 1 }
$Exe = (Resolve-Path -LiteralPath $Exe).Path

$scene = Join-Path $repoRoot "Dynamic_CPP\Assets\Scenes\FT_Primitives.creator"
$model = Join-Path $repoRoot "Dynamic_CPP\Assets\Models\CreatorRobot.glb"
foreach ($required in @($scene, $model)) {
    if (-not (Test-Path $required)) { "자산이 없다: $required"; exit 1 }
}
$template = Join-Path $repoRoot "scripts\editor_drop_animation.txt"
if (-not (Test-Path $template)) { "시나리오가 없다: $template"; exit 1 }

$scenario = Join-Path $Work "editor_drop_animation.txt"
$stdout = Join-Path $Work "editor_drop_animation.out.log"
$stderr = Join-Path $Work "editor_drop_animation.err.log"
$resultPath = Join-Path $Work "editor_drop_animation.results.jsonl"
if (Test-Path -LiteralPath $resultPath) { Remove-Item -LiteralPath $resultPath }
(Get-Content $template -Raw).
    Replace('__SCENE__', $scene.Replace('\', '/')).
    Replace('__MODEL_NAME__', [IO.Path]::GetFileNameWithoutExtension($model)).
    Replace('__MODEL__', $model.Replace('\', '/')) |
    Set-Content -LiteralPath $scenario -Encoding UTF8

$proc = Start-Process -FilePath $Exe -ArgumentList @('--commandlet-script', ('"' + $scenario + '"'), '--result-file', ('"' + $resultPath + '"')) `
    -WorkingDirectory $repoRoot -WindowStyle Hidden `
    -RedirectStandardOutput $stdout -RedirectStandardError $stderr -PassThru
if (-not $proc.WaitForExit($TimeoutSec * 1000)) {
    Stop-Process -Id $proc.Id -Force -ErrorAction SilentlyContinue
    "$TimeoutSec 초 내에 끝나지 않았다."
    exit 1
}

$log = Get-Content $stdout -Raw
$fail = @()
$results = @(Read-CommandResults $resultPath)
if ($proc.ExitCode -ne 0 -or @($results | Where-Object status -ne 'succeeded').Count) {
    $fail += '0 에디터 종료 코드 또는 시나리오 명령이 실패했다'
}

# ①② 드롭 경로가 typed generation 인스턴스화를 탄다(MBC9: 유일한 경로 —
#    [model.dual] experiment 로더 관측은 로더와 함께 은퇴했다).
# MBC10: 관측은 읽기 전용 스냅샷(assets.modeldiag)이다 — 제품 stdout 토큰은 없다.
$diag = [regex]::Match($log, '\[CLI\] assets\.modeldiag meshResolveGeneration=(\d+) meshResolveFailed=(\d+) instantiateGeneration=(\d+) instantiateRejected=(\d+) tickGeneration=(\d+) tickNone=(\d+) lastInstantiated=(\S+)')
if (-not $diag.Success) { $fail += "1 assets.modeldiag 스냅샷이 없다" }
elseif ([int]$diag.Groups[3].Value -lt 1 -or $diag.Groups[7].Value -ne 'CreatorRobot') {
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
# ④ 제거된 experiment.animtick 대신 실제 제품 팔레트를 두 시점에서 읽는다.
$poses = @($results | Where-Object command -eq 'animator.status' | ForEach-Object {
    $_.data.animators | Where-Object name -eq 'CreatorRobot'
})
if ($poses.Count -ne 2) {
    $fail += '4 드롭한 모델의 재생 스냅샷 2개가 없다'
} else {
    foreach ($pose in $poses) {
        if ($pose.path -ne 'generation' -or -not $pose.enabled -or $pose.clips -ne 8 -or
            $pose.bones -lt 53 -or $pose.publication.source -ne 'product' -or
            -not $pose.publication.uploaded -or $pose.publication.validBones -lt 53 -or
            $pose.publication.invalidBones -ne 0) {
            $fail += '4a 드롭한 모델의 제품 포즈 게시가 불완전하다'
        }
    }
    if ($poses[0].elapsed -eq $poses[1].elapsed -or $poses[0].palette -eq $poses[1].palette) {
        $fail += '4b Play 중 드롭한 모델의 시간 또는 팔레트가 갱신되지 않았다'
    }
}

if ($fail.Count -gt 0) {
    "에디터 드롭 재생 바인딩 실패 $($fail.Count)건:"
    $fail | ForEach-Object { "  $_" }
    "로그: $stdout"
    exit 1
}

"에디터 드롭 재생 통과 — generation 바인딩 · 제품 포즈 게시 · Play 시간/팔레트 갱신"
exit 0
