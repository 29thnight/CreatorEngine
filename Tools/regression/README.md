# UI 회귀 세트

에디터로 손수 확인하면 놓치는 것들을 기계로 잡기 위한 검사 묶음이다.
전부 종료 코드로 판정하므로 CI에 그대로 걸 수 있다.

## 실행 정책 — 전체 세트는 없다 (2026-09-16 폐지)

`run-all.ps1` 은 폐지했다. 전체 세트를 한 번에 돌리지 않는다. 폐지한 이유:

- 초록인 기준선이 한 번도 없었다. 선행 실패가 늘 섞여 있어서, 세트 결과로는 내 변경이
  무엇을 깼는지 가를 수 없었다.
- 수십 분짜리 세트는 거의 돌지 않았고, 그 사이 세트에 넣은 검사가 몇 주씩 붉은 채 남았다
  (예: PBR 장시간 검증의 예열 표본, 실험 계약 검사의 링크 파손).
- 에디터 창을 숨기지 않는 검사가 절반 가까이 섞여 있었다.

대신 **세션마다 이번 변경이 닿는 검사를 모아 묶음으로 집중해서 돌린다.**

1. **모은다** — 바꾼 파일·명령·자산에서 출발해, 그것을 부르거나 재는 검사를 찾는다
   (`Tools/regression/verify-*.ps1` 을 식별자·명령 이름으로 검색, 계획서 해당 절이 지목한 검사,
   아래 표). 새 검사를 만들었으면 그것도 넣는다.
2. **묶는다** — 같은 바이너리·같은 구성·같은 전제를 쓰는 것끼리 묶는다(예: 에디터 없이 컴파일만
   하는 검사 / Debug 에디터 / Release 에디터 / Player). 묶음마다 빌드를 한 번만 확인한다.
3. **집중해서 돌린다** — 묶음 단위로 돌리고, 붉으면 그 자리에서 원인을 가른다. 선행 실패인지는
   `git stash` → 같은 구성으로 빌드 → 단독 실행으로 HEAD 에서 재현해 확인한다.
4. **적는다** — 커밋 메시지나 계획서에 돌린 검사와 수를 남긴다. 돌리지 않은 인접 검사는
   "돌리지 않았다" 고 적는다.

지켜야 할 것:

- 에디터·Player 는 **창을 띄우지 않는다**(`CreateNoWindow` + `WindowStyle Hidden`). 창을 숨기지
  않는 검사를 돌려야 하면 먼저 그 검사를 고친다.
- `-Exe`/`-Editor` 를 **명시한다**. 검사마다 기본값이 Debug/Release 로 섞여 있다.
- pwsh 로 돌린다(Windows PowerShell 5.1 은 한글 주석을 깨뜨린다).

## 개별 검사

아래 표는 "왜 이렇게 재는가" 를 기록해 둘 가치가 있는 검사만 담는다. 목록의 정본은 이 표가
아니라 디렉터리의 `verify-*.ps1` 자체다.

| 검사 | 무엇을 지키는가 |
|------|-----------------|
| `verify-inspector-layout-matrix.ps1` | 인스펙터 배치의 **폭 × 사용자 배율 행렬**과 **줄 전환 경계**. 넘침만 재던 자가 못 보던 축을 연다 — 값 칸이 1 px 로 눌려도 오른쪽 끝은 작업 영역 **안**이라 넘침은 0 이다(착수 때 240 폭에서 배열 원소 칸이 1 px, 일반 경로의 `vector3` 칸이 24 px 였고 검사 셋이 전부 초록이었다). 값 칸 장부(`minFieldWidth`·`minAxisWidth`·`minLineValue`·`fieldDigest`)를 읽어 하한(`valueMin`·`axisValueMin`, 폰트에서 나오므로 배율을 따라간다) 위인지 보고, 같은 논리 폭의 칸 폭을 실제 배율로 나눈 값이 배율 사이에 같은지(**논리 불변** — 배율을 안 받는 고정 픽셀을 잡는다), 폭이 바뀌어도 값 칸 신원이 같은지(*"전환 시 ID 변경"*)를 단정한다. 경계는 224~432 논리 px 를 4 px 걸음으로 훑어 찾고 **양쪽 모두** 판정한다. ★ 이 자가 재는 것은 **배치**지 화면에 보이는가가 아니다 — 도크 패널은 창을 키워도 505 px 라 그보다 넓은 요청은 잘린다(그 사실을 `visibleWidth` 로 함께 낸다). |
| `verify-inspector-edit-roundtrip.ps1` | 인스펙터에서 **편집하는 동안과 왕복 뒤** 값과 필드 신원이 남는가. 값 칸을 실물로 누르고 끈다(`editor.nav pointer|press|release`, 좌표는 장부가 낸 첫 값 칸 사각형). 편집 중 폭을 경계 너머로 바꿔도 배치가 전환되지 않고(계약: 편집 중 보류) `activeId`·신원·값이 그대로이며 손을 뗀 뒤에야 전환되는 것을 **둘 다** 단정하고, 끌어 바뀐 값을 `undo`/`redo` 가 왕복하며, 선택·펼침·엔티티 활성 왕복과 씬 저장/재로드·프리팹 소환에서 값이 보존되는지 본다. 자극은 **보이는 폭 안에서만** 한다(잘린 자리를 누르면 아무 일도 없다). |
| `verify-editor-warmup-endpoint.ps1` | 예열이 **어디까지 갔는지**를 밖에서 읽는 창구(`GET /warmup`). 에디터는 창이 뜬 뒤에도 한참 쓸 수 없는데(이 기계 Release 실측: 창 1.6 s, 씬뷰 캔버스 18.8 s) 그 구간에 답할 수 있던 것은 `render.live.wait` 의 "끝났는가" 뿐이었고 `BootProgress` 는 로딩창에만 그렸다. 단계 여덟과 각 도달 시각·마지막 단계 뒤 흐른 시간·완료 여부를 단정하고, **미도달 단계도 목록에 있는지**, **예열 도중에 답하는지**, 그때 응답이 2 초 미만인지(★ 창구가 게임 스레드 큐를 타면 파이프라인 구축 구간에서 초 단위로 튄다 — 변이 실측 5019 ms), 도달 수가 줄지 않는지, 순서가 맞는지, 한 번 도달한 시각이 변하지 않는지, 끝내 완주하는지를 본다. |
| `verify-mathematics-contract.ps1` | 벤더링한 Mathematics SHA와 공통 include 배선을 확인한 뒤, 작은 독립 실행 파일을 MSVC x64 Debug/Release로 직접 컴파일·실행한다. vector/matrix/color/rect/bounds의 크기·offset, row-vector `S*R*T`, quaternion 곱 순서, AABB transform, frustum projection/transform, easing/tween manager를 고정 골든값과 독립 numeric/property 계약으로 검증한다. 저장소 소유 native source의 retired Mathf/DirectX math surface와 `vcpkg.json`/공통 build 설정 재도입도 실패시킨다. Editor 빌드는 필요 없으며 기본 실행이 두 구성을 모두 검사한다. |
| `ui_regression.txt` | 비정상 순서로 UI를 만들고 재생/정지를 반복한다. 캔버스 없이 UI를 먼저 만들거나 캔버스를 나중에 붙이는 경로 — 에디터에서 정상 순서로 만들면 절대 드러나지 않는 크래시가 여기서 나온다. |
| `verify-play-roundtrip.ps1` | Edit→Play→Stop이 씬을 보존하는지. E3가 play-mode 소유권을 Editor로 옮기기 전에 "지금 동작"을 못 박기 위해 만들었다 — 그 전까지 이 세트에는 재생 왕복을 재는 검사가 없었다. 재생 중 오브젝트를 하나 만들어 정지 후 사라지는지까지 본다(아무것도 안 바꾸고 비교하면 "복원했다"가 아니라 "건드린 게 없다"를 재게 된다). 엔진의 transform digest 해시는 열거 순서에 민감한데 왕복 후 슬롯 인덱스가 재배정되므로(실측: Main Camera↔Directional Light), 해시 대신 이름으로 정렬한 내용 집합을 비교하고 슬롯 순서는 실패시키지 않되 PASS 줄에 남긴다. |
| `verify-play-selection-undo.ps1` | 같은 왕복의 선택·Undo 쪽. E3-2+3이 play-mode transaction을 EditorPlayModeController로 옮기고 Undo를 SceneManager에서 들어내기 전에 만들었다 — 그 전까지 세트 전체에 selection/undo 단정이 0건이었다. **계획서 문구를 따르지 않는다**: 계획서는 "selection이 복원된다"고 적었지만 코드는 복원하지 않고 해제한다(선택은 씬 YAML에 실리지 않아 스냅샷에 담기지도 않는다). 선택이 Entity* 원시 포인터인데 정지가 엔티티를 전부 파괴하므로 해제가 안전한 동작이고, "복원"은 리팩터가 아니라 기능이다. 그래서 해제를 단정한다. 편집 스택과 게임 스택을 따로 찍는 이유는 `m_isGameMode`가 이름과 달리 "에디터 UI의 Play 버튼을 눌렀는가"라서다 — CLI 재생에서는 영원히 false이므로, 유효 스택 하나만 보면 편집 스택을 보면서 게임 스택을 검사한다고 착각한다. |
| `verify-play-mode-policy-boundary.ps1` | 재생 전환 정책의 소유권 경계(정적). Undo 이력 폐기가 `SceneManager`에서 `EditorPlayModeController`로 옮겨간 뒤, Player에서 "아무 일도 안 일어남"은 런타임으로 재기 어렵다 — 정상이 곧 무동작이라 관측할 것이 없다. 그래서 정적으로 못 박는다. 부재 단정만 두면 대상을 못 찾아도 0건이 나오므로, **찾을 수 있어야 하는 것을 먼저 찾는다**(컨트롤러가 실재하고 Undo를 다루는지, Core가 통지를 던지는지). 선택 해제가 `AllDestroyMark` **이전**에 남아 있는지도 함께 보는데, 그 순서는 장식이 아니라 댕글링 방지다 — 빼면 재생 정지가 ACCESS_VIOLATION으로 죽는다(실측). 순서 비교는 반드시 `EndPlayTransaction` 본문 안으로 한정한다. `AllDestroyMark`는 이 파일에 5번 나와서, 파일 전체에서 찾으면 엉뚱한 등장과 비교해 거짓 실패가 난다(이 게이트를 처음 쓸 때 실제로 그렇게 틀렸다). |
| `verify-frame-orchestration.ps1` | 재생 중 시뮬레이션 순서를 `Runtime::TickSimulationFrame` 하나가 소유하는지(정적). 이관 전에는 Editor와 Player가 각자 프레임 루프를 들고 있었는데 재생 구간은 순서까지 글자 그대로 같았다 — 관리 틱을 감싸는 두 함수는 주석만 다르고 본문이 완전히 동일했다. 복제된 순서는 한쪽만 고치면 조용히 갈라지고 그러면 "에디터에서는 되는데 빌드하면 안 된다"가 된다. 두 Host를 같은 시나리오로 나란히 태우는 하네스가 없어 런타임으로는 못 잡으므로 소스에서 못 박는다. 단계 순서 비교는 반드시 `TickSimulationFrame` 본문으로 한정한다 — 관리 틱 두 함수가 파일 앞쪽 익명 네임스페이스에 정의돼 있어 파일 전체에서 찾으면 호출이 아니라 정의를 잡아 거짓 실패가 난다(이 게이트를 처음 쓸 때 실제로 그렇게 틀렸다). `GameLogic`의 기본 인자가 되살아나지 않는지도 본다 — `= 0`이면 호출부가 인자를 생략해 delta 0이 조용히 들어간다. |
| `verify-prefab-editor-ownership.ps1` | 프리팹 편집 모드가 Editor 소유로 남아 있는지(정적). Player에서 "없다"는 관측할 것이 없는 성질이고, 링커가 이미 참조 없는 코드를 버려서 바이너리로도 이관 전후를 구분할 수 없다 — 바뀐 것은 컴파일 대상과 층 경계다. **주석은 걸러내고 코드만 본다**: Core의 여러 파일이 "PrefabEditor가 하던 일"을 설명하는 주석을 갖고 있어서, 그것까지 위반으로 세면 설명을 지워야 통과하는 게이트가 된다. 음성 테스트로 코드 사용은 잡고 주석 언급은 통과시키는 것을 둘 다 확인했다. |
| `verify-pipeline-composition.ps1` | 렌더 패스가 **어느 뷰에 조립되는가**. 패스 내부 렌더링은 `dx12.*`/`vk.*` 자가 검사 35종이 리드백으로 픽셀까지 재므로 이미 덮여 있고(그래서 E4-1의 파일 이동이 기준선과 정확히 일치했다), 빈 구멍은 조립이었다. 그래서 픽셀 캡처를 새로 만들지 않고 이미 있던 `LivePipelineDesc::Dump()`를 `pipeline.nodes` CLI로 내보낸다. **착수 실측: Editor와 Player 파이프라인이 완전히 동일하다** — 둘 다 19노드이고 Grid·GizmoIcon·GizmoLine이 `active` 술어 없이 `always`라 `DeclareAll`이 건너뛰지 않는다. 즉 출하 게임이 에디터 그리드·기즈모 패스를 매 프레임 그래프에 선언한다. 이 게이트는 그 현재 상태를 그대로 못 박아, E4-3이 노드를 걷어내면 붉어지게 한다 — 기대값을 고치는 것이 곧 변경의 증거다. Player 쪽은 CLI가 없어 `PlayerMain`이 스모크 로그에 같은 값을 찍는다(`[SMOKE] pipeline.node`). |
| `verify-ui-layout-golden.ps1` | UI 레이아웃 형상이 통째로 회귀하지 않았는지(`verify-authored-rects`의 후계). 원본은 저작 프리팹의 옛 `m_worldRect`를 정답지로 썼는데 그 키가 직렬화에서 빠져 소멸했으므로, 앵커 프리셋 8종과 3단 중첩을 CLI로 저작해 골든과 diff 0으로 대조한다. 규약 자체의 정합성은 `verify-resolution-sweep`이 수식으로 재고, 이 게이트는 "바뀌지 않았는가"만 잰다 — 둘은 상보적이다. 골든이 없으면 건너뛴다(`-Baseline`으로 생성, 뜨기 전에 값을 사람이 검산할 것). |
| `verify-resolution-sweep.ps1` | 해상도를 바꿔 가며 캔버스가 화면을 따라오는지, 배율이 uGUI와 같은 로그 보간 값인지, 자식 크기가 배율을 따르는지, 버튼의 클릭 판정 상자가 보이는 사각형과 같은지. 16:9 축소·4:3·21:9·세로형·복귀까지 7단계. |
| `verify-shutdown-order.ps1` | 첫 프레임이 만들어지기 전에 종료를 걸어, `Dx11Main::Finalize`가 렌더 스레드(CB/CE)를 완전히 세운 뒤에야 렌더 씬을 해체하는지. 순서가 뒤집히면 커맨드를 만드는 중에 발밑에서 자료구조가 사라진다. 확률적이라 6회 반복한다. |
| `verify-crash-dump.ps1` | `crash.test`로 일부러 죽여 크래시 경로(AV·abort·미처리 예외)가 실제로 `.dmp`와 심볼 붙은 스택을 남기는지. 덤프 코드는 크래시가 나야만 실행돼서 평소엔 아무도 확인하지 않고, 그래서 조용히 망가져 있었다 — 로그에 CRASH 줄만 남고 덤프가 통째로 없는 크래시가 실제로 있었다. |
| `verify-lifecycle-baseline.ps1` | 생명주기가 누구를 어떤 순서로 부르는지(PHASE 9-0). 지금 순서는 델리게이트의 우선순위 정렬과 등록 시점이 만드는 창발적 결과라 코드로는 알 수 없고, PHASE 9가 그 기구를 통째로 바꾼다. 교체 전에 기준선을 떠 두어야 교체 후 "동작이 같다"를 주장할 수 있다. 기준선 파일이 없으면 `run-all`이 이 항목을 건너뛴다. |
| `verify-bt-smoke.ps1` | 행동 트리가 **실제로 도는지**(PHASE 9-8). 이 세트의 나머지는 BT를 한 줄도 실행하지 않는다 — BT 컴포넌트는 프리팹에만 붙어 있고 다른 시나리오가 여는 씬에는 없다. 게다가 트리 생성·틱은 실패할 때만 로그를 남겨(성공은 무음) "트리가 안 서서 AI가 가만히 있다"와 "정상"이 로그에서 같아 보인다. 그래서 `bt.status`로 수를 센다: 소환 전 0개 → 소환 후 3개 → 재생 중 틱 증가 → 씬 교체 후 0개. 경계 불변식(프레임당 크로싱 ≤ 1회, 크로싱당 전달 틱 > 1)도 여기서 수치로 못 박는다. **게임 콘텐츠에 기대지 않는다** — 전용 노드(`GameScripts/BTProbeNodes.cs`)와 전용 그래프(`BTProbe.bt`/`.blackboard`/`.prefab`)를 쓴다. 게임 프리팹을 쓰면 콘텐츠가 바뀔 때마다 흔들리고, 엔진 경로를 재는 검사가 콘텐츠 회귀로 오해되기 시작하면 아무도 믿지 않게 된다. |
| `verify-asset-authoring-ownership.ps1` | E2의 asset writer 경계를 정적·동적으로 함께 고정한다. `ModelLoader`/`Terrain`에 filesystem writer가 재유입되지 않았는지 검사하고, 고유 GLB를 두 번 import해 Editor가 model cache와 embedded PNG를 처음 한 번만 게시하는지 확인한다. Terrain은 height/splat/texture를 임시 세대에 완성한 뒤 descriptor를 마지막에 게시하며, 실패 요청이 기존 descriptor·세대를 바꾸지 않는지도 검사한다. 같은 Editor 세션의 model reimport, Player writer 부재, `.tmp`·probe 잔여 검사도 함께 수행한다. |
| `verify-asset-runtime-change-boundary.ps1` | E2의 Editor→Runtime asset 변경 계약을 고정한다. `DataSystem`의 public catalog mutation primitive 재노출을 막고 `CatalogUpsert`/`ContentReload`/`Removed` 단일 계약, 이전 cache generation pin, Editor 게시 완료 후 발행, Player 생산자 부재를 검사한다. |
| `verify-asset-presentation-boundary.ps1` | E2의 picker/icon/font 경계를 고정한다. `DataSystem`에 ImGui·파일/gizmo 아이콘·폰트·material 전달 상태가 재유입되지 않는지, `EditorAssetPresentation`이 두 selector와 표시 리소스를 소유하는지, gizmo texture가 `ScriptBinder`의 Editor 역참조가 아니라 프레임 packet의 공유 수명 입력으로 전달되는지, Player가 presentation을 설치하지 않는지 검사한다. |
| `verify-mbc-cutover-freeze.ps1` | PHASE 3.75(모델 자산 빅뱅 전환) **변경 동결 래칫**(정적). `ModelAssetBigBangCutoverPlan §5.2`가 제품에서 제거하기로 한 표면 — 역브리지(`BuildLegacyModelFromExperiment`·`ModelSceneBridge`), A/B 스위치(`CREATOR_EXPERIMENT_VERTEX`), 병행 상태(`m_experimentMeshBindings`·`m_hashingMesh`), Assimp include·vcpkg port, pseudo-v5(`DeterministicSubAssetId`·`Uuid::FromName`), 무조건 진단 출력 — 의 코드 접촉 수(주석 제거 뒤)를 `mbc_cutover_freeze.baseline.tsv`와 대조해 **증가만 막는다**(MBC9 이후 제거 표면은 전부 0 — 기준선이 곧 "재유입 0")(감소는 그 슬라이스가 `-Baseline`으로 내려 고정). MBC10부터 제거 표면 12종(역브리지·A/B·병행 바인딩·Assimp include·pseudo-v5·ModelSceneBridge·LoadModelViaExperiment·무조건 진단 출력 4종)은 **하드 0**이고 vcpkg assimp 0, `m_hashingMesh`는 절차 지오메트리 허용목록 10파일 밖 0, 제품 소비자(SceneRuntime·Render·PrimitiveRenderProxy·EngineGUIWindow)의 `experiment::Model`/`TryGetMesh` 0, generation 게시 진입점(`m_modelAssetGenerations.Publish`)은 DataSystem 하나, `experiment.animlive`는 `PublishAnimatorPose`를 부르지 않고 읽기 전용 스냅샷을 읽으며 `assets.modeldiag`가 존재해야 한다. 그 밖의 하드 계약 셋도 래칫이 아니다: model sidecar writer는 허용목록(EditorAssetDatabase·ModelIdentityRefresher) 밖에 생기면 즉시 실패, 검사 전용 seam(`DeriveIdentityWithProfile`·`InsertUncheckedForTest`)은 `Assets/` 정의 밖 0건, 새 `Assets/` 계층 안에 legacy 신원 API(`FromName`·`IsAssetIdV4`·`CreateRandomV4`) 0건. 동결의 위반은 그림을 바꾸지 않으므로(폴백을 한 겹 더 붙이면 오히려 "고쳐진" 것처럼 보인다 — 2026-09-02 MeshRenderer 순서 해킹이 그랬다) 축은 픽셀이 아니라 접촉 수다. |
| `verify-model-scene-consumption.ps1` | PHASE 3.75 MBC7 — Scene/MeshRenderer/material 직접 소비 + Gunner cold-load closure(§6.2). 두 프로세스로 잰다: A(저작) 빈 씬에 Gunner를 cache 로드·배치(`model.loadcached`·`model.place` — import를 타지 않아 tracked sidecar 불변)→`assets.scenemodel`(씬 전수: renderer가 `ModelAssetGeneration` handle을 붙들고 `RHIModelMeshView`가 완비되며 영속 `m_meshAssetId`가 채워졌고, 재질의 embedded texture owner 6/6이 **generation closure**(`DataSystem::ResolveModelGenerationTexture`)에서 왔는가 — 전역 임베디드 등록부 출처 0, 누락 0)→저장. B(콜드) 저장 씬 로드→같은 폐포 단정(같은 프로세스의 이전 로드·등록부 없이 6/6 — 순서 해킹 없이 성립하는 것을 증명하는 축)→`assets.scenemodel reload`(ContentReload 뒤 이전 texture generation owner 재사용 0·retire 6)→`dx12.scene`(실GPU 업로드 전량 typed generation — 총계 == generation, 커버리지 > 0). 정적으로 순서 해킹 토큰(`modelGuidHint`) 0과 typed 배선 심볼을 요구한다. MBC10부터 배치·콜드 해석 관측은 `assets.modeldiag` 스냅샷(instantiateGeneration=1·meshResolveGeneration ≥ 10·실패 0)이고 제품 stdout 토큰 재유입을 단정하며, dx12.scene의 `generation` 업로드 계수를 함께 센다. MBC9에서 legacy 축(legacyOnly/legacyParity/registryTextures)은 은퇴했고 `unbound`(UUIDv8인데 generation 없음) 0을 요구한다. |
| `verify-model-multifile-import.ps1` | 다중 파일 `.gltf` 임포트가 사이드카를 **폴더 구조째** 옮기는가. `IsAllowedImportExtension`은 `.gltf`를 허용했는데 `ImportSourceAsset`는 `destinationDirectory / source.filename()`로 파일 하나만 복사해서, 하위 폴더가 평탄화되고 `buffers[].uri`·`images[].uri`가 새 위치에서 풀리지 않았다 — 받아들이는 척하고 깨진 자산을 만들었고, 실패한 임포트가 평탄화 사본을 잔해로 남겼다(2026-09-14 실측). 임포터(fastgltf)는 멀쩡했다. 깨진 것은 복사 단계뿐이다. 재현체인 Khronos `TextureSettingsTest`는 `.gitignore`의 `/Dynamic_CPP/Assets/Models/*`에 막혀 **추적 밖**이라 핵심 축에 쓸 수 없다 — 저장소가 소유하는 최소 fixture(`fixtures/gltf-multifile/`: 쿼드 1 + 외부 `.bin` + **하위 폴더**의 PNG 1)를 쓰고, Khronos 표본은 있을 때만 제자리 임포트 축으로 덧붙이며 건너뛰면 요약에 적는다. 텍스처 해석은 cooked `model.cemc`가 외부 texture의 assetId 16바이트를 담는지로 잰다 — `assets.scenemodel`의 `generationTextures`는 **임베디드** 텍스처만 세므로 외부 URI는 그 축에 잡히지 않는다. `..` 탈출(퍼센트 인코딩 위장 포함)·`http://` 원격·없는 사이드카를 각각 거부하고 잔해 0을 확인하며, 단일 파일 `.glb`가 평면 그대로인지(회귀 없음)와 훑기/임포터의 fastgltf 확장 집합 일치도 함께 본다. ★ 탈출 케이스는 원본 폴더 밖에 **실재하는** PNG를 심어야 자극된다 — 심지 않으면 "탈출이라 거부"가 아니라 "없어서 거부"가 되어 경로 봉쇄를 걷어내는 변이를 못 잡는다. ★ 단정 헬퍼가 throw이면 앞선 한 축이 뒤의 전부를 가린다(변이에서 실제로 S1 하나가 S3·S4·S5를 지웠다) — 실패를 적고 넘어가는 헬퍼로 바꿨다. | **이빨 확인(2026-09-14)**: ① 사이드카 복사 줄 제거 → S1·S1b·정적 8건, ② 사이드카를 평탄화해 복사 → 7건(정적 토큰은 살아 있어 런타임 축이 단독으로 잡는다), ③ 사이드카 `.meta` 생성 제거 → 7건, ④ 훑기 확장 집합을 `Extensions::None` 으로 → S5 1건, ⑤ 되돌리기 제거 → S3b 1건(잔해 `.gltf`·`.bin`). 경로 봉쇄는 두 겹(Engine 훑기 `IsContainedRelative` + Editor `IsSafeRelativeAssetPath`)이라 **한 겹만 걷으면 둘 다 초록이다** — 둘을 동시에 걷어야 붉어진다(8건, `../Probe.png` 가 실제로 `Models/Probe.png` 로 탈출한다). 그 둘은 "못 잡았다"가 아니라 "그 축이 재는 성질이 여전히 참"인 경우다. ★ 되돌리기 변이는 처음에 **초록이었다** — 거부 넷이 전부 복사 **전에** 끊겨 자극조차 못 했다. 잘린 `.bin` 도 깨진 PNG 도 임포트가 성공해서(실측) 자연 입력으로는 복사 후 실패를 못 만든다. 그래서 목적지의 사이드카 자리를 폴더로 막는 S3b 를 따로 세웠다.
| `verify-entity-rename-name-registry.ps1` | `object.rename` 과 그 Undo/Redo 가 Scene 이름 등록부(`m_entityNameSet`)를 옮기는가. 옛 `RenameCommand` 는 `m_name.SetString` 만 불러 옛 이름이 영영 반환되지 않고(Prim_Cube 를 바꾼 뒤 다시 배치하면 "Prim_Cube (1)") 새 이름도 예약되지 않았다(2026-09-16 실측). 이제 `Scene::RenameEntity` 가 유일한 창구다. 이름은 명령 결과 `data.name` 으로만 읽는다. | **이빨 확인(2026-09-16)**: ① 수정 전 코드 → ①옛 이름 반환·②새 이름 예약·③Redo 3건, ② Undo 만 `SetString` 으로 → ④·⑤ 2건. ★ ④⑤ 는 수정 전 코드에서 **우연히 초록**이다(예약도 반환도 안 하므로 결과가 같다) — Undo 경로의 이빨은 수정 뒤 변이로만 선다. |
| `Export-MbcCorpusBaseline.ps1` | (게이트가 아니라 **기준선 export**) PHASE 3.75 MBC0. 모델 corpus 14건의 source SHA-256·sidecar GUID·subasset closure와 `.creator/.prefab/.asset` 28건의 GUID 참조를 키별로 분류(model / model-subasset / other-meta / type-or-instance / nil / unresolved)해 `mbc0_corpus_baseline.json`에 굳힌다. MBC4의 참조 rewrite와 MBC11의 "old GUID 0건" 판정이 이 파일을 입력으로 쓴다. `Dynamic_CPP/Assets` 대부분이 gitignore라 이 파일은 로컬 상태의 archive다 — 한 번 떠서 커밋하고 다시 뜨지 않는다. |
| `verify-prefab-identity-injection.ps1` | 프리팹 identity가 **워처 스레드와의 경합**을 견디는지. `verify-prefab-duplicate`가 2026-08-30에 한 번 실패하고 재현되지 않았는데, 원인은 초기 상태가 아니라 efsw 워처였다 — 원자적 게시(`.tmp` → replace)를 목적지 경로의 Delete로 오독한 `HandleDeleted`가 본문이 멀쩡한데도 catalog 항목과 sidecar를 떨어뜨렸다(정상 실행 한 판에 두 번, 각 ~26ms 실측). 그 창에 `prefab.update`가 걸리면 `LoadPrefab`이 살아 있는 identity를 널로 덮고 → `SavePrefab`이 새 GUID를 발급하고 → `UpdateInstances`가 그 키로 조회해 **조용히 0건 적용**한다. 에러도 로그도 없고 판정 1~4는 전부 통과해서, 우연에 맡기면 원인을 못 가른다. 그래서 창을 열어 놓고 sidecar를 **밖에서 확정적으로** 떨어뜨린다. **교란이 실제로 먹었는지를 먼저 단정한다**(창 진입·삭제·삭제 직후 부재) — 그게 없으면 "교란을 넣지 못한 실행"이 통과로 나와 대조군을 검사로 착각한다. 판정은 원인(인스턴스 guid == sidecar guid)과 결과(`m_shadowCast`가 false)를 함께 본다. 고치기 전 RED, 고친 뒤 GREEN을 확인하고 편입했다. |
| `verify-editor-component-browser.ps1` | 에디터의 "새 스크립트" 가 **소스 생성 → 실제 컴파일 → 재로드 → 원래 대상에 정확히 한 번 부착** 까지 가는가(Debug 에디터, `script.create`·`script.creation`). 잘못된 이름·중복 소스 거부, 같은 스크립트 두 인스턴스의 필드 값이 재로드 뒤 보존되는지, 컴파일 중 선택 변경·늦은 잠금·취소·대상 삭제가 부착을 막는지, 의도적 컴파일 오류 뒤 재시도까지 본다. 이 경로를 부르는 검사는 이것 하나다. ★ 소스 체크아웃 에디터는 스크립트를 체크아웃의 `GameScripts/GameScripts.csproj` 로 컴파일한다(에디터 빌드 후처리와 같은 정의 — 예제 `GameScripts/*.cs` + 프로젝트 `Assets/Script`). 배포본 안에서 뜬 에디터만 그 배포본의 `CreatorBuildTool compile-game` 을 쓴다. 9-13 배포 전환 뒤로 체크아웃 에디터도 `Bin/x64-*/engine.distribution.path` 가 가리키는 **발행 스냅샷**으로 컴파일해서 4 일간 붉었다 — 핀(`Engine.<Config>.lock.json`, 추적 밖)이 없어 거부됐고, 핀을 세워도 스냅샷의 낡은 `ScriptCore.dll` 로 에디터가 물고 있는 새 판을 덮으려다 `Access to the path is denied` 로 죽었으며, 그걸 피해도 `Assets/Script` 만 컴파일해 `Bobber` 가 사라져 "Some script instances could not be restored" 였다. 컴파일 실패 시 게이트가 컴파일 로그 끝 20 줄을 사유에 싣는다(전에는 "inspect the compilation log" 한 줄이라 원인이 게이트 밖에 있었다). | **이빨 확인(2026-09-17)**: ① 에디터 두 파일을 HEAD 판으로(발행 스냅샷 경유) → 첫 생성에서 붉음, 사유 "Project engine pin missing" 이 게이트 출력에 남는다. ② 체크아웃 `Assets/Script` 만 컴파일(중간 시도) → 재로드 뒤 `Bobber` 복원 실패로 붉음. 수정본 초록 `checks=251~261` — 수는 컴파일 시간에 따라 폴링 단정이 늘고 준다. |

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
copy "%VCToolsInstallDir%bin\Hostx64\x64\clang_rt.asan_dynamic-x86_64.dll" Bin\x64-Debug\Editor\
set ASAN_OPTIONS=detect_leaks=0:halt_on_error=1:abort_on_error=1
```

## pak 배포 위생 (SerializationPlan D1)

```powershell
pwsh Tools/regression/verify-pak-source-exclusion.ps1
pwsh Tools/regression/verify-player-runtime-hygiene.ps1
pwsh Tools/build.ps1 -Config Release -InputMode Project
```

`verify-pak-source-exclusion.ps1`은 **합성 트리로** `.cpp/.h/.hpp/.meta` 배제를 판정한다.
실제 pak 입력 루트에는 C++ 소스가 0개라서, 실자산만 재면 해당 필터가 있든 없든
"0개를 걸렀다"가 나온다.
그래서 오염된 트리를 일부러 만들어 필터를 밟는다. 그리고 **배제와 보존을 함께** 단정한다 —
`.hlsl`/`.hlsli`는 pak에 실려야 한다(Player가 런타임에 컴파일한다). 과잉 필터는 누락보다
위험하다.

pak은 결정적이지 않아(같은 입력 2회의 SHA-256이 다르다) 바이트 비교로는 내용을 단정할 수
없다. AssetPacker의 `--list-entries`가 내보내는 reopen된 목록을 대조한다.

`Tools/build.ps1` Release Project 검증은 그 정책을 실제 배포 경로에서 다시 닫는다.
manifest 투영과 reopen된 pak 목록을 전수 대조하고, 격리 Player stdout의
`source=cemf identities=N metaParsed=0`, cooked/source identity 수 일치, cooked scene 문서
1건 이상, unpacked `.meta` 0개와 `Assets/Derived/asset-manifest.cemf` 존재를 확인한 뒤에만
stage를 게시한다.

`verify-player-runtime-hygiene.ps1`은 바이너리 검사에 **대조군**을 둔다. "Player.exe에
efsw 문자열 0"만 보면 빈 파일을 읽어도 통과하므로, 같은 방법으로 CreatorEditor.exe를 재서
거기서는 반드시 검출되어야 한다고 함께 단정한다.

## 은퇴한 기준선·parity 관문 (2026-09-12)

직렬화 기준선(D0) · cooked document parity(D5-d) · ryml 에러 정책(D3-b-1) 관문은
PHASE 17 폐쇄와 함께 은퇴했다. 그것들이 부르던 `serialize.bench` ·
`serialize.rymlerror` · `experiment.matcook` · `experiment.scenecook` · `experiment.catalog` 이
함께 사라졌다 — 경위는 [은퇴 기록](../../docs/analysis/ClosedPlanCommandletRetirement.md).
패키지 경로 자체는 `pwsh Tools/build.ps1 -Config Release -InputMode Project -BuildNative`
로 그대로 돌린다.

## Player text parser 은퇴 (SerializationPlan D6)

```powershell
pwsh Tools/regression/verify-player-runtime-text-parser.ps1
```

`Tools/build.ps1`은 EngineSettings를 YAML 상태에서 먼저 preflight한 뒤 ProjectSetting
3개·InputMap 6개·BT/BlackBoard 2개·Volume 7개, 총 18개를 CEDO1으로 바꿔 pak에 넣는다.
scene 8·prefab 9·material 2·ShaderMeta 6의 GUID-addressed artifact 25개도 같은 포맷이다.
Player 스모크는 실제 text parser 진입 카운터가 정확히 0인지 강제하고, 위 독립 관문은
runtime 모듈 direct ryml include/symbol 0, CEDO magic 43/43, legacy JSON 0을 다시 확인한다.
구 Animator/NodeEditor JSON 31개는 reader를 되살리지 않고 pak 필터에서 제외한다.

## yaml-cpp 은퇴 + Base64 계약 (SerializationPlan D3-b-4)

이행 중에만 필요했던 scalar/adapter/backend 대조 게이트는 yaml-cpp와 함께
은퇴했다. 현재 정본은 두 개다.

- `verify-yaml-cpp-retirement.ps1` — Engine/Editor/Tools C++의 yaml-cpp include,
  backend symbol, namespace alias, transition escape가 0인지 검사하고 vcpkg manifest,
  runtime 배치 목록, 빌드된 Editor PE import까지 확인한다.
- `verify-authoring-base64.ps1` — DataSystem payload가 backend codec에 기대지 않는지
  확인하고 strict RFC4648 known vector·0~255 roundtrip·잘못된 padding 거부를
  Debug/Release에서 실행한다.

## JSON 트랙 은퇴 (SerializationPlan D4)

구버전 JSON 호환은 제품 계약이 아니다. 현재 자산을 canonical YAML로 직접 이주했고
제품 loader에는 migration reader나 dual-read를 두지 않는다. 정본 관문은 두 개다.

- `verify-inputmap-yaml-corpus.ps1` — `.inputmap` 6개를 strict schemaVersion 1 YAML로
  전수 로드해 26 actions/104 keys와 키보드·게임패드 분류를 확인한다.
- `verify-nlohmann-retirement.ps1` — Engine/Editor/Player source, vcpkg manifest와
  설치 상태, `ISerializable`, Animator legacy JSON entry point, InputMap `.json`,
  Terrain JSON/compat 경로가 모두 0인지 확인한다. Terrain 동적 왕복은
  `verify-asset-authoring-ownership.ps1`이 함께 담당한다.

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

직렬화 기준선은 소스 변이로 증명한다. `ComponentFactory::LoadComponent`의
`SERIALIZATION_PROFILE_SCOPE` 한 줄을 주석 처리하고 빌드하면
`selfcheck=fail reason=child-stage-zero-calls`(scene)와
`component-load-zero-calls`(prefab)가 나와야 한다. 실제로 이 변이를 처음 돌렸을 때
scene만 빨개지고 prefab은 통과했고, 그래서 prefab 쪽 분기가 추가됐다 — 변이를
안 돌렸으면 그 구멍은 초록 뒤에 남아 있었을 것이다.

## 아직 없는 것

렌더된 픽셀을 직접 대조하는 시각 회귀는 없다. 백버퍼 캡처를 붙이려 했으나
게임 스레드에서 `DirectX::CaptureTexture`가 죽어(0x0000087A) 보류했다 —
렌더 루프의 안전 지점에서 실행해야 한다. 그때까지 UI 렌더 좌표는 숫자
대조와 수식 동등성으로만 검증된다.
