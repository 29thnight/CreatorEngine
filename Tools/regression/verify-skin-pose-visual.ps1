# 라이브 스키닝 시각 축 (I6-B4b 선행 ①·②)
#
# 살아 있는 애니메이터가 그린 **그림**을 잰다. B4b(legacy 재귀 틱 폐기)가 두 번
# 되돌려진 직접 사유는 "라이브 렌더를 재는 게이트가 없다"였다 — 헤드리스는
# 프레임을 완성하지 않고, 라이브 게이트는 저장 직전 Animator 를 꺼서 바인드
# 포즈만 쟀다. dx12.scene 이 포즈에서 포화하던 것은 하네스 한계가 아니라 glTF
# inverseBind 전치 결함이 팔레트를 폭발시킨 결과였고(2026-09-02 규명), 그것을
# 고치자 이 축이 열렸다.
#
# 세 팔을 같은 씬·같은 배치로 그린다:
#   bind  — model.load + Animator 끔            : 바인드 포즈(기존 라이브 게이트가 재던 것)
#   place — model.load + animpose 0.5           : 살아 있는 팔레트로 그린 포즈
#   drop  — model.loadcached + animpose 0.5     : 에디터 드롭 경로(B4b 회귀가 난 자리)
#
# 단정:
#   1  세 팔 전부 dx12.scene 통과 · 커버리지가 0 도 포화(65536)도 아니다
#   2  place ≠ bind — 포즈가 실제로 그림을 움직인다(같으면 팔레트가 화면에 안 닿는다)
#   3  place 커버리지·팔레트 digest 골든 — 포즈 산술이나 스키닝 규약이 바뀌면 붉는다
#   4  place·drop 팔에 tickNone 0(assets.modeldiag 스냅샷) · animlive enabled=1 — 틱이 실제로 돈다
#   5  drop 팔이 generation 경로(MBC9: 유일한 경로)이고 place 와 **커버리지·팔레트 동수** — 같은
#      로더면 같은 그림이어야 한다(B4b 의 A/B). legacy 틱이 살아 있던 마지막
#      창(2026-09-02, B4b 직전)의 실측은 drop legacy 51297 vs place 49617 였고
#      (정점·스켈레톤 출처가 달랐다), B4b 착지 뒤 drop 은 49617 로 동수가 됐다.
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
$template = Join-Path $repoRoot "scripts\skin_pose_visual.txt"
if (-not (Test-Path $template)) { "시나리오가 없다: $template"; exit 1 }
$modelName = [IO.Path]::GetFileNameWithoutExtension($model)

function Invoke-Arm([string]$label, [string]$loadCmd, [string]$animatorPre, [string]$animatorPost) {
    $scenario = Join-Path $Work "skin_pose_visual_$label.txt"
    $savedScene = Join-Path $Work "skin_pose_visual_$label.creator"
    $stdout = Join-Path $Work "skin_pose_visual_$label.out.log"
    $stderr = Join-Path $Work "skin_pose_visual_$label.err.log"
    (Get-Content $template -Raw).
        Replace('__SCENE__', $scene.Replace('\', '/')).
        Replace('__MODEL_NAME__', $modelName).
        Replace('__MODEL__', $model.Replace('\', '/')).
        Replace('__SAVED_SCENE__', $savedScene.Replace('\', '/')).
        Replace('__LOAD_CMD__', $loadCmd).
        Replace('__ANIMATOR_PRE__', $animatorPre).
        Replace('__ANIMATOR_POST__', $animatorPost) |
        Set-Content -LiteralPath $scenario -Encoding UTF8

    $proc = Start-Process -FilePath $Exe -ArgumentList @("--script", $scenario) `
        -WorkingDirectory $repoRoot -WindowStyle Hidden `
        -RedirectStandardOutput $stdout -RedirectStandardError $stderr -PassThru
    if (-not $proc.WaitForExit($TimeoutSec * 1000)) {
        Stop-Process -Id $proc.Id -Force -ErrorAction SilentlyContinue
        "[$label] $TimeoutSec 초 내에 끝나지 않았다."
        exit 1
    }
    $log = Get-Content $stdout -Raw

    $arm = [ordered]@{
        label = $label
        scenePass = ($log -match '\[CLI\] dx12\.scene 통과')
        coverage = -1
        tickNone = -1
        path = ''
        enabled = -1
        palette = ''
        ratio = -1.0
        log = $stdout
    }
    # 첫 매치가 [3/4] 씬 카메라 렌더의 커버리지다(vertex-live 와 같은 읽기).
    if ($log -match '커버리지\s+(\d+)/65536') { $arm.coverage = [int]$Matches[1] }
    # MBC10 — 틱 경로는 읽기 전용 스냅샷(assets.modeldiag)에서 읽는다.
    if ($log -match 'assets\.modeldiag .* tickGeneration=(\d+) tickNone=(\d+)') { $arm.tickNone = [int]$Matches[2] }
    if ($log -match 'animlive \S+ path=(\S+) enabled=(\d+) .* palette=([0-9A-F]{8})') {
        $arm.path = $Matches[1]; $arm.enabled = [int]$Matches[2]; $arm.palette = $Matches[3]
    }
    if ($log -match 'skinbounds \w+ .* worstRatio=([0-9.]+)') { $arm.ratio = [double]$Matches[1] }
    # 재질 축 — 드로우 수와 baseColor 를 가진 드로우 수. 스킨 메시의 임베디드
    # 텍스처가 experiment 경로에서 빠지면 여기서 갈린다(legacy 드롭 10/10 vs
    # experiment 8/10 — 2026-09-02 사용자 보고 "텍스처가 안 들어온다").
    $arm.draws = -1; $arm.texturedDraws = -1
    if ($log -match '\[3/4\] 씬 카메라 렌더 — 드로우\s+(\d+)') { $arm.draws = [int]$Matches[1] }
    if ($log -match 'baseColor 있는 드로우 (\d+)') { $arm.texturedDraws = [int]$Matches[1] }
    return $arm
}

# 바인드 팔은 저장 **전**에 끈다 — 재로드 뒤에 끄면 프록시가 t=0 팔레트를 이미
# 들고 있어 바인드가 아니다(실측 49684). 포즈 팔은 재로드 **뒤**에 고정한다.
$animatorOff = "object.property $modelName Animator m_isEnabled false"
$animatorPose = 'experiment.animpose 0.5'

$bind  = Invoke-Arm 'bind'  'model.load'       $animatorOff ''
$place = Invoke-Arm 'place' 'model.load'       ''           $animatorPose
$drop  = Invoke-Arm 'drop'  'model.loadcached' ''           $animatorPose

$fail = @()

# 1 — 세 팔 전부 그려지고, 0 도 포화도 아니다.
foreach ($arm in @($bind, $place, $drop)) {
    if (-not $arm.scenePass) { $fail += "1 $($arm.label) 팔 dx12.scene 실패 — 로그 $($arm.log)" }
    if ($arm.coverage -le 0) { $fail += "1b $($arm.label) 팔 커버리지 $($arm.coverage) — 아무것도 안 그려졌다" }
    if ($arm.coverage -ge 65536) { $fail += "1c $($arm.label) 팔 커버리지 포화 — 스킨 기하가 화면을 덮었다(팔레트 폭발)" }
}

# 2 — 포즈가 그림을 움직인다. 바인드 팔은 vertex-live 가 재는 값과 같아야 한다
#     (같은 배치·같은 바인드 — 42411).
if ($place.coverage -eq $bind.coverage) {
    $fail += "2 포즈 팔과 바인드 팔의 커버리지가 같다($($place.coverage)) — 라이브 팔레트가 화면에 안 닿는다"
}
$expectedBindCoverage = 42411
if ($bind.coverage -ne $expectedBindCoverage) {
    $fail += "2b 바인드 팔 커버리지 $($bind.coverage) ≠ vertex-live 기준 $expectedBindCoverage — 바인드 팔이 바인드가 아니거나 배치가 바뀌었다"
}

# 3 — place 골든. 2026-09-02 실측(두 실행 동일). 바뀌면 포즈 산술·스키닝
#     규약·배치 중 하나가 바뀐 것이다 — 왜 바뀌었는지 확인하고 갱신한다.
$expectedPlaceCoverage = 49617
$expectedPlacePalette = '384DCD0A'
if ($place.coverage -ne $expectedPlaceCoverage) {
    $fail += "3 place 커버리지 골든 불일치: $($place.coverage) (골든 $expectedPlaceCoverage)"
}
if ($place.palette -ne $expectedPlacePalette) {
    $fail += "3b place 팔레트 digest 골든 불일치: $($place.palette) (골든 $expectedPlacePalette)"
}
if ($place.ratio -lt 0 -or $place.ratio -gt 4.0) {
    $fail += "3c place 스킨 기하 비율 $($place.ratio) — 팔레트 규약이 어긋났다"
}

# 4 — 틱이 실제로 돈다.
foreach ($arm in @($place, $drop)) {
    if ($arm.tickNone -ne 0) { $fail += "4 $($arm.label) 팔 tickNone=$($arm.tickNone) — 틱이 안 돌거나 스냅샷이 없다" }
    if ($arm.enabled -ne 1) { $fail += "4b $($arm.label) 팔 animlive enabled=$($arm.enabled) — 애니메이터가 꺼져 있다" }
    if ($arm.path -ne 'generation') {
        $fail += "4c $($arm.label) 팔 animlive 경로를 못 읽었다('$($arm.path)')"
    }
}

# 5 — drop 팔 A/B. B4b 착지(2026-09-02) 뒤로 드롭 경로도 experiment 로더를 타므로
#     place 와 **같은 그림**이어야 한다. legacy 틱이 살아 있던 마지막 창에서 잰
#     값(drop legacy 51297 vs place 49617)은 B4b 직전 상태의 기록이고, 착지 뒤
#     drop 은 49617 로 place 와 동수가 됐다 — 틱 단일화가 그림을 안 바꿨다는 증거.
#     legacy 로 새면(experiment 로더 실패 → Assimp 폴백) 틱이 없어 4 가 먼저 붉는다.
$dropNote = ''
if ($drop.path -ne 'generation') {
    $fail += "5c 드롭 경로가 generation 이 아니다('$($drop.path)') — LoadModelAssetGenerationByPath 를 확인하라"
}
if ($drop.coverage -ne $place.coverage) {
    $fail += "5 드롭 경로 커버리지 $($drop.coverage) ≠ place $($place.coverage) — 같은 로더인데 그림이 다르다"
}
if ($drop.palette -ne $place.palette) {
    $fail += "5b 드롭 경로 팔레트 digest $($drop.palette) ≠ place $($place.palette)"
}

# 6 — 재질이 스킨 메시까지 닿는다. legacy 드롭 경로는 임베디드 텍스처를
#     Materials\ 에 뽑아 이름으로 붙였고, experiment 경로는 sidecar GUID 로
#     참조만 하다가 마감(FinalizeMaterialRuntime)에서 잃었다(8/10). 세 팔 전부
#     드로우 수 == baseColor 있는 드로우 수여야 한다.
foreach ($arm in @($bind, $place, $drop)) {
    if ($arm.draws -le 0 -or $arm.texturedDraws -ne $arm.draws) {
        $fail += "6 $($arm.label) 팔 baseColor 있는 드로우 $($arm.texturedDraws)/$($arm.draws) — 스킨 메시의 임베디드 텍스처가 빠졌다"
    }
}

if ($fail.Count -gt 0) {
    "라이브 스키닝 시각 축 실패 $($fail.Count)건:"
    $fail | ForEach-Object { "  $_" }
    "bind $($bind.coverage) · place $($place.coverage)/$($place.palette)/$($place.path) · drop $($drop.coverage)/$($drop.palette)/$($drop.path)"
    exit 1
}

"라이브 스키닝 시각 축 통과 — bind $($bind.coverage) · place $($place.coverage) (palette $($place.palette), 비율 $($place.ratio)) · drop $($drop.coverage) [$($drop.path)]$dropNote"
exit 0
