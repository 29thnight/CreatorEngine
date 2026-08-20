# 중첩 프리팹 정의 변경 전파 검증 (SceneGraphRedesignPlan 트랙 P — P4-b, 0단계 게이트)
#
# ★★ P4-b 착지 완료 — 이 게이트는 이제 GREEN이고 run-all.ps1에 편입돼 있다
# (2026-08-20). 착지 전에는 의도적 RED로 스위트 **밖**에 두었다: 상시 실패
# 스크립트를 자동 스위트에 넣으면 전체가 붉어져 진짜 회귀를 가리기 때문이다
# (P-write S1과 같은 규율).
#
# ── RED -> GREEN 전환 기록 (이것이 P4-b의 증명이다) ──
#
#   착수 전                        착지 후
#   판정 4  펼친 리터럴 1회        ->  0회 · 참조 마커 1회   (굽기가 참조로 바뀌었다)
#   판정 6  m_isEnableLOD=false     ->  true                  (정의 전파가 닿았다)
#   판정 8  m_shadowCast=false      ->  false                 (오버라이드가 살아남았다)
#
# ★ 판정 4는 **방향이 뒤집힌** 항목이다. 착수 전 문구는 "파일에 옛 값이 박혔는가"로
#   펼친 스냅샷을 **전제로 보호**했다 — P4-b가 성공할수록 그 전제가 깨지므로 착지와
#   함께 재작성했다(아래 판정 4 설명 참고). 즉 이 게이트는 판정 6만 뒤집힌 것이
#   아니라 자가 검증 축도 함께 뒤집혔다.
#
# 시나리오와 이 검사가 무엇을 메우는지는 prefab_nested_update_probe.txt 상단에 있다.
#
# ── 판정 항목 ──
#
#   1  시나리오가 끝까지 갔다              — 측정 횟수(objectguid 2회 · overrides 1회)
#                                            + 저장 파일 존재
#   2  시작 등록 0                         — 뒤의 값이 이 시나리오의 결과임을 보장
#   3  증인(LeafWitness)의 오버라이드 목록에 m_isEnableLOD가 없다
#                                          — 있으면 로컬 값이 갱신과 달라 시딩이
#                                            그것을 오버라이드로 잘못 집었다는 뜻
#                                            (자가 검증, 위 판정 6의 전제 보호)
#   4  ★ 자가 검증 — 상위 프리팹 **파일 자체**가 중첩 자식을 **참조로 굽었는가**.
#      즉 펼친 MeshRenderer 리터럴(m_isEnableLOD)이 **없고**, 참조 마커
#      (m_nestedPrefabRef)가 **있어야** 한다.
#      ※ 2026-08-20 재작성: 원래 이 판정은 정반대였다 — "구운 직후 파일에 옛 값
#        (false)이 박혔는가". 그것은 **P4-b 착수 전 상태를 보호하는 전제**였고,
#        참조 노드화가 착지하면 그 리터럴이 사라지는 것이 정상이다. 즉 이 게이트는
#        판정 6만 RED→GREEN으로 뒤집히는 것이 아니라 판정 4도 함께 방향이 바뀐다.
#        (착수 전 문구를 그대로 두면 P4-b가 성공할수록 판정 4가 실패한다.)
#   5  증인의 값이 실제로 바뀌었다(LeafWitness.m_isEnableLOD == true)
#      — 갱신 메커니즘 자체가 살아있다는 증거. 이게 실패하면 6번도 무의미하다.
#   6  ★★ 핵심 판정 — 리프 정의를 바꾼 **뒤에** 새로 소환한 상위 프리팹 인스턴스
#      (OuterAfter)의 중첩 자식이 새 값(true)을 받았는가.
#   7  중첩 자식의 정체성(guid)이 LeafWitness와 같다 — 같은 NestedUpdateLeaf의
#      인스턴스라는 전제(회귀 안전판, P4-a가 세운 불변식이 여기서도 유지되는지)
#   8  ★ 오버라이드 보존 — 중첩 자식의 로컬 수정(m_shadowCast=false)이 살아남았는가.
#      P4-b는 "{정의 GUID + 오버라이드}"이므로 정의 전파(6)만 되고 오버라이드가
#      유실되면 **절반만 한 것**이다. 참조 노드는 컴포넌트를 담지 않으므로,
#      되먹임 경로가 없으면 여기서 정의값(true)이 나온다.
#      ※ 판정 8은 판정 4와 짝일 때만 뜻을 갖는다 — 배선 전에는 펼친 스냅샷이
#        같은 false를 담아 "우연히" 통과하기 때문이다(시나리오 파일 주석 참고).
#
# ── 이름으로 자식을 찾지 않는 이유 (실측으로 걸린 함정) ──
#
# 처음에는 굽기 원본 오브젝트("NestedChild")를 object.rename으로 치우고 이름
# "NestedChild"를 새 인스턴스가 재사용하게 할 계획이었다. 실측(2026-08-20)으로
# 걸렸다: object.rename(EngineEntry/ConsoleCommandSystem.cpp:1685)은
# object->m_name만 바꾸고 Scene::m_gameObjectNameSet(Scene.cpp:2058-2083, 이름
# 중복 방지용 별도 집합)은 갱신하지 않는다 — RemoveGameObjectName을 부르지
# 않으므로 옛 이름이 그 집합에 영원히 남는다. 그래서 새로 소환한 인스턴스의
# 중첩 자식은 "NestedChild"를 요청해도 GenerateUniqueGameObjectName이 이미
# 점유된 것으로 보고 "NestedChild (1)"을 대신 붙인다(Scene.cpp:2058). 이름으로
# 다시 찾으려 하면 scene->GetGameObject의 첫 매치 반환(Scene.cpp:584)에 걸려
# 치운 줄 알았던 원본을 조용히 다시 읽게 된다.
#
# 그래서 이 판정은 이름을 전혀 쓰지 않는다 — OuterAfter 블록의
# m_childrenIndices로 자식의 **인덱스**를 얻고, 그 인덱스를 가진 블록을 찾는다.
# 이름이 무엇으로 귀결되든(정상적인 "NestedChild"든, 충돌로 밀린
# "NestedChild (1)"이든) 구조는 항상 옳다.
#
# ── 음성 시험 (2026-08-20, 참조 노드화 배선 전 실측) ──
#
#   판정 4 (자산 파일의 옛 값): false 1건 검출 -> 통과(이것이 정상 전제)
#   판정 5 (증인 값):          true            -> 통과(갱신 메커니즘은 살아있다)
#   판정 6 (OuterAfter 새 값): 기대 true, 실측 false -> **실패**
#     → 참조 노드화가 없으므로 정확히 여기서만 실패한다. 5는 통과하고 6만
#       실패하는 조합이 "갱신 메커니즘은 멀쩡한데 중첩 참조만 못 받는다"는
#       P4-b의 정확한 서술이다. P4-b가 구현되면 6도 통과로 바뀐다.
#
# 사용법:
#   pwsh Tools\regression\verify-prefab-nested-update.ps1
param(
    [string]$Exe = "C:\Users\lance\source\CreatorEngine\x64\Debug\Academy_4Q.exe",
    [string]$Work = $env:TEMP,
    [int]$TimeoutSeconds = 300
)

$exeDir = [System.IO.Path]::GetDirectoryName($Exe)
if (-not (Test-Path $Exe)) { "실행 파일이 없다: $Exe"; exit 1 }

$template = Join-Path $PSScriptRoot "prefab_nested_update_probe.txt"
if (-not (Test-Path $template)) { "시나리오가 없다: $template"; exit 1 }

$sceneOut = Join-Path $Work "PrefabNestedUpdate.creator"
if (Test-Path $sceneOut) { Remove-Item $sceneOut -Force }

# 이 시나리오가 굽는 프리팹 두 개. 매 실행마다 CLI로 다시 만드는 픽스처라(정책 —
# 레거시 저작 자산에 기대지 않는다) 이전 실행의 잔재가 남아 있으면 판정 4(자가
# 검증)가 이번 실행이 아니라 지난 실행의 결과를 읽을 수 있다 — 미리 지운다
# (OvWriteProbe.prefab과 같은 이유로 .gitignore 대상이다).
$leafAssetPath = Join-Path $PSScriptRoot "..\..\Dynamic_CPP\Assets\Prefabs\NestedUpdateLeaf.prefab"
$parentAssetPath = Join-Path $PSScriptRoot "..\..\Dynamic_CPP\Assets\Prefabs\NestedUpdateParent.prefab"
foreach ($p in @($leafAssetPath, $parentAssetPath)) {
    if (Test-Path $p) { Remove-Item $p -Force }
    $meta = "$p.meta"
    if (Test-Path $meta) { Remove-Item $meta -Force }
}

$scenario = Join-Path $Work "prefab_nested_update_probe_resolved.txt"
(Get-Content $template -Raw) -replace '\{\{SCENE_OUT\}\}', ($sceneOut -replace '\\', '/') |
    Set-Content $scenario -Encoding UTF8

$outPath = Join-Path $Work "prefab_nested_update_probe.out"
$errPath = Join-Path $Work "prefab_nested_update_probe.err"

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

if (-not (Test-Path $outPath)) { "출력이 없다: $outPath"; exit 1 }
$text = Get-Content -LiteralPath $outPath -Raw

$failed = @()
$nullGuid = "00000000-0000-0000-0000-000000000000"

# ── 측정 횟수로 "도중에 멈추지 않았다"를 먼저 확인 ──
#
# 부분 실행을 성공으로 착각하지 않기 위한 자가 검증이다 — 이 저장소가 반복해
# 겪은 "부분 자로 재고 남은 것을 세는" 함정을 피한다.
$statusMatches = [regex]::Matches($text,
    '\[prefab\.status\]\s*씬 인스턴스 (\d+)개 · 등록 (\d+)개 · 캐시 (\d+)개')
$guidMatches = [regex]::Matches($text,
    '\[prefab\.objectguid\]\s*(\S+)\s+guid=([0-9a-fA-F-]+)')
$overrideMeasure = [regex]::Match($text,
    '\[prefab\.overrides\] (\S+) · 인스턴스=(\S+) · 기록 (\d+)건')
# 오버라이드 덤프 한 줄의 형식은 "[prefab.overrides]   <타입#순번|(Entity)>.<필드> = <값>"
# (ConsoleCommandSystem.cpp Cmd_prefab_overrides 참고) — 필드명 앞에 반드시 '.'이
# 온다는 사실만 이용해 m_isEnableLOD 항목의 유무를 직접 문자열로 찾는다(그룹 캡처로
# 컴포넌트/필드를 갈라 뽑지 않는다 — 탐욕적 (\S+)가 앞부분까지 삼켜 오분류할
# 위험이 있어 이 판정에는 굳이 필요하지 않은 정밀도다).

if ($statusMatches.Count -lt 1) {
    "prefab.status 기록이 1회 이상이어야 하는데 $($statusMatches.Count)회다 — 시나리오가 시작하지 못했다"
    exit 1
}
if ($guidMatches.Count -ne 2) {
    "prefab.objectguid 기록이 2회여야 하는데 $($guidMatches.Count)회다 — 시나리오가 도중에 멈췄거나 오브젝트를 못 찾았다"
    "출력: $outPath"
    exit 1
}
if (-not $overrideMeasure.Success) {
    "prefab.overrides 측정을 못 찾았다 — LeafWitness 측정이 빠졌다"
    exit 1
}

$guidByName = @{}
foreach ($m in $guidMatches) { $guidByName[$m.Groups[1].Value] = $m.Groups[2].Value }

$baseline = [pscustomobject]@{ Registered = [int]$statusMatches[0].Groups[2].Value }
"시작    등록 $($baseline.Registered)개 (기대 0)"
if ($baseline.Registered -ne 0) {
    $failed += "시작 등록이 0이 아니다($($baseline.Registered)) — 앞선 씬의 인스턴스가 남았다"
}

$witnessIsInstance = $overrideMeasure.Groups[2].Value
$witnessOverrideCount = [int]$overrideMeasure.Groups[3].Value
"LeafWitness · 인스턴스=$witnessIsInstance · 기록 $witnessOverrideCount 건(참고용, 판정 대상 아님)"
if ($witnessIsInstance -ne '예') {
    $failed += "LeafWitness가 프리팹 인스턴스로 인식되지 않았다 — m_prefabFileGuid가 널이다. AssetMetaWatcher의 200ms 지연을 못 넘겼을 수 있다(prefab_nested_update_probe.txt의 wait 90 주석 참고)"
}

# 판정 3 — 오버라이드 "개수"가 아니라 "내용"을 본다. m_isEnableLOD가 시딩되지
# 않았어야 아래 판정 5·6의 값 비교가 갱신 결과를 있는 그대로 반영한다.
$hasLodOverride = [regex]::IsMatch($text, '\[prefab\.overrides\]\s+\S+\.m_isEnableLOD = ')
if ($hasLodOverride) {
    $failed += "판정 3 실패: LeafWitness의 오버라이드 목록에 m_isEnableLOD가 있다 — 로컬 값이 갱신과 어긋나 시딩이 그것을 오버라이드로 집었다는 뜻이다. 이 상태에서는 판정 5·6이 무엇을 재는지 알 수 없다"
}

$leafWitnessGuid = $guidByName['LeafWitness']
$outerAfterGuid  = $guidByName['OuterAfter']
"objectguid LeafWitness = $leafWitnessGuid"
"objectguid OuterAfter  = $outerAfterGuid"
if ($null -eq $leafWitnessGuid -or $leafWitnessGuid -eq $nullGuid) {
    $failed += "LeafWitness의 guid를 못 읽었거나 비어 있다 — NestedUpdateLeaf 인스턴스화 자체가 실패했을 수 있다"
}

# ── 판정 4 — 자가 검증: 상위 프리팹 파일이 중첩 자식을 참조로 굽었는가 ──
#
# scene.save가 만드는 런타임 스냅샷과는 완전히 다른 소스(디스크의 .prefab 자산
# 파일)를, 완전히 다른 방법(부분 문자열 검색)으로 읽는다. 아래 판정 6과 같은
# 파서가 우연히 항상 같은 답을 내는 것이 아님을 서로 다른 경로로 교차 확인하기
# 위해서다.
#
# RootSrc 자신은 MeshRenderer가 없다(object.create RootSrc Empty). 그러므로
# 이 파일에 m_isEnableLOD가 등장한다면 그것은 **중첩 자식을 펼쳐 구웠다**는
# 뜻이고, 참조 노드화가 안 됐다는 직접 증거다.
if (-not (Test-Path $parentAssetPath)) {
    $failed += "판정 4 실패: 상위 프리팹 자산 파일이 없다: $parentAssetPath — prefab.create가 저장하지 못했다"
} else {
    $assetRaw = Get-Content -LiteralPath $parentAssetPath -Raw
    $bakedFieldMatches = [regex]::Matches($assetRaw, 'm_isEnableLOD:\s*(true|false)')
    $refMarkerMatches  = [regex]::Matches($assetRaw, 'm_nestedPrefabRef:\s*true')

    "자산 파일 NestedUpdateParent.prefab — 펼친 m_isEnableLOD $($bakedFieldMatches.Count)회(기대 0) · 참조 마커 $($refMarkerMatches.Count)회(기대 1) ← 판정 4(자가 검증)"

    if ($bakedFieldMatches.Count -ne 0) {
        $failed += "판정 4 실패(자가 검증): NestedUpdateParent.prefab에 펼친 m_isEnableLOD가 $($bakedFieldMatches.Count)회 있다(기대 0) — SerializeRecursive가 중첩 인스턴스를 참조가 아니라 스냅샷으로 굽고 있다(Prefab.cpp). 이것이 P4-b가 고치는 결함이며, 이 상태에서는 판정 6·8이 무엇을 재는지 알 수 없다"
    }
    if ($refMarkerMatches.Count -ne 1) {
        $failed += "판정 4 실패(자가 검증): 참조 마커(m_nestedPrefabRef)가 $($refMarkerMatches.Count)회다(기대 1) — 중첩 자식이 참조 노드로 굽히지 않았다"
    }
}

# ── 저장된 씬이 판정 5·6·7의 정본이다 ──
if (-not (Test-Path $sceneOut)) {
    $failed += "저장 파일이 없다: $sceneOut — scene.save가 실행되지 않았다"
} else {
    $raw = Get-Content $sceneOut -Raw

    # 검증된 패턴 재사용(verify-prefab-override-write.ps1과 동일) — 블록을 먼저
    # 쪼갠 뒤 이름으로 고른다. 앞 오브젝트의 값을 물어 오는 함정(비탐욕 .*?가
    # 파일 첫 블록부터 통째로 무는 것)을 이렇게 피한다.
    $blocks = [regex]::Split($raw, '(?m)^  - (?:Entity|GameObject):')

    function Get-Field([string]$block, [string]$field) {
        # 줄 끝까지 잡는다 — (\S+)로 자르면 인라인 맵("{x: 7, ...}")이 중간에서
        # 끊겨 값 비교가 항상 실패하는 거짓 RED가 된다(이 저장소에서 실측으로
        # 걸린 함정, prefab_override_write.txt와 같은 자리).
        $f = [regex]::Match($block, "(?m)^\s*$([regex]::Escape($field)):\s*(.+?)\s*$")
        if (-not $f.Success) { return $null }
        return $f.Groups[1].Value
    }

    function Get-NamedBlocks([string]$objectName) {
        # ★ 실측으로 걸린 PowerShell 함정: $matched가 원소 1개짜리 배열이면
        # "return $matched"가 그 배열을 스칼라(문자열 자체)로 풀어 버린다.
        # 그러면 호출부의 .Count는 문자열에 합성된 Count=1을 그대로 돌려줘
        # "블록 1개 찾음"처럼 보이지만, [0] 인덱싱은 배열 원소가 아니라 그
        # 문자열의 **첫 글자**(Char)를 돌려준다 — 이후 Get-Field 호출이 전부
        # 조용히 빈 값을 낸다(에러 없이). 단항 콤마로 배열임을 강제해야 한다.
        $matched = @()
        foreach ($b in $blocks) {
            $nm = Get-Field $b 'm_name'
            if ($nm -eq $objectName) { $matched += $b }
        }
        return ,$matched
    }

    # 판정 5 — LeafWitness (이름 충돌이 없는 대상이라 이름으로 찾아도 안전하다).
    $witnessBlocks = Get-NamedBlocks 'LeafWitness'
    if ($witnessBlocks.Count -ne 1) {
        $failed += "LeafWitness 블록이 저장된 씬에서 $($witnessBlocks.Count)개다(기대 1)"
    } else {
        $witnessLod = Get-Field $witnessBlocks[0] 'm_isEnableLOD'
        "저장된 씬 — LeafWitness.m_isEnableLOD = $witnessLod (기대 true) ← 판정 5"
        if ($witnessLod -ne 'true') {
            $failed += "판정 5 실패: LeafWitness가 리프의 새 정의(true)를 못 받았다('$witnessLod') — 프리팹 갱신 메커니즘 자체가 이 실행에서 돌지 않았다. 판정 6의 실패는 이 탓일 수 있으므로 P4-b와 무관하게 재확인이 필요하다"
        }
    }

    # 판정 6·7 — OuterAfter를 이름으로 찾고(루트라 충돌 걱정이 없다 — override
    # 이름은 매번 유일해야 한다는 게 별도 불변식이다), 그 m_childrenIndices로
    # 진짜 중첩 자식을 구조적으로 찾는다(이름 충돌 함정 회피, 위 헤더 주석 참고).
    $outerBlocks = Get-NamedBlocks 'OuterAfter'
    if ($outerBlocks.Count -ne 1) {
        $failed += "OuterAfter 블록이 저장된 씬에서 $($outerBlocks.Count)개다(기대 1) — 핵심 판정(6)을 진행할 수 없다"
    } else {
        $outerBlock = $outerBlocks[0]
        $outerIndex = Get-Field $outerBlock 'm_index'
        $childrenRaw = Get-Field $outerBlock 'm_childrenIndices'
        "OuterAfter — m_index=$outerIndex · m_childrenIndices=$childrenRaw"

        if ($null -eq $childrenRaw -or $childrenRaw -eq '~' -or $childrenRaw -notmatch '^\[(\d+)\]$') {
            $failed += "판정 6 실패: OuterAfter의 m_childrenIndices가 '$childrenRaw'다(기대 자식 1개짜리 목록, 예: [6]) — 중첩 자식이 아예 안 달렸다. NestedUpdateParent.prefab의 children 노드 자체가 안 실렸을 수 있다"
        } else {
            $childIndex = $Matches[1]

            # 구조적 탐색: m_index가 childIndex인 블록을 찾는다. 정확히 하나여야
            # 한다 — 0개면 인덱스가 가리키는 오브젝트가 없는 것이고, 2개 이상이면
            # 인덱스 중복이라는 이 자체가 다른 결함이다.
            $childBlocks = @()
            foreach ($b in $blocks) {
                if ((Get-Field $b 'm_index') -eq $childIndex) { $childBlocks += $b }
            }

            if ($childBlocks.Count -ne 1) {
                $failed += "판정 6 실패: m_index=$childIndex 인 블록이 $($childBlocks.Count)개다(기대 1) — 구조적 자식 탐색이 실패했다(형상이 바뀌었는지 확인)"
            } else {
                $child = $childBlocks[0]
                $childName = Get-Field $child 'm_name'
                $childLod = Get-Field $child 'm_isEnableLOD'
                $childShadow = Get-Field $child 'm_shadowCast'
                $childGuid = Get-Field $child 'm_prefabFileGuid'

                "OuterAfter의 중첩 자식(m_name=$childName) — m_isEnableLOD=$childLod (기대 true) ← 판정 6(핵심)"
                "OuterAfter의 중첩 자식 — m_shadowCast=$childShadow (기대 false) ← 판정 8(오버라이드 보존)"
                "OuterAfter의 중첩 자식 — m_prefabFileGuid=$childGuid ← 판정 7"

                # 판정 8 — 로컬 오버라이드가 참조 노드를 건너 되먹여졌는가.
                # 정의값은 true(MeshRenderer.h)이므로 true가 나오면 유실이다.
                if ($null -eq $childShadow) {
                    $failed += "판정 8 실패: 중첩 자식 블록에서 m_shadowCast를 못 읽었다 — MeshRenderer 자체가 안 붙었을 수 있다(참조 해석이 컴포넌트를 만들지 못한 경우)"
                } elseif ($childShadow -ne 'false') {
                    $failed += "판정 8 실패: 중첩 자식의 로컬 오버라이드(m_shadowCast=false)가 유실되고 정의값 '$childShadow'가 됐다 — 참조 노드가 오버라이드를 싣지 않았거나 되먹임 경로가 없다. P4-b는 '{정의 GUID + 오버라이드}'이므로 정의 전파(판정 6)만으로는 절반이다"
                }

                if ($null -eq $childLod) {
                    $failed += "판정 6 실패: 중첩 자식 블록에서 m_isEnableLOD를 못 읽었다 — 오브젝트 블록 형상이 바뀌었는지 확인(MeshRenderer가 없거나 필드명이 바뀌었을 수 있다)"
                } elseif ($childLod -ne 'true') {
                    $failed += "판정 6 실패(핵심): OuterAfter의 중첩 자식이 리프의 새 정의(true)를 받지 못하고 '$childLod'로 남았다 — SerializeRecursive가 중첩 인스턴스를 참조가 아니라 스냅샷으로 굽기 때문이다(Prefab.cpp:94). 이것이 P4-b가 고치는 결함이다"
                }

                if ($null -ne $childGuid -and $null -ne $leafWitnessGuid -and $childGuid -ne $leafWitnessGuid) {
                    $failed += "판정 7 실패: 중첩 자식의 guid($childGuid)가 LeafWitness의 guid($leafWitnessGuid)와 다르다 — 같은 NestedUpdateLeaf의 인스턴스라는 전제가 깨졌다(엉뚱한 블록을 짚었을 수 있다)"
                }
            }
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
"전체 통과 — 중첩 프리팹 정의 변경이 상위 프리팹의 새 인스턴스에 전달된다"
exit 0
