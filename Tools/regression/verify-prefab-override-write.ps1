# 프리팹 오버라이드 기록 배선 대조 (SceneGraphRedesignPlan 트랙 P — P-write)
#
# 시나리오와 이 검사가 무엇을 메우는지는 prefab_override_write.txt 상단에 있다.
#
# ── 판정 항목 ──
#
#   1  시나리오가 끝까지 갔다          — prefab.overrides 측정 5회 + 저장 파일 존재
#   2  인스턴스로 인식됐다             — 인스턴스=예. 아니면 이 검사는 프리팹이
#                                        아닌 것을 재고 있다(S0.5가 고친 그 결함)
#   3  로컬 수정이 기록됐다            — ★ object.property 직후 기록 > 0
#   4  손대지 않은 인스턴스는 0건      — 과잉 기록 방지
#   5  갱신이 실제로 돌았다            — InstB의 m_shadowCast가 false로 바뀜
#   6  로컬 수정이 갱신을 살아남았다   — ★★ InstA의 m_isEnableLOD가 여전히 true
#
# 3과 6이 이 게이트의 존재 이유다. 5는 **자가 진짜인지 재는 자**다 —
# 갱신이 아예 안 돌았는데 6이 통과하면 아무것도 증명하지 못한다.
#
# ── 음성 시험 (2026-08-20, 배선 전 실측) ──
#
#   판정 3: 기록 0건                -> 실패 (기대한 실패 — 배선이 없다)
#   판정 6: InstA.m_isEnableLOD=true -> **통과**
#   판정 5: InstB.m_shadowCast=true  -> 실패 (갱신이 InstB에 안 닿았다)
#
# ★ 판정 6이 배선 없이 통과하는 이유: UpdateInstances가 적용 직전에 부르는
# 과도기 시딩(SeedOverridesFromSnapshot)이 로컬 수정을 잡아내 배제 집합에
# 넣어 준다. 즉 **같은 세션 안에서는** 보호가 된다. 이것이 무너지는 지점은
# 씬을 저장하고 다시 열었을 때다 — m_prefabOriginal이 비직렬화라 시딩의
# 근거가 사라진다. 그 창을 재는 것은 이 게이트의 후속 과제다.
#
# ★★ 처음 이 자리에 "판정 6 실패 — 로컬 수정이 조용히 소멸"이라고 적었는데
# **틀렸다.** 원인은 이 스크립트의 블록 정규식이 앞 오브젝트(Src)의 값을
# 물어 온 것이었다(아래 Get-Field 주석). 자를 먼저 검증하지 않으면 거짓
# 실패가 그대로 진단이 된다 — 이 저장소가 반복해 겪은 실패 양식이다.
#
# 사용법:
#   pwsh Tools\regression\verify-prefab-override-write.ps1
param(
    [string]$Exe = "C:\Users\lance\source\CreatorEngine\x64\Debug\Academy_4Q.exe",
    [string]$Work = $env:TEMP,
    [int]$TimeoutSeconds = 300
)

$exeDir = [System.IO.Path]::GetDirectoryName($Exe)
if (-not (Test-Path $Exe)) { "실행 파일이 없다: $Exe"; exit 1 }

$template = Join-Path $PSScriptRoot "prefab_override_write.txt"
if (-not (Test-Path $template)) { "시나리오가 없다: $template"; exit 1 }

$sceneOut = Join-Path $Work "PrefabOverrideWrite.creator"
if (Test-Path $sceneOut) { Remove-Item $sceneOut -Force }

$scenario = Join-Path $Work "prefab_override_write_resolved.txt"
(Get-Content $template -Raw) -replace '\{\{SCENE_OUT\}\}', ($sceneOut -replace '\\', '/') |
    Set-Content $scenario -Encoding UTF8

$outPath = Join-Path $Work "prefab_override_write.out"
$errPath = Join-Path $Work "prefab_override_write.err"

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

$text = Get-Content $outPath -Raw

# [prefab.overrides] <이름> · 인스턴스=<예|아니오> · 기록 <n>건
$measures = [regex]::Matches($text,
    '\[prefab\.overrides\] (\S+) · 인스턴스=(\S+) · 기록 (\d+)건')

$failed = @()

if ($measures.Count -lt 5) {
    "prefab.overrides 측정이 $($measures.Count) 건뿐이다 (기대 5). 시나리오가 도중에 멈췄다."
    if ($proc.ExitCode -ne 0) { "  종료 코드: 0x{0:X8}" -f $proc.ExitCode }
    exit 1
}

# 순서: [0] 기준선 InstA · [1] 수정 직후 InstA · [2] InstB(갱신 전)
#       [3] 갱신 후 InstA · [4] 갱신 후 InstB
$baseA    = [int]$measures[0].Groups[3].Value
$afterA   = [int]$measures[1].Groups[3].Value
$untouchB = [int]$measures[2].Groups[3].Value
$updatedA = [int]$measures[3].Groups[3].Value
$updatedB = [int]$measures[4].Groups[3].Value
$isInstA  = $measures[0].Groups[2].Value

"기준선 InstA        기록 $baseA 건 (기대 0)"
"로컬 수정 직후 InstA 기록 $afterA 건 (기대 1 이상) ← 판정 3"
"손대지 않은 InstB    기록 $untouchB 건 (기대 0)      ← 판정 4"
"프리팹 갱신 후 InstA 기록 $updatedA 건 (기대 1 이상)"
"프리팹 갱신 후 InstB 기록 $updatedB 건"
"인스턴스 인식        $isInstA (기대 예)"

if ($isInstA -ne '예') {
    $failed += "InstA가 프리팹 인스턴스로 인식되지 않았다 — m_prefabFileGuid가 널이다. 이 검사는 프리팹이 아닌 것을 재고 있다(SavePrefab의 GUID 발급 확인)"
}
if ($baseA -ne 0) {
    $failed += "기준선 기록이 $baseA 건이다 — 수정하기 전인데 이미 기록이 있다"
}
if ($afterA -lt 1) {
    $failed += "판정 3 실패: 로컬 수정 직후 기록이 $afterA 건이다 — object.property가 m_prefabOverrides에 남기지 않는다(RecordPropertyOverride 배선 확인)"
}
if ($untouchB -ne 0) {
    $failed += "판정 4 실패: 손대지 않은 InstB에 기록이 $untouchB 건이다 — 과잉 기록이다"
}
if ($updatedA -lt 1) {
    $failed += "프리팹 갱신 뒤 기록이 $updatedA 건으로 사라졌다 — UpdateInstances가 목록을 지운다"
}

# ── 저장된 값이 정본이다 ──
if (-not (Test-Path $sceneOut)) {
    $failed += "저장 파일이 없다: $sceneOut — scene.save가 실행되지 않았다"
} else {
    $raw = Get-Content $sceneOut -Raw

    # ★ 오브젝트 블록을 **먼저 쪼갠 뒤** 이름으로 고른다.
    #
    # 이전 판은 "(?ms)^  - Entity:.*?m_name: <이름>...(?=다음 블록)" 하나로 잡았는데,
    # 앞의 .*?가 비탐욕이라 **파일 첫 블록부터** 대상 이름 줄까지를 통째로 물었다.
    # 그 덩어리 안에서 필드를 찾으면 앞선 오브젝트(Src)의 값이 먼저 걸린다.
    # 실제로 그 버그가 "InstA.m_isEnableLOD=false"라는 거짓 실패를 만들었고,
    # 그 거짓 실패가 "로컬 수정이 조용히 소멸한다"는 잘못된 진단으로 이어졌다.
    # 자를 먼저 검증하라는 규율이 정확히 이 자리를 위한 것이다.
    $blocks = [regex]::Split($raw, '(?m)^  - (?:Entity|GameObject):')

    function Get-Field([string]$objectName, [string]$field) {
        foreach ($b in $blocks) {
            $nm = [regex]::Match($b, '(?m)^    m_name:\s*(.+?)\s*$')
            if (-not $nm.Success) { continue }
            if ($nm.Groups[1].Value -ne $objectName) { continue }
            $f = [regex]::Match($b, "$([regex]::Escape($field)):\s*(\S+)")
            if (-not $f.Success) { return $null }
            return $f.Groups[1].Value
        }
        return $null
    }
    function Get-BoolField([string]$objectName, [string]$field) {
        return (Get-Field $objectName $field)
    }

    $aLod    = Get-BoolField 'InstA' 'm_isEnableLOD'
    $bLod    = Get-BoolField 'InstB' 'm_isEnableLOD'
    $bShadow = Get-BoolField 'InstB' 'm_shadowCast'

    ""
    "저장된 값 — InstA.m_isEnableLOD = $aLod (기대 true)   ← 판정 6"
    "           InstB.m_isEnableLOD = $bLod (기대 false)"
    "           InstB.m_shadowCast  = $bShadow (기대 false) ← 판정 5"

    if ($null -eq $aLod -or $null -eq $bShadow) {
        $failed += "저장된 씬에서 값을 읽지 못했다 — 오브젝트 블록 파싱 실패(형상이 바뀌었는지 확인)"
    } else {
        if ($bShadow -ne 'false') {
            $failed += "판정 5 실패: InstB.m_shadowCast가 '$bShadow'다 — 프리팹 갱신이 아예 돌지 않았다. 이 상태로는 판정 6이 통과해도 아무것도 증명하지 못한다(prefab.update의 정체성 승계 확인)"
        }
        if ($aLod -ne 'true') {
            $failed += "판정 6 실패: InstA.m_isEnableLOD가 '$aLod'다 — 로컬 수정이 프리팹 갱신에 조용히 덮였다. 이것이 P-write가 고치려는 결함이다"
        }
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
"전체 통과 — 로컬 수정이 기록되고 프리팹 갱신을 살아남는다"
exit 0
