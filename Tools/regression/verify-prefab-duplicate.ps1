# 프리팹 인스턴스 복제 대조 (SceneGraphRedesignPlan 트랙 P)
#
# 시나리오와 이 검사가 무엇을 메우는지는 prefab_duplicate.txt 상단에 있다.
#
# ── 판정 항목 ──
#
#   1  시나리오가 끝까지 갔다        — prefab.status 2회 + 저장 파일 존재
#   2  복제 전 씬 인스턴스 == 등록   — 기준선이 성립한다(안 그러면 이후 비교가 무의미)
#   3  복제 후에도 씬 인스턴스 == 등록 — ★ 본 판정. 복제본이 등록부에 이어졌는가
#   4  복제본이 인스턴스로 인식된다  — prefab.overrides가 인스턴스=예
#   5  복제본이 프리팹 갱신을 받는다 — ★★ 저장된 Copy.m_shadowCast가 false
#   6  프리팹 identity가 유지됐다   — 저장된 인스턴스 guid == 최종 sidecar guid
#   7  적용 건수가 정직하다         — prefab.update가 "2개 중 2개에 적용"이라고 말한다
#
# 3과 5는 같은 결함의 앞뒤다. 3만 보면 "등록은 됐는데 갱신은 안 오는" 경우를 놓치고,
# 5만 보면 왜 안 왔는지 못 가른다.
#
# 6·7은 2026-08-30에 이 게이트가 한 번 실패하고 재현되지 않은 뒤에 붙였다. 원인은
# 초기 상태가 아니라 **워처 스레드와의 경합**이었다 — catalog가 프리팹 경로의 GUID를
# 잠깐 잃는 ~26ms 창에 prefab.update가 걸리면, SavePrefab이 새 GUID를 발급하고
# UpdateInstances가 그 키로 조회해 조용히 0건 적용한다. 그때 판정 1~4는 전부 통과하고
# 5만 빨개지므로 "왜"를 가를 수 없었다. 6이 원인(identity 갈림)을, 7이 그 즉시 증상
# (실제 적용 0건)을 잡는다. 그 경합을 확정적으로 재현하는 것은
# verify-prefab-identity-injection.ps1 이고, 이 게이트는 우연히 걸렸을 때 원인을
# 말할 수 있게만 한다.
#
# ★ 자가 틀렸을 때 어떻게 드러나는가 (이 저장소에서 파서 버그가 네 번 났다):
#   · 개수를 못 읽으면 판정 1이 먼저 걸린다(측정 2회 단정)
#   · 오브젝트 블록을 잘못 가르면 Orig/Copy가 같은 값으로 읽히는데,
#     Orig는 갱신을 받고 Copy는 못 받는 것이 결함의 모습이므로 **둘을 함께**
#     단정해 한쪽만 맞는 파싱이 통과하지 못하게 한다.
#
# 사용법:
#   pwsh Tools\regression\verify-prefab-duplicate.ps1
param(
    [string]$Exe = (Join-Path $PSScriptRoot "..\..\Bin\x64-Debug\Editor\CreatorEditor.exe"),
    [string]$Work = $env:TEMP,
    [int]$TimeoutSeconds = 300
)

$exeDir = [System.IO.Path]::GetDirectoryName($Exe)
if (-not (Test-Path $Exe)) { "실행 파일이 없다: $Exe"; exit 1 }

$template = Join-Path $PSScriptRoot "prefab_duplicate.txt"
if (-not (Test-Path $template)) { "시나리오가 없다: $template"; exit 1 }

$sceneOut = Join-Path $Work "PrefabDuplicate.creator"
if (Test-Path $sceneOut) { Remove-Item $sceneOut -Force -ErrorAction SilentlyContinue }
if (Test-Path $sceneOut) { "이전 실행의 저장 파일을 지우지 못했다: $sceneOut"; exit 1 }

# 이전 실행이 남긴 프리팹을 지운다 — 남아 있으면 "레지스트리에 이미 있는" 상태를
# 재게 되어, 세션 중 생성 경로(SavePrefab의 즉시 등록)를 검사하지 못한다.
#
# ★ 지워졌는지 **확인한다**. 예전에는 -ErrorAction SilentlyContinue만 걸어 두어
# 삭제 실패가 아무 흔적 없이 통과했다 — 초기 상태가 실행마다 달라지는데 그것을
# 알 길이 없었다. (실측으로는 잔재가 있어도 결과가 갈리지 않았지만, 그 사실 자체를
# 게이트가 증명해야 한다.)
$repoRoot  = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
$prefabDir = Join-Path $repoRoot "Dynamic_CPP\Assets\Prefabs"
$prefabAsset = Join-Path $prefabDir "DupProbe.prefab"
$prefabMeta  = Join-Path $prefabDir "DupProbe.prefab.meta"
foreach ($p in @($prefabAsset, $prefabMeta)) {
    if (Test-Path $p) { Remove-Item $p -Force -ErrorAction SilentlyContinue }
    if (Test-Path $p) { "이전 실행이 남긴 픽스처를 지우지 못했다: $p"; exit 1 }
}

$scenario = Join-Path $Work "prefab_duplicate_resolved.txt"
(Get-Content $template -Raw) -replace '\{\{SCENE_OUT\}\}', ($sceneOut -replace '\\', '/') |
    Set-Content $scenario -Encoding UTF8

$outPath = Join-Path $Work "prefab_duplicate.out"
$errPath = Join-Path $Work "prefab_duplicate.err"

$proc = Start-Process -FilePath $Exe -ArgumentList "--commandlet-script", $scenario `
    -WorkingDirectory $exeDir `
    -RedirectStandardOutput $outPath `
    -RedirectStandardError $errPath -PassThru

$proc.WaitForExit($TimeoutSeconds * 1000) | Out-Null
if (-not $proc.HasExited) {
    $proc.Kill()
    "타임아웃 ($TimeoutSeconds 초). 시나리오가 끝나지 않았다."
    exit 1
}

$text = Get-Content $outPath -Raw

$status  = [regex]::Matches($text, '\[prefab\.status\] 씬 인스턴스 (\d+)개 · 등록 (\d+)개')
$inst    = [regex]::Matches($text, '\[prefab\.overrides\] (\S+) · 인스턴스=(\S+) · 기록 (\d+)건')
$applied = [regex]::Match($text, '\[prefab\.update\] \S+ <- \S+ · 등록 인스턴스 (\d+)개 중 (\d+)개에 적용')

$failed = @()

if ($status.Count -lt 2) {
    "prefab.status 측정이 $($status.Count) 건뿐이다 (기대 2). 시나리오가 도중에 멈췄다."
    if ($proc.ExitCode -ne 0) { "  종료 코드: 0x{0:X8}" -f $proc.ExitCode }
    exit 1
}

$beforeScene = [int]$status[0].Groups[1].Value
$beforeReg   = [int]$status[0].Groups[2].Value
$afterScene  = [int]$status[1].Groups[1].Value
$afterReg    = [int]$status[1].Groups[2].Value

"복제 전  씬 인스턴스 $beforeScene · 등록 $beforeReg  (기대 1 · 1)"
"복제 후  씬 인스턴스 $afterScene · 등록 $afterReg  (기대 2 · 2) ← 판정 3"

if ($beforeScene -ne 1 -or $beforeReg -ne 1) {
    $failed += "복제 전 기준선이 씬 $beforeScene · 등록 $beforeReg 다 (기대 1·1) — 프리팹이 인스턴스로 서지 않았다(SavePrefab의 GUID 발급·즉시 등록 확인)"
}
if ($afterScene -ne 2) {
    $failed += "복제 후 씬 인스턴스가 $afterScene 개다 (기대 2) — 복제 자체가 안 됐다"
}
if ($afterReg -ne $afterScene) {
    $failed += "판정 3 실패: 복제 후 씬 인스턴스 $afterScene · 등록 $afterReg 로 벌어졌다 — 복제본이 PrefabUtility 등록부에 이어지지 않았다(Object::Instantiate의 RegisterInstance 확인)"
}

$copyMeasure = $inst | Where-Object { $_.Groups[1].Value -eq 'Copy' } | Select-Object -First 1
if ($null -eq $copyMeasure) {
    $failed += "복제본 Copy의 prefab.overrides 측정이 없다 — 이름 지정이 먹지 않았거나 시나리오가 멈췄다"
} elseif ($copyMeasure.Groups[2].Value -ne '예') {
    $failed += "판정 4 실패: 복제본이 프리팹 인스턴스로 인식되지 않는다 (m_prefabFileGuid가 복사되지 않았다)"
}

# ── 저장된 값이 정본이다 ──
if (-not (Test-Path $sceneOut)) {
    $failed += "저장 파일이 없다: $sceneOut"
} else {
    $raw = Get-Content $sceneOut -Raw
    $blocks = [regex]::Split($raw, '(?m)^  - (?:Entity|GameObject):')

    function Get-Field([string]$objectName, [string]$field) {
        foreach ($b in $blocks) {
            $nm = [regex]::Match($b, '(?m)^    m_name:\s*(.+?)\s*$')
            if (-not $nm.Success) { continue }
            if ($nm.Groups[1].Value -ne $objectName) { continue }
            $f = [regex]::Match($b, "(?m)^\s*$([regex]::Escape($field)):\s*(.+?)\s*$")
            if (-not $f.Success) { return $null }
            return $f.Groups[1].Value
        }
        return $null
    }

    $origShadow = Get-Field 'Orig' 'm_shadowCast'
    $copyShadow = Get-Field 'Copy' 'm_shadowCast'
    $origGuid   = Get-Field 'Orig' 'm_prefabFileGuid'
    $copyGuid   = Get-Field 'Copy' 'm_prefabFileGuid'

    $sidecarGuid = $null
    if (Test-Path $prefabMeta) {
        $m = [regex]::Match((Get-Content $prefabMeta -Raw), '(?m)^guid:\s*(\S+)\s*$')
        if ($m.Success) { $sidecarGuid = $m.Groups[1].Value }
    }

    ""
    "저장된 값 — Orig.m_shadowCast = $origShadow (기대 false)"
    "           Copy.m_shadowCast = $copyShadow (기대 false) ← 판정 5"
    "identity — sidecar $sidecarGuid · Orig $origGuid · Copy $copyGuid ← 판정 6"

    # 판정 6 — 원인 쪽. 인스턴스가 든 guid가 sidecar와 갈리면 SavePrefab이 새
    # GUID를 발급했다는 뜻이고, 그 순간 UpdateInstances는 아무것도 찾지 못한다.
    if ($null -eq $sidecarGuid) {
        $failed += "판정 6 실패: 최종 sidecar가 없거나 guid를 읽지 못했다: $prefabMeta"
    } elseif ($origGuid -ne $sidecarGuid -or $copyGuid -ne $sidecarGuid) {
        $failed += "판정 6 실패: 인스턴스 guid(Orig=$origGuid Copy=$copyGuid)가 sidecar guid($sidecarGuid)와 다르다 — 실행 중에 프리팹 identity를 잃고 새 GUID를 발급했다"
    }

    if ($null -eq $origShadow -or $null -eq $copyShadow) {
        $failed += "저장된 씬에서 Orig/Copy 값을 읽지 못했다 — 블록 파싱 실패(형상이 바뀌었는지 확인)"
    } else {
        # 둘을 함께 단정한다 — 한쪽만 맞는 파싱이 통과하지 못하게.
        if ($origShadow -ne 'false') {
            $failed += "Orig.m_shadowCast가 '$origShadow'다 — 원본 인스턴스조차 갱신을 못 받았다(이 상태면 판정 5는 아무것도 증명하지 못한다)"
        }
        if ($copyShadow -ne 'false') {
            $failed += "판정 5 실패: Copy.m_shadowCast가 '$copyShadow'다 — 복제본이 프리팹 갱신을 못 받았다. 등록부에 없어 UpdateInstances가 건너뛴 것이다"
        }
    }
}

# 판정 7 — 로그가 정직한가. 예전 이 줄은 맞은 버킷이 아니라 등록부 전체를 세어,
# 0건 적용된 실행에서도 "2개에 적용"이라고 말했다. 등록과 적용이 벌어지면 신호다.
if (-not $applied.Success) {
    $failed += "판정 7 실패: prefab.update의 적용 건수 줄을 읽지 못했다 (형식: '등록 인스턴스 N개 중 M개에 적용')"
} else {
    $registered = [int]$applied.Groups[1].Value
    $applyCount = [int]$applied.Groups[2].Value
    "prefab.update 적용 — 등록 $registered 개 중 $applyCount 개 (기대 2 중 2) ← 판정 7"
    if ($applyCount -ne 2) {
        $failed += "판정 7 실패: 실제 적용이 $applyCount 건이다 (기대 2) — UpdateInstances가 인스턴스를 찾지 못했다"
    }
}

if ($proc.ExitCode -ne 0) { $failed += ("종료 코드 비정상: 0x{0:X8}" -f $proc.ExitCode) }

if ($failed.Count -gt 0) {
    ""
    "실패 $($failed.Count)건:"
    $failed | ForEach-Object { "  $_" }
    exit 1
}

""
"전체 통과 — 복제된 프리팹 인스턴스가 등록부에 이어지고 갱신을 받는다"
exit 0
