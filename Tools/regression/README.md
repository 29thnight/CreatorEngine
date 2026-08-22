# UI 회귀 세트

에디터로 손수 확인하면 놓치는 것들을 기계로 잡기 위한 검사 묶음이다.
전부 종료 코드로 판정하므로 CI에 그대로 걸 수 있다.

## 전체 실행

```powershell
pwsh Tools/regression/run-all.ps1
```

빌드된 `x64\Debug\Academy_4Q.exe`가 필요하다. 다른 위치를 쓰려면 `-Exe`로 넘긴다.

## 개별 검사

전체 스위트는 `run-all.ps1`의 Run-Step 목록이 정본이다(현재 27종). 아래 표는 그중
"왜 이렇게 재는가"를 기록해 둘 가치가 있는 검사만 담는다 — 표에 없다고 스위트에
없는 것이 아니다.

| 검사 | 무엇을 지키는가 |
|------|-----------------|
| `ui_regression.txt` | 비정상 순서로 UI를 만들고 재생/정지를 반복한다. 캔버스 없이 UI를 먼저 만들거나 캔버스를 나중에 붙이는 경로 — 에디터에서 정상 순서로 만들면 절대 드러나지 않는 크래시가 여기서 나온다. |
| `verify-play-roundtrip.ps1` | Edit→Play→Stop이 씬을 보존하는지. E3가 play-mode 소유권을 Editor로 옮기기 전에 "지금 동작"을 못 박기 위해 만들었다 — 그 전까지 이 세트에는 재생 왕복을 재는 검사가 없었다. 재생 중 오브젝트를 하나 만들어 정지 후 사라지는지까지 본다(아무것도 안 바꾸고 비교하면 "복원했다"가 아니라 "건드린 게 없다"를 재게 된다). 엔진의 transform digest 해시는 열거 순서에 민감한데 왕복 후 슬롯 인덱스가 재배정되므로(실측: Main Camera↔Directional Light), 해시 대신 이름으로 정렬한 내용 집합을 비교하고 슬롯 순서는 실패시키지 않되 PASS 줄에 남긴다. |
| `verify-ui-layout-golden.ps1` | UI 레이아웃 형상이 통째로 회귀하지 않았는지(`verify-authored-rects`의 후계). 원본은 저작 프리팹의 옛 `m_worldRect`를 정답지로 썼는데 그 키가 직렬화에서 빠져 소멸했으므로, 앵커 프리셋 8종과 3단 중첩을 CLI로 저작해 골든과 diff 0으로 대조한다. 규약 자체의 정합성은 `verify-resolution-sweep`이 수식으로 재고, 이 게이트는 "바뀌지 않았는가"만 잰다 — 둘은 상보적이다. 골든이 없으면 건너뛴다(`-Baseline`으로 생성, 뜨기 전에 값을 사람이 검산할 것). |
| `verify-resolution-sweep.ps1` | 해상도를 바꿔 가며 캔버스가 화면을 따라오는지, 배율이 uGUI와 같은 로그 보간 값인지, 자식 크기가 배율을 따르는지, 버튼의 클릭 판정 상자가 보이는 사각형과 같은지. 16:9 축소·4:3·21:9·세로형·복귀까지 7단계. |
| `verify-shutdown-order.ps1` | 첫 프레임이 만들어지기 전에 종료를 걸어, `Dx11Main::Finalize`가 렌더 스레드(CB/CE)를 완전히 세운 뒤에야 렌더 씬을 해체하는지. 순서가 뒤집히면 커맨드를 만드는 중에 발밑에서 자료구조가 사라진다. 확률적이라 6회 반복한다. |
| `verify-crash-dump.ps1` | `crash.test`로 일부러 죽여 크래시 경로(AV·abort·미처리 예외)가 실제로 `.dmp`와 심볼 붙은 스택을 남기는지. 덤프 코드는 크래시가 나야만 실행돼서 평소엔 아무도 확인하지 않고, 그래서 조용히 망가져 있었다 — 로그에 CRASH 줄만 남고 덤프가 통째로 없는 크래시가 실제로 있었다. |
| `verify-lifecycle-baseline.ps1` | 생명주기가 누구를 어떤 순서로 부르는지(PHASE 9-0). 지금 순서는 델리게이트의 우선순위 정렬과 등록 시점이 만드는 창발적 결과라 코드로는 알 수 없고, PHASE 9가 그 기구를 통째로 바꾼다. 교체 전에 기준선을 떠 두어야 교체 후 "동작이 같다"를 주장할 수 있다. 기준선 파일이 없으면 `run-all`이 이 항목을 건너뛴다. |
| `verify-reflection-golden.ps1` | 리플렉션 직렬화 출력이 변하지 않았는지(PHASE 18 CT0). `reflect.golden`이 등록 전 타입을 기본 생성해 직렬화한 덤프를 골든과 diff 0으로 대조한다 — 컴파일타임 전환(CT4~CT5) 구간에서 "필드가 조용히 빠지는" 회귀를 잡는 유일한 자다. 씬·프리팹 콘텐츠에 기대지 않으므로 GUID 같은 실행마다 다른 값이 안 섞인다. `perf.reflect` 수치(씬 Serialize·InstantiatePrefab)는 기록만 하고 판정하지 않는다 — 시간에 문턱을 걸면 머신 편차로 거짓 실패가 나 검사가 신뢰를 잃는다. 골든이 없으면 `run-all`이 건너뛴다(`-Baseline`으로 생성). |
| `verify-bt-smoke.ps1` | 행동 트리가 **실제로 도는지**(PHASE 9-8). 이 세트의 나머지는 BT를 한 줄도 실행하지 않는다 — BT 컴포넌트는 프리팹에만 붙어 있고 다른 시나리오가 여는 씬에는 없다. 게다가 트리 생성·틱은 실패할 때만 로그를 남겨(성공은 무음) "트리가 안 서서 AI가 가만히 있다"와 "정상"이 로그에서 같아 보인다. 그래서 `bt.status`로 수를 센다: 소환 전 0개 → 소환 후 3개 → 재생 중 틱 증가 → 씬 교체 후 0개. 경계 불변식(프레임당 크로싱 ≤ 1회, 크로싱당 전달 틱 > 1)도 여기서 수치로 못 박는다. **게임 콘텐츠에 기대지 않는다** — 전용 노드(`GameScripts/BTProbeNodes.cs`)와 전용 그래프(`BTProbe.bt`/`.blackboard`/`.prefab`)를 쓴다. 게임 프리팹을 쓰면 콘텐츠가 바뀔 때마다 흔들리고, 엔진 경로를 재는 검사가 콘텐츠 회귀로 오해되기 시작하면 아무도 믿지 않게 된다. |
| `verify-asset-authoring-ownership.ps1` | E2의 asset writer 경계를 정적·동적으로 함께 고정한다. `ModelLoader`/`Terrain`에 filesystem writer가 재유입되지 않았는지 검사하고, 고유 GLB를 두 번 import해 Editor가 model cache와 embedded PNG를 처음 한 번만 게시하는지 확인한다. Terrain은 height/splat/texture를 임시 세대에 완성한 뒤 descriptor를 마지막에 게시하며, 실패 요청이 기존 descriptor·세대를 바꾸지 않는지도 검사한다. 같은 Editor 세션의 model reimport, Player writer 부재, `.tmp`·probe 잔여 검사도 함께 수행한다. |
| `verify-asset-runtime-change-boundary.ps1` | E2의 Editor→Runtime asset 변경 계약을 고정한다. `DataSystem`의 public catalog mutation primitive 재노출을 막고 `CatalogUpsert`/`ContentReload`/`Removed` 단일 계약, 이전 cache generation pin, Editor 게시 완료 후 발행, Player 생산자 부재를 검사한다. |
| `verify-asset-presentation-boundary.ps1` | E2의 picker/icon/font 경계를 고정한다. `DataSystem`에 ImGui·파일/gizmo 아이콘·폰트·material 전달 상태가 재유입되지 않는지, `EditorAssetPresentation`이 두 selector와 표시 리소스를 소유하는지, gizmo texture가 `ScriptBinder`의 Editor 역참조가 아니라 프레임 packet의 공유 수명 입력으로 전달되는지, Player가 presentation을 설치하지 않는지 검사한다. |

## 생명주기 기준선 뜨기 (PHASE 9-0)

PHASE 9 교체 **전에** 한 번 떠서 커밋해 둔다. 교체 후에 뜨면 비교 대상이 사라진다.

```powershell
pwsh Tools/regression/verify-lifecycle-baseline.ps1 -Baseline
```

이후 9-1~9-3 각 단계 뒤에 인자 없이 실행하면 기준선과 대조한다.
인스턴스 ID와 프레임 번호는 실행마다 달라지므로 비교에서 뺀다 — 남는 것은
(단계, 타입, 오브젝트 이름)의 **순서**이고, 그것이 생명주기의 계약이다.

## 생명주기 디스패치 경로 (PHASE 9-1 · 9-2)

경로는 하나다 — Scene 소유 단계 리스트(레지스트리). 9-2에서 컴포넌트 26종이 옮겨 가며
델리게이트 경로에는 구독자가 0이 됐다. `lifecycle_baseline.tsv`가 그 유일한 기준선이다.

9-1 동안에는 경로가 둘이라 기준선도 둘이었다. 두 경로는 같은 사건을 냈지만 **단계 안의
순서가 달랐다**: 델리게이트 쪽은 우선순위 정렬 삽입(`lower_bound` + `>` 비교자)이 같은
우선순위에서 항상 맨 앞에 꽂아 **등록 역순**으로 돌았다 — 설계된 계약이 아니라 자료구조에서
나온 부수 효과다. 레지스트리는 등록 순서로 돈다. 9-2에서 등록 순서를 최종 계약으로 확정했다
(근거: 훅 본문이 전부 타입별 리스트에 Collect하는 일이라 단계 안 순서에 의존하지 않고,
UI 회귀 294건·저작 배치 12건이 그 변경 위에서 통과한다).

검증 실패 시 출력은 **"사건이 빠졌다"와 "순서만 다르다"를 갈라서** 보고한다. 전자는 결함이고
후자는 설계 판단이라 성격이 다르다.

## AddressSanitizer 빌드 (PHASE 9-0 / 0-5)

```powershell
msbuild CreatorEngine.sln /p:Configuration=Debug /p:Platform=x64 /p:EngineAsan=true
```

솔루션 구성을 늘리지 않고 스위치로 켠다(`Directory.Build.targets` 참조).

**주의 — 이것을 모르면 ASan이 아무것도 잡지 못한다.** 엔진의 `GameObject`·`Component`는
전부 `shared_alloc` → `MyAlloc` → `mi_malloc`을 지나는데, mimalloc은 ASan의 가로채기
바깥이라 그 메모리는 ASan에게 존재하지 않는다. `EngineAsan=true`가 `ENGINE_ASAN`을
정의해 `MemoryManager.cpp`가 CRT `malloc/free`로 돌아가게 하는 이유가 그것이다.
이 우회 없이 ASan을 켜면 컴포넌트 UAF를 한 건도 못 잡은 채 "무사고"로 보고된다.

실행 전에 런타임 DLL과 옵션을 챙긴다.

```
copy "%VCToolsInstallDir%bin\Hostx64\x64\clang_rt.asan_dynamic-x86_64.dll" x64\Debug\
set ASAN_OPTIONS=detect_leaks=0:halt_on_error=1:abort_on_error=1
```

## 검사가 조용히 건너뛰지 않게 하기

`verify-resolution-sweep.ps1`은 히트박스 단정이 **한 건도 실행되지 않으면 실패**로 끝난다.
버튼이 없는 프리팹을 띄우는 바람에 그 단정이 통째로 건너뛰어지고도 "전체 통과"가
나온 적이 있어서 넣은 장치다. 검사를 추가할 때도 같은 원칙을 지킬 것 —
"확인하지 못했다"와 "확인했고 문제없다"는 다르다.

## 검사가 실제로 실패하는지 확인하기

기대값을 일부러 틀리게 주면 실패해야 한다.

```powershell
pwsh Tools/regression/verify-resolution-sweep.ps1 -RefWidth 1280 -RefHeight 720
```

기준 해상도를 거짓으로 주었으므로 배율 단정이 전부 실패하고 종료 코드 1이 나온다.
이게 나오지 않으면 검사가 아무것도 보고 있지 않다는 뜻이다.

## 아직 없는 것

렌더된 픽셀을 직접 대조하는 시각 회귀는 없다. 백버퍼 캡처를 붙이려 했으나
게임 스레드에서 `DirectX::CaptureTexture`가 죽어(0x0000087A) 보류했다 —
렌더 루프의 안전 지점에서 실행해야 한다. 그때까지 UI 렌더 좌표는 숫자
대조와 수식 동등성으로만 검증된다.
