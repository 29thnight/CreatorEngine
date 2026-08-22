# UI 회귀 세트 전체 실행.
#
# 각 검사는 종료 코드로 판정한다. 하나라도 실패하면 이 스크립트도 실패로 끝난다.
#
#   ui_regression.txt         비정상 순서로 UI를 만들고 재생/정지를 반복한다(PHASE 6).
#                             에디터에서 정상 순서로 만들면 드러나지 않는 크래시를 잡는다.
#   verify-ui-layout-golden   앵커 9종·3단 중첩의 레이아웃 형상을 통째로 고정(7-2·7-4).
#   verify-resolution-sweep   해상도를 바꿔 가며 캔버스·자식·클릭 판정이 따라오는지(7-1·7-3·7-6).
param(
    [string]$Exe = "C:\Users\lance\source\CreatorEngine\x64\Debug\Academy_4Q.exe",
    [string]$Work = $env:TEMP
)

$exeDir = [System.IO.Path]::GetDirectoryName($Exe)
if (-not (Test-Path $Exe)) { "실행 파일이 없다: $Exe"; exit 1 }

$failed = @()

function Run-Step([string]$name, [scriptblock]$body) {
    "=== $name ==="
    & $body
    if ($LASTEXITCODE -ne 0) { $script:failed += $name }
    ""
}

Run-Step "UI 생성 순서 회귀" {
    # 모델 경로를 저장소 루트 기준으로 채운다(2026-08-20). 예전에는 시나리오가
    # 사용자 개인 폴더의 GLB를 절대 경로로 가리켜 그 기계에서만 돌았다.
    $template = Join-Path $PSScriptRoot "ui_regression.txt"
    $repoRoot = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
    $modelPath = Join-Path $repoRoot "Dynamic_CPP\Assets\Models\Prim_Suzanne.glb"
    if (-not (Test-Path $modelPath)) {
        "모델이 없다: $modelPath"; $global:LASTEXITCODE = 1; return
    }
    $script = Join-Path $Work "ui_regression_resolved.txt"
    (Get-Content $template -Raw) -replace '\{\{MODEL_PATH\}\}', ($modelPath -replace '\\', '/') |
        Set-Content $script -Encoding UTF8

    $proc = Start-Process -FilePath $Exe -ArgumentList "--script", $script `
        -WorkingDirectory $exeDir `
        -RedirectStandardOutput (Join-Path $Work "ui_regression.out") `
        -RedirectStandardError (Join-Path $Work "ui_regression.err") -PassThru
    $proc.WaitForExit(300000) | Out-Null
    if (-not $proc.HasExited) { $proc.Kill(); "TIMEOUT"; $global:LASTEXITCODE = 1; return }

    # UiTextProbe가 로그에 통과/실패를 남긴다. 종료 코드만으로는 단정 실패를 알 수 없다.
    $log = Get-ChildItem (Join-Path $exeDir "Log\Editor_*.html") |
           Sort-Object LastWriteTime -Descending | Select-Object -First 1
    $text = (Get-Content $log.FullName -Raw) -replace '<[^>]+>', ''
    $passes = ([regex]::Matches($text, 'UiTextProbe\] 전체 통과')).Count
    $fails = ([regex]::Matches($text, 'UiTextProbe\] .*실패')).Count
    $crash = $text -match '미처리 예외'

    "통과 $passes 회 · 실패 $fails 회 · 종료 코드 $($proc.ExitCode)"
    if ($crash) { "크래시가 기록됐다" }
    # 종료 코드도 판정에 넣는다 — 프로브가 다 통과하고도 종료 시점 힙 손상
    # (0xC0000374)으로 죽은 실행이 '전체 통과'로 지나간 적이 있다. 간헐 크래시는
    # 검사를 흔들리게 만들지만, 숨겨지는 것보다 흔들리는 쪽이 낫다.
    $exitOk = ($proc.ExitCode -eq 0)
    if (-not $exitOk) { ("종료 코드 비정상: 0x{0:X8}" -f $proc.ExitCode) }
    $global:LASTEXITCODE = if ($passes -ge 4 -and $fails -eq 0 -and -not $crash -and $exitOk) { 0 } else { 1 }
}

# "저작 배치 재현"(verify-authored-rects)은 2026-08-21 은퇴했다. 그 검사는 저작
# 프리팹 파일에 남아 있던 옛 m_worldRect를 정답지로 삼았는데, 7-2에서 그 키를
# 직렬화에서 뺀 뒤로 **자산을 다시 저장할 때마다 정답지가 소멸하는** 한시적 자였다
# (스크립트 자신의 주석이 그렇게 적고 있었다). 저작 자산 폐기(§0.05)로 정답지가
# 통째로 사라져 은퇴가 확정됐다.
#
# 재던 축은 아래 "UI 레이아웃 골든"이 승계한다 — 앵커 9종과 3단 중첩(부모 rect
# 전파)을 CLI 저작본으로 잰다. 잃은 것은 **규모**뿐이다(153 rect짜리 실제 UI 트리).
# 잃은 것이 하나 더 있다: 원본의 정답지는 에디터가 실제로 배치한 **외부** 값이라
# "계산이 옳다"를 쟀고, 골든은 자기가 계산한 값을 굳히므로 "계산이 바뀌지 않았다"만
# 잡는다. 골든을 뜰 때 눈으로 검산한 것이 그 자리를 일부 메운다.

Run-Step "해상도 스위프" {
    & pwsh -NoProfile -File (Join-Path $PSScriptRoot "verify-resolution-sweep.ps1") -Exe $Exe -Work $Work
}

Run-Step "종료 순서" {
    & pwsh -NoProfile -File (Join-Path $PSScriptRoot "verify-shutdown-order.ps1") -Exe $Exe -Work $Work
}

Run-Step "크래시 덤프 경로" {
    & pwsh -NoProfile -File (Join-Path $PSScriptRoot "verify-crash-dump.ps1") -Exe $Exe -Work $Work
}

# 행동 트리 동작(PHASE 9-8).
#
# 이 세트의 나머지 검사는 BT를 전혀 실행하지 않는다 — BT 컴포넌트는 프리팹에만
# 붙어 있고 다른 시나리오가 여는 씬에는 없다. 그래서 이 항목이 없으면 세트 전체가
# 통과해도 BT 코드는 한 줄도 돌지 않은 채 통과한다. "안 깨졌다"와 "동작한다"를
# 가르는 자리다.
Run-Step "행동 트리 동작" {
    & pwsh -NoProfile -File (Join-Path $PSScriptRoot "verify-bt-smoke.ps1") -Exe $Exe -Work $Work
}

# 스크립트 부착 이중 초기화(트랙 C · C2-2).
#
# 인스펙터(ImGui)의 AttachManagedScript와 콘솔의 script.add가 예전에 같은 결함을
# 가졌다 — AddComponentAllowMultiple 직후 OnInitialized()를 수동으로 불러
# PendingAwake 큐의 자동 드레인과 겹치며 이중 호출이 났다. 인스펙터는 헤드리스로
# 몰 수 없지만 script.add는 몰 수 있어 이 항목으로 자동 회귀에 넣는다.
Run-Step "스크립트 부착 초기화 1회" {
    & pwsh -NoProfile -File (Join-Path $PSScriptRoot "verify-script-add-awake-once.ps1") -Exe $Exe -Work $Work
}

# 프리팹 왕복(트랙 P, P2에서 완료).
#
# 다른 검사는 전부 한 번 띄운 상태만 본다. 저장했다 다시 여는 왕복이 없어서
# "인스턴스가 프리팹과의 연결을 잃는다"는 회귀가 통째로 사각지대였다.
#
# P0~P1 시점에는 왕복 후 등록 복원이 미구현이라(P-a) 그 항목만 예상된 실패로
# 구분해 부분 통과로 끝났다. P2가 EntityHandle 기반 재연결을 배선하면서 등록
# 복원도 다른 항목과 같은 기본 판정이 됐다 — 이제 완전 통과를 요구한다.
Run-Step "프리팹 왕복" {
    & pwsh -NoProfile -File (Join-Path $PSScriptRoot "verify-prefab-roundtrip.ps1") -Exe $Exe -Work $Work
}

# U7/E7-c: UI Navigation을 전역 instanceID에서 source-relative hierarchy route로
# 바꾼 수직 회귀. 같은 프리팹 2개 배치 격리, UI ID 재발급, 공간 컴포넌트 조합,
# 구 navObject 인메모리 승격을 저작 자산 없이 한 명령에서 검증한다.
Run-Step "UI Navigation 프리팹 로컬 참조" {
    & pwsh -NoProfile -File (Join-Path $PSScriptRoot "verify-ui-navigation-local.ps1") -Exe $Exe -Work $Work
}

# 중첩 프리팹 정체성·등록(트랙 P · P4-a, 0단계 게이트).
#
# 위의 "프리팹 왕복"이 쓰는 BTProbe.prefab은 자식이 하나도 없다 — 그래서 여러
# 노드나 중첩 프리팹(자식이 다른 프리팹의 인스턴스인 경우)은 이 세트가 지금까지
# 단 한 번도 태운 적이 없었다. Prefab::InstantiateRecursive가 재귀 프레임마다
# 무조건 바깥 프리팹의 guid로 자식을 덮어써 중첩 정체성을 파괴하는 결함과,
# 중첩 루트가 RegisterInstance에 잡히지 않는 결함이 그 사각지대에서 살아남았다
# — 이 검사가 그 둘을 전용 자산(NestedProbeParent/NestedProbeLeaf)으로 잡는다.
Run-Step "중첩 프리팹 정체성·등록" {
    & pwsh -NoProfile -File (Join-Path $PSScriptRoot "verify-prefab-nested.ps1") -Exe $Exe -Work $Work
}

# 광원 슬롯 복원(트랙 S — Scene 부기 자료구조). 저작 자산 폐기(§0.05) 과정에서
# 생명주기 게이트를 CLI 저작본으로 옮기다 드러난 결함의 자다. m_lightIndex가
# 직렬화되므로 로드된 LightComponent는 Scene::GetLight로 슬롯을 집는데, 그 범위
# 조건이 off-by-one이라 **라이트가 둘 이상인 씬을 열면 죽었다**(0xC0000409).
# 하나면 살아나 오래 숨어 있었다 — 그래서 이 게이트는 라이트를 셋 둔다.
Run-Step "광원 슬롯 복원" {
    & pwsh -NoProfile -File (Join-Path $PSScriptRoot "verify-light-slot-restore.ps1") -Exe $Exe -Work $Work
}

# DDOL 씬 이송 — 캔버스 캐시 재등록(트랙 E · E5-R2 후속).
#
# DontDestroyOnLoad 이송 경로는 SceneManager의 씬 로드 안에서만 불려, 이 세트가
# 지금까지 단 한 번도 태운 적이 없었다. 그 사각지대에서 결함이 살아남았다 —
# Canvas의 캐시 등록이 OnDeserialized에만 있어 이송된 캔버스가 새 씬의 목록에
# 들어가지 않았다. 오브젝트는 살아 넘어오므로 다른 신호는 멀쩡해 보이고
# (ui.status의 "Image 1/1 연결"), 캔버스 목록만 비어 UI 내비게이션이 조용히 죽는다.
Run-Step "DDOL 캔버스 재등록" {
    & pwsh -NoProfile -File (Join-Path $PSScriptRoot "verify-ddol-canvas.ps1") -Exe $Exe -Work $Work
}

# DDOL 이송 신호가 C#까지 닿는가(트랙 L · L3 잔여).
#
# 관리 측 생명주기는 드라이버가 둘이라(네이티브는 생성·파괴만, 나머지는
# BehaviourRegistry의 자체 큐) 네이티브에서만 일어나는 이송이 스크립트에 닿지
# 않았다. 이 검사는 그 통지가 실제로 도착하는지와, 그 과정에서 관리 핸들이
# 죽지 않는지를 함께 본다 — 후자는 ScriptObjectRegistry::Clear가 살아남는 DDOL
# 오브젝트의 핸들까지 지우던 결함이다(스크립트가 자기 GameObject를 잃는다).
Run-Step "DDOL 스크립트 이송 통지" {
    & pwsh -NoProfile -File (Join-Path $PSScriptRoot "verify-ddol-script.ps1") -Exe $Exe -Work $Work
}

Run-Step "AI 레지스트리 DDOL 재등록" {
    & pwsh -NoProfile -File (Join-Path $PSScriptRoot "verify-ai-registry.ps1") -Exe $Exe -Work $Work
}

# Entity 단독 소유권(트랙 E · E5-d). 동적 게이트들이 파괴/DDOL 이송을 검증하는
# 동안, 이 정적 게이트는 저장소가 다시 shared_ptr로 돌아가거나 공개 소스에
# shared_ptr<Entity>가 재유입되는 것을 막는다.
Run-Step "Entity unique_ptr 단독 소유" {
    & pwsh -NoProfile -File (Join-Path $PSScriptRoot "verify-entity-ownership.ps1")
}

# E2: runtime은 model cache payload와 terrain 목적지만 요청하고, 실제 source
# asset 쓰기와 embedded image encoding은 Editor adapter가 소유해야 한다.
Run-Step "Editor asset-authoring 소유권" {
    & pwsh -NoProfile -File `
        (Join-Path $PSScriptRoot "verify-asset-authoring-ownership.ps1") -EditorExe $Exe
}

# H3: 런타임의 Store불일치=0만으로는 Entity 계층 필드가 되살아나거나 직렬화가
# Store 밖으로 새는 회귀를 구분할 수 없다. 필드 부재와 read/write/save/load DTO
# 경계를 별도 정적 게이트로 봉인한다.
Run-Step "HierarchyStore 읽기 경계" {
    & pwsh -NoProfile -File (Join-Path $PSScriptRoot "verify-hierarchy-read-boundary.ps1")
}

# Scene 구 GameObject API 제거 및 Entity 직렬화 키 전환. 새 파일은 m_Entities로
# 쓰되, 기존 .creator의 m_SceneObjects는 읽기 별칭으로 유지해야 한다.
Run-Step "Scene Entity API·구 YAML 호환" {
    & pwsh -NoProfile -File (Join-Path $PSScriptRoot "verify-scene-entity-api.ps1") -Exe $Exe -Work $Work
}

# 프리팹 인스턴스 복제(트랙 P). 에디터 Ctrl+D와 같은 원시 함수(Object::Instantiate)를
# CLI object.duplicate로 태워, 복제본이 PrefabUtility 등록부에 이어지고 이후 프리팹
# 갱신을 받는지 본다. 복제본은 m_prefabFileGuid를 물려받아 "인스턴스처럼 보이는데"
# 등록이 없으면 갱신에서 조용히 빠진다.
Run-Step "프리팹 인스턴스 복제" {
    & pwsh -NoProfile -File (Join-Path $PSScriptRoot "verify-prefab-duplicate.ps1") -Exe $Exe -Work $Work
}

# 프리팹 오버라이드 기록(트랙 P — P-write). 인스턴스의 로컬 수정이 저작 시점에
# 기록되고, 프리팹 갱신이 그것을 존중하는지 값 단위로 본다. 기존 프리팹 검사들이
# 개수·정체성만 보고 값은 안 보던 사각지대다. object.property(리플렉션 경유)와
# object.transform(세터 직접 호출) 두 축을 모두 태운다.
Run-Step "프리팹 오버라이드 기록" {
    & pwsh -NoProfile -File (Join-Path $PSScriptRoot "verify-prefab-override-write.ps1") -Exe $Exe -Work $Work
}

# UI 레이아웃 골든(PHASE 7 승계 — verify-authored-rects의 후계). 앵커 프리셋 8종과
# 3단 중첩을 CLI로 저작해 형상을 통째로 고정한다. 원본은 저작 프리팹의 m_worldRect를
# 정답지로 썼는데 그 키가 직렬화에서 빠져(7-2) 소멸 예정이라, 이전이 아니라 신설이다.
# 골든이 없으면 건너뛴다(reflect_golden과 같은 관례).
if (Test-Path (Join-Path $PSScriptRoot "ui_layout_golden.expected")) {
    Run-Step "UI 레이아웃 골든" {
        & pwsh -NoProfile -File (Join-Path $PSScriptRoot "verify-ui-layout-golden.ps1") -Exe $Exe -Work $Work
    }
} else {
    "=== UI 레이아웃 골든 === 건너뜀 (골든 없음 — verify-ui-layout-golden.ps1 -Baseline)"
}

# 중첩 프리팹 정의 전파(트랙 P — P4-b). 중첩 인스턴스를 펼친 스냅샷이 아니라
# 참조 노드로 굽는지, 그래서 중첩 프리팹의 정의 변경이 상위 프리팹의 새 인스턴스에
# 전달되는지를 본다. 동시에 그 인스턴스의 로컬 오버라이드가 살아남는지도 함께
# 판정한다 — 정의 전파만 되고 오버라이드가 유실되면 절반만 한 것이다.
#
# 2026-08-20까지 이 게이트는 **의도적 RED로 스위트 밖**에 있었다(P4-b 미착지).
# 착지와 함께 편입한다 — 그 RED->GREEN 전환이 증명이다.
Run-Step "중첩 프리팹 정의 전파" {
    & pwsh -NoProfile -File (Join-Path $PSScriptRoot "verify-prefab-nested-update.ps1") -Exe $Exe -Work $Work
}

# 계층 표기 불변식(트랙 E — 루트 규약 통일). "최상위"를 0과 -1 두 값으로 적던
# 시절의 어긋난 쌍("부모가 없다면서 루트 children에 실려 있다")이 순회가 서브트리를
# 통째로 빠뜨리는 결함의 뿌리였다. 표기를 하나로 모은 뒤 그 상태를 고정한다.
Run-Step "계층 표기 불변식" {
    & pwsh -NoProfile -File (Join-Path $PSScriptRoot "verify-hierarchy-convention.ps1") -Exe $Exe -Work $Work
}

# 트랜스폼 값 왕복(트랙 S — S1-b 선행 게이트). 프리팹 왕복이 개수만 보고
# 골든이 기본 생성 타입만 보는 사각지대를 메운다 — 저작 씬의 위치·회전·크기가
# 저장·재로드를 실제로 건너는지 값 단위로 대조하는 유일한 검사다.
Run-Step "트랜스폼 값 왕복" {
    & pwsh -NoProfile -File (Join-Path $PSScriptRoot "verify-transform-roundtrip.ps1") -Exe $Exe -Work $Work
}

# 리플렉션 골든 대조(PHASE 18 CT0)는 골든 파일이 있을 때만 돈다.
# 골든을 뜨려면 컴파일타임 전환 착수 전에 한 번:
#   .\verify-reflection-golden.ps1 -Baseline
# 이 diff 0이 CT4~CT5(메타 전환) 구간의 "출력 동등" 증명이다 — 생명주기
# 기준선이 PHASE 9에 대해 했던 역할을 리플렉션에 대해 한다.
if (Test-Path (Join-Path $PSScriptRoot "reflect_golden.yaml")) {
    Run-Step "리플렉션 골든" {
        & pwsh -NoProfile -File (Join-Path $PSScriptRoot "verify-reflection-golden.ps1") -Exe $Exe -Work $Work
    }
} else {
    "=== 리플렉션 골든 === 건너뜀 (골든 없음 — verify-reflection-golden.ps1 -Baseline)"
    ""
}

# 생명주기 순서 대조(PHASE 9-0)는 기준선 파일이 있을 때만 돈다.
# 기준선을 뜨려면 PHASE 9 교체 전에 한 번:
#   .\verify-lifecycle-baseline.ps1 -Baseline
# 기준선이 없는데 실패로 처리하면, 이 항목을 아직 시작하지 않은 사람에게
# 회귀 세트가 통째로 빨갛게 보인다 — 그러면 세트 전체가 무시되기 시작한다.
if (Test-Path (Join-Path $PSScriptRoot "lifecycle_baseline.tsv")) {
    Run-Step "생명주기 순서" {
        & pwsh -NoProfile -File (Join-Path $PSScriptRoot "verify-lifecycle-baseline.ps1") -Exe $Exe -Work $Work
    }
} else {
    "=== 생명주기 순서 === 건너뜀 (기준선 없음 — verify-lifecycle-baseline.ps1 -Baseline)"
    ""
}

if ($failed.Count -gt 0) {
    "실패한 검사: " + ($failed -join ', ')
    exit 1
}
"회귀 세트 전체 통과"
exit 0
