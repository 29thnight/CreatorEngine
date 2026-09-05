# 스크립트 생명주기 6단계가 네이티브와 C#에서 같은 뜻을 갖는지 검사한다 (PHASE 9.5).
#
# ── 왜 있나 ──
#
# ScriptLifecyclePhase.h가 스스로 이렇게 적어 두고 있다:
#
#   "ScriptCore/ScriptRegistry.cs의 LifecyclePhase와 **값이 같아야 한다.** 경계를
#    넘는 것은 int 하나이므로 컴파일러가 불일치를 잡아 주지 않는다."
#
# 계약은 주석으로만 서 있었고 그것을 재는 게이트가 없었다(2026-09-05 실측). 형제
# 미러 셋은 이미 못이 박혀 있다 — API 표(check-api-table), BT 열거 5종
# (check-bt-enums), 진입점 이름(check-entry-points). 여기만 비어 있었다.
#
# ── 세 축을 보는 이유 ──
#
# 값만 대조하면 "양쪽이 사이좋게 같이 틀린" 경우를 못 잡는다. 이 계약은 셋이 함께
# 서야 뜻이 보존된다:
#
#   판정 1 값 미러   — 두 열거가 자리별로 같은 이름·같은 값인가.
#                      어긋나면 관리 측이 **다른 단계의 훅**을 부른다. 크래시가
#                      아니라 "스크립트가 좀 이상하다"로 나타나 원인을 짚기 어렵다.
#
#   판정 2 계약 순서 — 열거 순서가 Component.h의 6단계 virtual 선언 순서와 같은가.
#                      헤더가 "순서는 Component.h의 6단계 선언 순서를 따른다"고
#                      규정한다. 이 축이 없으면 양쪽 열거를 함께 고쳐 네이티브
#                      계약에서 떨어져 나가도 판정 1이 초록이다.
#                      틱 훅이 이 열거에 섞여 드는 것도 여기서 걸린다 —
#                      PrePhysics/PostPhysics는 6단계 virtual 목록에 없다
#                      (헤더의 ★ 경고, NetworkFrameworkPlan N3-b).
#
#   판정 3 전달 배선 — ScriptComponent가 6단계를 모두 override하고, 각 override가
#                      **자기 단계**를 NotifyManagedLifecycle에 넘기는가.
#                      이것이 가장 조용한 결함을 막는다: LifecycleRegistry의
#                      MaskOfType은 `&T::Hook != &Component::Hook`으로 비트를 세우므로
#                      override를 빠뜨리면 비트가 안 서고 그 단계는 **아무 소리 없이**
#                      관리 측에 영영 닿지 않는다. 라벨 뒤바뀜(OnAddedToScene이
#                      OnBeginSimulation을 넘기는 식)도 같은 자리에서 걸린다.
#
# 사용법: pwsh ScriptCore\check-lifecycle-enums.ps1
# 어긋나면 종료 코드 1과 함께 어느 축의 어느 자리인지 보여 준다.

$ErrorActionPreference = 'Stop'

. (Join-Path $PSScriptRoot 'EnumMirror.ps1')

$root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)

$nativeEnumPath  = Join-Path $root 'Engine\SceneRuntime\ScriptLifecyclePhase.h'
$managedEnumPath = Join-Path $root 'ScriptCore\ScriptRegistry.cs'
$componentPath   = Join-Path $root 'Engine\SceneRuntime\Component.h'
$scriptHeaderPath = Join-Path $root 'Engine\SceneRuntime\ScriptComponent.h'
$scriptSourcePath = Join-Path $root 'Engine\SceneRuntime\ScriptComponent.cpp'

$failed = New-Object System.Collections.Generic.List[string]

# ── 판정 1: 값 미러 ────────────────────────────────────────────────────────────
#
# 이름이 양쪽에서 다르다(네이티브 ScriptLifecyclePhase / 관리 LifecyclePhase).
# BT 게이트는 이름이 같은 미러만 다루므로 여기서는 따로 넘긴다.

$native  = Get-EnumMembers -Path $nativeEnumPath  -EnumName 'ScriptLifecyclePhase' -Language 'cpp'
$managed = Get-EnumMembers -Path $managedEnumPath -EnumName 'LifecyclePhase'       -Language 'cs'

Write-Output ("판정 1 값 미러    : 네이티브 {0}개 · 관리 {1}개" -f $native.Count, $managed.Count)

$diff = Compare-EnumMirror -Native $native -Managed $managed
if ($null -ne $diff) {
    Write-Output $diff
    $failed.Add('값 미러')
}

# ── 판정 2: 네이티브 계약 순서 ─────────────────────────────────────────────────
#
# Component.h의 6단계는 활성 축(OnEnable/OnDisable)보다 **앞에** 선언돼 있고, 그
# 순서가 곧 열거의 순서다. 활성 축은 6단계와 직교라 이 열거로 오지 않으므로
# (ScriptLifecyclePhase.h) 앞에서부터 열거 개수만큼만 짝지어 본다.

$phaseDecls = New-Object System.Collections.Generic.List[string]
foreach ($line in (Get-Content -LiteralPath $componentPath)) {
    if ($line -match '^\s*virtual\s+void\s+(On[A-Za-z0-9_]*)\s*\(\s*\)') {
        $phaseDecls.Add($Matches[1])
    }
}

if ($phaseDecls.Count -lt $native.Count) {
    Write-Output ("판정 2 계약 순서  : Component.h의 무인자 virtual On* 선언이 {0}개뿐 — 열거 {1}개를 덮지 못한다" -f $phaseDecls.Count, $native.Count)
    $failed.Add('계약 순서')
}
else {
    $orderMismatch = $null
    for ($i = 0; $i -lt $native.Count; $i++) {
        if ($native[$i].Name -ne $phaseDecls[$i]) {
            $orderMismatch = "  [$i] 불일치 — 열거 $($native[$i].Name) / Component.h $($phaseDecls[$i])"
            break
        }
    }

    Write-Output ("판정 2 계약 순서  : Component.h 선두 {0}개와 대조" -f $native.Count)
    if ($null -ne $orderMismatch) {
        Write-Output $orderMismatch
        Write-Output '  열거 순서는 Component.h의 6단계 선언 순서를 따른다(ScriptLifecyclePhase.h 값 규약).'
        $failed.Add('계약 순서')
    }
}

# ── 판정 3: 전달 배선 ──────────────────────────────────────────────────────────
#
# .h의 override 목록과 .cpp의 전달 인자를 함께 본다. override가 빠지면 MaskOfType이
# 비트를 세우지 않아 그 단계가 조용히 사라지고, 인자가 어긋나면 다른 단계가 전달된다.

$headerText = Get-Content -LiteralPath $scriptHeaderPath -Raw
$sourceLines = Get-Content -LiteralPath $scriptSourcePath

# 각 정의의 본문에서 넘기는 단계를 모은다. 본문의 끝은 열 0의 '}'로 판정한다 —
# 이 파일은 그 관례를 지킨다(중첩 블록은 전부 들여쓰기돼 있다).
$forwarded = @{}
$current = $null
foreach ($line in $sourceLines) {
    if ($line -match '^void\s+ScriptComponent::(On[A-Za-z0-9_]*)\s*\(\s*\)') {
        $current = $Matches[1]
        $forwarded[$current] = New-Object System.Collections.Generic.List[string]
        continue
    }
    if ($null -eq $current) { continue }
    if ($line -match '^\}') { $current = $null; continue }
    if ($line -match 'NotifyManagedLifecycle\(\s*ScriptLifecyclePhase::([A-Za-z0-9_]+)\s*\)') {
        $forwarded[$current].Add($Matches[1])
    }
}

$wiringProblems = New-Object System.Collections.Generic.List[string]
foreach ($member in $native) {
    $name = $member.Name

    if ($headerText -notmatch "void\s+$name\s*\(\s*\)\s*override") {
        $wiringProblems.Add("  $name — ScriptComponent.h에 override 선언이 없다(MaskOfType이 비트를 세우지 않아 단계가 조용히 사라진다)")
        continue
    }

    if (-not $forwarded.ContainsKey($name)) {
        $wiringProblems.Add("  $name — ScriptComponent.cpp에 정의가 없다")
        continue
    }

    $sent = $forwarded[$name]
    if ($sent.Count -eq 0) {
        $wiringProblems.Add("  $name — NotifyManagedLifecycle 호출이 없다(관리 측에 이 단계가 닿지 않는다)")
        continue
    }
    foreach ($s in $sent) {
        if ($s -ne $name) {
            $wiringProblems.Add("  $name — 다른 단계를 넘긴다: ScriptLifecyclePhase::$s")
        }
    }
}

Write-Output ("판정 3 전달 배선  : 6단계 override·전달 인자 대조 (정의 {0}개 발견)" -f $forwarded.Count)
if ($wiringProblems.Count -gt 0) {
    foreach ($p in $wiringProblems) { Write-Output $p }
    $failed.Add('전달 배선')
}

# ── 판정 ──────────────────────────────────────────────────────────────────────

Write-Output ''
if ($failed.Count -gt 0) {
    Write-Output "어긋난 축: $($failed -join ', ')"
    Write-Output '단계 번호는 경계를 int 하나로 건너간다 — 컴파일도 링크도 통과하고'
    Write-Output '실행 중에 다른 훅이 불리거나 한 단계가 통째로 사라진다.'
    exit 1
}

Write-Output "생명주기 단계 미러 3축 통과 — 값 $($native.Count)개 · 계약 순서 · 전달 배선"
exit 0
