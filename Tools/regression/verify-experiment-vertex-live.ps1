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
#   1d 씬 배치 인스턴스화가 experiment 직행이다 (D4d)
#   1e 라이브 애니 틱이 legacy 경로로 새지 않는다 (D4e-1)
#   6  재생 팔레트 패리티 — legacy 재귀 vs experiment 단일 순회, 제품 함수
#      직접 대조 (D4e-1)
#   7  이벤트·루프 오버라이드 이관 — 합성 seed→저장·재로드→왕복·비오염·발화
#      (D4e-2, on/off 양쪽 — 이관은 스위치 무관)
#   8  본 이름 해석 창구 — 전수 A/B(실물 ResolveBoneIndex vs legacy FindBone),
#      경로 실분기 관측 (D4e-3)
#   9  AvatarMask 트리 생성 — experiment 단일 패스 vs legacy 재귀, 순서까지
#      대조(저장분 인덱스 대응 계약) (D4e-3)
#   10 Foliage 메시 핸들 합류 — 합성 seed→재로드→바인딩·DrawSource·뷰 완비
#      (D5a, off 대조군 4o)
#   11 에디터 실소비 창구 — 클립 열거·이름(인덱스별)과 메시 존재 가드를
#      씬 전수로 legacy 직소비와 대조 (D5b, off 대조군 4p)
#   12 재질 병행 표현 — 합성 새 정본 seed→저장(ref 표기)→재로드→저작 원본
#      기반 합성이 legacy 왕복과 값 동등한가 + **왕복 손실 실측** (D5c1)
#      + packing 바이트 A/B(c2-1)와 프록시 저작 정본 운반(c2-2)
#   13 편집 반영 — 실물 편집 창구가 legacy→인스턴스→프록시까지 값을 나르는가
#      (D5c3, c2-2가 만든 비대칭을 닫는다)
#   14 texture owner — sealing이 저작 GUID(M2 resolver)로 얻은 owner가 legacy
#      이름 맵과 같은가 (D5c3-2, M2의 첫 제품 소비자)
#   16 저작 단독 시공 — 제품 seal이 legacy Material을 읽지 않고 저작본만으로
#      SealSource를 짓는다. 기존 2단계와 texture owner 전수 대조 + 인스턴스
#      채널의 유일한 실소비(useNormalMap) 유도가 공허하지 않은가 (D5c5)
#   17 임베디드 텍스처 신원 — 소스 로드 경로가 모델 sidecar의 subAssets를 읽어
#      texture property에 GUID를 싣는가. 생략 결함은 present 계수로만 보인다 (I2-E)
#   15 바운드 축 — 역브리지의 legacy 정점 시공 절단(legacyVertices on 0 ·
#      off 전량)·바운드 생존(degenerate 0)·두 유도의 값 동수(digest를
#      on/off로 대조 — on은 experiment 정본 주입, off는 정점→min/max
#      유도다). 드로우·커버리지 축은 바운드에 눈멀다(D4f-0 예행 실측:
#      legacy 정점 없이도 드로우 9·커버리지 42411) (D4f-1, off 4q)
#   4  A/B 대조 — 스위치 끄면 experiment 0, 드로우·커버리지·밝기 동일,
#      하네스 여전히 통과 (경로만 바뀌고 그리는 대상·그림 판정은 같다)
#   4i off 대조군의 인스턴스화는 전량 legacy 재귀다 (D4d)
#   5  두 경로가 저장한 씬의 구조 동수 — 엔티티 이름 전수·본/렌더러 계수·
#      (이름 ← 부모이름) 쌍 (D4d)
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
    # I5-D5a: foliage 자산 게시 위치·verify 모드 치환(on=experiment, off=legacy).
    # 게시는 저작 루트(Assets\Foliage) 안만 허용된다(AssetAuthoringPort 경로
    # 가드) — 게이트 산물(gate_foliage.*)은 아래 finally 정리가 걷는다.
    $foliageMode = if ("1" -eq $vertexSwitch) { "experiment" } else { "legacy" }
    $foliageDir = Join-Path $repoRoot "Dynamic_CPP\Assets\Foliage"
    # I5-D5c1: 합성 새 정본 재질 게시 위치. 코퍼스에 shaderAssetId/ref 표기가
    # 0건이라(실측) 저작 경로가 실자산에서 돈 적이 없다 — 게이트 산물
    # (GateAuthoredMat.*)은 아래 finally 정리가 걷는다.
    $materialDir = Join-Path $repoRoot "Dynamic_CPP\Assets\Materials"
    (Get-Content $template -Raw).Replace('__SCENE__', $Scene.Replace('\', '/')).
        Replace('__SKINNED_MODEL__', $SkinnedModel.Replace('\', '/')).
        Replace('__SAVED_SCENE__', $savedScene.Replace('\', '/')).
        Replace('__FOLIAGE_DIR__', $foliageDir.Replace('\', '/')).
        Replace('__MATERIAL_DIR__', $materialDir.Replace('\', '/')).
        Replace('__FOLIAGE_MODE__', $foliageMode) |
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
function Get-HandleUploads([string]$log) {
    # I5-D4b: 핸들 진입점 계수. lookup 폴백과 분리 — 핸들이 안 실려도 lookup이
    # 받쳐 experiment 계수는 그대로이므로, 이 계수 없이는 핸들 경로 소실이
    # 조용히 통과한다.
    if ($log -match '메시 업로드\s+\d+\(experiment\s+\d+,\s+handle\s+(\d+)') { return [int]$Matches[1] }
    return -1
}
function Get-BoundsDigest([string]$log) {
    # I5-D4f-1: 두 경로가 산출한 바운드의 값 digest. on/off가 서로 다른
    # 유도(주입 vs 정점 min/max)를 쓰므로 이 문자열이 같아야 한다.
    if ($log -match 'experiment\.meshbounds pass .*digest=([0-9A-F]{8})') { return $Matches[1] }
    return ""
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
# ★ 1c(I5-D4c) — 씬 postLoad의 이름→메시 해석이 experiment 정본을 탔는가.
#   legacy 해석이 하나라도 남으면 GetMeshShared(name) 참조가 살아 있는 것이다
#   (Assimp 폴백 모델이 없는 이 씬에서는 전량 experiment여야 한다).
$resolveExpOn = ([regex]::Matches($logOn, '\[mesh\.resolve\] experiment:')).Count
$resolveLegacyOn = ([regex]::Matches($logOn, '\[mesh\.resolve\] legacy:')).Count
if ($resolveExpOn -lt 1 -or $resolveLegacyOn -ne 0) {
    $fail += "1c 메시 해석 experiment $resolveExpOn · legacy $resolveLegacyOn — 이름 해석이 legacy로 샜다"
}
# ★ 1d(I5-D4d) — model.place의 인스턴스화가 experiment 직행(parent 단일 순회)
#   인가. legacy 계수가 하나라도 있으면 폴백(계약 불일치·조용한 실패)으로
#   샌 것이다 — 폴백이 받치면 아래 계수·픽셀 단정은 전부 초록이라 여기서만
#   갈린다.
$instExpOn = ([regex]::Matches($logOn, '\[model\.instantiate\] experiment:')).Count
$instLegacyOn = ([regex]::Matches($logOn, '\[model\.instantiate\] legacy:')).Count
if ($instExpOn -lt 1 -or $instLegacyOn -ne 0) {
    $fail += "1d 인스턴스화 experiment $instExpOn · legacy $instLegacyOn — 씬 배치가 legacy 재귀로 샜다"
}
# ★ 1e(I5-D4e-1) — 라이브 애니 틱 경로. Gunner 배치 직후 Animator가 꺼지기
#   전까지의 틱은 experiment 경로여야 한다. legacy 계수가 있으면 재생 바인딩
#   (EnsureExperimentAnimationBinding)이 조용히 실패한 것이다.
# I6-B4b: legacy 재귀 틱이 죽어 로그의 두 번째 값이 'legacy'에서 'none'으로
# 바뀌었다. 뜻도 바뀐다 — 예전엔 "폴백으로 샜다"였고 이제는 "이 애니메이터는
# **안 돈다**"다. 둘 다 on 팔에서 0이어야 한다(옛 토큰도 계속 막는다: 되살아나면
# 그것도 실패다).
$animTickLegacyOn = ([regex]::Matches($logOn, '\[anim\.tick\] (legacy|none)')).Count
if ($animTickLegacyOn -ne 0) {
    $fail += "1e 라이브 애니 틱이 experiment가 아닌 경로 $animTickLegacyOn 건 — 재생 바인딩이 비었다"
}
# ★ 7(I5-D4e-2) — 이벤트·루프 오버라이드가 Animator 소유로 왕복하고 공유
#   자산은 불변이며 발화 규칙이 오버라이드를 소비한다. 합성 seed가 전제 —
#   코퍼스 저작분 0이라 실자산만으로는 이 이관이 영원히 초록이다.
if ($logOn -notmatch '\[CLI\] experiment\.animevent verify pass roundtrip=ok contamination=none firing=ok') {
    $fail += "7 이벤트·루프 이관 실패(on) — animevent 출력을 확인하라"
}
# ★ 8(I5-D4e-3) — 본 해석 창구. 전수 인덱스 일치 + 전량 experiment 실분기
#   (legacy 폴백이 인덱스를 1:1로 받쳐도 실분기 계수가 소실을 가른다).
if ($logOn -notmatch '\[CLI\] experiment\.boneresolve pass bones=(\d+) experiment=\1 legacy=0') {
    $fail += "8 본 해석 창구 실패(on) — boneresolve 출력을 확인하라"
}
# ★ 8b(I6-B2) — 본 캐시 무효화 **신원**의 출처. 8과 별개 축이다: 인덱스를
#   experiment로 풀면서 신원만 legacy 객체 수명에 묶여 있으면 그 객체를
#   은퇴시킬 수 없다(신원이 0이 되면 Scene의 본 전파가 통째로 꺼진다).
if ($logOn -notmatch 'bones=(\d+) experiment=\1 legacy=0 mismatch=0 unresolved=0 serialExperiment=\1 serialLegacy=0') {
    $fail += "8b 본 캐시 신원이 experiment로 서지 않는다(on) — boneresolve serial* 을 확인하라"
}
# ★ 8c(I6-B4a) — **legacy를 안 쓰는** 해석 대조. 이름→인덱스→이름 왕복이다.
#   축 8의 legacy 대조는 은퇴하면 잴 상대가 없어진다 — 그 전에 같은 결함을
#   legacy 없이 잡는 축을 세워 겹치는 구간에서 함께 돌린다(대조군 인수인계).
if ($logOn -notmatch 'boneresolve pass .* roundtrip=0') {
    $fail += "8c 본 이름 왕복이 깨졌다(on) — boneresolve roundtrip 을 확인하라"
}
# ★ 9b(I6-B4a) — 마스크 트리를 **스켈레톤 원자료**와 직접 맞춘다(부모 관계·
#   계수·preorder). 축 9의 legacy 재귀 대조를 대체할 축이다.
if ($logOn -notmatch 'animmask pass masks=[1-9]\d* viaExperiment=1 structure=ok') {
    $fail += "9b 마스크 구조 자기 대조 실패(on) — animmask structure 를 확인하라"
}
# ★ 6b(I6-B4a) — 재생 팔레트 골든. legacy 파리티(축 6)가 은퇴하면 이 digest가
#   재생 산술의 유일한 회귀 감시자다. 정확성이 아니라 **안정성**을 잰다 —
#   의도한 변경이면 값을 갱신하고 왜 바뀌었는지 커밋 메시지에 남긴다.
#   ★ B4b에서 값이 858071B5 → 093A1FC2로 바뀌었다: 하네스가 legacy의
#     Linear 강등 사본이 아니라 **실제 스켈레톤**(Step 보존)을 재게 됐다.
#   ★ 양자화(1/4096)에도 **엄격하다** — 실측으로 0.0001 섭동에서 값이 바뀐다
#     (표본이 많아 늘 어떤 값이 반올림 경계에 있다). x64 Debug 고정 골든이다.
if ($logOn -notmatch 'animtick pass .* poseDigest=093A1FC2') {
    $fail += "6b 재생 팔레트 골든이 달라졌다(on) — animtick poseDigest 를 확인하라"
}
# ★ 9(I5-D4e-3) — 마스크 트리 A/B. 순서 재현(저장분 인덱스 대응)까지 대조.
if ($logOn -notmatch '\[CLI\] experiment\.animmask pass masks=[1-9]\d* viaExperiment=1') {
    $fail += "9 마스크 트리 대조 실패(on) — animmask 출력을 확인하라"
}
# ★ 10(I5-D5a) — Foliage 핸들 합류: postLoad 재해석 바인딩·프록시 DrawSource·
#   뷰 완비까지 실물 사슬로 판정(합성 seed 전제 — 코퍼스 Foliage 저작분 0).
if ($logOn -notmatch '\[CLI\] experiment\.foliage verify pass mode=experiment') {
    $fail += "10 Foliage 핸들 합류 실패(on) — foliage 출력을 확인하라"
}
# ★ 11(I5-D5b) — 에디터 실소비 창구. 에디터 UI는 헤드리스 관측 밖이라 UI가
#   지나게 된 창구 둘을 씬 전수로 잰다: 클립 열거/이름(인덱스별 — 개수만
#   재면 순서 뒤집힘에 눈멀다)과 메시 존재 가드. 경로 계수로 experiment
#   실분기까지 실증한다(legacy 폴백이 같은 값을 받쳐도 계수가 갈린다).
if ($logOn -notmatch '\[CLI\] experiment\.editorsurface pass ') {
    $fail += "11 에디터 창구 A/B 실패(on) — editorsurface 출력을 확인하라"
}
if ($logOn -notmatch '\[CLI\] experiment\.editorsurface pass animators=\d+ clipExperiment=[1-9]') {
    $fail += "11b 클립 열거가 experiment 분기를 한 번도 타지 않았다(on)"
}
if ($logOn -notmatch 'experiment\.editorsurface pass .*meshExperiment=[1-9]') {
    $fail += "11c 메시 가드가 experiment 분기를 한 번도 타지 않았다(on)"
}
# ★ 17(I2-E) — 임베디드 텍스처 신원. 모델 sidecar의 subAssets.embeddedTextures를
#   소스 로드 경로가 읽어 texture property에 실제 GUID를 싣는가. 생략은 '없는
#   property'라 값 대조로는 안 보인다 — present(textureProps)를 함께 센다.
#   실측: 고치기 전 Gunner textureProps=0(전량 생략), 고친 뒤 6/6 valid.
if ($logOn -notmatch 'experiment\.embedded pass .*textureProps=([1-9]\d*) validAssetId=([1-9]\d*) fallbackOnly=0') {
    $fail += "17 임베디드 텍스처 신원이 실리지 않았다(on) — property가 생략됐거나 GUID가 nil이다"
}

# ★ 15(I5-D4f-1) — 바운드 축. 절단이 실제로 일어났고(legacyVertices=0),
#   바운드가 기본값으로 남지 않았으며(degenerate=0), experiment 정본과
#   대조된 메시가 실존하는가(expBound>0). 값 동수는 아래 4r이 진다.
if ($logOn -notmatch '\[CLI\] experiment\.meshbounds pass ') {
    $fail += "15 바운드 축 실패(on) — meshbounds 출력을 확인하라"
}
# 이 씬의 메시는 전량 역브리지 산물이다(1·1b가 그것을 따로 잰다). Assimp
# 폴백이나 절차 생성 메시가 섞이면 그쪽도 여기서 붉는데, 그것도 알아야 할
# 사실이다 — on 경로에 legacy 정점 원본이 남았다는 뜻이므로.
if ($logOn -notmatch 'experiment\.meshbounds pass .*legacyVertices=0 ') {
    $fail += "15b legacy 정점 시공이 남아 있다(on) — 절단이 돌지 않았거나 legacy 원본 메시가 섞였다"
}
if ($logOn -notmatch 'experiment\.meshbounds pass .*expBound=[1-9]') {
    $fail += "15c experiment 정본과 대조된 메시가 0이다(on) — 축이 비었다"
}
# ★ 12(I5-D5c1) — 재질 병행 표현. seed가 저작 경로 그대로 새 정본 자산을
#   게시하고 renderer를 base에 링크하므로, 저장·재로드가 ref 표기를 왕복해야
#   병행 표현이 채워진다. withInstance=0이면 저작 경로 어딘가가 끊긴 것이다
#   (skip으로 나오며, 이 단정이 그것을 통과로 읽지 않는다).
if ($logOn -notmatch '\[CLI\] experiment\.matruntime pass ') {
    $fail += "12 재질 병행 표현 실패(on) — matruntime 출력을 확인하라"
}
if ($logOn -notmatch 'experiment\.matruntime pass .*withInstance=[1-9]') {
    $fail += "12b 병행 표현이 하나도 채워지지 않았다(on) — 저작 경로가 끊겼다"
}
if ($logOn -notmatch 'experiment\.matruntime pass .*compared=[1-9]') {
    $fail += "12c 대조가 한 건도 성립하지 않았다(on)"
}
# ★ 12d(I5-D5c2-2) — 프록시가 저작 정본을 나르는가. 이 단정이 없으면 프록시
#   배선이 끊겨도 proxyValueMismatch=0(비교할 것이 없으니)으로 통과한다 —
#   "0개를 비교해 차이 0"의 전형이다.
if ($logOn -notmatch 'experiment\.matruntime pass .*proxyAuthored=[1-9]') {
    $fail += "12d 프록시가 저작 정본을 하나도 나르지 않았다(on) — 스냅샷이 끊겼다"
}
# ★ 13(I5-D5c3) — 편집 반영. `MaterialScriptBinding.h`의 계약은 "논리 값 갱신이
#   곧 화면 갱신"인데, c2-2가 sealing을 저작 정본 직행으로 바꾸면서 저작 재질에서
#   그 계약이 깨져 있었다(편집은 legacy만 바꾸고 sealing은 인스턴스를 읽는다).
#   이 축은 실물 편집 창구를 태워 legacy→인스턴스→**프록시**까지 값이 닿는지
#   본다 — 프록시가 종착점이다(인스턴스만 따라오면 화면은 여전히 안 바뀐다).
if ($logOn -notmatch '\[CLI\] experiment\.matruntime edit pass ') {
    $fail += "13 편집이 저작 정본에 반영되지 않는다(on) — edit 출력을 확인하라"
}
# ★ 14(I5-D5c3-2) — texture owner 전환. sealing이 texture generation owner를
#   legacy 이름 맵 대신 저작 GUID(M2 resolver)에서 얻는다. 이 전환이 그림을
#   바꾸지 않으려면 두 경로가 **같은 owner**를 줘야 한다.
#   texResolvedOwners 단정이 없으면 seed에 텍스처가 없을 때 nullptr끼리 비교해
#   통과한다 — 실제로 한 번 그렇게 나왔다(눈먼 초록).
if ($logOn -notmatch 'experiment\.matruntime pass .*texOwnerMismatch=0 texResolveFailed=0') {
    $fail += "14 저작 texture 해석이 legacy 맵과 갈렸다(on) — 그림이 바뀐다"
}
if ($logOn -notmatch 'experiment\.matruntime pass .*texResolvedOwners=[1-9]') {
    $fail += "14b resolver가 실제 texture owner를 하나도 해석하지 않았다(on)"
}
# ★ 16(I5-D5c5) — 저작 단독 시공. 제품 seal은 이제 legacy Material을 읽지 않고
#   저작본만으로 SealSource를 짓는다. 그것이 기존 2단계(legacy 시공→덮어쓰기)와
#   같은 산출인지 in-process로 대조한다(texture owner 전수).
if ($logOn -notmatch 'experiment\.matruntime pass .*sealAuthored=[1-9]') {
    $fail += "16 저작 단독 시공이 한 번도 성립하지 않았다(on)"
}
if ($logOn -notmatch 'experiment\.matruntime pass .*sealAuthoredFail=0 sealAuthoredTexMismatch=0') {
    $fail += "16b 저작 단독 시공이 기존 2단계와 갈렸다(on) — 그림이 바뀐다"
}
# 16c — useNormalMap은 인스턴스 채널의 유일한 실소비다(ForwardShade:441·
#   GBuffer:237). 저작 유도가 1을 내지 못하면 그 축은 0과 0을 비교한 것이라
#   판별력이 없다 — seed가 normalMap 슬롯을 싣는 이유가 여기 있다.
if ($logOn -notmatch 'experiment\.matruntime pass .*normalMapDerived=[1-9]') {
    $fail += "16c useNormalMap이 저작 정본에서 유도되지 않았다(on) — 축이 공허하다"
}
# 왕복 손실은 **판정하지 않고 보고**한다 — 이 슬라이스의 목적은 손실을 없애는
# 것이 아니라 크기를 아는 것이다(처방은 c2). 값을 아래 요약이 찍는다.
if ($uploadsOn -le 0) { $fail += "2 experiment 업로드 $uploadsOn — GPU 정점 출처가 legacy다" }
# I5-D34b: 업로드 전량이 experiment여야 한다(N == M). 스킨 메시 하나라도
# legacy로 새면 여기서 갈린다 — 스킨 전용 계수 없이 성립하는 전량 단정.
$totalOn = Get-TotalUploads $logOn
if ($totalOn -lt 0 -or $totalOn -ne $uploadsOn) {
    $fail += "2b 업로드 전량이 experiment가 아니다 — 총 $totalOn vs experiment $uploadsOn"
}
# ★ 2c(I5-D4b) — 전량이 핸들 진입점이어야 한다. 이 씬의 메시는 전부 모델
#   유래(바인딩 존재)라 핸들이 하나도 새면 프록시→아이템→패스 사슬 어딘가가
#   핸들을 흘린 것이다. lookup 폴백이 받쳐 2·2b는 초록이므로 여기서만 갈린다.
$handleOn = Get-HandleUploads $logOn
if ($handleOn -lt 0 -or $handleOn -ne $totalOn) {
    $fail += "2c 핸들 경로 업로드 $handleOn/$totalOn — 핸들이 새고 lookup 폴백이 받쳤다"
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

"on  — model.dual $dualCount 건 · 인스턴스화 experiment $instExpOn/legacy $instLegacyOn · 메시 해석 experiment $resolveExpOn/legacy $resolveLegacyOn · experiment 업로드 $uploadsOn/$totalOn(핸들 $handleOn) · 드로우 $drawsOn(포워드 $fwdOn) · 커버리지 $coverOn · dx12.scene $(if ($scenePassOn) {'통과'} else {'실패'})"

# ── B: 스위치 끔 ──
$logOff = Invoke-Run "off" "0"
$uploadsOff = Get-ExperimentUploads $logOff
$drawsOff = Get-DrawCount $logOff
$coverOff = Get-Coverage $logOff
$scenePassOff = $logOff -match '\[CLI\] dx12\.scene 통과'

if ($uploadsOff -ne 0) { $fail += "4a 스위치를 껐는데 experiment 업로드 $uploadsOff" }
# I5-D4b: 스위치가 핸들 경로도 막는가 — TryGetExperimentMeshBinding이 스위치를
# 안 보면 off 대조군이 반쪽이 된다.
$handleOff = Get-HandleUploads $logOff
if ($handleOff -ne 0) { $fail += "4g 스위치를 껐는데 핸들 업로드 $handleOff" }
# I5-D4c: 스위치가 이름 해석 정본 전환도 막는가 — off 대조군은 전량 legacy
# 해석이어야 한다(TryGetExperimentModel이 스위치를 본다).
$resolveExpOff = ([regex]::Matches($logOff, '\[mesh\.resolve\] experiment:')).Count
if ($resolveExpOff -ne 0) { $fail += "4h 스위치를 껐는데 experiment 해석 $resolveExpOff" }
# I5-D4d: 스위치가 인스턴스화 정본 전환도 막는가 — off 대조군은 전량 legacy
# 재귀여야 한다(TryGetExperimentModel이 스위치를 본다).
$instExpOff = ([regex]::Matches($logOff, '\[model\.instantiate\] experiment:')).Count
$instLegacyOff = ([regex]::Matches($logOff, '\[model\.instantiate\] legacy:')).Count
if ($instExpOff -ne 0 -or $instLegacyOff -lt 1) {
    $fail += "4i 스위치를 껐는데 인스턴스화 experiment $instExpOff · legacy $instLegacyOff"
}
# I5-D4e-1: 스위치가 재생 경로도 막는가 — off 대조군의 애니 틱은 전량 legacy,
# animtick은 대상 0(skip)이어야 한다(TryGetExperimentModel이 스위치를 본다).
$animTickExpOff = ([regex]::Matches($logOff, '\[anim\.tick\] experiment')).Count
if ($animTickExpOff -ne 0) {
    $fail += "4j 스위치를 껐는데 experiment 애니 틱 $animTickExpOff 건"
}
if ($logOff -notmatch '\[CLI\] experiment\.animtick skip animators=0') {
    $fail += "4k 스위치를 껐는데 animtick 대상이 0이 아니다 — 재생 바인딩이 스위치를 무시한다"
}
# I5-D4e-2: 이관은 스위치와 무관한 무조건 경로 — off 대조군에서도 왕복·비오염·
# 발화가 같이 통과해야 한다(legacy 틱도 같은 오버라이드를 소비한다).
if ($logOff -notmatch '\[CLI\] experiment\.animevent verify pass roundtrip=ok contamination=none firing=ok') {
    $fail += "4l 이벤트·루프 이관 실패(off) — 이관이 스위치 뒤로 샜다"
}
# I5-D4e-3: off 대조군 — 창구는 전량 legacy 폴백이어야 하고(인덱스는 그래도
# 전수 일치), 마스크 생성도 legacy 경로다.
if ($logOff -notmatch '\[CLI\] experiment\.boneresolve pass bones=(\d+) experiment=0 legacy=\1') {
    $fail += "4m 스위치를 껐는데 본 해석이 experiment로 샜다(또는 실패)"
}
# I6-B2: off 대조군 — 신원도 전량 legacy serial이어야 한다. 이 팔이 있어야
# "언제나 experiment라고 적는 계수기"가 8b를 공짜로 통과하지 못한다.
if ($logOff -notmatch 'bones=(\d+) experiment=0 legacy=\1 mismatch=0 unresolved=0 serialExperiment=0 serialLegacy=\1') {
    $fail += "4m2 스위치를 껐는데 본 캐시 신원이 experiment로 샜다"
}
# I6-B4a: 왕복 축은 backend에 무관한 자기 검사라 off에서도 성립해야 한다 —
# 여기서 갈리면 legacy 폴백의 이름 사상이 깨진 것이다.
if ($logOff -notmatch 'boneresolve pass .* roundtrip=0') {
    $fail += "4m3 스위치를 껐는데 본 이름 왕복이 깨졌다"
}
if ($logOff -notmatch '\[CLI\] experiment\.animmask pass masks=[1-9]\d* viaExperiment=0') {
    $fail += "4n 스위치를 껐는데 마스크 생성이 experiment로 샜다(또는 실패)"
}
# I5-D5a: off 대조군 — Foliage 바인딩·뷰가 전량 legacy(핸들 0)여야 한다.
if ($logOff -notmatch '\[CLI\] experiment\.foliage verify pass mode=legacy') {
    $fail += "4o 스위치를 껐는데 Foliage 핸들이 experiment로 샜다(또는 실패)"
}
# I5-D5b: off 대조군 — 창구는 전량 legacy 폴백이어야 하고(값은 그래도 전수
# 일치: 역브리지가 두 출처를 1:1로 묶는다), 경로 계수가 0이어야 한다.
if ($logOff -notmatch '\[CLI\] experiment\.editorsurface pass ') {
    $fail += "4p 에디터 창구 A/B 실패(off) — 대조군이 성립하지 않는다"
}
if ($logOff -notmatch 'experiment\.editorsurface pass .*clipExperiment=0 ') {
    $fail += "4p-1 스위치를 껐는데 클립 열거가 experiment로 샜다"
}
if ($logOff -notmatch 'experiment\.editorsurface pass .*meshExperiment=0 ') {
    $fail += "4p-2 스위치를 껐는데 메시 가드가 experiment로 샜다"
}
# ★ 4q(I5-D4f-1) — off 대조군은 legacy 정점을 **짓는다**. 이 슬라이스가
#   스위치의 뜻을 넓힌 자리다: 넓히지 않으면 역브리지가 정점을 안 채워 off가
#   그릴 것을 잃는다(D4f-0 예행 실측 — 드로우 0·커버리지 0). legacyVertices가
#   0이면 그 확장이 끊긴 것이고, 아래 4b/4d 동수는 둘 다 빈 그림이라 통과할
#   수 있다("0개를 비교해 차이 0").
if ($logOff -notmatch '\[CLI\] experiment\.meshbounds pass ') {
    $fail += "4q 바운드 축 실패(off) — 대조군이 성립하지 않는다"
}
if ($logOff -notmatch 'experiment\.meshbounds pass .*legacyVertices=[1-9]') {
    $fail += "4q-1 스위치를 껐는데 legacy 정점을 짓지 않았다 — off 대조군이 빈 그림이다"
}
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
# ★ 4r(I5-D4f-1) — 바운드 값 동수. 이 축의 실질 이빨이다. on은 experiment
#   정본을 주입하고 off는 legacy 정점에서 min/max를 유도하므로, 두 산출이
#   같은 값을 내야 한다. 주입이 틀린 출처를 쓰면(다른 메시·다른 공간) 여기서만
#   갈린다 — 드로우·커버리지·밝기는 바운드를 읽지 않고, degenerate=0은 "비지
#   않았다"만 말한다.
$boundsOn = Get-BoundsDigest $logOn
$boundsOff = Get-BoundsDigest $logOff
if ([string]::IsNullOrEmpty($boundsOn) -or $boundsOn -ne $boundsOff) {
    $fail += "4r 바운드 digest가 다르다 — on '$boundsOn' vs off '$boundsOff' (주입이 legacy 유도와 갈렸다)"
}
$lumOn = Get-Luminance $logOn
$lumOff = Get-Luminance $logOff
if ([string]::IsNullOrEmpty($lumOn) -or $lumOn -ne $lumOff) {
    $fail += "4e 밝기가 다르다 — on '$lumOn' vs off '$lumOff' (experiment 레이아웃이 라이팅을 바꿨다)"
}

"off — experiment 업로드 $uploadsOff · 드로우 $drawsOff · 커버리지 $coverOff · dx12.scene $(if ($scenePassOff) {'통과'} else {'실패'}) (기대: 0 · $drawsOn · $coverOn · 통과)"

# ★ 5(I5-D4d) — 두 경로가 저장한 씬의 구조 동수. experiment 단일 순회가
#   legacy 재귀와 같은 계층을 세웠는가를 픽셀 밖에서 직접 잰다: 엔티티 이름
#   전수(정렬)·BoneComponent·MeshRenderer 계수가 문자열까지 같아야 한다.
#   (부모 관계·트랜스폼 차이는 4b/4d/4e의 렌더 동수가 가른다 — 이름 동수만으론
#   못 잡는 축이라 겹으로 둔다.)
$sceneOnPath = Join-Path $Work "experiment_vertex_live_on.creator"
$sceneOffPath = Join-Path $Work "experiment_vertex_live_off.creator"
if (-not (Test-Path $sceneOnPath) -or -not (Test-Path $sceneOffPath)) {
    $fail += "5 저장 씬이 없다 — on $(Test-Path $sceneOnPath) / off $(Test-Path $sceneOffPath)"
}
else {
    $sceneOnText = Get-Content $sceneOnPath
    $sceneOffText = Get-Content $sceneOffPath
    # 엔티티 수준 m_name은 4칸 들여쓰기(컴포넌트 수준은 8칸)라 여기서 갈린다.
    # 씬 루트는 저장 파일명을 따라가므로(on/off 라벨 차이 — 하네스 산물)
    # 정규화한다. 실측: 이것만 달랐고 나머지 75개는 동일했다.
    $namesOn = ($sceneOnText | Where-Object { $_ -match '^    m_name: (.+)$' } |
        ForEach-Object { $Matches[1] -replace '^experiment_vertex_live_on$', '<sceneroot>' }) | Sort-Object
    $namesOff = ($sceneOffText | Where-Object { $_ -match '^    m_name: (.+)$' } |
        ForEach-Object { $Matches[1] -replace '^experiment_vertex_live_off$', '<sceneroot>' }) | Sort-Object
    if (($namesOn -join '|') -ne ($namesOff -join '|')) {
        $fail += "5a 엔티티 이름 집합이 다르다 — on $($namesOn.Count)개 vs off $($namesOff.Count)개"
    }
    foreach ($componentName in @('BoneComponent', 'MeshRenderer')) {
        $countOn = ($sceneOnText | Where-Object { $_ -match "- ${componentName}:" }).Count
        $countOff = ($sceneOffText | Where-Object { $_ -match "- ${componentName}:" }).Count
        if ($countOn -ne $countOff -or $countOn -lt 1) {
            $fail += "5b $componentName 계수가 다르다 — on $countOn vs off $countOff"
        }
    }
    # ★ 5c — (이름 ← 부모이름) 쌍 동수. 이름 집합(5a)·계수(5b)는 부착점 훼손
    #   (평탄화)에 불변이고, 렌더 동수(4d/4e)도 스킨 메시에서는 노드 트랜스폼에
    #   눈멀다(M2 변이 실측: 트랜스폼 생략이 전 단정 초록). 계층 구조 자체는
    #   여기서만 갈린다. m_index/m_parentIndex의 절대값은 순회 순서에 따라
    #   달라도 되므로 이름으로 조인해 비교한다.
    function Get-HierarchyPairs([string[]]$sceneLines, [string]$rootLabel) {
        $entities = @()
        $current = $null
        foreach ($line in $sceneLines) {
            if ($line -match '^  - Entity:') {
                if ($null -ne $current) { $entities += $current }
                $current = @{ name = ''; index = -1; parent = -1 }
            }
            elseif ($null -ne $current) {
                if ($line -match '^    m_name: (.+)$') {
                    $current.name = $Matches[1] -replace ('^' + [regex]::Escape($rootLabel) + '$'), '<sceneroot>'
                }
                elseif ($line -match '^    m_index: (\d+)$') { $current.index = [int]$Matches[1] }
                elseif ($line -match '^    m_parentIndex: (-?\d+)$') { $current.parent = [int]$Matches[1] }
            }
        }
        if ($null -ne $current) { $entities += $current }
        $nameByIndex = @{}
        foreach ($entity in $entities) { $nameByIndex[$entity.index] = $entity.name }
        return ($entities | ForEach-Object {
            $parentName = if ($_.parent -lt 0) { '<none>' }
                elseif ($nameByIndex.ContainsKey($_.parent)) { $nameByIndex[$_.parent] }
                else { '<missing>' }
            "$($_.name) <- $parentName"
        }) | Sort-Object
    }
    $pairsOn = Get-HierarchyPairs $sceneOnText 'experiment_vertex_live_on'
    $pairsOff = Get-HierarchyPairs $sceneOffText 'experiment_vertex_live_off'
    if (($pairsOn -join '|') -ne ($pairsOff -join '|')) {
        $diffCount = (Compare-Object $pairsOn $pairsOff | Measure-Object).Count
        $fail += "5c 부모 관계가 다르다 — 어긋난 쌍 $diffCount 건 (계층 구조가 갈렸다)"
    }
    "구조 — 엔티티 $($namesOn.Count)/$($namesOff.Count) · 부모쌍 $($pairsOn.Count)/$($pairsOff.Count)"
}

# I5-D5c1: 왕복 손실 실측 보고(판정 아님 — c2가 화면을 바꾸는지 가르는 기준선).
if ($logOn -match 'experiment\.matruntime \w+ .*compared=(\d+) .*onlyAuthored=(\d+) onlyLegacy=(\d+)') {
    "재질 — 대조 $($Matches[1]) · 저작에만 $($Matches[2]) · legacy에만 $($Matches[3]) (왕복 손실 실측)"
}

# I5-D5a: 게이트가 저작 루트에 게시한 합성 foliage 자산을 걷는다(성공·실패
# 공통 — 코퍼스 오염 방지). 이름이 고정이라 재실행은 어차피 덮어쓴다.
foreach ($gateAsset in @("gate_foliage.foliage", "gate_foliage.foliage.meta")) {
    $gateAssetPath = Join-Path $repoRoot "Dynamic_CPP\Assets\Foliage\$gateAsset"
    if (Test-Path $gateAssetPath) { Remove-Item -LiteralPath $gateAssetPath -Force }
}
# I5-D5c1: 같은 이유로 합성 재질 자산도 걷는다.
foreach ($gateAsset in @("GateAuthoredMat.asset", "GateAuthoredMat.asset.meta")) {
    $gateAssetPath = Join-Path $repoRoot "Dynamic_CPP\Assets\Materials\$gateAsset"
    if (Test-Path $gateAssetPath) { Remove-Item -LiteralPath $gateAssetPath -Force }
}

if ($fail.Count -gt 0) {
    ""
    "실패 $($fail.Count) 건:"
    $fail | ForEach-Object { "  [실패] $_" }
    exit 1
}

""
"전체 통과 — 정적 메시의 GPU 정점 출처가 experiment packed로 바뀌었고, 하네스 단정은 두 경로에서 같다"
exit 0
