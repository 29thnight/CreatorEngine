# DDOL 이송 신호가 C#까지 닿는가 (트랙 L · L3 잔여)
#
# ── 이 검사가 메우는 구멍 ──
#
# 관리 측 생명주기의 드라이버가 둘이다. 네이티브 ScriptComponent는 인스턴스의
# 생성(OnInitialized)과 파괴(OnUninitializing)만 알리고, 그 사이 네 단계는
# BehaviourRegistry가 자기 큐(_pendingAwake/_pendingStart/_pendingRemove)로 굴린다.
# 그래서 네이티브에서만 일어나는 사건 — DontDestroyOnLoad 이송 —이 스크립트에
# 전혀 닿지 않았다. 오브젝트는 살아서 씬을 건너는데 스크립트는 모른다.
#
# ★ 처음 돌렸을 때 두 번째 결함이 함께 나왔다: ClrHost::NotifySceneUnload가
# ScriptObjectRegistry::Clear()를 부르는데 그 함수가 살아남는 DDOL 오브젝트의
# 핸들까지 죽였다. 스크립트는 살아 도는데 자기 GameObject를 잃는다 —
# GameObject.Name이 빈 문자열이 되고 그 경유 API가 전부 무응답이 된다.
# 판정 4가 그것을 잡는다(이름이 남아 있는가).
#
# ── 왜 로그를 세는가 ──
#
# 스크립트의 Log()는 Native.Log를 거쳐 Debug 시스템으로 간다 — stdout이 아니라
# Editor_*.html이다(verify-script-add-awake-once.ps1의 같은 주석 참고).
#
# ── 판정 항목 ──
#
#   1  Awake 1회                      — 관리 드레인이 실제로 돌았다(0이면 재생 안 됨)
#   2  AddedToScene 2회               — ★ 생성 1 + 이송 재부착 1
#   3  RemovingFromScene 2회          — ★ 이송 이탈 1 + 종료 TearDown 1
#   4  이름 없는 훅 로그 0건          — ★ 핸들이 이송을 살아 건넜다
#
# 2·3은 "이중 발화 없음"도 함께 본다 — 예상보다 많으면 그것도 실패다. 이 축은
# 특히 중요하다: 네이티브 훅을 그대로 전달하면 파괴 경로에서 관리 측 TearDown과
# 겹쳐 이중 발화하고, 그것이 이 슬라이스가 이송 경로에서만 통지하는 이유다
# (ScriptBinder/ScriptLifecyclePhase.h 상단).
#
# 음성 시험(2026-08-20 실측):
#   전달 배선 없음            AddedToScene 1 · RemovingFromScene 1  -> 판정 2·3 실패
#   Clear()가 DDOL도 지움     이송 AddedToScene 거부(IsAlive=false) · 이름 빈 로그
#                             -> 판정 2·4 실패
#
# 사용법:
#   pwsh Tools\regression\verify-ddol-script.ps1
param(
    [string]$Exe = "C:\Users\lance\source\CreatorEngine\x64\Debug\Academy_4Q.exe",
    [string]$Work = $env:TEMP,
    [int]$TimeoutSeconds = 300,
    [string]$DestScene = "FT_Material"
)

$exeDir = [System.IO.Path]::GetDirectoryName($Exe)
if (-not (Test-Path $Exe)) { "실행 파일이 없다: $Exe"; exit 1 }

$template = Join-Path $PSScriptRoot "ddol_script_probe.txt"
if (-not (Test-Path $template)) { "시나리오가 없다: $template"; exit 1 }

$repoRoot = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
$dst = Join-Path $repoRoot "Dynamic_CPP\Assets\Scenes\$DestScene.creator"
if (-not (Test-Path $dst)) { "목적 씬이 없다: $dst"; exit 1 }

$scenario = Join-Path $Work "ddol_script_resolved.txt"
(Get-Content $template -Raw) -replace '\{\{DEST_SCENE\}\}', ($dst -replace '\\', '/') |
    Set-Content $scenario -Encoding UTF8

$outPath = Join-Path $Work "ddol_script.out"
$errPath = Join-Path $Work "ddol_script.err"

$proc = Start-Process -FilePath $Exe -ArgumentList "--script", $scenario `
    -WorkingDirectory $exeDir `
    -RedirectStandardOutput $outPath `
    -RedirectStandardError $errPath -PassThru

$proc.WaitForExit($TimeoutSeconds * 1000) | Out-Null
if (-not $proc.HasExited) {
    $proc.Kill()
    "타임아웃 ($TimeoutSeconds 초). 시나리오가 끝나지 않았다."
    exit 1
}

$logDir = Join-Path $exeDir "Log"
$editorLog = Get-ChildItem (Join-Path $logDir "Editor_*.html") -ErrorAction SilentlyContinue |
             Sort-Object LastWriteTime -Descending | Select-Object -First 1
if (-not $editorLog) { "에디터 로그를 찾지 못했다: $logDir\Editor_*.html"; exit 1 }

$logText = (Get-Content -LiteralPath $editorLog.FullName -Raw) -replace '<[^>]+>', ''

$probeName = "DdolScriptProbe"
function Count-Hook([string]$hook) {
    return ([regex]::Matches($logText, "\[Probe\] $hook — $([regex]::Escape($probeName))")).Count
}

$awake     = Count-Hook "Awake"
$added     = Count-Hook "AddedToScene"
$removing  = Count-Hook "RemovingFromScene"

# 이름이 비어 있는 훅 로그 = 그 시점 GameObject 핸들이 죽어 있었다는 뜻이다.
$nameless = ([regex]::Matches($logText, '\[Probe\] (AddedToScene|RemovingFromScene) — (\s|·)')).Count

# ── 훅 순서 ──
#
# 개수만으로는 "드라이버를 네이티브로 옮기는 동안 순서가 보존됐는가"를 못 본다
# (설계 문서 §4 트랙 L · L3 잔여 2단계). 네이티브 기준선(200사건)은 네이티브
# 컴포넌트만 담아 관리 측 훅을 말하지 못하므로, 이 자가 유일하다.
$sequence = ([regex]::Matches($logText, '\[Probe\] (\w+) —')) |
            ForEach-Object { $_.Groups[1].Value }
$sequenceText = ($sequence -join ' > ')

"훅 순서: $sequenceText"
""
"Awake              $awake 회 (기대 1 — 0이면 재생/드레인이 안 돌았다)"
"AddedToScene       $added 회 (기대 2 — 생성 1 + 이송 재부착 1)"
"RemovingFromScene  $removing 회 (기대 2 — 이송 이탈 1 + 종료 1)"
"이름 없는 훅 로그  $nameless 건 (기대 0 — 핸들이 이송을 살아 건넜는가)"
""

$failed = @()
if ($awake -ne 1) {
    $failed += "Awake가 $awake 회다 — 0이면 재생이 안 돌아 이 검사가 아무것도 재지 못한 것이다(play 확인)"
}
if ($added -lt 2) {
    $failed += "AddedToScene가 $added 회다 — 이송 재부착 통지가 스크립트에 닿지 않았다(Scene::AttachExistingGameObject의 NotifyManagedLifecycle 확인)"
} elseif ($added -gt 2) {
    $failed += "AddedToScene가 $added 회다 — 이중 발화다(관리 측 드레인과 겹쳤는지 확인)"
}
if ($removing -lt 2) {
    $failed += "RemovingFromScene가 $removing 회다 — 이송 이탈 통지가 닿지 않았다(Scene::DetachGameObjectHierarchy 확인)"
} elseif ($removing -gt 2) {
    $failed += "RemovingFromScene가 $removing 회다 — 이중 발화다(파괴 경로에서도 전달되고 있는지 확인)"
}
if ($nameless -gt 0) {
    $failed += "이름 없는 훅 로그가 $nameless 건이다 — DDOL 오브젝트의 관리 핸들이 씬 언로드에서 죽었다(ScriptObjectRegistry::Clear의 DDOL 예외 확인)"
}
# 기대 순서. 관리 측 드라이버를 옮겨도 이 줄이 바뀌면 안 된다 — 바뀌었다면
# 그것이 이 슬라이스의 회귀다(개수는 맞는데 순서만 틀리는 경우를 잡는다).
# 2026-08-20 실측 기준선. 이 줄이 트랙 L5가 제시한 구조를 그대로 증명한다:
#
#   · SimulateStart가 Start(OnBeginSimulation) **직후**    — 본문의 시작 지점
#   · SimulateResume                                        — Scope.Delay가 엔진 dt로 흘러 재개
#   · RemovingFromScene > AddedToScene 사이를 건너 살아남음 — 이송에서 취소되지 않는다
#     (사용자 결정: Remove Entity에서만 취소)
#   · SimulateCancel > EndSimulation > RemovingFromScene    — ★ 취소가 **먼저**다
#
# 종료 시 Disable이 그 앞에 오는 것은 BehaviourRegistry.Clear가 OnDisable을 부른 뒤
# TearDown을 부르기 때문이다(TearDown이 Scope.Cancel부터 한다).
$expectedSequence = "Awake > AddedToScene > Enable > Start > SimulateStart > SimulateResume > RemovingFromScene > AddedToScene > Disable > SimulateCancel > EndSimulation > RemovingFromScene > Uninitializing"
if ($sequenceText -ne $expectedSequence) {
    $failed += "훅 순서가 다르다`n      기대: $expectedSequence`n      실측: $sequenceText"
}

if ($proc.ExitCode -ne 0) { $failed += ("종료 코드 비정상: 0x{0:X8}" -f $proc.ExitCode) }

if ($failed.Count -gt 0) {
    "실패 $($failed.Count)건:"
    $failed | ForEach-Object { "  $_" }
    exit 1
}

"전체 통과 — DDOL 이송의 씬 편입/이탈이 C#까지 닿고, 핸들이 이송을 살아 건넌다"
exit 0
