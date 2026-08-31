# 프리팹 identity 교란 게이트 (SceneGraphRedesignPlan 트랙 P)
#
# 시나리오와 이 검사가 무엇을 메우는지는 prefab_identity_injection.txt 상단에 있다.
#
# ── 판정 항목 ──
#
#   1  시나리오가 끝까지 갔다      — prefab.status 2회 + 저장 파일 존재
#   2  주입 창을 실제로 잡았다     — 못 잡았으면 아무것도 검증하지 못한 것이다
#   3  교란이 실제로 먹었다        — 지운 직후 sidecar가 정말 없었다
#   4  identity를 지켰다           — ★ 저장된 인스턴스 guid == 최종 sidecar guid
#   5  갱신이 실제로 전파됐다      — ★★ Orig/Copy.m_shadowCast가 false
#   6  적용 건수를 정직하게 셌다   — prefab.update가 "2개 중 2개에 적용"이라고 말한다
#
# ★ 자가 틀렸을 때 어떻게 드러나는가:
#   · 판정 2·3이 없으면 "교란을 넣지 못한 실행"이 통과로 나온다. 그건 대조군이지
#     검사가 아니다(빈 집합을 성공으로 읽는 거짓 통과).
#   · 판정 4가 없으면 "새 GUID를 발급하고 0건 적용"이 판정 5의 우연한 통과에
#     묻힌다. 4와 5는 같은 결함의 앞뒤다 — 4는 원인, 5는 결과.
#   · 판정 6이 없으면 로그가 "2개에 적용"이라고 거짓말해도 아무도 묻지 않는다.
#     (고치기 전 이 줄은 맞은 버킷이 아니라 m_instanceMap 전체를 셌다.)
#
# 사용법:
#   pwsh Tools\regression\verify-prefab-identity-injection.ps1
param(
    [string]$Exe = (Join-Path $PSScriptRoot "..\..\Bin\x64-Debug\Editor\CreatorEditor.exe"),
    [string]$Work = $env:TEMP,
    [int]$TimeoutSeconds = 300,
    # 주입 창의 프레임 수. 게이트는 stdout으로 창 진입을 확인하고 지우므로 이 값은
    # 여유일 뿐이지만, 너무 짧으면 지우기 전에 창이 닫힌다.
    [int]$WindowFrames = 900
)

$exeDir = [System.IO.Path]::GetDirectoryName($Exe)
if (-not (Test-Path $Exe)) { "실행 파일이 없다: $Exe"; exit 1 }

$template = Join-Path $PSScriptRoot "prefab_identity_injection.txt"
if (-not (Test-Path $template)) { "시나리오가 없다: $template"; exit 1 }

$repoRoot  = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
$prefabDir = Join-Path $repoRoot "Dynamic_CPP\Assets\Prefabs"
$prefab    = Join-Path $prefabDir "DupProbe.prefab"
$meta      = Join-Path $prefabDir "DupProbe.prefab.meta"

# 이전 실행의 잔재를 지운다 — 그리고 **지워졌는지 확인한다.** 조용히 실패하면
# 초기 상태가 실행마다 달라져 결과를 못 믿는다.
foreach ($p in @($prefab, $meta)) {
    if (Test-Path $p) { Remove-Item $p -Force -ErrorAction SilentlyContinue }
    if (Test-Path $p) { "이전 실행이 남긴 픽스처를 지우지 못했다: $p"; exit 1 }
}

$sceneOut = Join-Path $Work "PrefabIdentityInjection.creator"
if (Test-Path $sceneOut) { Remove-Item $sceneOut -Force -ErrorAction SilentlyContinue }
if (Test-Path $sceneOut) { "이전 실행의 저장 파일을 지우지 못했다: $sceneOut"; exit 1 }

$scenario = Join-Path $Work "prefab_identity_injection_resolved.txt"
((Get-Content $template -Raw) -replace '\{\{SCENE_OUT\}\}', $sceneOut.Replace([char]92, [char]47)) `
    -replace '\{\{WINDOW\}\}', $WindowFrames | Set-Content $scenario -Encoding UTF8

$outPath = Join-Path $Work "prefab_identity_injection.out"
$errPath = Join-Path $Work "prefab_identity_injection.err"

$proc = Start-Process -FilePath $Exe -ArgumentList "--script", $scenario `
    -WorkingDirectory $exeDir `
    -RedirectStandardOutput $outPath `
    -RedirectStandardError $errPath -PassThru

# ── 교란 주입 ──
# "[prefab.overrides] Copy"가 stdout에 뜨면 시나리오가 주입 창에 들어온 것이다.
# 그 직후 sidecar를 지운다 — 워처의 HandleDeleted가 도는 경로가 원자적 게시의
# Delete 알림과 같으므로, 이것이 그 경합의 확정적 대역이다.
$armed = $false
$injected = $false
$sidecarGone = $false
$sw = [Diagnostics.Stopwatch]::StartNew()
while (-not $proc.HasExited -and $sw.Elapsed.TotalSeconds -lt $TimeoutSeconds) {
    try { $seen = Get-Content $outPath -Raw -ErrorAction Stop } catch { $seen = '' }
    if ($seen -match '\[prefab\.overrides\] Copy') { $armed = $true; break }
    Start-Sleep -Milliseconds 50
}
if ($armed) {
    Start-Sleep -Milliseconds 300
    if (Test-Path $meta) {
        Remove-Item $meta -Force -ErrorAction SilentlyContinue
        $injected = $true
        # 지운 **직후** 부재를 확인한다. 나중에 보면 prefab.update의 SavePrefab이
        # 이미 다시 만들어 놓아 교란이 있었는지 알 수 없다.
        $sidecarGone = -not (Test-Path $meta)
    }
}

$proc.WaitForExit($TimeoutSeconds * 1000) | Out-Null
if (-not $proc.HasExited) {
    $proc.Kill()
    "타임아웃 ($TimeoutSeconds 초). 시나리오가 끝나지 않았다."
    exit 1
}

$text = Get-Content $outPath -Raw
$status  = [regex]::Matches($text, '\[prefab\.status\] 씬 인스턴스 (\d+)개 · 등록 (\d+)개')
$applied = [regex]::Match($text, '\[prefab\.update\] \S+ <- \S+ · 등록 인스턴스 (\d+)개 중 (\d+)개에 적용')

$failed = @()

if ($status.Count -lt 2) {
    "prefab.status 측정이 $($status.Count) 건뿐이다 (기대 2). 시나리오가 도중에 멈췄다."
    if ($proc.ExitCode -ne 0) { "  종료 코드: 0x{0:X8}" -f $proc.ExitCode }
    exit 1
}

"주입 창 진입 = $armed · sidecar 삭제 = $injected · 삭제 직후 부재 확인 = $sidecarGone"

if (-not $armed) {
    $failed += "판정 2 실패: 주입 창을 잡지 못했다 — 교란 없이 끝났으므로 이 실행은 아무것도 검증하지 않았다"
} elseif (-not $injected) {
    $failed += "판정 2 실패: 창에는 들어갔는데 지울 sidecar가 없었다 — prefab.create가 sidecar를 만들지 않았다"
} elseif (-not $sidecarGone) {
    $failed += "판정 3 실패: sidecar를 지웠는데 그대로 있다 — 교란이 먹지 않았으므로 통과해도 의미가 없다"
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
    if (Test-Path $meta) {
        $m = [regex]::Match((Get-Content $meta -Raw), '(?m)^guid:\s*(\S+)\s*$')
        if ($m.Success) { $sidecarGuid = $m.Groups[1].Value }
    }

    ""
    "최종 sidecar guid = $sidecarGuid"
    "저장 Orig guid    = $origGuid  shadowCast = $origShadow (기대 false)"
    "저장 Copy guid    = $copyGuid  shadowCast = $copyShadow (기대 false) ← 판정 5"

    if ($null -eq $origShadow -or $null -eq $copyShadow) {
        $failed += "저장된 씬에서 Orig/Copy 값을 읽지 못했다 — 블록 파싱 실패(형상이 바뀌었는지 확인)"
    } else {
        # 판정 4 — 원인. identity가 갈리면 SavePrefab이 새 GUID를 발급했다는 뜻이고,
        # 그 순간 UpdateInstances는 아무 인스턴스도 찾지 못한다.
        if ($null -eq $sidecarGuid) {
            $failed += "판정 4 실패: 최종 sidecar가 없거나 guid를 읽지 못했다 — catalog 정본이 사라졌다"
        } elseif ($origGuid -ne $sidecarGuid -or $copyGuid -ne $sidecarGuid) {
            $failed += "판정 4 실패: 인스턴스 guid(Orig=$origGuid Copy=$copyGuid)가 sidecar guid($sidecarGuid)와 다르다 — 교란 창에서 identity를 잃고 새 GUID를 발급했다"
        }
        # 판정 5 — 결과. 둘을 함께 단정한다(한쪽만 맞는 파싱이 통과하지 못하게).
        if ($origShadow -ne 'false') {
            $failed += "Orig.m_shadowCast가 '$origShadow'다 — 원본 인스턴스조차 갱신을 못 받았다"
        }
        if ($copyShadow -ne 'false') {
            $failed += "판정 5 실패: Copy.m_shadowCast가 '$copyShadow'다 — 복제본이 프리팹 갱신을 못 받았다"
        }
    }
}

# 판정 6 — 로그가 정직한가. 고치기 전 이 줄은 맞은 버킷이 아니라 등록부 전체를
# 세어, 0건 적용된 실행에서도 "2개에 적용"이라고 말했다.
if (-not $applied.Success) {
    $failed += "판정 6 실패: prefab.update의 적용 건수 줄을 읽지 못했다 (형식: '등록 인스턴스 N개 중 M개에 적용')"
} else {
    $registered = [int]$applied.Groups[1].Value
    $applyCount = [int]$applied.Groups[2].Value
    "prefab.update 적용 — 등록 $registered 개 중 $applyCount 개 (기대 2 중 2) ← 판정 6"
    if ($applyCount -ne 2) {
        $failed += "판정 6 실패: 실제 적용이 $applyCount 건이다 (기대 2) — UpdateInstances가 인스턴스를 찾지 못했다"
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
"전체 통과 — sidecar를 실행 중에 떨어뜨려도 프리팹 identity가 유지되고 갱신이 전파된다"
exit 0
