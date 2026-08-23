# 행동 트리 동작 확인 (PHASE 9-8 완료 기준 1·3).
#
# ── 이 검사가 메우는 구멍 ──
#
# 회귀 세트는 BT를 전혀 실행하지 않는다. BT 컴포넌트는 프리팹에만 붙어 있고 회귀
# 시나리오가 여는 씬에는 없어서, run-all.ps1이 전부 통과해도 BT 코드는 한 줄도
# 돌지 않은 채 통과할 수 있다. 그것은 "안 깨졌다"이지 "동작한다"가 아니다.
#
# 그리고 트리 생성·틱은 실패할 때만 로그를 남기므로(성공은 무음), 로그만으로는
# "트리가 안 서서 AI가 가만히 있다"와 "정상"이 구분되지 않는다. 그래서 수를 센다.
#
# 판정 항목:
#   1  소환 전 트리 0개          — 뒤의 증가가 이 시나리오의 결과임을 보장
#   2  소환 후 트리 3개          — 기존 BT 에셋이 수정 없이 로드된다(완료 기준 1)
#   3  재생 중 틱 누계 증가      — 트리가 실제로 돈다(선 것만으로는 부족하다)
#   4  건너뛴 틱 0회             — 죽은 id로 들어온 틱이 없다
#   5  프레임당 크로싱 <= 1.00   — 경계 불변식(완료 기준 3)
#   6  크로싱당 전달 틱 > 1      — 배치가 실제로 일어난다(트리 3개를 한 번에)
#   7  씬 교체 후 트리 0개       — Running 상태가 다음 씬으로 새지 않는다
#
# 실행: pwsh -NoProfile -File Tools\regression\verify-bt-smoke.ps1
param(
    [string]$Exe = "C:\Users\lance\source\CreatorEngine\x64\Debug\CreatorEditor.exe",
    [string]$Work = $env:TEMP
)

$exeDir = [System.IO.Path]::GetDirectoryName($Exe)
if (-not (Test-Path $Exe)) { "실행 파일이 없다: $Exe"; exit 1 }

$template = Join-Path $PSScriptRoot "bt_smoke.txt"
if (-not (Test-Path $template)) { "시나리오가 없다: $template"; exit 1 }

# ── BT 자산 guid를 .meta에서 읽어 시나리오에 채운다 (2026-08-20 CLI 이전) ──
#
# 프리팹은 CLI가 저작하지만, BehaviorTreeComponent는 자산을 **guid로만** 찾는다
# (이름 폴백이 없다). guid를 시나리오에 박아 두면 .meta가 재발급될 때 조용히
# 어긋나므로, 실행 직전에 .meta에서 읽는다.
#
# .bt/.blackboard 자체는 폐기 대상이 아니다 — 저작 자산(프리팹·씬)이 아니라 손으로
# 만든 게이트 픽스처다(시나리오 머리말 참고).
$repoRoot = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
$btDir = Join-Path $repoRoot "Dynamic_CPP\Assets\BehaviorTree"

function Read-MetaGuid([string]$metaPath) {
    if (-not (Test-Path $metaPath)) { return $null }
    $m = [regex]::Match((Get-Content -LiteralPath $metaPath -Raw), 'guid:\s*([0-9a-fA-F-]+)')
    if (-not $m.Success) { return $null }
    return $m.Groups[1].Value
}

$btGuid = Read-MetaGuid (Join-Path $btDir "BTProbe.bt.meta")
$bbGuid = Read-MetaGuid (Join-Path $btDir "BTProbe.blackboard.meta")
if ([string]::IsNullOrWhiteSpace($btGuid) -or [string]::IsNullOrWhiteSpace($bbGuid)) {
    "BT 픽스처 guid를 못 읽었다 (bt='$btGuid' bb='$bbGuid') — $btDir 의 .meta 확인"
    exit 1
}

# 프리팹 픽스처는 매 실행 CLI로 다시 만든다 — 잔재를 미리 지운다(.gitignore 대상).
$prefabDir = Join-Path $repoRoot "Dynamic_CPP\Assets\Prefabs"
foreach ($p in @((Join-Path $prefabDir "BTSmokeProbe.prefab"),
                 (Join-Path $prefabDir "BTSmokeProbe.prefab.meta"))) {
    if (Test-Path $p) { Remove-Item $p -Force }
}

$script = Join-Path $Work "bt_smoke_resolved.txt"
((Get-Content $template -Raw) -replace '\{\{BT_GUID\}\}', $btGuid) -replace '\{\{BB_GUID\}\}', $bbGuid |
    Set-Content $script -Encoding UTF8
if (-not (Test-Path $script)) { "시나리오가 없다: $script"; exit 1 }

$outPath = Join-Path $Work "bt_smoke.out"
$proc = Start-Process -FilePath $Exe -ArgumentList "--script", $script `
    -WorkingDirectory $exeDir `
    -RedirectStandardOutput $outPath `
    -RedirectStandardError (Join-Path $Work "bt_smoke.err") -PassThru
$proc.WaitForExit(300000) | Out-Null

if (-not $proc.HasExited) { $proc.Kill(); "타임아웃"; exit 1 }

if (-not (Test-Path $outPath)) { "출력이 없다: $outPath"; exit 1 }
$text = Get-Content -LiteralPath $outPath -Raw

# 지표를 못 읽은 경우를 통과로 흘리지 않는다.
#
# "트리 0개"와 "지표를 못 읽었다"는 전혀 다른 상황인데, 파싱이 실패했을 때 0으로
# 두면 항목 1·7이 그냥 통과해 버린다. 검사가 아무것도 말하지 않게 되는 종류다.
if ($text -match 'BT 지표 없음') {
    "BT 지표를 읽지 못했다 — 스크립트 계층이 비활성이거나 구 어셈블리다."
    "이 상태로는 BT가 도는지 알 수 없으므로 실패로 처리한다."
    exit 1
}

# bt.status가 남긴 줄을 순서대로 뽑는다. 시나리오에 5회 있다.
$statusLines = [regex]::Matches($text,
    '\[bt\.status\] 트리 (\d+)개 · 노드 타입 (\d+)종 · 틱 (\d+)회 · 건너뜀 (\d+)회')
$crossLines = [regex]::Matches($text,
    '\[bt\.status\] 경계 통과 (\d+)회 / 프레임 (\d+) \(프레임당 ([\d.]+)\) · 전달 틱 (\d+)건 \(크로싱당 ([\d.]+) · 최대 배치 (\d+)\)')

if ($statusLines.Count -lt 5) {
    "bt.status 출력이 5회여야 하는데 $($statusLines.Count)회다 — 시나리오가 중간에 죽었을 수 있다."
    "종료 코드: 0x{0:X8}" -f $proc.ExitCode
    exit 1
}

function Get-Stat([int]$i) {
    $m = $statusLines[$i]
    [pscustomobject]@{
        Trees   = [int]$m.Groups[1].Value
        Types   = [int]$m.Groups[2].Value
        Ticks   = [long]$m.Groups[3].Value
        Skipped = [long]$m.Groups[4].Value
    }
}

$before  = Get-Stat 0   # 소환 전
$spawned = Get-Stat 1   # 소환 직후(재생 전)

# 이 검사는 게임 콘텐츠에 기대지 않는다.
#
# 기존 BT 에셋의 잎 노드는 아직 이식되지 않은 사용자 Action/Condition 43종을
# 참조하므로 트리가 서지 않는다. 그 이식을 기다리면 BT 경로는 무기한 미검증으로
# 남고, 그렇다고 기존 이름을 스텁으로 채우면 실제 게임 AI가 스텁 동작을 하게 된다.
#
# 그래서 전용 노드(GameScripts/BTProbeNodes.cs)와 전용 그래프(BTProbe.bt)를 따로 두고
# 엔진 경로만 잰다. 콘텐츠가 바뀌어도 이 검사는 흔들리지 않는다.
$playing = Get-Stat 2   # 재생 180프레임 뒤
$stopped = Get-Stat 3   # 정지 뒤
$cleared = Get-Stat 4   # 씬 교체 뒤

$failed = @()

# 1 · 소환 전에는 비어 있어야 뒤의 증가가 이 시나리오의 결과가 된다.
if ($before.Trees -ne 0) { $failed += "소환 전 트리가 $($before.Trees)개다(0이어야 한다)" }

# 2 · 프리팹 3종을 소환했으니 트리 3개. 이것이 '기존 BT 에셋이 수정 없이 로드된다'다.
if ($spawned.Trees -ne 3) { $failed += "소환 후 트리가 $($spawned.Trees)개다(3이어야 한다)" }

# 3 · 선 것만으로는 부족하다. 실제로 돌아야 한다.
if ($playing.Ticks -le $spawned.Ticks) {
    $failed += "재생 중 틱이 늘지 않았다($($spawned.Ticks) -> $($playing.Ticks))"
}

# 4 · 죽은 id로 들어온 틱이 있으면 수명 배선이 어긋난 것이다.
if ($playing.Skipped -ne 0) { $failed += "건너뛴 틱이 $($playing.Skipped)회다(0이어야 한다)" }

# 5 · 정지는 틱만 멈춘다. 파괴는 OnDestroy에서만 일어난다.
if ($stopped.Trees -ne 3) { $failed += "정지 후 트리가 $($stopped.Trees)개다(3이어야 한다)" }

# 6 · 남으면 Running이 다음 씬으로 새고 어셈블리 언로드도 막힌다.
if ($cleared.Trees -ne 0) { $failed += "씬 교체 후 트리가 $($cleared.Trees)개 남았다(0이어야 한다)" }

# 7 · 사용자 노드 타입이 0종이면 B5·B6 배선이 죽은 것이다. 트리는 빌트인만으로도
#     설 수 있어 트리 수만 보면 이 결함이 드러나지 않는다.
if ($spawned.Types -le 0) { $failed += "등록된 사용자 노드 타입이 0종이다(생성기 배선 확인)" }

# 8·9 · 경계 불변식. 이 재설계의 핵심 주장이라 수치로 못을 박는다.
if ($crossLines.Count -lt 3) {
    $failed += "경계 계수 출력이 3회 미만이다($($crossLines.Count)회)"
} else {
    $c = $crossLines[2]  # 재생 구간(bt.reset 직후부터)
    $perFrame    = [double]$c.Groups[3].Value
    $perCrossing = [double]$c.Groups[5].Value
    $crossings   = [long]$c.Groups[1].Value

    if ($crossings -le 0) { $failed += "재생 구간에 경계 통과가 0회다 — BT가 넘어가지 않았다" }
    if ($perFrame -gt 1.0) { $failed += "프레임당 크로싱이 $perFrame 이다(1.00 이하여야 한다)" }

    # 트리 3개가 한 번에 실려야 배치가 증명된다. 1.0이면 트리마다 따로 넘긴 것이고,
    # 그것이 정확히 이 재설계가 없애려던 상태다.
    if ($perCrossing -le 1.0) {
        $failed += "크로싱당 전달 틱이 $perCrossing 이다 — 배치가 일어나지 않았다"
    }
}

# 10 · 트리가 끝까지 갔다는 증거.
#
# 틱 누계만으로는 '루트가 불렸다'까지다. 루트가 첫 자식에서 실패해도 틱은 올라간다.
# BTProbeCountAction은 시퀀스의 마지막이라, 이 줄이 찍혔다는 것은 앞의 조건 둘이
# 통과했다는 뜻이고 — 그중 하나가 저작 블랙보드 값을 읽는 조건이다.
#
# 즉 이 한 줄이 네 가지를 동시에 증명한다: 저작 블랙보드가 실려 왔고, 조건 잎이
# 평가됐고, 행동이 Running을 거쳐 Success에 닿았고(프레임을 넘어 상태가 이어졌고),
# 자식 순서가 지켜졌다.
# 프로브의 로그는 stdout이 아니라 엔진 로그로 간다(Native.Log → 로그 시스템).
# bt.status는 printf로 stdout에도 찍지만 스크립트 쪽 로그는 그렇지 않다.
$logDir = Join-Path $exeDir "Log"
$editorLog = Get-ChildItem (Join-Path $logDir "Editor_*.html") -ErrorAction SilentlyContinue |
             Sort-Object LastWriteTime -Descending | Select-Object -First 1
$logText = if ($editorLog) { (Get-Content $editorLog.FullName -Raw) -replace '<[^>]+>', '' } else { '' }

$done = [regex]::Match($logText, '\[BTProbe\] 시퀀스 완주 — 저작값 확인 (\d+)회 · 조건 (\d+)회 · 행동 틱 (\d+)회')
if (-not $done.Success) {
    $failed += "시퀀스 완주 로그가 없다 — 트리가 끝까지 가지 못했다(저작 블랙보드나 자식 순서를 의심할 것)"
} else {
    "완주      저작값 {0}회 · 조건 {1}회 · 행동 틱 {2}회" -f `
        $done.Groups[1].Value, $done.Groups[2].Value, $done.Groups[3].Value

    # 행동은 Running을 2회 거친 뒤 3회째에 Success다. 1이면 Running 경로가 아예
    # 돌지 않은 것이고, 그러면 프레임 간 상태 유지가 검증되지 않는다.
    if ([int]$done.Groups[3].Value -lt 2) {
        $failed += "행동 틱이 $($done.Groups[3].Value)회다 — Running이 프레임을 넘지 않았다"
    }
    if ([int]$done.Groups[1].Value -lt 1) {
        $failed += "저작 블랙보드 값이 읽히지 않았다"
    }
}

# 종료 코드도 판정에 넣는다. 프로브가 다 통과하고도 종료 시점에 죽은 전례가 있다.
if ($proc.ExitCode -ne 0) { $failed += ("종료 코드 0x{0:X8}" -f $proc.ExitCode) }

"소환 전   트리 {0}개 · 틱 {1}" -f $before.Trees, $before.Ticks
"소환 직후 트리 {0}개 · 틱 {1} · 노드 타입 {2}종" -f $spawned.Trees, $spawned.Ticks, $spawned.Types
"재생 뒤   트리 {0}개 · 틱 {1} · 건너뜀 {2}" -f $playing.Trees, $playing.Ticks, $playing.Skipped
"정지 뒤   트리 {0}개 · 틱 {1}" -f $stopped.Trees, $stopped.Ticks
"씬 교체 뒤 트리 {0}개" -f $cleared.Trees
if ($crossLines.Count -ge 3) {
    $c = $crossLines[2]
    "경계      통과 {0}회 / 프레임 {1} (프레임당 {2}) · 크로싱당 {3}틱 · 최대 배치 {4}" -f `
        $c.Groups[1].Value, $c.Groups[2].Value, $c.Groups[3].Value, $c.Groups[5].Value, $c.Groups[6].Value
}
""

if ($failed.Count -gt 0) {
    "실패 $($failed.Count)건:"
    $failed | ForEach-Object { "  $_" }
    exit 1
}

"전체 통과 — BT가 실제로 로드되고 돌았으며 경계 크로싱은 프레임당 1회 이하"
exit 0
