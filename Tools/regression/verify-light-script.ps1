# Light 래퍼 게이트 (W2).
#
# ── 무엇을 잡는가 ──
#
# 두 축을 함께 본다. 둘 중 하나만 보면 눈먼 곳이 생긴다.
#
#   ① 경계 왕복 — LightScriptProbe가 스스로 판정한다. 저작값을 그대로 읽는지,
#      쓴 값이 그대로 돌아오는지, 범위를 벗어난 열거가 무시되는지, 라이트가
#      없는 오브젝트에서 null이 나오는지.
#
#   ② dirty 사슬 — light.proxy가 함께 찍는 누계로 발행→커밋→큐잉을 잰다.
#
# ②가 왜 따로 필요한가: 렌더는 LightComponent를 보지 않는다. 값은
# LightRenderProxy로 복사돼 있고, 그 복사는 Scene::CommitRenderProxies가
# dirty 큐에 실린 것만 훑어서 한다. 그래서 값을 넣는 쪽이
# PublishRenderProxyDirty를 부르지 않으면 화면이 그대로인데 —
# **스크립트가 되읽으면 새 값이 나온다**(컴포넌트 필드는 바뀌었으니까).
# ①만 보는 게이트는 이 결함에 원리적으로 눈멀다.
#
# ── 왜 프록시 "값"이 아니라 누계를 재는가 (2026-09-04 실측) ──
#
# 처음에는 프록시의 intensity를 직접 읽어 대조했다. 그런데 값이 영영 바뀌지
# 않았다. 계측을 붙여 갈라 보니 발행도 커밋도 정상이었다:
#
#   기준선  publish=1 committed=1 queued=3 applied=2
#   이후    publish=9 committed=2 queued=4 applied=2
#
# queued는 늘고 applied는 멈춰 있다. UpdateCommand는 프록시에 값을 쓰지 않고
# ProxyCommandQueue에 델타를 넣을 뿐이며, 그것을 적용하는 ExecuteBatch는 렌더
# 소비 스레드 전용이다. --script 헤드리스는 렌더 프레임이 거의 돌지 않아
# (이 실행의 종료 로그가 publish 64 / consume 3 / overflow 61) 델타가 적용되지
# 않는다. 프록시 값 축은 이 하네스로는 원리적으로 관측할 수 없다.
#
# 그래서 게임 스레드 쪽 사슬 전체(writer → dirty → 커밋 → 큐잉)를 대신 잰다.
# 마지막 한 칸(렌더 스레드의 적용)은 이 게이트가 보지 못한다 — 그 축은
# 렌더가 실제로 도는 하네스가 생겨야 닫힌다.
#
# ── 이빨 확인(2026-09-04) ──
#
# LightComponent의 setter 5종에서 PublishRenderProxyDirty를 지우니 ①은 그대로
# 통과하고 ②만 붉어졌다(publish 증가분 8 → 0). 되돌려 초록을 다시 확인했다.
# 그 변이가 곧 지금의 리플렉션 인스펙터 경로다 — ReflectionTypedDraw.h는
# 값을 대입만 하고 dirty를 모른다.
#
# 사용법:
#   pwsh Tools\regression\verify-light-script.ps1
param(
    [string]$Exe = (Join-Path $PSScriptRoot "..\..\Bin\x64-Debug\Editor\CreatorEditor.exe"),
    [string]$Work = $env:TEMP,
    [int]$TimeoutSeconds = 300
)

$ErrorActionPreference = 'Stop'

if (-not (Test-Path $Exe)) { "실행 파일이 없다: $Exe"; exit 1 }
$exeDir = [System.IO.Path]::GetDirectoryName($Exe)

$repoRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot "..\.."))

# 대상은 에디터가 코드로 만드는 기본 씬의 "Directional Light"다.
# 저작 씬을 열지 않는 이유는 시나리오 파일 머리에 적어 뒀다.
$scenario = Join-Path $repoRoot "scripts\light_script_probe.txt"
if (-not (Test-Path $scenario)) { "시나리오가 없다: $scenario"; exit 1 }

$outPath = Join-Path $Work "light_script_probe.out"
$errPath = Join-Path $Work "light_script_probe.err"

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

if (-not (Test-Path $outPath)) { "표준 출력이 없다: $outPath"; exit 1 }
$stdout = Get-Content -LiteralPath $outPath -Raw

# ── ① 경계 왕복 — 프로브 자기 판정 ──
#
# Debug->Log/LogError는 stdout에 안 나가고 인메모리·HTML 싱크로만 간다
# (verify-script-add-awake-once.ps1의 같은 주석 참고). 로그는 실행이
# 끝난 뒤 가장 최근 것을 집는다 — 이 프로세스가 방금 만든 것이다.
$logDir = Join-Path $exeDir "Saved\Log"
$editorLog = Get-ChildItem (Join-Path $logDir "Editor_*.html") -ErrorAction SilentlyContinue |
             Where-Object { $_.LastWriteTime -ge $proc.StartTime } |
             Sort-Object LastWriteTime -Descending | Select-Object -First 1

if (-not $editorLog) {
    "이 실행이 만든 에디터 로그를 찾지 못했다: $logDir\Editor_*.html"
    exit 1
}

$logText = (Get-Content -LiteralPath $editorLog.FullName -Raw) -replace '<[^>]+>', ''

$probeStarted = $logText -match '\[LightScriptProbe\] 시작'
$probePassed  = $logText -match '\[LightScriptProbe\] 전체 통과 \((\d+)건\)'
$probeCount   = if ($probePassed) { [int]$Matches[1] } else { 0 }
$probeFailed  = $logText -match '\[LightScriptProbe\] (\d+)건 실패'

"① 경계 왕복"
if (-not $probeStarted) {
    "  프로브가 시작조차 하지 않았다 — script.add가 실패했거나 대상이 없다"
    "  (기본 씬에 'Directional Light'가 서 있어야 한다 — EditorMain.cpp)"
    exit 1
}

if ($probeFailed) {
    "  프로브 실패 — 로그의 [LightScriptProbe] 실패 줄을 볼 것"
    ($logText -split "`n" | Where-Object { $_ -match '\[LightScriptProbe\] 실패' }) |
        ForEach-Object { "    $($_.Trim())" }
    exit 1
}

# 건수를 정확히 못 박는다. "N건 이상"으로 두면 프로브에서 단정 하나가
# 조용히 빠져도 통과한다 — 커버리지가 줄어드는 것을 보는 유일한 자리다.
# 내역: 저작값 6 · 왕복 6 · 열거 가드 2 · 부재 1.
# 프로브에 단정을 더하면 이 값도 함께 고칠 것.
$ExpectedAssertions = 15
if ($probeCount -ne $ExpectedAssertions) {
    "  단정 $probeCount 건 — $ExpectedAssertions 건이어야 한다."
    "  줄었다면 프로브가 조기 반환했고, 늘었다면 이 기대값을 갱신할 것."
    exit 1
}
"  통과 $probeCount 건"


# ── ② dirty 사슬 — light.proxy가 함께 찍는 누계 ──
"② dirty 사슬"

# 시나리오는 light.proxy를 두 번 부른다 — 스크립트가 손대기 전(기준선)과 후.
# 두 줄을 함께 봐야 "원래 그랬다"와 "스크립트가 냈다"가 갈린다.
$statLines = @($stdout -split "`n" | Where-Object { $_ -match '\[light\.proxy\] count=\d+ publish=' })
if ($statLines.Count -lt 2) {
    "  누계 줄이 $($statLines.Count) 개 — 기준선과 이후 두 줄이 나와야 한다."
    "  (light.proxy가 등록되지 않았거나 시나리오가 한 번만 부른다)"
    exit 1
}

$statPattern = 'count=(\d+) publish=(\d+) committed=(\d+) pending=(\d+) queued=(\d+) applied=(\d+)'

if ($statLines[0] -notmatch $statPattern) {
    "  기준선 누계를 해석하지 못했다: $($statLines[0].Trim())"
    exit 1
}
$baseCount     = [int]$Matches[1]
$basePublish   = [int]$Matches[2]
$baseCommitted = [int]$Matches[3]
$baseQueued    = [int]$Matches[5]

if ($statLines[-1] -notmatch $statPattern) {
    "  이후 누계를 해석하지 못했다: $($statLines[-1].Trim())"
    exit 1
}
$publish   = [int]$Matches[2]
$committed = [int]$Matches[3]
$queued    = [int]$Matches[5]

"  기준선: $($statLines[0].Trim())"
"  이후  : $($statLines[-1].Trim())"

if ($baseCount -lt 1) {
    "  기준선 프록시 0개 — 광원이 없거나 전부 꺼져 있다(스냅샷은 Disabled를 거른다)."
    "  대상 오브젝트('Directional Light')가 기본 씬에 서지 않았다."
    exit 1
}

# 프로브가 값을 넣는 횟수. CheckRoundTrip이 여섯 번 쓰고 끝에 두 번 되쓴다.
# 열거 가드(CheckEnumGuards)의 두 번은 범위 밖이라 네이티브가 무시하므로
# 발행되지 않는다 — 그것까지 세면 이 값이 10이 되어야 하고, 되면 가드가
# 뚫린 것이다. 프로브를 고치면 이 값도 함께 고칠 것.
$ExpectedPublishDelta = 8

$publishDelta   = $publish - $basePublish
$committedDelta = $committed - $baseCommitted
$queuedDelta    = $queued - $baseQueued

"  증가분: publish=$publishDelta committed=$committedDelta queued=$queuedDelta"

if ($publishDelta -ne $ExpectedPublishDelta) {
    "  publish 증가분 $publishDelta — $ExpectedPublishDelta 이어야 한다."
    if ($publishDelta -eq 0) {
        "  0이면 writer가 dirty를 아예 내지 않았다(PublishRenderProxyDirty 누락)."
    } elseif ($publishDelta -gt $ExpectedPublishDelta) {
        "  더 많으면 범위 밖 열거까지 발행됐을 수 있다 — 네이티브 가드를 볼 것."
    } else {
        "  더 적으면 setter 중 일부가 dirty를 내지 않는다."
    }
    exit 1
}

# 발행이 큐를 실제로 통과했는지. 발행만 되고 커밋이 안 되면 등록이 없어
# stale로 버려진 것이라 고칠 자리가 다르다.
if ($committedDelta -lt 1) {
    "  committed 증가분 $committedDelta — 발행은 됐는데 커밋되지 않았다."
    "  레지스트리에 등록이 없어 stale로 버려졌을 수 있다(Scene::CollectLightComponent)."
    exit 1
}
if ($queuedDelta -lt 1) {
    "  queued 증가분 $queuedDelta — 커밋은 됐는데 프록시 커맨드가 큐에 들어가지 않았다."
    "  RenderScene::UpdateCommand(LightComponent*) 경로를 볼 것."
    exit 1
}

""
"전체 통과 — Light 래퍼가 경계를 왕복하고, 쓴 값이 dirty를 세워 프록시 커맨드까지 간다"
"(렌더 스레드의 최종 적용은 이 하네스가 보지 못한다 — 파일 머리 주석 참고)"
exit 0
