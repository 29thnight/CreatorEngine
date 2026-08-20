# 중첩 프리팹 정체성/등록 게이트 판정 (트랙 P · P4-a, 0단계).
#
# prefab_nested_probe.txt가 만든 로그를 읽어 두 가지를 대조한다.
#   결함 3(등록 누락) — prefab.status의 "등록" 개수가 소환 후 2개인가(1개면
#                        중첩 루트가 빠진 것).
#   결함 2(정체성 파괴) — prefab.objectguid로 찍은 각 오브젝트의 실제
#                        m_prefabFileGuid 값이 기대한 프리팹과 일치하는가.
#
# ── guid 값을 하드코딩하지 않는 이유 ──
#
# .meta 파일은 .gitignore(*.meta)에 걸려 커밋되지 않는다(BTProbe.prefab.meta도
# 같은 처지 — git ls-files에 안 잡힌다). 이 저장소를 새로 클론한 머신에서
# AssetMetaWatcher::ScanAndGenerateMissingMeta가 NestedProbeParent/Leaf의 .meta를
# 못 찾으면(파일을 안 복사해 왔다면) 무작위로 새 guid를 발급한다 — 그러면 이
# 스크립트가 만든 .prefab.meta의 고정값(NestedProbeParent.prefab.meta 등)과
# 어긋난다. 그래서 판정은 "정확히 이 값"이 아니라 "서로 다른가/같은가"라는
# 상대 비교로 한다 — 어떤 guid가 실제로 발급됐든 성립해야 하는 불변식이다:
#   NP_NestedLeaf의 guid는 NP_Root_A(바깥)와 달라야 한다(자기 프리팹을 유지) —
#     같으면 결함 2(바깥 것으로 덮임)가 재현된 것이다.
#   NP_NestedLeaf의 guid는 비어 있으면 안 된다(00000000-... 는 "덮이지도, 자기
#     것을 유지하지도 않고 사라짐" — 위와는 다른 파손이지만 역시 실패다).
#   NP_PlainChild의 guid는 NP_Root_A(바깥)와 같아야 한다(순수 자식은 계속
#     부모의 guid를 물려받는다 — 회귀 안전판).
#
# 실행: pwsh -NoProfile -File Tools\regression\verify-prefab-nested.ps1
param(
    [string]$Exe = "C:\Users\lance\source\CreatorEngine\x64\Debug\Academy_4Q.exe",
    [string]$Work = $env:TEMP,
    [int]$TimeoutSeconds = 300
)

$exeDir = [System.IO.Path]::GetDirectoryName($Exe)
if (-not (Test-Path $Exe)) { "실행 파일이 없다: $Exe"; exit 1 }

$script = Join-Path $PSScriptRoot "prefab_nested_probe.txt"
if (-not (Test-Path $script)) { "시나리오가 없다: $script"; exit 1 }

# 이 시나리오가 굽는 프리팹 둘. 매 실행마다 CLI로 다시 만드는 픽스처라(2026-08-20
# CLI 이전 — 그전에는 guid가 박힌 저작 자산이었다) 이전 실행의 잔재가 남아 있으면
# 이번 실행이 아니라 지난 실행의 형상을 잴 수 있다. 미리 지운다
# (NestedUpdate*·OvWriteProbe와 같은 규율이고 .gitignore 대상이다).
$repoRoot = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
$prefabDir = Join-Path $repoRoot "Dynamic_CPP\Assets\Prefabs"
foreach ($n in @('NestedProbeLeaf', 'NestedProbeParent')) {
    foreach ($p in @((Join-Path $prefabDir "$n.prefab"), (Join-Path $prefabDir "$n.prefab.meta"))) {
        if (Test-Path $p) { Remove-Item $p -Force }
    }
}

$outFile = Join-Path $Work "prefab_nested_probe.out"
$errFile = Join-Path $Work "prefab_nested_probe.err"

$proc = Start-Process -FilePath $Exe -ArgumentList @("--script", $script) `
    -WorkingDirectory $exeDir `
    -RedirectStandardOutput $outFile -RedirectStandardError $errFile -PassThru
$proc.WaitForExit($TimeoutSeconds * 1000) | Out-Null
if (-not $proc.HasExited) { $proc.Kill(); "TIMEOUT"; exit 1 }

if (-not (Test-Path $outFile)) { "출력이 없다: $outFile"; exit 1 }
$text = Get-Content -LiteralPath $outFile -Raw

$nullGuid = "00000000-0000-0000-0000-000000000000"

# prefab.status 두 번(기준선·소환 후)을 순서대로 집는다.
$statusMatches = [regex]::Matches($text,
    '\[prefab\.status\]\s*씬 인스턴스 (\d+)개 · 등록 (\d+)개 · 캐시 (\d+)개')

if ($statusMatches.Count -lt 2) {
    "prefab.status 기록이 2회여야 하는데 $($statusMatches.Count)회다 — 시나리오가 도중에 멈췄다"
    "출력: $outFile"
    exit 1
}

function Get-Status([int]$i) {
    [pscustomobject]@{
        Scene      = [int]$statusMatches[$i].Groups[1].Value
        Registered = [int]$statusMatches[$i].Groups[2].Value
        Cached     = [int]$statusMatches[$i].Groups[3].Value
    }
}

$before = Get-Status 0
$after  = Get-Status 1

# prefab.objectguid 세 번(이름 -> guid)을 이름으로 색인한다.
$guidMatches = [regex]::Matches($text,
    '\[prefab\.objectguid\]\s*(\S+)\s+guid=([0-9a-fA-F-]+)')

$guidByName = @{}
foreach ($m in $guidMatches) {
    $guidByName[$m.Groups[1].Value] = $m.Groups[2].Value
}

"시작    씬 $($before.Scene) · 등록 $($before.Registered) · 캐시 $($before.Cached)"
"소환 후 씬 $($after.Scene) · 등록 $($after.Registered) · 캐시 $($after.Cached)"
foreach ($name in @('NP_Root_A', 'NP_PlainChild', 'NP_NestedLeaf')) {
    $g = if ($guidByName.ContainsKey($name)) { $guidByName[$name] } else { '(없음)' }
    "objectguid $name = $g"
}
""

$hard = @()

# ★ 등록은 **증가분**으로 판정한다 (2026-08-20 CLI 이전).
#
# 예전에는 "시작 등록이 0"을 요구했다. 저작 자산을 소환만 하던 시절에는 그것이
# 기준선 확보였지만, 저작을 시나리오 안으로 들여오면 성립하지 않는다 — 리프를
# 한 번 인스턴스화하므로 등록부가 0에서 시작하지 않는다.
#
# 증가분은 잔재가 있어도 정확하고, 원래 의도(이 소환이 만든 것만 센다)를 더
# 곧게 표현한다.
$registeredDelta = $after.Registered - $before.Registered
$sceneDelta      = $after.Scene - $before.Scene
"소환이 만든 것 — 씬 인스턴스 +$sceneDelta · 등록 +$registeredDelta (기대 +3 · +2)"
""

if ($guidMatches.Count -lt 3) {
    $hard += "prefab.objectguid 기록이 3회여야 하는데 $($guidMatches.Count)회다 — 시나리오가 도중에 멈췄거나 오브젝트를 못 찾았다"
}

# ★ 결함 3 — 등록. 버그 상태에서는 증가분이 1(최외곽 루트만) — 중첩 루트가
# RegisterInstance의 `parent == 0` 가드에 걸려 한 번도 안 잡힌다.
if ($registeredDelta -ne 2) {
    $hard += "이 소환의 등록 증가분이 2가 아니다($registeredDelta) — 중첩 루트(NP_NestedLeaf)가 등록부에 안 잡혔다(결함 3)"
}

# 씬 인스턴스 셋(루트·순수 자식·중첩 루트)이 전부 guid를 갖고 태어나야 한다.
# 0이면 소환 자체가 실패한 것이고, 그 상태에서 아래 guid 비교는 무의미하다.
if ($sceneDelta -ne 3) {
    $hard += "이 소환의 씬 인스턴스 증가분이 3이 아니다($sceneDelta) — 루트·순수 자식·중첩 루트 셋이 다 서지 않았다"
}

$rootGuid  = $guidByName['NP_Root_A']
$leafGuid  = $guidByName['NP_NestedLeaf']
$plainGuid = $guidByName['NP_PlainChild']

# ★ 결함 2 — 정체성(상대 비교, 위 주석 참고). 버그 상태에서는 leafGuid ==
# rootGuid다 — InstantiateRecursive가 재귀 프레임마다 무조건 바깥 guid를
# 스탬핑해 중첩 루트의 원래 guid를 지운다.
if ($null -ne $leafGuid) {
    if ($leafGuid -eq $nullGuid) {
        $hard += "NP_NestedLeaf의 guid가 비어 있다($nullGuid) — 자기 프리팹의 정체성을 전혀 못 받았다"
    }
    elseif ($leafGuid -eq $rootGuid) {
        $hard += "NP_NestedLeaf의 guid($leafGuid)가 바깥 루트(NP_Root_A)와 같다 — 중첩 정체성이 바깥 프리팹의 guid로 덮였다(결함 2)"
    }
}

# 안전판 — 순수 자식은 여전히 바깥(부모)의 guid를 물려받아야 한다(회귀 방지).
if ($null -ne $plainGuid -and $plainGuid -ne $rootGuid) {
    $hard += "NP_PlainChild의 guid($plainGuid)가 바깥 루트(NP_Root_A, $rootGuid)와 다르다 — 순수 자식의 guid 상속이 깨졌다"
}

foreach ($h in $hard) { "실패: $h" }

if ($hard.Count -gt 0) { exit 1 }

"전체 통과 — 중첩 프리팹이 정체성을 유지한 채 인스턴스 루트로 등록된다"
exit 0
