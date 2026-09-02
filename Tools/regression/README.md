# UI 회귀 세트

에디터로 손수 확인하면 놓치는 것들을 기계로 잡기 위한 검사 묶음이다.
전부 종료 코드로 판정하므로 CI에 그대로 걸 수 있다.

## 전체 실행

```powershell
pwsh Tools/regression/run-all.ps1
```

빌드된 `Bin\x64-Debug\Editor\CreatorEditor.exe`가 필요하다. 다른 위치를 쓰려면 `-Exe`로 넘긴다.

## 개별 검사

전체 런타임 스위트는 `run-all.ps1`의 Run-Step 목록이 정본이다(현재 28종). 아래 표는
그 항목과 단계 전용 standalone gate 중 "왜 이렇게 재는가"를 기록해 둘 가치가 있는
검사만 담는다. 표의 standalone gate는 해당 단계에서 명시적으로 실행한다.

| 검사 | 무엇을 지키는가 |
|------|-----------------|
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
| `verify-reflection-golden.ps1` | 리플렉션 직렬화 출력이 변하지 않았는지(PHASE 18 CT0). `reflect.golden`이 등록 전 타입을 기본 생성해 직렬화한 덤프를 골든과 diff 0으로 대조한다 — 컴파일타임 전환(CT4~CT5) 구간에서 "필드가 조용히 빠지는" 회귀를 잡는 유일한 자다. 씬·프리팹 콘텐츠에 기대지 않으므로 GUID 같은 실행마다 다른 값이 안 섞인다. `perf.reflect` 수치(씬 Serialize·InstantiatePrefab)는 기록만 하고 판정하지 않는다 — 시간에 문턱을 걸면 머신 편차로 거짓 실패가 나 검사가 신뢰를 잃는다. 골든이 없으면 `run-all`이 건너뛴다(`-Baseline`으로 생성). |
| `verify-bt-smoke.ps1` | 행동 트리가 **실제로 도는지**(PHASE 9-8). 이 세트의 나머지는 BT를 한 줄도 실행하지 않는다 — BT 컴포넌트는 프리팹에만 붙어 있고 다른 시나리오가 여는 씬에는 없다. 게다가 트리 생성·틱은 실패할 때만 로그를 남겨(성공은 무음) "트리가 안 서서 AI가 가만히 있다"와 "정상"이 로그에서 같아 보인다. 그래서 `bt.status`로 수를 센다: 소환 전 0개 → 소환 후 3개 → 재생 중 틱 증가 → 씬 교체 후 0개. 경계 불변식(프레임당 크로싱 ≤ 1회, 크로싱당 전달 틱 > 1)도 여기서 수치로 못 박는다. **게임 콘텐츠에 기대지 않는다** — 전용 노드(`GameScripts/BTProbeNodes.cs`)와 전용 그래프(`BTProbe.bt`/`.blackboard`/`.prefab`)를 쓴다. 게임 프리팹을 쓰면 콘텐츠가 바뀔 때마다 흔들리고, 엔진 경로를 재는 검사가 콘텐츠 회귀로 오해되기 시작하면 아무도 믿지 않게 된다. |
| `verify-asset-authoring-ownership.ps1` | E2의 asset writer 경계를 정적·동적으로 함께 고정한다. `ModelLoader`/`Terrain`에 filesystem writer가 재유입되지 않았는지 검사하고, 고유 GLB를 두 번 import해 Editor가 model cache와 embedded PNG를 처음 한 번만 게시하는지 확인한다. Terrain은 height/splat/texture를 임시 세대에 완성한 뒤 descriptor를 마지막에 게시하며, 실패 요청이 기존 descriptor·세대를 바꾸지 않는지도 검사한다. 같은 Editor 세션의 model reimport, Player writer 부재, `.tmp`·probe 잔여 검사도 함께 수행한다. |
| `verify-asset-runtime-change-boundary.ps1` | E2의 Editor→Runtime asset 변경 계약을 고정한다. `DataSystem`의 public catalog mutation primitive 재노출을 막고 `CatalogUpsert`/`ContentReload`/`Removed` 단일 계약, 이전 cache generation pin, Editor 게시 완료 후 발행, Player 생산자 부재를 검사한다. |
| `verify-asset-presentation-boundary.ps1` | E2의 picker/icon/font 경계를 고정한다. `DataSystem`에 ImGui·파일/gizmo 아이콘·폰트·material 전달 상태가 재유입되지 않는지, `EditorAssetPresentation`이 두 selector와 표시 리소스를 소유하는지, gizmo texture가 `ScriptBinder`의 Editor 역참조가 아니라 프레임 packet의 공유 수명 입력으로 전달되는지, Player가 presentation을 설치하지 않는지 검사한다. |
| `verify-mbc-cutover-freeze.ps1` | PHASE 3.75(모델 자산 빅뱅 전환) **변경 동결 래칫**(정적). `ModelAssetBigBangCutoverPlan §5.2`가 제품에서 제거하기로 한 표면 — 역브리지(`BuildLegacyModelFromExperiment`·`ModelSceneBridge`), A/B 스위치(`CREATOR_EXPERIMENT_VERTEX`), 병행 상태(`m_experimentMeshBindings`·`m_hashingMesh`), Assimp include·vcpkg port, pseudo-v5(`DeterministicSubAssetId`·`Uuid::FromName`), 무조건 진단 출력 — 의 코드 접촉 수(주석 제거 뒤)를 `mbc_cutover_freeze.baseline.tsv`와 대조해 **증가만 막는다**(감소는 그 슬라이스가 `-Baseline`으로 내려 고정). 하드 계약 셋은 래칫이 아니다: model sidecar writer는 허용목록(EditorAssetDatabase·ModelIdentityRefresher) 밖에 생기면 즉시 실패, 검사 전용 seam(`DeriveIdentityWithProfile`·`InsertUncheckedForTest`)은 `Assets/` 정의 밖 0건, 새 `Assets/` 계층 안에 legacy 신원 API(`FromName`·`IsAssetIdV4`·`CreateRandomV4`) 0건. 동결의 위반은 그림을 바꾸지 않으므로(폴백을 한 겹 더 붙이면 오히려 "고쳐진" 것처럼 보인다 — 2026-09-02 MeshRenderer 순서 해킹이 그랬다) 축은 픽셀이 아니라 접촉 수다. |
| `verify-asset-identity.ps1` | PHASE 3.75 MBC1 자산 신원 프로필 `ce.uuidv8.sha256.v1`. **세 갈래 독립 유도**가 같은 값을 내야 통과한다: ① 제품 C++(`assets.identity` selftest — FIPS 180-4 KAT 5종, BCrypt `ComputeSha256`과 44개 버퍼 대조, 벡터 15건의 입력 바이트열·SHA·UUID, fail-closed 4+6+2종, legacy v4/v5/pseudo-v5 표기 거부, registry Registered/DuplicateTuple/UuidCollision/RecomputeMismatch), ② Python hashlib(`Generate-AssetIdentityVectors.py`가 낸 `asset_identity_vectors.json`), ③ .NET SHA256(이 스크립트가 §2.2 바이트 계약을 **다시 조립**해 계산). ①=②만 보면 같은 (틀린) 규약을 공유한 눈먼 초록이 가능해(experiment.anim D4e-1) ③을 둔다. 변이는 벡터 안에 있다 — 프로필 문자열 한 글자(`v0`) 변이 벡터가 원본과 달라야 하고, 길이 접두 없이 같은 바이트열이 되는 `ab\|c` vs `a\|bc` 쌍이 달라야 한다. 단정 수 150 미만이면 검사 범위 축소로 실패. |
| `Export-MbcCorpusBaseline.ps1` | (게이트가 아니라 **기준선 export**) PHASE 3.75 MBC0. 모델 corpus 14건의 source SHA-256·sidecar GUID·subasset closure와 `.creator/.prefab/.asset` 28건의 GUID 참조를 키별로 분류(model / model-subasset / other-meta / type-or-instance / nil / unresolved)해 `mbc0_corpus_baseline.json`에 굳힌다. MBC4의 참조 rewrite와 MBC11의 "old GUID 0건" 판정이 이 파일을 입력으로 쓴다. `Dynamic_CPP/Assets` 대부분이 gitignore라 이 파일은 로컬 상태의 archive다 — 한 번 떠서 커밋하고 다시 뜨지 않는다. |
| `verify-prefab-identity-injection.ps1` | 프리팹 identity가 **워처 스레드와의 경합**을 견디는지. `verify-prefab-duplicate`가 2026-08-30에 한 번 실패하고 재현되지 않았는데, 원인은 초기 상태가 아니라 efsw 워처였다 — 원자적 게시(`.tmp` → replace)를 목적지 경로의 Delete로 오독한 `HandleDeleted`가 본문이 멀쩡한데도 catalog 항목과 sidecar를 떨어뜨렸다(정상 실행 한 판에 두 번, 각 ~26ms 실측). 그 창에 `prefab.update`가 걸리면 `LoadPrefab`이 살아 있는 identity를 널로 덮고 → `SavePrefab`이 새 GUID를 발급하고 → `UpdateInstances`가 그 키로 조회해 **조용히 0건 적용**한다. 에러도 로그도 없고 판정 1~4는 전부 통과해서, 우연에 맡기면 원인을 못 가른다. 그래서 창을 열어 놓고 sidecar를 **밖에서 확정적으로** 떨어뜨린다. **교란이 실제로 먹었는지를 먼저 단정한다**(창 진입·삭제·삭제 직후 부재) — 그게 없으면 "교란을 넣지 못한 실행"이 통과로 나와 대조군을 검사로 착각한다. 판정은 원인(인스턴스 guid == sidecar guid)과 결과(`m_shadowCast`가 false)를 함께 본다. 고치기 전 RED, 고친 뒤 GREEN을 확인하고 편입했다. |

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
```

`verify-pak-source-exclusion.ps1`은 **합성 트리로만** 판정한다. 실제 pak 입력 루트에는
`.cpp/.h/.hpp`가 0개라서, 실자산으로 재면 필터가 있든 없든 "0개를 걸렀다"가 나온다.
그래서 오염된 트리를 일부러 만들어 필터를 밟는다. 그리고 **배제와 보존을 함께** 단정한다 —
`.hlsl`/`.hlsli`는 pak에 실려야 한다(Player가 런타임에 컴파일한다). 과잉 필터는 누락보다
위험하다.

pak은 결정적이지 않아(같은 입력 2회의 SHA-256이 다르다) 바이트 비교로는 내용을 단정할 수
없다. AssetPacker의 `--list-entries`가 내보내는 reopen된 목록을 대조한다.

`verify-player-runtime-hygiene.ps1`은 바이너리 검사에 **대조군**을 둔다. "Player.exe에
efsw 문자열 0"만 보면 빈 파일을 읽어도 통과하므로, 같은 방법으로 CreatorEditor.exe를 재서
거기서는 반드시 검출되어야 한다고 함께 단정한다.

## 직렬화 기준선 (SerializationPlan D0) — 이 검사만 Release를 쓴다

```powershell
pwsh Tools/regression/verify-serialization-baseline.ps1 -Baseline
```

`verify-serialization-baseline.ps1`은 세트에서 **유일하게 Release exe를 요구**한다.
Debug는 같은 워크로드에서 단계별로 4~16배 느린 데다 **비중까지 뒤집는다** — 실측
배율이 SceneParse 15.9배, ComponentLoad 5.5배라 Debug로 보면 파싱의 몫을 67%로,
Release로 보면 58%로 읽게 된다. 그래서 Release가 없을 때 Debug로 **대체하지 않고**
실패하며, `run-all.ps1`은 Release 부재 시 이 항목만 건너뛰고 그 사실을 출력한다.

측정은 벤치가 재현한 모형이 아니라 제품 로드 경로 안에서 이뤄진다
(`Engine/Utility_Framework/SerializationProfiler.h`의 Scope가 `SceneManager`·
`ComponentFactory`·`PrefabUtility`·`DataSystem` 본체에 들어 있다). 계측 플래그는
기본 꺼짐이고 `serialize.bench`가 켰다가 되돌린다.

`-Baseline`을 주면 계획서에 옮길 표 형식으로 전체 수치를 찍는다.

## ryml 에러 정책 (SerializationPlan D3-b-1)

`verify-ryml-error-policy.ps1` — ryml의 기본 에러 처리는 예외도 반환값도 아니라
**프로세스 abort**다. 파서를 제품 경로에 넣기 전에 그 abort를 예외로 바꾸는 정책이
설치돼 있어야 한다.

**이 검사의 이빨은 종료 코드가 아니라 크래시다.** ryml 0.16은 에러 콜백을
`m_error_basic`/`m_error_parse`/`m_error_visit` 셋으로 나눈다. 하나만 빠져도 이
명령은 "fail"을 찍는 것이 아니라 프로세스가 그 자리에서 죽는다 — 변이 2회로
확인했다(basic 제거 → exit 3, parse 제거 → exit 3, 둘 다 게이트가 잡음).
그래서 검사는 요약 라인의 **존재**와 종료 코드를 함께 본다.

트리거는 지어내지 말고 재야 한다. 처음 쓴 재현("CRLF", "멀티라인 스칼라 키")을
**ryml이 둘 다 조용히 받아들였다** — 게이트가 초록인데 아무것도 증명하지 않는
상태였다. 14종을 태워 확인한 실제 트리거는 **홀로 선 CR**(basic)과
**탭 들여쓰기**(parse)다. **CRLF는 정상 파싱되므로 오히려 통과해야 하는 대조군**이고,
이것이 깨지면 파싱 전 정규화 사본이 다시 필요해져 D3-b의 성능 계산이 바뀐다.

## 스칼라 변환 파리티 (SerializationPlan D3-b-2)

`verify-scalar-conversion-parity.ps1` — 파서 동등성이 증명하지 못하는 축이다.
두 파서가 만든 트리가 구조적으로 같아도 `as<bool>`이 `"yes"`를 다르게 읽으면
**값의 의미만 조용히 달라진다.** 로드는 성공하고 값만 틀린다.

**두 축을 잰다.** 이식 변환기(`Authoring::Scalar`) 대 yaml-cpp는 **차이 0이어야
한다** — 그것이 "backend가 바뀌어도 값의 의미가 그대로"의 정의다. 실제로 이 단정이
이식 오류 3건을 잡았다(부호 없는 정수가 음수를 받아들임, 널 노드의 문자열 표현 2건).

ryml 대 yaml-cpp는 다르다 — **"차이 0"을 단정하지 않는다.** 67케이스 중 21건이
실제로 갈린다. 차이를 없애는 것은 구현의 일(D3-b-2b-1a가 변환을 문자열 위로 옮겼다)
이고, 검사의 일은 **알려진 목록을 고정**해 새 차이만 빨개지게 하는 것이다.
**목록보다 적어도 실패다** — 차이가 사라졌다면 누가 변환을 바꾼 것이고, 조용히
넘기면 표가 낡는다.

★ **가장 위험한 것은 실패가 아니라 "둘 다 성공하는데 값이 다른" 쪽이다.**
`010`은 yaml-cpp에서 8(8진), ryml에서 10(10진)이다. `1.5x`는 yaml-cpp가 거부하고
ryml이 1.5로 읽는다. 오버플로는 ryml이 쓰레기 값을 낸다. 그래서 ryml의
`from_chars`를 직접 쓰면 안 된다.

갈리는 것과 위험한 것은 다르다. 차이가 손상이 되려면 코퍼스에 그 표기가 있어야
하므로, 같은 게이트가 자산 281개(`Assets` + `ProjectSetting`)를 스캔해 `.inf`/`.nan` 0건과 YAML 1.1 불리언
기준선 2건을 함께 단정한다.

## backend 경계 래칫 (SerializationPlan D3-b-2b-1b)

`verify-authoring-backend-boundary.ps1` — 읽기 경로를 ryml로 옮기는 일은 한 번에
못 한다. 그래서 아직 backend 노드를 만지는 자리마다 **이름이 흉한 탈출구**를 두었다
(`BackendNodeDuringTransition`). 개수가 곧 진행률이다.

**이 게이트가 막는 것은 실패가 아니라 역행이다.** 새 코드가 어댑터 대신 backend
노드를 직접 잡으면 빌드도 다른 게이트도 전부 통과하므로 아무도 모른다. 기준선보다
늘면 실패하고, 줄면 기준선을 갱신하라고 말하며(숫자가 낡으면 래칫이 풀린다),
**0이 되면 이 게이트를 은퇴시키라고 말한다.**

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
