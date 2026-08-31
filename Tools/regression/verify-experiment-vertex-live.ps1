# experiment 정점 실증 게이트 (I5-D34a)
#
# 정적 메시의 GPU 정점 출처가 experiment packed(48B)로 바뀌었는지 실증한다.
# dx12 스윕의 자가 씬은 이 전환에 눈멀어 있고(D1b 실측), --script 헤드리스의
# 라이브는 프레임을 완성하지 않으므로(렌더 0프레임 실측), 실씬(FT_Primitives)을
# 로드한 뒤 dx12.scene 오프라인 하네스로 관측한다 — 하네스에는 라이브와 같은
# 조회 주입이 걸려 있고 커버리지·밝기 단정이 있다.
#
# ── 판정 항목 ──
#
#   1  로드가 experiment 경로다      — [model.dual] experiment 경로 ≥ 1
#   1b 스킨 모델(Gunner)도 experiment 경로다 (D34b)
#   2  ★ GPU 업로드가 experiment다  — "메시 업로드 N(experiment M" M > 0
#   2b ★ 업로드 전량이 experiment다 — N == M (스킨 메시가 legacy로 새면
#      스킨 전용 계수 없이도 여기서 갈린다) (D34b)
#   3  하네스 단정 전체 통과          — dx12.scene 통과 (커버리지·밝기 포함:
#      experiment 버퍼로 그린 그림이 통째로 틀리면 여기가 붉는다)
#   4  A/B 대조 — 스위치 끄면 experiment 0, 드로우·커버리지·밝기 동일,
#      하네스 여전히 통과 (경로만 바뀌고 그리는 대상·그림 판정은 같다)
#
# ★ 자가 틀렸을 때 어떻게 드러나는가:
#   · 2가 없으면 "로드만 experiment, 업로드는 legacy"가 통과로 나온다.
#   · 4의 드로우 동수가 없으면 experiment PSO 부재로 배치가 조용히 빠져도
#     2·3이 통과할 수 있다.
#
# 한계(정직): 하네스 단정은 픽셀 diff 0이 아니다 — bitangent 재구성의 시각
# 정확성(노멀맵 방향)은 못 가른다. 정적 픽셀 diff는 D34b 스킨 픽셀 게이트와
# 함께 세운다.
param(
    [string]$Exe = "",
    [string]$Scene = "",
    [string]$Work = $env:TEMP,
    [int]$TimeoutSec = 300
)

$ErrorActionPreference = 'Stop'
$repoRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot "..\.."))

if ([string]::IsNullOrEmpty($Exe)) {
    $Exe = Join-Path $repoRoot "Bin\x64-Debug\Editor\CreatorEditor.exe"
}
if (-not (Test-Path $Exe)) { "실행 파일이 없다: $Exe"; exit 1 }
$Exe = (Resolve-Path -LiteralPath $Exe).Path

if ([string]::IsNullOrEmpty($Scene)) {
    $Scene = Join-Path $repoRoot "Dynamic_CPP\Assets\Scenes\FT_Primitives.creator"
}
if (-not (Test-Path $Scene)) { "씬이 없다: $Scene"; exit 1 }
$Scene = (Resolve-Path -LiteralPath $Scene).Path

# I5-D34b: 스킨 메시. FT 프리미티브는 전부 정적이라 이 모델 없이는 스킨
# 레이아웃 축(BLENDINDICES uint4)이 한 번도 돌지 않는다.
$SkinnedModel = Join-Path $repoRoot "Dynamic_CPP\Assets\Models\Gunner_F_Mythic.glb"
if (-not (Test-Path $SkinnedModel)) { "스킨 모델이 없다: $SkinnedModel"; exit 1 }
$SkinnedModel = (Resolve-Path -LiteralPath $SkinnedModel).Path

$template = Join-Path $repoRoot "scripts\experiment_vertex_live.txt"
if (-not (Test-Path $template)) { "시나리오가 없다: $template"; exit 1 }

function Invoke-Run([string]$label, [string]$vertexSwitch) {
    $scenario = Join-Path $Work "experiment_vertex_live_$label.txt"
    $savedScene = Join-Path $Work "experiment_vertex_live_$label.creator"
    Remove-Item -LiteralPath $savedScene -Force -ErrorAction SilentlyContinue
    (Get-Content $template -Raw).Replace('__SCENE__', $Scene.Replace('\', '/')).
        Replace('__SKINNED_MODEL__', $SkinnedModel.Replace('\', '/')).
        Replace('__SAVED_SCENE__', $savedScene.Replace('\', '/')) |
        Set-Content -LiteralPath $scenario -Encoding UTF8

    $stdout = Join-Path $Work "experiment_vertex_live_$label.out.log"
    $stderr = Join-Path $Work "experiment_vertex_live_$label.err.log"
    $env:CREATOR_EXPERIMENT_VERTEX = $vertexSwitch
    try {
        # 작업 디렉터리는 저장소 루트다 — dx12.scene의 셰이더 해석이 루트 기준.
        $proc = Start-Process -FilePath $Exe -ArgumentList @("--script", $scenario) `
            -WorkingDirectory $repoRoot -WindowStyle Hidden `
            -RedirectStandardOutput $stdout -RedirectStandardError $stderr -PassThru
        if (-not $proc.WaitForExit($TimeoutSec * 1000)) {
            # 잔존 PID 오인을 막는다 — 이 게이트가 띄운 그 PID만 죽인다.
            Stop-Process -Id $proc.Id -Force -ErrorAction SilentlyContinue
            "[$label] $TimeoutSec 초 내에 끝나지 않았다."
            exit 1
        }
    }
    finally {
        Remove-Item Env:CREATOR_EXPERIMENT_VERTEX -ErrorAction SilentlyContinue
    }

    return Get-Content $stdout -Raw
}

function Get-ExperimentUploads([string]$log) {
    if ($log -match '메시 업로드\s+\d+\(experiment\s+(\d+)') { return [int]$Matches[1] }
    return -1
}
function Get-TotalUploads([string]$log) {
    if ($log -match '메시 업로드\s+(\d+)\(experiment') { return [int]$Matches[1] }
    return -1
}
function Get-DrawCount([string]$log) {
    if ($log -match '\[3/4\] 씬 카메라 렌더 — 드로우\s+(\d+)') { return [int]$Matches[1] }
    return -1
}
function Get-Coverage([string]$log) {
    if ($log -match '커버리지\s+(\d+)/65536') { return [int]$Matches[1] }
    return -1
}
function Get-Luminance([string]$log) {
    if ($log -match '라이팅 — 광원 \d+개 밝기 (\d+\.\d+)') { return $Matches[1] }
    return ""
}
function Get-ForwardDraws([string]$log) {
    if ($log -match 'Forward\+ — 포워드 드로우 (\d+)') { return [int]$Matches[1] }
    return -1
}
function Get-ForwardBatches([string]$log) {
    # 큐 크기가 아니라 패스가 실제 구성한 배치 수 — 배치 구성이 조용히
    # 버리는 결함(레이아웃 축 소실)은 이 계수로만 갈린다. "발행"(드로우
    # 계수)은 배치 이전의 큐 순회 수라 판별력이 없다(변이 실측).
    if ($log -match '포워드 드로우 \d+\(발행 \d+ · 배치 (\d+)') { return [int]$Matches[1] }
    return -1
}

$fail = @()

# ── A: 스위치 켬(기본) ──
$logOn = Invoke-Run "on" "1"
$dualCount = ([regex]::Matches($logOn, '\[model\.dual\] experiment 경로')).Count
$uploadsOn = Get-ExperimentUploads $logOn
$drawsOn = Get-DrawCount $logOn
$coverOn = Get-Coverage $logOn
$scenePassOn = $logOn -match '\[CLI\] dx12\.scene 통과'

if ($dualCount -lt 1) { $fail += "1 [model.dual] experiment 경로 0건 — 로드가 legacy다" }
# I5-D34b: 스킨 모델도 experiment 경로로 로드됐는가 — 이게 없으면 스킨
# 레이아웃 축이 legacy로 새어도 아래 합산 단정이 못 가른다.
if ($logOn -notmatch '\[model\.dual\] experiment 경로: Gunner_F_Mythic\.glb') {
    $fail += "1b 스킨 모델(Gunner)이 experiment 경로가 아니다"
}
if ($uploadsOn -le 0) { $fail += "2 experiment 업로드 $uploadsOn — GPU 정점 출처가 legacy다" }
# I5-D34b: 업로드 전량이 experiment여야 한다(N == M). 스킨 메시 하나라도
# legacy로 새면 여기서 갈린다 — 스킨 전용 계수 없이 성립하는 전량 단정.
$totalOn = Get-TotalUploads $logOn
if ($totalOn -lt 0 -or $totalOn -ne $uploadsOn) {
    $fail += "2b 업로드 전량이 experiment가 아니다 — 총 $totalOn vs experiment $uploadsOn"
}
if (-not $scenePassOn) { $fail += "3 dx12.scene 실패(on) — experiment 버퍼로 그린 그림이 단정을 깼다" }
# I5-D34c: forward 큐가 실제로 채워졌는가 — matmode 없이는 Forward 레이아웃
# 축이 한 번도 돌지 않고, 이 단정 없이는 그 누락이 조용히 통과로 나온다.
$fwdOn = Get-ForwardDraws $logOn
if ($fwdOn -le 0) { $fail += "3b 포워드 드로우 $fwdOn — Forward 레이아웃 축이 돌지 않았다" }
# ★ 3c — 큐에 있는 드로우가 실제 배치로 구성됐는가. experiment 메시를
#   배치가 조용히 버리면(레이아웃 축 소실) 큐 크기(3b)는 그대로라 여기서만
#   갈린다(변이 실측: 배치 continue가 정확히 배치 0으로 드러남).
$fwdBatchOn = Get-ForwardBatches $logOn
if ($fwdBatchOn -le 0) {
    $fail += "3c 포워드 배치 $fwdBatchOn — 큐 $fwdOn 인데 배치가 비었다(드로우를 버렸다)"
}

"on  — model.dual $dualCount 건 · experiment 업로드 $uploadsOn/$totalOn · 드로우 $drawsOn(포워드 $fwdOn) · 커버리지 $coverOn · dx12.scene $(if ($scenePassOn) {'통과'} else {'실패'})"

# ── B: 스위치 끔 ──
$logOff = Invoke-Run "off" "0"
$uploadsOff = Get-ExperimentUploads $logOff
$drawsOff = Get-DrawCount $logOff
$coverOff = Get-Coverage $logOff
$scenePassOff = $logOff -match '\[CLI\] dx12\.scene 통과'

if ($uploadsOff -ne 0) { $fail += "4a 스위치를 껐는데 experiment 업로드 $uploadsOff" }
if ($drawsOff -ne $drawsOn) {
    $fail += "4b 드로우 수가 다르다 — on $drawsOn vs off $drawsOff (경로 전환이 그리는 대상을 바꿨다)"
}
if (-not $scenePassOff) { $fail += "4c dx12.scene 실패(off) — 대조군이 성립하지 않는다" }
$fwdOff = Get-ForwardDraws $logOff
if ($fwdOff -ne $fwdOn) {
    $fail += "4f 포워드 드로우가 다르다 — on $fwdOn vs off $fwdOff (경로 전환이 forward 큐를 바꿨다)"
}
# ★ 4d — 이 게이트의 실질 이빨. 하네스 자체의 커버리지 단정은 "0이 아니다"
#   수준이라 지오메트리 붕괴에 눈멀었다(변이 실측: POSITION 오프셋 +12로
#   36706 → 2245가 됐는데 하네스는 초록). 같은 씬·같은 카메라를 두 경로로
#   그리므로 커버리지는 **정확히 같아야** 한다 — 레이아웃 유도가 틀리면
#   여기가 붉는다.
if ($coverOn -lt 0 -or $coverOff -lt 0 -or $coverOn -ne $coverOff) {
    $fail += "4d 커버리지가 다르다 — on $coverOn vs off $coverOff (experiment 레이아웃이 지오메트리를 바꿨다)"
}
# ★ 4e — 커버리지가 못 잡는 축. NORMAL 오프셋이 틀리면 지오메트리(커버리지)는
#   그대로인데 라이팅만 틀린다. 언팩 왕복이 float를 비트 보존하므로(modelbridge
#   게이트의 필드 대조) 두 경로의 밝기는 문자열까지 같아야 한다.
$lumOn = Get-Luminance $logOn
$lumOff = Get-Luminance $logOff
if ([string]::IsNullOrEmpty($lumOn) -or $lumOn -ne $lumOff) {
    $fail += "4e 밝기가 다르다 — on '$lumOn' vs off '$lumOff' (experiment 레이아웃이 라이팅을 바꿨다)"
}

"off — experiment 업로드 $uploadsOff · 드로우 $drawsOff · 커버리지 $coverOff · dx12.scene $(if ($scenePassOff) {'통과'} else {'실패'}) (기대: 0 · $drawsOn · $coverOn · 통과)"

if ($fail.Count -gt 0) {
    ""
    "실패 $($fail.Count) 건:"
    $fail | ForEach-Object { "  [실패] $_" }
    exit 1
}

""
"전체 통과 — 정적 메시의 GPU 정점 출처가 experiment packed로 바뀌었고, 하네스 단정은 두 경로에서 같다"
exit 0
