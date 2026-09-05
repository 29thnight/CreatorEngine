# UI 회귀 세트 전체 실행.
#
# 각 검사는 종료 코드로 판정한다. 하나라도 실패하면 이 스크립트도 실패로 끝난다.
#
#   ui_regression.txt         비정상 순서로 UI를 만들고 재생/정지를 반복한다(PHASE 6).
#                             에디터에서 정상 순서로 만들면 드러나지 않는 크래시를 잡는다.
#   verify-ui-layout-golden   앵커 9종·3단 중첩의 레이아웃 형상을 통째로 고정(7-2·7-4).
#   verify-resolution-sweep   해상도를 바꿔 가며 캔버스·자식·클릭 판정이 따라오는지(7-1·7-3·7-6).
param(
    [string]$Exe = (Join-Path $PSScriptRoot "..\..\Bin\x64-Debug\Editor\CreatorEditor.exe"),
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

# PHASE 15 트랙 H — 캐시 해시/문자열 불변식, 부분 string_view 길이,
# 해시 컨테이너 키 계약을 Debug/Release 독립 프로브로 고정한다. 인스펙터 경로는
# mutable data()를 다시 열지 않는지도 정적 래칫으로 함께 본다.
#
# ── 이 스텝은 12일간 스크립트 없이 서 있었다(2026-09-05 확인) ──
#
# 733b0008이 Run-Step만 넣고 verify-hashing-string.ps1은 커밋하지 않았다. 저장소
# 어느 ref에도 그 파일의 이력이 없다 — 계획만 있고 구현이 없던 자리다. 그 사이
# 세트는 매 실행 "스크립트 파일 이름으로 인식되지 않습니다"로 이 칸이 빨갰다.
#
# ── 어느 바이너리를 재는가 ──
#
# 아무것도 재지 않는다. 게이트가 제품 헤더를 그 자리에서 Debug/Release로 각각
# 컴파일하므로 Bin\ 산출물과 무관하다 — 그래서 -Exe를 넘기지 않는다.
#
# ── 이빨 확인(2026-09-05) ──
#
# 여섯 변이가 각각 정확한 자리에서 빨개졌다. ① string_view 대입의 해시 갱신
# 삭제 → 그 경로 8건만("대입(string_view)"·"부분 뷰 대입"), ② string_view
# 생성자가 .data()를 받게 → 부분 뷰 축 10건만, ③ std::hash 특수화가 다른 값을
# 내게 → 특수화 단정만 전 경로, ④ ==를 해시 전용으로 되돌림 → 정적 래칫,
# ⑤ data()의 char* 재개방 → 정적 래칫(고치기 전 HEAD가 정확히 이 상태였다),
# ⑥ 인스펙터 브랜치 원복 → 정적 래칫. 여섯 다 되돌려 Debug/Release 초록 재확인.
#
# H5(빈 문자열 정책) 착지분에 세 개를 더했다(같은 날). ⑦ 기본 생성자를
# `= default`로 복귀 → 정적 래칫, ⑧ 래칫을 우회해 기본 생성자가 0을 캐시하게
# → 실행 축 8건("기본 생성"·빈 상태 수렴), ⑨ 빈 문자열 금지를 어설션 아닌
# 형태로 되살림 → Debug 실행이 죽어 빨강(정책을 주석이 아니라 실행이 지킨다),
# ⑩ Scene.h의 `m_sceneName = *.data()` 복귀 → 정적 래칫.
Run-Step "HashingString 계약" {
    & pwsh -NoProfile -File (Join-Path $PSScriptRoot "verify-hashing-string.ps1")
}

# 네이티브 ↔ 관리 미러 대조(9-4). exe가 필요 없는 정적 검사라 앞쪽에 둔다.
#
# ── 왜 세트 안에 있어야 하는가 ──
#
# 두 검사는 수동 실행 전제로 ScriptCore\ 밑에 있었고, 세트 밖이라 2026-08-24의
# ScriptBinder → Engine\SceneRuntime 개명(E7-e)에 경로가 낡은 채로 남았다.
# 그날부터 실행하면 첫 Get-Content에서 예외로 죽었는데, 아무도 부르지 않으니
# 빨개질 곳이 없어 12일간 드러나지 않았다. 그 사이 표가 맞았던 것은 가드가
# 지켜서가 아니라 관리 측을 아무도 건드리지 않아서다. 세트에 태우는 것이
# 경로 파손 자체를 잡는 유일한 방법이다(경로가 없으면 종료 코드 1이다).
#
# ── 무엇을 잡는가 ──
#
# 버전·구조체 크기 검사는 순서가 뒤바뀐 것을 잡지 못한다. 실제로 필드 하나가
# 표 끝으로 밀렸는데 개수와 크기가 그대로라 통과했고, 관리 코드가 엉뚱한 함수
# 포인터를 불러 접근 위반으로 죽었다. 열거 5종은 값이 int로 경계를 넘으므로
# 어긋나도 컴파일과 링크가 통과하고 런타임에만 다른 판정이 난다.
#
# ── 이빨 확인(2026-09-04) ──
#
# Native.cs의 인접한 두 필드를 순서만 맞바꾸니 개수는 182/182로 같은 채
# [91] 첫 불일치로 빨개졌고, BTEnum.h의 ParallelPolicy 항목을 뒤바꾸니
# ParallelPolicy [0]에서 빨개졌다. 둘 다 되돌려 초록을 다시 확인했다.
Run-Step "스크립트 API 표 순서" {
    & pwsh -NoProfile -File (Join-Path $PSScriptRoot "..\..\ScriptCore\check-api-table.ps1")
}

Run-Step "BT 열거 미러 대조" {
    & pwsh -NoProfile -File (Join-Path $PSScriptRoot "..\..\ScriptCore\check-bt-enums.ps1")
}

# 생명주기 6단계 미러(9-5). 위 미러 셋과 같은 종류인데 여기만 비어 있었다 —
# ScriptLifecyclePhase.h가 "값이 같아야 한다, 컴파일러가 잡아 주지 않는다"고
# 스스로 적어 두고 그것을 재는 검사가 없었다.
#
# 값 축만 보면 얕아서 셋으로 세웠다. ① 두 열거의 자리별 이름·값, ② 열거 순서가
# Component.h의 6단계 virtual 선언 순서와 같은가(양쪽을 함께 틀리게 고치면 ①이
# 초록이다), ③ ScriptComponent가 6단계를 모두 override하고 각각 자기 단계를
# 넘기는가. ③이 가장 조용한 결함을 막는다 — MaskOfType은
# `&T::Hook != &Component::Hook`으로 비트를 세우므로 override를 빠뜨리면 비트가
# 안 서고 그 단계가 아무 소리 없이 사라진다.
#
# 이빨 확인(2026-09-05): 변이 넷을 각각 심어 자기 축만 붉는 것을 봤다.
# ① 관리 열거의 값 3↔4 교환 → 판정 1 [3]. ② 양쪽 열거를 함께 2↔3 재배열 →
# 판정 1은 초록이고 판정 2 [2]만 빨강. ③ ScriptComponent.h의 override 한 줄 제거,
# ④ OnAddedToScene이 OnBeginSimulation을 넘기게 변경 → 둘 다 판정 3만 빨강.
Run-Step "생명주기 단계 미러 대조" {
    & pwsh -NoProfile -File (Join-Path $PSScriptRoot "..\..\ScriptCore\check-lifecycle-enums.ps1")
}

# 엔진 API 진입 검사 전수 대조(9-5 · LC5-c). 게임 스레드 검사는 Native의 정적
# 메서드 206곳에 하나씩 들어간다. 그 규약을 사람이 지키게 두면 곧 드리프트한다 —
# 새 API를 더하면서 검사를 빠뜨려도 컴파일은 되고, 실행 게이트
# (verify-lifecycle-thread)는 자기가 부르는 두 API만 보므로 나머지가 새는 것을
# 못 본다. 그래서 "빠진 곳이 있는가"는 실행이 아니라 소스에서 센다.
#
#   1 전수 배선  _api 를 만지는 메서드는 전부 Entered() 를 거친다
#   2 우회 없음  _bound 원본은 허용된 자리에서만 읽힌다
#   3 검사 실질  Entered 가 실제로 스레드를 비교하고 경계 밖을 보고한다
#
# 이빨 확인(2026-09-05): 변이 셋이 각각 자기 축만 붉혔다. ① 한 메서드에서
# Entered() 제거 → 판정 1. ② Entered()를 부르되 결과를 버리고 _bound 로 우회 →
# 판정 1은 초록이고 판정 2만 빨강(이것이 판정 2의 존재 이유다). ③ Entered 안의
# 스레드 비교 삭제 → 판정 3만 빨강.
Run-Step "엔진 API 진입 검사 대조" {
    & pwsh -NoProfile -File (Join-Path $PSScriptRoot "..\..\ScriptCore\check-native-thread-guard.ps1")
}

# 진입점 이름 결합(9-4). 위 두 검사가 못 보는 세 번째 결합면이다.
#
# ClrHost는 관리 함수를 문자열 이름으로 찾는다. API 표 검사는 함수 포인터의 배치를
# 볼 뿐 이 결합은 보지 않으므로, C# 쪽 이름만 바뀌면 컴파일·링크·표 검사가 모두
# 통과하고 런타임에 "CLR 초기화 실패"로만 드러난다.
#
# 이빨 확인(2026-09-04): Bootstrap.cs의 CreateComponent 하나를 CreateComponentX로
# 바꾸니 그 이름을 지목하며 빨개졌고, 되돌리니 초록. 덤으로 같은 날 실수로 Bootstrap.cs를
# 옛 이름 상태로 되돌렸을 때도 이 검사가 먼저 잡았다.
Run-Step "스크립트 진입점 결합" {
    & pwsh -NoProfile -File (Join-Path $PSScriptRoot "..\..\ScriptCore\check-entry-points.ps1")
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
    $log = Get-ChildItem (Join-Path $exeDir "Saved\Log\Editor_*.html") |
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

# 생명주기의 6단계가 아니라 그 둘레의 두 축을 잰다(9-5) — 훅이 **어느 모드에서**
# 왔는가(편집/재생)와 그때 **틱이 함께 돌았는가**. 위 항목들은 훅의 순서와 횟수를
# 보므로 이 둘에 원리적으로 눈이 멀다: 편집 모드에서 6단계가 모두 소진돼도
# "순서 맞음·1회씩"이라 전부 초록이었다.
#
# 판정 여덟이 각각 결함 하나씩을 지킨다. 특히 판정 6은 관리 → 네이티브 방향
# 다리가 죽었는지를 **폴백 경고 건수**로 잰다 — 폴백이 훅도 틱도 똑같이 만들어
# 결과만 보면 판별력이 0이기 때문이다.
Run-Step "생명주기 활성·모드 축" {
    & pwsh -NoProfile -File (Join-Path $PSScriptRoot "verify-lifecycle-axis.ps1") -Exe $Exe -Work $Work
}

# 생명주기 실패 경로(9-5). 위 항목들과 두 골든은 전부 **정상 경로**를 굳힌다 —
# 훅이 던졌을 때 무슨 일이 벌어지는지는 그 어디에도 한 줄도 없었다.
#
# 픽스처 일곱이 실패를 만들고 판정 여섯이 그 뒤를 본다:
#   A·B·C 실패한 훅이 뒤 단계를 끌고 가지 않는가(LC1 — 성공한 훅의 짝만 부른다)
#   E     정리 콜백 예외가 종료 절차를 끊지 않고, 원인이 보고되는가(LC2)
#   F     동기 throw·즉시 faulted·나중 faulted가 같은 정책을 따르는가(LC5)
#   D     대조군 — 이웃의 예외가 정상 인스턴스를 끊지 않는가
#
# D가 있어야 "픽스처가 조용하다"를 고침이 들었다고 읽을지 하네스가 죽었다고
# 읽을지 가를 수 있다. F는 일관성만이 아니라 **방향**(격리)까지 못 박는다 —
# 일관성만 재면 정책이 반대로 통일돼도 초록이다.
Run-Step "생명주기 실패 경로" {
    & pwsh -NoProfile -File (Join-Path $PSScriptRoot "verify-lifecycle-failure.ps1") -Exe $Exe -Work $Work
}

# 생명주기 스레드 경계(9-5 · LC5-b). 위 항목들이 전부 "무엇이 언제 오는가"를
# 재는 데 반해 이것은 "그것이 **어느 스레드**에서 도는가"를 잰다.
#
# 관리 훅은 전부 게임 스레드 전용인데(ClrHost.h 규약) await 한 줄로 그 밖에
# 나갈 수 있었다. 나간 뒤의 본문은 여전히 Transform·Entity를 부른다 — 함수
# 포인터로 C++에 곧장 들어가는 것들이다.
#
#   H  외부 await 재개가 게임 스레드로 돌아오는가 (프레임 경계 마셜링)
#   G  지원 경로(Scope.Delay) 재개는 그대로인가   (비회귀)
#   I  워커가 실제로 다른 스레드였는가            (H의 거짓 초록 방지)
#
# I가 없으면 Task.Run이 인라인된 실행에서 H가 고쳐진 것처럼 보인다.
Run-Step "생명주기 스레드 경계" {
    & pwsh -NoProfile -File (Join-Path $PSScriptRoot "verify-lifecycle-thread.ps1") -Exe $Exe -Work $Work
}

# 생명주기 재진입(9-5 · LC4). §2.1의 대역 시험은 완료 continuation이 내부
# Cancel()을 불러 _pending이 비면 다음 인덱스 접근이 터지는 것을 재현했다.
# 그 시험은 내부 메서드를 직접 불렀다 — 저작 표면에서 같은 일이 되는지는 재지
# 않았고, 계획서도 그것을 "강제 재현"이라고 적어 두었다.
#
# 이 항목이 그 간극을 잰다. 창을 여는 조건 둘을 저작 수단만으로 만든다:
# ConfigureAwait(false)로 재개를 Tick 루프 안에 두고(LC5-b의 컨텍스트를 포기하는
# 유일한 길), 대기 둘 중 나중 것을 먼저 기다려 역순 순회에 방문할 항목을 남긴다.
#
#   L 순회 무사고  Tick이 던지지 않았다
#   M 이웃 온전    대조군이 훅을 잃지 않았다
#   N 대기 생존    비활성화 뒤에도 남은 대기가 재개됐다
#
# M이 L의 절반을 진다 — Tick이 던지면 PrePhysicsTick 전체가 중단되어 그 프레임의
# 모든 인스턴스가 훅과 배수를 잃는데, 픽스처 자신은 이미 제거·비활성화 중이라
# 그 손실이 잘 안 보인다.
Run-Step "생명주기 재진입" {
    & pwsh -NoProfile -File (Join-Path $PSScriptRoot "verify-lifecycle-reentrancy.ps1") -Exe $Exe -Work $Work
}

# 생명주기 자기 제거·배수 재진입(9-5 · LC0 잔여). 위 항목들이 실패·스레드·재진입
# 창을 덮는 동안 **자기 제거**는 어느 것도 보지 않았다.
#
# 자리를 셋으로 나눈다 — 부르는 순간 엔진이 무엇을 순회하고 있는지가 다르다.
# ① 시작 훅 안(네이티브 드레인) ② 틱 안(_active 순회) ③ 배수 안.
# ③은 LC5-b가 이번 세션에 새로 만든 실행 지점이다. 새 자리를 만들고 그 위에서
# 무엇이 되는지 재지 않으면 결함이 생겨도 어느 게이트도 보지 않는다.
#
#   Q 축소 한 번   셋 다 end·removing·uninit 을 정확히 한 번씩 (0도 2도 아니다)
#   R 순회 무사고  틱 예외 0 · 경계 밖 거부 0
#   S 배수 예산    배수 안의 재게시가 다음 프레임으로 넘어간다
#   T 대조군 온전  하네스 생존 확인
#
# S는 건수로 재면 안 된다 — 예산이 없어도 열 번은 다 온다. 가르는 것은 몇
# 프레임에 걸쳤는가다. 변이로 확인했다(예산 제거 → 10회가 1프레임에 몰림).
Run-Step "생명주기 자기 제거" {
    & pwsh -NoProfile -File (Join-Path $PSScriptRoot "verify-lifecycle-selfremove.ps1") -Exe $Exe -Work $Work
}

# 생명주기 대기 참조 유지(9-5 · LC3). Delay가 token.Register의 반환 등록을 버려
# 완료된 대기가 취소 토큰 쪽에서 계속 도달 가능했다 — 오래 사는 스코프에서
# 누적된다. 실측 50/50 → 고침 뒤 0/50.
#
# 축을 메모리 총량이 아니라 **도달 가능성**으로 잡았다. GC.GetTotalMemory는 엔진
# 전체의 잡음을 함께 재서 몇십 KB 차이가 묻힌다. 완료된 Task마다 약한 참조를
# 남기고 강한 참조를 버린 뒤 GC를 돌려 몇 개가 살아남는지 세면 잡음이 0이고
# 살아남은 개수가 곧 누수 개수다.
#
#   U 생성 확인  50개를 실제로 만들었다 (0개를 만들면 0개가 살아남는다)
#   V 회수       스코프가 살아 있는 채로 완료 Task가 전부 회수된다
#   W 축소 온전  참조 해제가 종료 절차를 깨뜨리지 않는다
Run-Step "생명주기 대기 참조 유지" {
    & pwsh -NoProfile -File (Join-Path $PSScriptRoot "verify-lifecycle-retention.ps1") -Exe $Exe -Work $Work
}

# 생명주기 재생 세대(9-5 · LC7). 위 생명주기 항목이 전부 재생을 **한 번**만
# 태운다 — 두 번째 재생에서 무엇이 되는지는 어느 게이트도 보지 않았다. 그런데
# 재생·정지 반복은 저작 중 가장 자주 하는 일이고, 정지는 씬을 백업에서 되살리는
# 방식이라 인스턴스가 통째로 새로 만들어진다(설계 문서 §4 트랙 L1).
#
#   X 세대 분리   재생마다 새 인스턴스가 하나씩 서고 세대 구간이 겹치지 않는다
#   Y 세대 완결   각 세대가 전체 생명주기를 정확히 한 번씩 받는다
#   Z 누수 없음   대기가 정지에서 취소되고 다음 세대로 흘러가지 않는다
#
# Z 는 취소 건수와 누수 건수를 **함께** 센다. 변이(정지 축소의 Scope.Cancel 제거)
# 에서 누수는 여전히 0이었다 — 컴포넌트가 목록에서 빠져 재개될 기회 자체가 없기
# 때문이다. "누수 0" 만으로는 아무것도 증명되지 않는다.
Run-Step "생명주기 재생 세대" {
    & pwsh -NoProfile -File (Join-Path $PSScriptRoot "verify-lifecycle-generation.ps1") -Exe $Exe -Work $Work
}

# 생명주기 대기 인자(9-5 · LC7-b). Scope.Delay(seconds)는 저작 표면의 관용구인데
# 인자의 뜻이 적힌 적이 없다. 착수 시 실측에서 둘이 결함이었다.
#
#   · NaN이 조용히 삼켜졌다 — `Remaining > 0f`가 NaN에 거짓이라 0초와 구별되지
#     않는다. 저작자의 계산 실수가 "한 프레임 대기"가 되고 신호가 남지 않는다.
#   · 같은 Delay(0f)가 첫 호출에서 0프레임, 이후엔 1프레임을 썼다. 갈림은 등록이
#     그 프레임의 Scope.Tick보다 앞이냐 뒤냐인데, 그건 네이티브가 훅을 프레임의
#     어느 지점에서 부르는지에 달렸다 — 저작자가 볼 수 없는 우연이다.
#
#   AA 0초 계약 · BB 음수 동치 · CC NaN 거부 · DD 연쇄 진행 · EE 무한 대기
#
# DD가 LC4에서 넘어온 "무한 같은 프레임 재개 방지"의 실측 자리다.
Run-Step "생명주기 대기 인자" {
    & pwsh -NoProfile -File (Join-Path $PSScriptRoot "verify-lifecycle-delayarg.ps1") -Exe $Exe -Work $Work
}

# Light 래퍼(W2, 9-4). 저작 자산에 LightComponent가 30개 있는데 스크립트가
# 만질 길이 없었다 — 그 경계를 열고 이 항목이 실제로 태운다.
#
# 두 축을 함께 본다. ① 경계 왕복은 프로브가 스스로 판정하고, ② dirty 사슬은
# 발행→커밋→큐잉 누계를 잰다. ①만 있으면 눈먼 곳이 생긴다: 렌더는
# LightComponent가 아니라 LightRenderProxy를 읽고 그 복사는 dirty 큐에 실린
# 것만 따라가는데, 값을 넣는 쪽이 dirty를 안 내도 **되읽으면 새 값이 나온다**.
# 그 상태가 지금 리플렉션 인스펙터가 놓인 자리다(ReflectionTypedDraw.h는
# 값을 대입만 하고 dirty를 모른다).
#
# 이빨 확인(2026-09-04): LightComponent의 setter 5종에서
# PublishRenderProxyDirty 6줄을 지우니 ①은 15건 그대로 통과하고 ②만
# publish 증가분 8 → 0으로 붉어졌다. 되돌려 초록을 다시 확인했다.
#
# 못 보는 것도 적어 둔다: 렌더 스레드가 프록시에 실제로 적용하는 마지막
# 한 칸은 --script 헤드리스에서 관측할 수 없다(queued는 늘고 applied는
# 멈춰 있다 — 게이트 파일 머리에 실측치가 있다).
Run-Step "Light 래퍼 경계" {
    & pwsh -NoProfile -File (Join-Path $PSScriptRoot "verify-light-script.ps1") -Exe $Exe -Work $Work
}

# CameraComponent 래퍼(W2, 9-4). 저작 20건인데 스크립트에서 만질 길이 없었다 —
# 있던 것은 핸들을 받지 않는 전역 질의 셋뿐이라 "이 오브젝트의 카메라"라는 축이
# 아예 없었다.
#
# Light와 달리 축이 하나다. 카메라는 렌더 프록시를 쓰지 않고(Scene.cpp의 프록시
# Kind 열거에 Camera가 없다) 매 프레임 CaptureFrameSnapshot으로 읽히므로 발행할
# dirty가 애초에 없다. 대신 프로브가 Camera.Main과의 일치를 함께 단정한다 —
# 컴포넌트 축과 전역 접근점이 같은 카메라를 가리키는지 재는 유일한 자리다.
#
# 이빨 확인(2026-09-04): Api_Camera_SetFov의 대입을 지우니 "fov 왕복" 한 건만
# 붉어졌고, Api_Camera_GetPrimaryHandle을 무효 핸들로 바꾸니 Camera.Main 관련
# 2건이 붉어졌다. 둘 다 되돌려 초록을 다시 확인했다.
Run-Step "Camera 래퍼 경계" {
    & pwsh -NoProfile -File (Join-Path $PSScriptRoot "verify-camera-script.ps1") -Exe $Exe -Work $Work
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
# ScriptRegistry의 자체 큐) 네이티브에서만 일어나는 이송이 스크립트에 닿지
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

# D2-c: material payload와 sidecar의 UUIDv4가 같고, target+meta rename 뒤에도
# catalog 참조가 같은 GUID로 새 경로를 해석하는지 실제 Editor authoring host로 잰다.
Run-Step "Asset GUID 전역 strict 계약" {
    & pwsh -NoProfile -File `
        (Join-Path $PSScriptRoot "verify-asset-guid-contract.ps1") -Strict
}

Run-Step "Asset GUID rename 불변식" {
    & pwsh -NoProfile -File `
        (Join-Path $PSScriptRoot "verify-asset-guid-rename.ps1") -Exe $Exe -Work $Work
}

# D2-c: 실제 experiment 이행 fixture가 catalog GUID로 모델/재질을 되찾고,
# RenderThread의 새 씬 proxy delta를 적용한 뒤 DX12 draw까지 만드는지 확인한다.
Run-Step "Experiment FT_Primitives 실제 draw" {
    & pwsh -NoProfile -File `
        (Join-Path $PSScriptRoot "verify-experiment-ft-primitives.ps1") -Exe $Exe -Work $Work
}

# D2-d: 저장소의 저작 씬 14개를 원본에 쓰지 않고 외부 임시 트리로 두 번
# 저장한다. 첫 저장 결과를 다시 열어 같은 이름으로 재저장했을 때 byte hash가
# 같아야 하고, 실행 전후 원본 hash도 같아야 한다.
Run-Step "Scene authoring 전수 왕복" {
    & pwsh -NoProfile -File `
        (Join-Path $PSScriptRoot "verify-scene-authoring-corpus.ps1") -Exe $Exe -Work $Work
}

# D2-d: 현재 Prefabs 디렉터리의 9개를 모두 소환하고 외부 임시 씬 저장·재로드
# 전후 identity/override/등록 multiset을 비교한다. prefab 원본은 읽기 전용이다.
Run-Step "Prefab authoring 전수 왕복" {
    & pwsh -NoProfile -File `
        (Join-Path $PSScriptRoot "verify-prefab-authoring-corpus.ps1") -Exe $Exe -Work $Work
}

# D2-d: standalone material 2개의 sidecar identity, ShaderMeta/texture GUID와
# canonical payload가 메모리 왕복에서 보존되는지 확인한다. UUID version 검사는
# 위의 전역 strict 계약이 맡고 이 항목에서는 중복 판정하지 않는다.
Run-Step "Material authoring 전수 왕복" {
    & pwsh -NoProfile -File `
        (Join-Path $PSScriptRoot "verify-material-authoring-corpus.ps1") -Exe $Exe -Work $Work
}

# D5-a: source preview의 nil identity/fallback path가 cooked artifact로 조용히
# 게시되지 않는지, 그리고 명시적인 model/material/ShaderMeta/texture resolver가
# 실자산 import → checked writer → reader 왕복에서 모두 보존되는지 확인한다.
Run-Step "Experiment cooked identity 게시 계약" {
    & pwsh -NoProfile -File `
        (Join-Path $PSScriptRoot "verify-experiment-cooked-identities.ps1") -Exe $Exe -Work $Work
}

# D5-b2a: in-memory 계약을 별도 production tool까지 잇는다. 실제 model/.meta와
# ShaderMeta sidecar를 읽어 새 staging tree에 CEMC/CEMF를 쓰고 재검증한 뒤 한 번에
# 게시해야 한다. 두 번의 출력 hash가 같고 실패 요청은 partial tree를 남기지 않는다.
Run-Step "Experiment AssetCooker 실제 산출물 게시" {
    & pwsh -NoProfile -File `
        (Join-Path $PSScriptRoot "verify-experiment-asset-cooker.ps1") -Work $Work
}

# D5-d: 현재 source corpus의 scene/prefab/material을 전부 실제 producer로 굽고,
# authoring과 cooked payload를 같은 ReadNode 구조로 파싱해 값 동등을 단정한다.
# 파일 바이트만 비교하면 parser/decoder가 다른 값을 내도 놓치므로 구조 parity도 본다.
Run-Step "Experiment document 전수 cook parity" {
    & pwsh -NoProfile -File `
        (Join-Path $PSScriptRoot "verify-experiment-document-cook-parity.ps1") -Exe $Exe -Work $Work
}

# D5-b2b1: tracked model 전부와 현재 checkout의 선택적 local model이 strict
# subasset UUIDv4 sidecar를 가지며,
# 두 번의 전수 Cook이 같은 14 CEMC + CEMF를 만들고 source를 수정하지 않아야 한다.
# UUID 재발급은 복제 fixture에서 명시적 migration 모드로만 검증한다.
Run-Step "Experiment model 전수 identity/cook" {
    & pwsh -NoProfile -File `
        (Join-Path $PSScriptRoot "verify-experiment-model-cook-all.ps1") -Work $Work
}

# cooked catalog 제품 소비(D5-c/D5-d). 실제 패키징과 같은 producer closure를
# 굽고, 마운트한 catalog로 모델·texture·scene 문서가 cooked artifact를 타는지
# 잰다. 대조군(마운트 없음)은 model/texture cooked 0, scene authoring이어야 한다.
# I6-C: 렌더 패스가 legacy Mesh 포인터를 신원으로 쓰지 않는가(정적). 이 전환은
# 그림을 바꾸지 않으므로 픽셀 축이 되돌림을 못 잡는다 — 은퇴·전환 슬라이스의
# 게이트는 "소비 0"과 "빌드가 막는다" 쪽이라는 I6 정찰의 규칙이다.
Run-Step "렌더 지오메트리 신원 경계" {
    & pwsh -NoProfile -File `
        (Join-Path $PSScriptRoot "verify-render-geometry-identity.ps1")
}

# I6-B: legacy Skeleton 은퇴 래칫(정적). 은퇴 슬라이스는 자기 A/B 대조군을
# 없애므로(타입이 죽으면 off 팔이 지을 것이 없다) 축은 "소비 0"과 "빌드가
# 막는다"다. 접촉 수가 늘어나는 방향만 막는다.
Run-Step "legacy Skeleton 은퇴 경계" {
    & pwsh -NoProfile -File `
        (Join-Path $PSScriptRoot "verify-legacy-skeleton-retirement.ps1")
}

# PHASE 3.75 MBC0: cutover 변경 동결 래칫(정적). §5.2가 제거 대상으로 적은
# legacy·experiment 표면(역브리지·A/B 스위치·pseudo-v5·Assimp·무조건 진단)의 코드
# 접촉이 늘지 않고, model sidecar writer가 허용목록(현존 둘) 밖에 생기지 않는다.
Run-Step "MBC cutover 변경 동결" {
    & pwsh -NoProfile -File `
        (Join-Path $PSScriptRoot "verify-mbc-cutover-freeze.ps1")
}

# PHASE 3.75 MBC1: 자산 신원 프로필 ce.uuidv8.sha256.v1. C++(제품)·Python(생성기)·
# .NET(이 게이트) 세 독립 유도가 벡터 15건에서 같고, selftest가 FIPS KAT·BCrypt
# 대조·fail-closed·registry 네 판정을 통과한다.
Run-Step "자산 신원 프로필 UUIDv8" {
    & pwsh -NoProfile -File `
        (Join-Path $PSScriptRoot "verify-asset-identity.ps1") -Exe $Exe -Work $Work
}

# PHASE 3.75 MBC2: epoch header·stable key 규칙 엔진·sidecar schema v2. 합성 단정 위에
# 실자산 corpus 14를 임포트해 폐포를 재유도하고 전 모델을 한 registry에 넣는다.
# 디스크에 쓰지 않는다(원본 해시 전후 동일) — 쓰기는 MBC3 transaction의 몫.
Run-Step "model sidecar schema v2" {
    & pwsh -NoProfile -File `
        (Join-Path $PSScriptRoot "verify-asset-sidecar-v2.ps1") -Exe $Exe -Work $Work
}

# PHASE 3.75 MBC3~MBC5: 원자 authoring transaction(실패 주입 5단계·collision 거부),
# 전 corpus UUIDv8 cutover(rollback·참조 rewrite·old 참조 0), runtime
# ModelAssetGeneration closure/cache(변조 4종 게시 전 거부·generation 1→2 교체).
# 이 넷은 2026-09-02 구현 뒤 세트에 배선되지 않은 채였다 — 도는 세트에 없으면
# 없는 것과 같다.
Run-Step "model authoring transaction(MBC3)" {
    & pwsh -NoProfile -File `
        (Join-Path $PSScriptRoot "verify-model-authoring-transaction.ps1") -Work $Work
}
Run-Step "model corpus UUIDv8 closure(MBC4)" {
    & pwsh -NoProfile -File `
        (Join-Path $PSScriptRoot "verify-model-corpus-v8.ps1")
}
Run-Step "model corpus cutover rollback/rewrite(MBC4)" {
    & pwsh -NoProfile -File `
        (Join-Path $PSScriptRoot "verify-model-corpus-v8-cutover.ps1") -Work $Work
}
Run-Step "model asset generation cache(MBC5)" {
    & pwsh -NoProfile -File `
        (Join-Path $PSScriptRoot "verify-model-asset-generation.ps1") -Work $Work
}
# PHASE 3.75 MBC6: RHI/GBuffer/Forward/Shadow의 typed generation 직접 소비와 SU 전체
# mask 조합(실GPU DX12/Vulkan). MBC7: Scene/MeshRenderer가 그 typed 뷰를 실제로
# 채우고 Gunner 콜드 로드가 등록부·순서 없이 closure에서 텍스처를 푼다.
Run-Step "model render wiring(MBC6)" {
    & pwsh -NoProfile -File `
        (Join-Path $PSScriptRoot "verify-model-render-wiring.ps1") -Work $Work
}
Run-Step "scene model consumption + Gunner cold-load(MBC7)" {
    & pwsh -NoProfile -File `
        (Join-Path $PSScriptRoot "verify-model-scene-consumption.ps1") -Work $Work
}
# PHASE 3.75 MBC8: Animator·Foliage·Editor 창구가 typed generation을 스위치와 무관하게
# 소비한다 — A/B 스위치를 끈 프로세스에서 typed 재생 골든·본 해석·마스크·클립 열거·
# Foliage typed 뷰가 전량 generation 축이어야 한다.
Run-Step "typed consumers Animator/Foliage/Editor(MBC8)" {
    & pwsh -NoProfile -File `
        (Join-Path $PSScriptRoot "verify-model-typed-consumers.ps1") -Work $Work
}

# 본 팔레트가 렌더에 도달하는가. X8의 dirty 게이팅에 "팔레트가 바뀌었다"
# 축이 없어 스킨 메시가 첫 포즈에서 굳었다 — 그림을 못 재는 헤드리스에서
# 프록시 커밋 누계로 잰다.
Run-Step "스킨 프록시 갱신" {
    & pwsh -NoProfile -File `
        (Join-Path $PSScriptRoot "verify-skinned-proxy-refresh.ps1") -Work $Work
}

# I6-B4-pre: 그림의 입력을 잰다 — 유한성·본 인덱스 범위·크기 상한·포즈별 digest.
Run-Step "스킨 포즈 무결성" {
    & pwsh -NoProfile -File `
        (Join-Path $PSScriptRoot "verify-skin-pose-integrity.ps1") -Work $Work
}

# I6-B4b 선행: 살아 있는 애니메이터의 그림을 dx12.scene으로 잰다(bind·place·drop
# 세 팔). B4b가 두 번 되돌려진 자리 — 이 축이 없으면 틱 폐기가 화면을 깨도 초록.
Run-Step "라이브 스키닝 시각 축" {
    & pwsh -NoProfile -File `
        (Join-Path $PSScriptRoot "verify-skin-pose-visual.ps1") -Work $Work
}

# I6-B4b 후속: 콘텐츠 브라우저 드롭 경로(LoadCachedModelShared)의 재생
# 바인딩. 라이브 게이트는 model.load 쪽만 태워서 이 경로가 구멍이었고,
# legacy 재귀 틱을 걷자 "드롭한 애니메이션 모델이 안 그려진다"로 나왔다.
# 그림은 위 "라이브 스키닝 시각 축"의 drop 팔이 잰다 — 여기는 바인딩·포즈 표본.
Run-Step "에디터 드롭 재생 바인딩" {
    & pwsh -NoProfile -File `
        (Join-Path $PSScriptRoot "verify-editor-drop-animation.ps1") -Work $Work
}

Run-Step "Experiment cooked catalog 기동" {
    & pwsh -NoProfile -File `
        (Join-Path $PSScriptRoot "verify-experiment-cooked-catalog.ps1") -Exe $Exe -Work $Work
}

# D6: package-time zero assertion과 별도로 현재 Release stage를 다시 실행해
# parser counter 0, runtime module direct ryml reference 0, CEDO corpus와 retired
# JSON 배제를 한 번에 확인한다. 패키지가 아직 없으면 다른 Release 전용 gate처럼
# 명시적으로 건너뛴다.
$currentStagePointer = Join-Path $PSScriptRoot "..\..\Build\Staging\Dynamic_CPP.current.json"
if (Test-Path -LiteralPath $currentStagePointer -PathType Leaf) {
    Run-Step "Player runtime text parser 은퇴(D6)" {
        & pwsh -NoProfile -File `
            (Join-Path $PSScriptRoot "verify-player-runtime-text-parser.ps1")
    }
} else {
    "=== Player runtime text parser 은퇴(D6) === 건너뜀 (Release stage 없음)"
    ""
}

# E2: Editor import 완료 결과는 하나의 RuntimeAssetChange 계약으로만 Core에
# 전달하고, reload는 이전 generation의 raw 참조 수명을 보존해야 한다.
Run-Step "Runtime asset-change 경계" {
    & pwsh -NoProfile -File `
        (Join-Path $PSScriptRoot "verify-asset-runtime-change-boundary.ps1")
}

# E2: picker/icon/font는 Editor presentation이 소유하고, ScriptBinder의 gizmo
# 수집은 Editor singleton 대신 packet 수명에 묶인 명시 입력만 받아야 한다.
Run-Step "Editor asset-presentation 소유권" {
    & pwsh -NoProfile -File `
        (Join-Path $PSScriptRoot "verify-asset-presentation-boundary.ps1")
}

# E3-0: 재생 왕복 구조 대조. E3는 play-mode 소유권을 SceneManager에서 Editor로
# 옮기는데, 그 전까지 이 세트에는 재생 왕복이 씬을 보존하는지 재는 검사가 없었다.
# 옮기기 전에 "지금 동작"을 못 박아야 옮긴 뒤 "동작이 같다"를 주장할 수 있다.
Run-Step "재생 왕복 구조" {
    & pwsh -NoProfile -File `
        (Join-Path $PSScriptRoot "verify-play-roundtrip.ps1") -Exe $Exe
}

# 같은 왕복의 다른 관심사. E3-2+3이 play-mode transaction을 EditorPlayModeController로
# 옮기고 Undo를 SceneManager에서 들어내는데, 그 전까지 세트에는 selection/undo를
# 구동·단정하는 검사가 0건이었다. 계획서 문구는 "selection이 복원된다"지만 코드는
# 해제할 뿐이라, 이 게이트는 실측대로 해제를 단정한다(스크립트 머리말 참고).
Run-Step "재생 선택·Undo" {
    & pwsh -NoProfile -File `
        (Join-Path $PSScriptRoot "verify-play-selection-undo.ps1") -Exe $Exe
}

# E3-2: Undo 폐기가 Editor 소유로 옮겨간 뒤, Player에서 "아무 일도 안 일어남"은
# 런타임으로 재기 어렵다(정상이 곧 무동작이다). 정적으로 못 박는다. 선택 해제가
# 파괴 이전에 남아 있는지도 함께 본다 — 그 순서가 댕글링 방지 안전 속성이다.
Run-Step "재생 정책 경계" {
    & pwsh -NoProfile -File (Join-Path $PSScriptRoot "verify-play-mode-policy-boundary.ps1")
}

# E3-7: 재생 중 시뮬레이션 순서를 Runtime 하나가 소유하는지. 이관 전에는 두 Host가
# 글자 그대로 같은 순서를 각자 들고 있었고, 복제된 순서는 한쪽만 고치면 조용히
# 갈라진다("에디터에서는 되는데 빌드하면 안 된다"). 두 Host를 같은 시나리오로
# 나란히 태우는 하네스가 없어 런타임으로는 못 잡으므로 소스에서 못 박는다.
Run-Step "프레임 오케스트레이션" {
    & pwsh -NoProfile -File (Join-Path $PSScriptRoot "verify-frame-orchestration.ps1")
}

# E3-4: 프리팹 편집 모드는 저작 도구라 Editor 소유다. Player에서 "없다"는 관측할
# 것이 없는 성질이고, 링커가 이미 참조 없는 코드를 버려서 바이너리로도 이관 전후를
# 구분할 수 없다 — 바뀐 것은 컴파일 대상과 층 경계다. 그래서 정적으로 본다.
Run-Step "PrefabEditor 소유권" {
    & pwsh -NoProfile -File (Join-Path $PSScriptRoot "verify-prefab-editor-ownership.ps1")
}

# E4: 렌더 패스가 **어느 뷰에 조립되는가**를 못 박는다. 패스 내부 렌더링은
# dx12.*/vk.* 자가 검사가 리드백으로 픽셀까지 재지만 그것은 격리된 합성 씬이라
# 조립 결과는 안 본다. 착수 실측: Editor와 Player 파이프라인이 완전히 동일하고
# Grid·GizmoIcon·GizmoLine이 active 술어 없이 always라 Player에서도 선언된다.
Run-Step "파이프라인 구성" {
    & pwsh -NoProfile -File `
        (Join-Path $PSScriptRoot "verify-pipeline-composition.ps1") -Exe $Exe
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

# 프리팹 identity 교란(트랙 P). 위 "프리팹 인스턴스 복제"가 2026-08-30에 한 번
# 실패하고 재현되지 않았다. 원인은 초기 상태가 아니라 **efsw 워처 스레드와의
# 경합**이었다 — 원자적 게시(.tmp -> replace)를 워처가 Delete로 오독해 본문이
# 멀쩡한데도 catalog 항목과 sidecar를 떨어뜨렸고(정상 실행 한 판에 두 번, 각
# ~26ms 실측), 그 창에 prefab.update가 걸리면 새 GUID를 발급하고 인스턴스를
# 하나도 못 찾은 채 조용히 0건 적용했다.
#
# 그 좁은 창을 우연에 맡기지 않고, sidecar를 실행 중에 확정적으로 떨어뜨려
# 엔진이 identity를 지켜 내는지 본다. 고치기 전 RED(판정 4·5·6), 고친 뒤 GREEN을
# 확인하고 편입했다.
Run-Step "프리팹 identity 교란" {
    & pwsh -NoProfile -File (Join-Path $PSScriptRoot "verify-prefab-identity-injection.ps1") -Exe $Exe -Work $Work
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

# TransformUpdatePlan X1 — C++/C#/reflection/prefab/Animator/Physics/Socket 등
# known writer가 PublishLocalWrite로 합류하는지 정적 inventory와 runtime reason으로
# 확인하고, 각 writer marker 제거 mutation이 실제 RED인지 고정한다.
Run-Step "Transform 쓰기 publication 단일화" {
    & pwsh -NoProfile -File (Join-Path $PSScriptRoot "verify-transform-write-publication.ps1") -Exe $Exe -Work $Work
}

# TransformUpdatePlan X2 — 정지 queue-empty와 UI/Spatial 독립 publication,
# paused UI-only 소비 및 LayoutUISubtree 즉시 의미를 한 probe에서 고정한다.
Run-Step "트랜스폼 UI/Spatial 도메인 게이트" {
    & pwsh -NoProfile -File (Join-Path $PSScriptRoot "verify-transform-domain-gates.ps1") -Exe $Exe -Work $Work
}

# TransformUpdatePlan X3 — runtime 계층 변경을 handle 기반 Scene::Reparent로
# 단일화하고, loader bulk-build의 topology version을 transaction당 한 번만 올린다.
Run-Step "계층 mutation transaction" {
    & pwsh -NoProfile -File (Join-Path $PSScriptRoot "verify-hierarchy-mutation.ps1") -Exe $Exe -Work $Work
}

# TransformUpdatePlan X4 — stable Entity identity와 실행 위치를 분리한 두 packed
# projection의 mapping/nearest-parent/preorder range를 검증하고, 10k topology
# transaction compile이 60 Hz 프레임 예산 안인지 Release에서 4회 잰다.
# ★ 이 게이트는 **Release로만** 잰다(2026-09-05 정정).
#
# 게이트 자체의 기본 -Exe는 Release인데 세트가 `-Exe $Exe`(Debug)로 덮고 있었다.
# 10k 노드 벤치의 예산 16,666µs는 한 프레임이고 Release 기준으로 잡은 값이다.
# 같은 기계에서 나란히 쟀다:
#
#     Release  median   5,872 µs  → PASS
#     Debug    median 168,833 µs  → FAIL   (28.7배)
#
# 즉 이 칸의 빨강은 제품 회귀가 아니라 축 불일치였다 — 성능 판정은 Release로만
# 한다는 규칙이 세트 배선에서 깨져 있었다.
# 다른 Release 전용 게이트들과 같은 관례로 옮긴다 — Release가 없으면 건너뛰고
# Debug로 대체하지 않는다.
#
# 아래 X5·X6도 같은 `-Exe $Exe` 덮어쓰기를 받는다. 그쪽 성능 단정은 절대 예산이
# 아니라 A/B 상대 비교라 Debug에서도 통과하므로 지금 옮기지 않는다 — 옮긴다면
# 그것은 별도 슬라이스이고, 낡은 Release exe를 재게 되는 쪽이 더 큰 위험이다.
$x4ReleaseExe = Join-Path $PSScriptRoot "..\..\Bin\x64-Release\Editor\CreatorEditor.exe"
if (Test-Path -LiteralPath $x4ReleaseExe -PathType Leaf) {
    Run-Step "Transform sparse execution graph" {
        & pwsh -NoProfile -File (Join-Path $PSScriptRoot "verify-transform-execution-graphs.ps1") -Work $Work
    }
} else {
    "=== Transform sparse execution graph === 건너뜀 (Release 미빌드 — Debug로 대체하지 않는다)"
    ""
}

# TransformUpdatePlan X5 — setter publish를 node epoch로 dedupe하고 canonical preorder
# subtree range로 병합한 뒤 affected packed range만 resolve한다. recursive fallback과
# 10k full-movement A/B 성능 상한도 같은 Release probe에서 고정한다.
Run-Step "Transform dirty-root sparse resolver" {
    & pwsh -NoProfile -File (Join-Path $PSScriptRoot "verify-transform-sparse-resolver.ps1") -Exe $Exe -Work $Work
}

# TransformUpdatePlan X6 — C# world getter/setter 앞의 즉시 pull을 packed parent chain에
# 연결하되 X5 global queue와 sibling propagation epoch를 소비하지 않는다.
Run-Step "Transform targeted immediate pull" {
    & pwsh -NoProfile -File (Join-Path $PSScriptRoot "verify-transform-targeted-pull.ps1") -Exe $Exe -Work $Work
}

# TransformUpdatePlan X7 — skeleton binding 때 bone index를 한 번만 해석하고,
# worker barrier 뒤 Animator pose·Socket 및 Physics world write를 bulk publish한다.
Run-Step "Transform Animator/Physics bulk writers" {
    & pwsh -NoProfile -File (Join-Path $PSScriptRoot "verify-transform-bulk-writers.ps1") -Exe $Exe -Work $Work
}

# TransformUpdatePlan X8 — all render writers OR semantic bits into a frame-persistent
# queue; the final render stage drains once and rejects stale registration generations.
Run-Step "Render proxy dirty-mask final commit" {
    & pwsh -NoProfile -File (Join-Path $PSScriptRoot "verify-render-proxy-dirty.ps1") -Exe $Exe -Work $Work
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

# D3-b(SerializationPlan): 저작 텍스트 자산의 개행이 LF로 고정돼 있는지. 결과(자산의 CRLF 0)와 원인(writer가 텍스트 모드를 쓰지 않음, .gitattributes의 eol=lf)을 함께 본다 — 결과만 재면 "지금은 깨끗하지만 다음 저장에서 되돌아오는" 상태를 통과시킨다.
Run-Step "저작 개행 LF 고정(D3-b)" {
    & pwsh -NoProfile -File `
        (Join-Path $PSScriptRoot "verify-authoring-line-endings.ps1")
}

# D3-b-1(SerializationPlan): ryml 에러가 프로세스 abort가 아니라 예외로 오는가.
# ★ 이 검사의 이빨은 종료 코드가 아니라 크래시다 — 채널 하나만 빼도 실패가 아니라
#   프로세스가 죽는다(변이 2회로 확인: 둘 다 exit 3). ryml을 제품 경로에 넣기 전에
#   반드시 초록이어야 하는 선결 조건이다.
Run-Step "ryml 에러 정책(D3-b-1)" {
    & pwsh -NoProfile -File `
        (Join-Path $PSScriptRoot "verify-ryml-error-policy.ps1") -Exe $Exe -Work $Work
}

# D3-b-L(SerializationPlan): TagManager 읽기 경로. `Load`를 ryml로 옮긴 뒤 변이를
# 넣었더니 **어떤 게이트도 잡지 못했다** — 기존 tag 프로브는 메모리 조작만 쟀다.
# ★ 이 검사는 자산을 스냅샷 뜨고 되돌린다. 에디터가 종료 시 TagManager를 저장하므로
#   읽기가 깨진 빌드로 돌리면 빈 상태가 디스크를 덮는다(실제로 한 번 잃었다).
Run-Step "TagManager 읽기(D3-b-L)" {
    & pwsh -NoProfile -File `
        (Join-Path $PSScriptRoot "verify-tag-authoring-read.ps1") -Exe $Exe -Work $Work
}

# D3-b-L(SerializationPlan): ShaderMeta 읽기 경로. 이 파서의 계약은 `dx12.selftest`
# 안에만 있었고 그것은 이 세트에 없다 — 게다가 자기 하네스는 vcpkg baseline
# preflight에 막혀 돌지 않는다. 변이로 확인했다: unknown-field 거부를 지우면
# `dx12.selftest`는 빨개지지만 `verify-experiment-asset-cooker`는 초록이었다.
# ★ 수용(실자산 6개의 이름 집합)과 거절(사유별 6종)을 함께 잰다. 저작 코퍼스는
#   전부 유효하므로 수용만 재면 느슨해지는 이식을 원리적으로 못 잡는다.
Run-Step "ShaderMeta 읽기(D3-b-L)" {
    & pwsh -NoProfile -File `
        (Join-Path $PSScriptRoot "verify-shadermeta-authoring-read.ps1") -Exe $Exe -Work $Work
}

# D3-b-4(SerializationPlan): 이중 backend migration probe와 경계 래칫을 은퇴시키고
# yaml-cpp include/symbol/manifest/runtime packaging이 다시 들어오지 못하게 0을 고정한다.
Run-Step "yaml-cpp 은퇴(D3-b-4)" {
    & pwsh -NoProfile -File `
        (Join-Path $PSScriptRoot "verify-yaml-cpp-retirement.ps1")
}

# Material constant buffer의 binary embedding이 parser 라이브러리에 다시 묶이지 않고
# 표준 base64 known vector/전 byte round-trip/손상 입력 거부를 지키는지 본다.
Run-Step "저작 base64 계약(D3-b-4)" {
    & pwsh -NoProfile -File `
        (Join-Path $PSScriptRoot "verify-authoring-base64.ps1")
}

# D4(SerializationPlan): Animator controller graph은 별도 JSON 없이 씬 reflection
# YAML 하나로만 왕복하고 owner/current/Any/transition/condition 링크를 복원한다.
Run-Step "Animator 씬 YAML 단일 정본(D4)" {
    & pwsh -NoProfile -File `
        (Join-Path $PSScriptRoot "verify-animator-scene-single-truth.ps1") -Exe $Exe -Work $Work
}

# D4: 실제 InputMap 6개를 canonical `.inputmap` YAML로 읽어 의미 수치와
# source 무변이를 고정한다.
Run-Step "InputMap YAML 코퍼스(D4)" {
    & pwsh -NoProfile -File `
        (Join-Path $PSScriptRoot "verify-inputmap-yaml-corpus.ps1") -Exe $Exe -Work $Work
}

# D4: header-only nlohmann은 PE import로 잡히지 않는다. source/manifest/installed
# tree/status와 세 도메인의 legacy JSON 진입점을 모두 0으로 단정한다.
Run-Step "nlohmann 은퇴(D4)" {
    & pwsh -NoProfile -File `
        (Join-Path $PSScriptRoot "verify-nlohmann-retirement.ps1")
}

# D3-a-1(SerializationPlan Y-6): 오버라이드 시딩의 값 동등 판정이 Dump 문자열 비교에서
# 구조 비교로 바뀌었다. 판정 규칙 14종과 "Dump와 갈리는 지점이 예상한 3건뿐"임을 본다.
Run-Step "저작 노드 구조 비교(D3-a-1)" {
    & pwsh -NoProfile -File `
        (Join-Path $PSScriptRoot "verify-authoring-node-equality.ps1") -Exe $Exe -Work $Work
}

# D1(SerializationPlan Y-3): Player가 파일 워처·`.meta` 생성 스캔을 끌고 들어가지
# 않는지. 정적 검사 + 산출 바이너리 대조라 빠르고, exe가 없으면 스스로 실패한다.
Run-Step "Player 런타임 위생(D1)" {
    & pwsh -NoProfile -File `
        (Join-Path $PSScriptRoot "verify-player-runtime-hygiene.ps1")
}

# D1(SerializationPlan Y-4): 네이티브 스크립트 소스가 pak에 실리지 않고, 셰이더
# 소스/sidecar는 그대로 실리는지. 실자산에는 `.cpp/.h/.hpp`가 0개라 합성 트리로
# 판정한다 — 실자산으로 재면 필터가 있든 없든 "0개를 걸렀다"가 나온다.
$assetPackerExe = Join-Path $PSScriptRoot "..\..\Bin\x64-Release\Tools\AssetPacker\AssetPacker.exe"
if (Test-Path -LiteralPath $assetPackerExe -PathType Leaf) {
    Run-Step "pak 소스 배제(D1)" {
        & pwsh -NoProfile -File `
            (Join-Path $PSScriptRoot "verify-pak-source-exclusion.ps1")
    }
} else {
    "=== pak 소스 배제(D1) === 건너뜀 (AssetPacker Release 미빌드)"
    ""
}

# D0(SerializationPlan): 직렬화 기준선. 이 항목만 Release exe를 요구한다 —
# Debug는 단계별로 4~16배 느리고 **단계 간 비중까지 뒤집어**(SceneParse 15.9배 vs
# ComponentLoad 5.5배) 성능 기준선으로 쓸 수 없다. 그래서 $Exe를 넘기지 않고
# 스크립트 기본값(Release)을 그대로 쓴다.
#
# Release가 없으면 건너뛴다. 위 리플렉션 골든·생명주기 기준선과 같은 이유다 —
# 아직 Release를 빌드하지 않은 사람에게 세트가 통째로 빨갛게 보이면 세트 전체가
# 무시되기 시작한다. 대신 건너뛴 사실을 조용히 넘기지 않고 한 줄로 남긴다.
$releaseExe = Join-Path $PSScriptRoot "..\..\Bin\x64-Release\Editor\CreatorEditor.exe"
if (Test-Path -LiteralPath $releaseExe -PathType Leaf) {
    Run-Step "직렬화 기준선(D0, Release)" {
        & pwsh -NoProfile -File `
            (Join-Path $PSScriptRoot "verify-serialization-baseline.ps1") -Work $Work
    }
} else {
    "=== 직렬화 기준선(D0, Release) === 건너뜀 (Release 미빌드 — Debug로 대체하지 않는다)"
    ""
}

# PHASE 3.75 MBC11 §8.4: 모델 cutover 성능 예산. 위와 같은 이유로 Release exe만 쓴다 —
# B1/B2는 임시 프로젝트 사본에서 저작·재로드를 재고, B6는 archive가 있을 때만 비회귀를
# 판정한다(archive는 `-Archive`로 사람이 뜬다 — 세트는 기록하지 않는다).
if (Test-Path -LiteralPath $releaseExe -PathType Leaf) {
    Run-Step "모델 cutover 성능 예산(MBC11 §8.4, Release)" {
        & pwsh -NoProfile -File `
            (Join-Path $PSScriptRoot "verify-model-cutover-budget.ps1") -Work $Work
    }
} else {
    "=== 모델 cutover 성능 예산(MBC11 §8.4, Release) === 건너뜀 (Release 미빌드 — Debug로 대체하지 않는다)"
    ""
}

if ($failed.Count -gt 0) {
    "실패한 검사: " + ($failed -join ', ')
    exit 1
}
"회귀 세트 전체 통과"
exit 0
