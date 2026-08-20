# UI 시스템 전면 재설계 — UIManager 해체와 "uGUI 저작 · Slate 런타임" 하이브리드 (PHASE 16)

작성: 2026-08-16 · **전면 재작성: 2026-08-19** · 계기(원안): UIManager와 관련 구조의 전면
재설계 요청. 레퍼런스는 유니티·언리얼 양쪽을 조사해 장점을 접목하기로 했다.
**기존 구조 재활용은 요구사항이 아니다**(사용자 명시). 따라서 아래 설계에서 무언가를
승계하는 항목은 전부 "다시 만들 이유가 없어서"이지 "호환 때문"이 아니다 — 승계 여부는
항목마다 실측 근거로 판정했다.

방법(원안): 병렬 조사 13기 — 현 시스템 7개 축 실측(매니저·캔버스/레이아웃·위젯·렌더·
입력·게임 소비자·에디터/직렬화), Unity(UGUI + UI Toolkit)·Unreal(UMG + Slate) 이식성
조사 2건, 독립 설계안 3종(uGUI 충실 계승 / Slate 위젯트리 / 하이브리드) 생성 후 심판 채점.
결과: **하이브리드 83점 / uGUI 74점 / Slate 66점 → 하이브리드 채택**, 단 심판 교정 3건과
탈락안 접목 5건 반영.

관련 문서: `SceneGraphRedesignPlan.md`(트랙 L·C·S·K·E·P — 이 문서가 딛고 선 엔진 기반),
`RefactoringPlanDashboard.html`(Phase 7 — 3중 장부가 크래시 3건의 근본 원인이라는 선행
실측), `RhiBoundaryPlan.md`(멀티 RHI 중립 제약 — DX11이 은퇴하고 DX12·Vulkan 둘만 남았다),
`BuildPipelinePlan.md`(트랙·게이트 관례 승계), `SerializationPlan.md`(U7 변환 도구의 형식 제약).

---

## 개정 이력 — 2026-08-19, 무엇을 왜 고쳤나

원안은 2026-08-16 작성이다. 그 사이 `SceneGraphRedesignPlan`의 트랙 **L**(생명주기 6단계)·
**C**(시스템 스케줄)·**S**(스토어·Transform 컴포넌트화)·**K**(컴포넌트 UUID)·**E**(Entity
정체성)가 착지하면서 원안이 딛고 선 엔진 기반이 통째로 바뀌었다. 그대로 U1 이후에
착수하면 **이미 사라진 축 위에 설계가 서게 된다.**

**전략은 그대로 둔다.** "저작 표면 = uGUI 형상 / 런타임 내부 = Slate 실행 모델"의 하이브리드
채택, 별도 위젯 트리 금지, 프록시 브리지 무변경 — 이 셋은 이번 재확인에서도 근거가 유지됐고
(§2), 바뀐 것은 그 전략을 얹을 바닥이다. 아래 표의 "고친 것"은 전부 *바닥에 맞춰 다시
그린 것*이지 전략 변경이 아니다.

### A. 무효화된 전제 (착수 전 표에 있던 5건)

| 원안의 서술 | 2026-08-19 실측 | 이 재작성에서 한 일 |
|---|---|---|
| `UIElement`가 `Awake`에서 rect 캐시·캔버스 연결(§3.1, §4 U1) | `Awake`/`Start`/`OnDestroy`가 **없다**. L3이 레거시 훅 3종과 브리지를 철거했다. 지금 축은 6단계 + 직교하는 `OnEnable`/`OnDisable`이다 (`ScriptBinder/Component.h`) | §3.0을 신설해 **훅 배정 규칙**부터 세웠다. DDOL 재부착 시 어느 훅이 다시 불리는지를 실측으로 확정하고(아래 B-1), 등록·해지·연결을 각각 다른 훅에 배정했다. rect 캐시는 **폐기**한다(B-2) |
| 위젯이 매 프레임 스스로 도는 것을 전제한 틱 설계 | 틱 축이 `Component`에 **없다**. `Update`/`LateUpdate`/`FixedUpdate` 가상이 전부 사라졌고 `UITickSystem`이 SpriteSheet·Text·UIButton·Canvas·Image 5종을 조밀 배열로 전담한다 (`ScriptBinder/UITickSystem.h`) | §3.2·§3.4를 "시스템 **안에서** dirty를 본다"로 다시 썼다. 새 위젯을 UITickSystem에 더하지 않고, 현행 5종의 조밀 순회에 early-out을 거는 방향으로 뒤집었다 |
| "UIButton은 `Awake`/`OnDestroy`가 없어 등록 자체가 안 됨"(MEDIUM) | **해소.** C3에서 `UIButton::OnAddedToScene`/`OnRemovingFromScene`이 `UITickSystem`에 등록·해지한다 (`ScriptBinder/UIButton.cpp`) | 결함 목록에서 빼고 §1.1에 경위를 기록했다. 다만 **다른 결함이 그 자리에 남아 있다** — 버튼이 *캔버스*에 연결되지 않는 경로(§1.3 H-2) |
| UI 판정에 `GameObjectType::UI` · `Scene.cpp:1806-1813` 인용 | 그 분기는 **없다**. S3로 UI는 `Transform` 없이 `RectTransformComponent`만 갖고 **Canvas만 둘 다** 갖는다 (`GameObject::AttachSpatialComponent`). 순회는 `Scene::UpdateModelRecursive`의 `HasTransform()`으로 가르고 저장된 enum을 안 본다(E7-a) | 인용을 전부 심볼 기준으로 갈아 끼웠다. Canvas 예외가 **월드 공간 캔버스를 살리기 위한 것**이라는 사실이 §3.4·§1.3 H-1의 새 근거가 됐다 |
| U7의 YAML 변환 도구가 "타입명 치환" 기반 | 이름은 정본이 아니다. K1-b가 컴포넌트 영속 UUID(`m_typeUUID`)를 세웠고, `Meta::ExtractTypeFromYAML`이 UUID를 **최우선**으로 본다 | U7의 Navigation만 별도 슬라이스로 재설계했다. 이후 저작 자산 215개가 폐기되어 과거의 218파일 UUID 각인·일괄 변환 전제도 소멸했다. 구 Navigation은 런타임 로더가 메모리 안에서 승격한다 |

### B. 재작성 중 새로 드러난 것 (전부 이번에 직접 확인)

1. **DDOL 훅 비대칭이 등록 설계의 축이다.** `Scene::RegistryDrainAwakeAndStart`가
   `OnInitialized`/`OnBeginSimulation`을 `State_AwakeCalled`/`State_StartCalled` 비트로
   게이트해 **컴포넌트 평생 1회**만 부른다. 반면 `OnAddedToScene`/`OnRemovingFromScene`은
   게이트가 없고 `Scene::AttachExistingGameObject`/`DetachGameObjectHierarchy`가 DDOL 이송
   때마다 **직접** 부른다. 즉 씬 스코프 등록부는 후자에, 프로세스 스코프 등록부는 전자에
   걸어야 한다 — 이 규칙을 §3.0으로 명문화했다.
2. **"rect를 캐시한다"는 원안의 최적화 전제가 약해졌다.** ① K2가 `GameObject::FindComponent`를
   "타입 마스크 선판정 + 소배열 선형 탐색"으로 바꿔 조회 자체가 싸졌고(오브젝트당 컴포넌트
   0~6개), ② `PrefabUtility::ApplyComponentDiff`가 컴포넌트를 그 자리에서 파괴하고 같은
   타입을 다시 채우는 경로라 raw 포인터 캐시는 댕글링 위험을 진다. 실제로 현행
   `UIButton::UpdateCollider`·`ImageComponent::TickLayout`이 매 틱 재조회하고 있고 그것이
   문제로 잡힌 적이 없다. → **캐시하지 않는다**로 뒤집었다(§3.1).
3. **U7의 실제 규모가 측정됐고, 원안이 걱정하던 것보다 훨씬 작다.** 저작 자산 실측:
   `navObject`가 비어 있지 않은 Navigation 링크는 **39개, 전부 프리팹 안, 5개 파일**
   (MenuSettingCanvas 8 · UI_CanvasesBoss 12 · UI_CanvasesIngame 12 · SettingCanvas 4 ·
   PauseBox 3). 그리고 **39/39가 같은 파일 안의 `m_instanceID`로 해소된다** — 파일을 건너뛰는
   참조가 하나도 없다. 즉 "프리팹-로컬 인덱스 재계산"은 닫힌 문제다. 반대로 **`m_typeUUID`를
   실은 자산은 0/218**이라(프리팹 206 · 씬 12) UUID 기반 도구는 *먼저 전 자산을 한 번
   재저장해 UUID를 각인*하지 않으면 아무것도 못 찾는다. U7의 단계 순서를 그렇게 고쳤다.
4. **U0.5(WorkerPools 레이스 검증 스파이크)는 답이 나왔다 — 레이스 없음.** `Scene::UpdateRenderData`의
   UI 푸시 3개 태스크는 카메라 루프 끝의 `WorkerPools->NotifyAllAndWait()`가 조인하고
   (`ThreadPool::NotifyAllAndWait`은 태스크 카운트 0까지 이벤트 대기), 공유 버퍼는
   `RenderPassData::FrameUIProxyIDs = concurrent_vector<HashedGuid>`다. 컴포넌트 실파괴는
   프레임 끝 `SceneManager::DisableOrEnable` → `Scene::OnDestroy` → `FlushPendingDestroy`이고
   그것은 `LateUpdate`(=`UpdateRenderData`)보다 **뒤**다. → **U0.5 슬라이스를 폐지**하고
   판정 근거를 §1.1에 남겼다.
5. **`CanvasRenderMode`는 배선이 끝났는데 저작 표면이 없다.** `Canvas`의 `[[Property]]`
   8필드 중 인스펙터가 그리는 것은 `CanvasName`·`CanvasOrder` **둘뿐**이다
   (`InspectorWindow::ImGuiDrawHelperCanvas` — 손짠 커스텀 드로어가 제네릭 드로어를
   대체하므로 나머지 6필드가 통째로 가려진다). 저작 자산에서 `RenderMode:` 키가
   나오는 곳은 **0곳**이다. 즉 3모드 중 2모드는 코드·콘솔로만 켤 수 있다 — 원안이 §1의
   C4에서 "배선 완료 ≠ 화면에 나옴"이라 부른 함정의 저작 측 쌍둥이다. §1.3 H-1로 올리고
   **U0에서 지혈**한다(이후 모든 3모드 픽셀 회귀의 선행이라서).
6. **게임 소비자의 정체가 바뀌었다.** 원안이 센 "UI 스크립트 13개 1,747줄"·"World→Screen
   재구현 7파일"·"위치 기반 자식 바인딩 5파일"은 전부 `Dynamic_CPP/Assets/Script/*.cpp`이고,
   그 폴더는 **컴파일 대상이 아니다** — 솔루션의 7개 프로젝트 어디에도 없고 `.vcxproj`도
   없다(C++ 핫리로드 은퇴 후 남은 데이터 보존 폴더). 살아 있는 소비자는 `GameScripts/`의
   C# 5파일 517줄(`RectLayoutProbe`·`TutorialUI`·`UiNavProbe`·`UiProbe`·`UiTextProbe`)과
   `ClrHost`의 UI 네이티브 API 표면이다. §6(이식 비용)과 U7을 그 기준으로 다시 썼다.
7. **DX11이 은퇴했다.** `RenderBackend`는 `DX12`·`Vulkan` 둘뿐이다(`Utility_Framework/EngineSetting.h`).
   원안 U0의 완료 조건에 있던 "DX11 UI 지원 여부 결정"은 이미 결정돼 있다 — **삭제**하고,
   대신 "UI 경로는 두 백엔드 모두에 서야 한다"를 제약으로 승격했다(`LivePipeline`·
   `VulkanLivePipeline` 둘 다 `EnhancedUIPass ui` 멤버를 갖는다).
8. **Create/Update 필드 드리프트가 1건이 아니라 4건이다.** 원안은 `TextComponent::filpEffect`
   하나를 들었는데, 재확인 결과 ⑴ Text `filpEffect` 누락, ⑵ SpriteSheet `filpEffect` 누락,
   ⑶ Text `fontSize` — Create는 `fontSize * layoutScale`, Update는 `fontSize`만,
   ⑷ `m_isEnabled` — Create 경로는 Image에서만 세우고 Text/SpriteSheet에서는 아예 안 세운다.
   §3.4의 "생산 단일화"가 고치는 대상이 넷으로 늘었고, U2의 회귀 항목도 그만큼 넓어졌다.
9. **회귀 세트가 왜 C1을 못 잡는지가 확인됐다.** §1.6에 별도로 적었다 — 검증 항목을
   쓸 때마다 이 사각을 먼저 봐야 한다.

### C. 유지한 것

전략 판정 전부(§0·§2), 프록시 브리지 무변경, WidgetRegistry 단일 진본, InputRouter의
FReply 소비 모델, dirty-root 부분 순회, clipRect discard 클리핑, UIManager 해체 표(§3.5).
슬라이스 뼈대 U0~U7도 유지하되 U0.5를 폐지하고 각 슬라이스의 선행 조건을 다시 배열했다.

---

## 0. 전략 요약

**경계는 "누가 만지는가"로 긋는다.**

- **저작 표면 = uGUI 형상.** 디자이너·게임 스크립트가 만지는 것(GameObject + RectTransform +
  Canvas + 위젯 컴포넌트, `[[Property]]` 직렬화)은 uGUI 모델을 따른다. 재확인 결과 이 엔진의
  RectTransform(앵커/피벗/sizeDelta/16종 프리셋, y-down 규약)과 `Canvas::ComputeScaleFactor`
  (로그 공간 보간까지 uGUI와 동일), CanvasRenderMode 3종은 **이미 uGUI와 동형으로 수렴해
  있다** — 재발명은 손해다. (단 3모드 중 2모드는 저작 표면이 없다 → §1.3 H-1.)
- **런타임 내부 = Slate 실행 모델.** 값이 바뀐 위젯만 dirty로 자신을 알리고(전량 순회 폐지),
  draw 데이터 생산은 단일 함수로 통일해 프록시 커맨드로 발행하며, 입력은 정렬된 히트테스트
  + leaf→root 버블/소비(FReply) 모델로 재편한다.
- **프록시 브리지는 무변경.** 이 엔진의 ProxyCommand 델타 발행 모델은 Slate의 draw-element
  스냅샷 철학과 이미 구조적으로 같다. Slate의 "매 프레임 위젯 트리 재순회 + 서브트리 캐시"
  실행 모델을 통째로 이식하면 오히려 델타 발행의 이점을 깎아먹는다. 가져올 것은 게임 스레드
  쪽(2-pass 레이아웃, 입력 버블링, 인밸리데이션 아이디어)뿐이다.
- **Slate식 별도 위젯 트리는 만들지 않는다.** 이 엔진은 UI가 GameObject와 1:1이고 공간
  컴포넌트가 생성 시점에 자동 부착된다(`GameObject::AttachSpatialComponent` — UI는
  RectTransform만, Canvas는 RectTransform + Transform 둘 다). Unreal에서 UWidget↔SWidget
  분리를 정당화하는 조건(액터와 독립된 위젯 수명)이 여기엔 없다 — 별도 트리를 만들면
  "듀얼 트리 동기화 누락"이라는 새 버그 클래스만 산다(Slate안 탈락 사유).
- **컴포넌트는 스스로 돌지 않는다(신규 전제).** 트랙 C3이 `Component`에서 틱 축을 걷어냈다.
  "무엇이 언제 도는가"는 이제 시스템과 `Scene`의 호출 순서가 전부 말해 준다. 따라서 이
  재설계의 dirty·인밸리데이션은 **위젯이 자기를 부르는 방식이 아니라, 시스템이 조밀 배열을
  돌며 dirty를 보고 건너뛰는 방식**으로 구현된다(§3.2).
- **순서: 지혈(U0) → 구조 재편.** 실측된 CRITICAL을 먼저 죽여야 이후 회귀를 "재설계 탓"과
  "원래 버그 탓"으로 구분할 수 있다. (원안의 U0.5 스파이크는 답이 나와 폐지 — §1.1.)

---

## 1. 실측 — 2026-08-19 전수 재확인

원안(2026-08-16)의 항목을 하나도 그대로 옮기지 않고 전부 그 자리를 다시 열어 확인했다.
줄 번호는 적지 않는다 — 원안의 인용 상당수가 8-16 이후 편집으로 무효화됐고, 같은 함정을
재생산하지 않기 위해 **함수·심볼 이름으로 가리킨다**. 위치가 필요하면 심볼로 grep한다.

### 1.1 해소 확인 — 원안의 결함 중 이미 죽은 것

| 원안 | 지금 | 경위 |
|---|---|---|
| **C2** navigations가 로드마다 2배로 불어난다 | **해소** | PHASE 18 CT6-d가 `ComponentFactory::LoadComponent`의 타입별 하드코딩 분기 17개를 걷어내고 역직렬화를 typed 디스패치로, 후처리를 각 컴포넌트의 `OnDeserialized`로 옮겼다. 그 과정에서 navigations 수동 복원 루프를 **의도적으로 미이식**했다 — `ImageComponent::OnDeserialized`·`TextComponent::OnDeserialized`·`ComponentFactory.cpp`의 주석 셋이 같은 판정을 남겨 뒀다: "반영 멤버라 typed가 채우며, 그 루프는 레거시 벡터 경로의 침묵 실패(`MakeAnyFromRaw`에 vector 캐스터 부재)가 가리던 이중 적재였다." 즉 **다른 트랙의 부수 효과로 죽었고, 죽은 이유가 기록돼 있다** |
| **MEDIUM** "UIButton은 `Awake`/`OnDestroy`가 없어 등록 자체가 안 됨" | **해소** | 트랙 C·C3(레인 UI)이 `UIButton::TickInteraction`을 만들고 `OnAddedToScene`/`OnRemovingFromScene`에서 `UITickSystem::RegisterButton`/`UnregisterButton`을 부른다. **`Awake`가 아니라 6단계 훅을 고른 근거가 그 자리에 적혀 있다** — DDOL 오브젝트가 씬을 건널 때 `Awake`(=`OnInitialized`)는 `State_AwakeCalled` 비트로 평생 1회라 다시 안 불리고, 그러면 새 씬에서 영원히 틱을 못 받는다. 이 판정이 §3.0의 규칙이 됐다 |
| **U0.5** WorkerPools 레이스 검증 스파이크(원안이 "미확인"으로 남긴 유일한 스레드 안전성 항목) | **레이스 없음 — 스파이크 폐지** | `Scene::UpdateRenderData`의 UI 푸시 3태스크는 카메라 루프 끝의 `WorkerPools->NotifyAllAndWait()`가 조인한다(`ThreadPool::NotifyAllAndWait`이 `m_taskCounts`가 0이 될 때까지 이벤트 대기 — 큐 배출이 아니라 완료 대기다). 공유 목적지 `RenderPassData::m_findUIProxyVec`는 `concurrent_vector<HashedGuid>`라 동시 push_back이 안전하다. 컴포넌트 실파괴는 `SceneManager::DisableOrEnable` → `Scene::OnDestroy` → `Scene::FlushPendingDestroy`이고, 프레임 순서상 `LateUpdate`(=`UpdateRenderData`)보다 **뒤**다. **판정 없이 U1 착수 금지라던 조건은 충족됐다** |

> ★ 이 셋을 지우고 나면 원안의 CRITICAL 5건 중 **셋이 남는다**(C1·C3·C4). C5는 여전히
> 휴면이고, 그 대신 이번에 HIGH 3건이 새로 올라왔다(§1.3 H-1·H-2·H-3).

### 1.2 남은 CRITICAL

| # | 결함 | 근거(심볼) |
|---|---|---|
| **C1** | **마우스 클릭 히트테스트가 항상 실패한다.** `UIButton::CheckClick`이 읽는 `InputManager::m_gameViewPos`/`m_gameViewSize`는 **리포 전체에서 선언 2줄과 이 읽기 2줄이 전부**다 — 대입이 0회라 항상 (0,0). `screenSize.x / gameViewSize.x`가 0-나누기로 Inf/NaN이 되어 판정이 사실상 항상 false가 된다. `GameViewWindow::RenderGameViewWindow`는 Game 패널 rect를 매 프레임 계산하지만 아무 데도 기록하지 않는다 | `ScriptBinder/UIButton.cpp`(`CheckClick`), `ScriptBinder/InputManager.h`, `EngineGUIWindow/GameViewWindow.cpp` |
| **C3** | **해소(2026-08-21).** `Navigation::navObject`의 전역 instanceID 참조를 소스 UI 기준 계층 경로(`parentHops` + `childOrdinals`)로 바꿨다. 프리팹 UI 예외가 사라져 모든 노드가 새 ID를 받고, 링크는 각 인스턴스 계층 안에서만 해소된다. `ui.navprobe`가 같은 프리팹 2개 배치 격리와 구 YAML 승격을 함께 검증한다 | `RenderEngine/Interfaces/Navigation.h`, `ScriptBinder/UIComponent.cpp`, `ScriptBinder/Prefab.cpp`, `Tools/regression/verify-ui-navigation-local.ps1` |
| **C4** | **Text/SpriteSheet는 등록만 되고 그려지지 않는다.** 렌더 소비처가 여전히 **ImageData 하나뿐**이다: `EnhancedUIPass::BuildRectsFromQueue`는 `std::get_if<ImageData>`가 실패하면 `++skipped; continue`("텍스트·스프라이트시트. 폰트 아틀라스가 붙기 전까지 건너뛴다"), 라이브 3D 평면 경로(`ResolveImageRect`·`AppendImageToPlane`)도 `ImageData` 전용이다. `TextData`/`SpriteSheetData`는 `UIRenderProxy.h`·`UIProxyBridge.cpp`·`ProxyCommand.h`에만 존재한다. ★ **SpriteSheet는 폰트 아틀라스와 무관한데도 같이 막혀 있다** — 주석이 둘을 한 문장으로 묶은 탓이다 | `RenderEngine/Render/Passes/Editor/EnhancedUIPass.cpp`, `RenderEngine/Render/Scene/EnhancedSceneRendererLive.cpp` |
| **C5** | **(잠재·휴면)** `ICollision2D` 소멸자가 싱글톤 원시 포인터를 `GetIfAlive()` 가드 없이 역참조한다. 상속자 0·발행처 0이라 지금은 안 터진다 — §1.4에서 삭제 대상 | `ScriptBinder/UIManager.h` |

### 1.3 남은 HIGH (원안 승계 + 신규 3건)

**신규 — 이번 재작성에서 처음 보인 것**

- **H-1. Canvas의 저작 표면이 사실상 없다(신규 · U0에서 지혈).** `Canvas`는 `[[Property]]`
  8필드(`ScaleMode`·`ReferenceResolution`·`MatchWidthOrHeight`·`ScaleFactor`·`RenderMode`·
  `PlaneDistance`·`CanvasOrder`·`CanvasName`)를 갖는데, `InspectorWindow::ImGuiDrawHelperCanvas`가
  그리는 것은 **`CanvasName`과 `CanvasOrder` 둘뿐**이다. 손짠 커스텀 드로어가 제네릭
  드로어(`Meta::DrawObject`)를 대체하므로 나머지 6필드는 통째로 가려진다. 실증: 저작 자산
  전수에서 `RenderMode:` 키가 나오는 파일이 **0개**, Canvas 노드 19개는 전부
  `CanvasOrder`·`CanvasName`만 싣는다.
  → **ScreenSpaceCamera/WorldSpace를 저작으로 켤 방법이 없다.** 게다가 `InspectorWindow`의
  최상단 분기가 `RectTransformComponent`가 있으면 `Transform` 드로어를 **아예 대신한다**
  (`if (rectTransform) ... else ...`). S3가 Canvas에만 남겨 둔 `Transform`은 그래서
  인스펙터에 없다 — **월드 공간 캔버스를 배치할 수단도 없다.**
- **H-2. `UIButton`만 붙은 오브젝트는 캔버스에 영원히 연결되지 않는다(신규).**
  `UIManager::Update`의 지연 연결(`tryLink`)은 `Images`/`Texts`/`SpriteSheets` 세 레지스트리를
  돈다. `UIButton`은 어느 레지스트리에도 등록되지 않는다(`UIManagers->Register*`를 부르는 것은
  `ImageComponent`·`TextComponent`·`SpriteSheetComponent`의 `OnInitialized` 셋뿐). 버튼이
  연결되는 유일한 길은 *같은 오브젝트에 Image/Text/SpriteSheet가 함께 있어*
  `Canvas::AddUIObject`가 덤으로 링크해 주는 것이다. 저작 콘텐츠는 우연히 그 조건을
  만족하지만(프리팹 실측 UIButton 8 : ImageComponent 24~280), 에디터의 "Button" 메뉴는
  **빈 스텁**이고 `UIManager::MakeButton`은 호출자 0이라 "버튼만 붙인 오브젝트"를 정상
  경로로 만들면 조용히 안 눌린다. C3(등록 해소)이 고친 것은 **틱**이지 **연결**이 아니다.
- **H-3. 게임패드 네비게이션 플래그가 마우스 클릭까지 끈다(신규).** `UIManager::CheckInput`은
  `if (!isEnableUINavigation) return;`을 **마우스 클릭 처리보다 앞**에 둔다. 기본값이 true라
  평소엔 안 보이지만, 게임이 게임패드 네비를 끄는 순간 마우스도 함께 죽는다. 두 축이 한
  플래그를 공유하고 있다.

**원안 승계 — 지금도 그대로**

- **3중 장부.** 같은 UI가 ① `UIManager::Images`/`Texts`/`SpriteSheets`(raw pointer 전역 평면
  리스트 — `Scene::CommitRenderProxies`가 읽는 실제 프록시 갱신 소스), ② `Canvas::UIObjs`
  (weak_ptr — 히트테스트·일시정지 갱신 소스), ③ `Scene::Canvases`/`CanvasMap`(GameObject
  이름 키)에 서로 다른 시점·조건으로 등록된다.
- **히트테스트 break 버그.** `UIManager::CheckInput`의 클릭 루프에서 `break;`가 `if (uiObjPtr)`
  블록 **밖**에 있다 — 만료 weak_ptr 하나를 만나면 그 프레임의 남은 클릭 판정이 통째로
  스킵된다(사실상 루프가 첫 원소만 보고 끝난다).
- **World 캔버스 클릭 좌표계 불일치.** `UIButton::CheckClick`에 RenderMode 분기가 없다 —
  worldRect를 항상 화면 픽셀로 취급하므로 Overlay/Camera는 우연히 맞고 World는 어긋난다.
  (`UIButton::UpdateCollider`는 S3 이후 회전을 아예 버렸다 — "UI 클릭박스는 회전하지
  않는다"가 코드에 명시돼 있고, 월드 공간 회전 UI가 필요해지면 RectTransform 쪽에 회전을
  두어야 한다는 조건도 함께 적혀 있다. §3.1의 `m_rotationZ` 신설이 그 조건이다.)
- **컴포넌트 → 프록시 매핑 드리프트 — 1건이 아니라 4건.** `UIProxyBridge.cpp`(Create)와
  `ProxyCommand.cpp`(Update)에 같은 필드 매핑이 복붙돼 있고 이미 갈라졌다:
  ⑴ `TextData::filpEffect` — Create만 세운다, ⑵ `SpriteSheetData::filpEffect` — Create만
  세운다, ⑶ `TextData::fontSize` — Create는 `fontSize * layoutScale`, Update는 `fontSize`,
  ⑷ `m_isEnabled` — Create는 Image 생성자에서만 세우고 Text/SpriteSheet 생성자에서는 아예
  안 세운다(Update는 셋 다 세운다).
- **씬 스코프 이중 정책.** `Scene::CommitRenderProxies`는 `UIManagers->Images` 전역 리스트를
  씬 필터 없이 전부 `UpdateCommand`한다. 반면 `Scene::UpdateRenderData`의 가시성 푸시 단계는
  `scene == this || (DDOL && scene == 활성 씬)` 가드를 건다. additive 로드 시 비활성 씬 UI도
  매 프레임 갱신 대상이다.
- **클릭 z-order 부재.** `Canvas::UIObjs`는 삽입 순서 그대로이고 `UIComponent::CompareLayerOrder`는
  정의만 있고 **호출처 0곳**이다 — 겹친 버튼 중 "먼저 등록된 쪽"이 클릭을 가로챈다.
  (렌더 쪽은 다르다 — `EnhancedUIPass::PrepareFrame`이 `(canvasOrder, layerOrder)`로
  `stable_sort`한다. **보이는 앞뒤와 눌리는 앞뒤가 서로 다른 규칙을 쓴다**는 뜻이다.)
- **동순위 정렬이 unordered_map 순회 순서에 암묵 의존.** 위 `stable_sort`의 입력 순서는
  `RenderScene::GetUIProxySnapshot`이 `UIProxyMap = unordered_map<size_t, ...>`를 훑어 만든
  것이다. 안정 정렬이라 **동순위(canvasOrder·layerOrder가 같은 요소들)의 앞뒤가 실행마다
  달라질 수 있다.**
- **에디터 Undo 불균형.** `Meta::MakeCustomChangeCommand` 호출은 리포 전체에서
  `HierarchyWindow` 1곳 · `ImGuiDrawHelperMeshRenderer` 1곳 · `InspectorWindow`의 Transform
  드로어 3곳 · `SceneViewWindow` 3곳 · 제네릭 드로어 2곳뿐이다. UI 쪽 커스텀 드로어
  (`ImGuiDrawHelperImageComponent`·`ImGuiDrawHelperCanvas`·`ImGuiDrawHelperRectTransformComponent`)에는
  **0건** — 필드 편집 Undo가 안 된다. Text/UIButton/SpriteSheet만 제네릭 드로어를 타서
  스칼라 Undo가 된다.
- **재부모화가 캔버스 소속을 안 바꾼다.** `HierarchyWindow`의 `SCENE_OBJECT` 드롭 처리는
  부모 인덱스와 `RectTransformComponent::SetParentKeepWorldPosition`만 갱신하고
  `m_ownerCanvasObject`/`Canvas::UIObjs`는 방치한다. `tryLink`는 `GetOwnerCanvas() != nullptr`이면
  즉시 return이라 씬 리로드 전까지 옛 캔버스 소속으로 남는다.
- **`navigations`가 Image 인스펙터에만 있다.** 제네릭 typed 드로어(`ReflectionTypedDraw.h`)는
  `vector<string|int|float|Vector2|Vector3>`만 처리하고 `vector<Navigation>`은 "어느 분기에도
  없던 타입은 조용히 넘어갔다"는 레거시 파리티 else로 빠진다. Navigation 편집 UI는
  `ImGuiDrawHelperImageComponent` 안에만 손짜여 있다.
- **부모 Rect 클리핑(마스킹) 부재.** 진행률(Filled) 클립 5종(`UIClipping.h`의
  `CalculateClippedRects`)뿐이고 RectMask2D류는 리포 전체 0건 — 스크롤뷰·팝업 마스킹을
  만들 수단이 없다.
- **캔버스 하나만 입력을 받는다.** `UIManager::Update`가 씬 캔버스 목록을 뒤에서부터 훑어
  처음 만난 활성 캔버스를 `CurCanvas`로 잡고, `CheckInput`은 그 캔버스의 `UIObjs`만 검사한다.
  겹친 캔버스 사이의 입력 라우팅 개념이 없다.
- **`elapsed` 디바운스가 캔버스 부재 구간에도 누적된다.** `CheckInput`이 `elapsed += tick;`을
  `if (!curCanvasObj) return;`보다 **앞**에서 한다.
- **Canvas 파괴 정리가 무관한 조건에 걸린다.** `Canvas::OnUninitializing`의 정리 블록 전체가
  `!UIManagers->CurCanvas.expired()` 안에 있다 — 현재 캔버스가 만료돼 있으면 `DeleteCanvas`가
  아예 안 불린다.
- **UI 레이아웃은 매 프레임 씬 전체를 훑는다.** `Scene::UpdateUILayout`은 씬 루트의 모든 자식에
  대해 `Scene::LayoutUINode`를 재귀한다. `RectTransformComponent`가 없는 노드도 자식 순회는
  그대로 잇는다. dirty(`RectTransformComponent::MarkDirty`/`UpdateLayout`의 반환값)는
  **재계산만** 건너뛰게 하고 **순회는 전량**이다.

### 1.4 죽은 코드 (삭제 대상)

| 항목 | 확인 |
|---|---|
| `ICollision2D` + `UIManager::m_clickEvent` | 상속자 0, 발행처 0 — 완성된 모양의 죽은 브로드캐스트 설계 |
| `UItype` enum | 주석부터 "아직안씀". `ImageComponent`/`TextComponent` 생성자가 대입하지만 **읽는 곳 0**. UIButton/SpriteSheet는 아무도 안 채워 항상 `None` |
| `extern float MaxOreder` | 오타 포함. 정의(`UIComponent.cpp`)와 선언(`UIComponent.h`) 외 참조 0 |
| `UIManager.h`의 `<DirectXTK/SpriteFont.h>` | DX11 은퇴 후 잔재 |
| `UIComponent::CompareLayerOrder` | 정의만 있고 호출 0 — 새 설계에서 "처음으로 켜지는" 기능임을 명시하고 배선 |
| `RenderPassData::PushUIRenderQueue`/`SortUIRenderQueue`/`m_UIRenderQueue` **(신규 발견)** | 두 함수 모두 **호출자 0**. `m_UIRenderQueue`를 읽는 곳은 `EnhancedSceneRenderer.cpp` 한 곳뿐이고 항상 비어 있다. 라이브 경로는 `RenderScene::GetUIProxySnapshot`을 직접 쓴다 — 즉 **"UI 정렬 API"라는 이름의 죽은 코드가 살아 있는 정렬(`EnhancedUIPass::PrepareFrame`)과 나란히 있다.** 지우지 않으면 다음 사람이 이쪽을 고친다 |
| `UIManager::MakeButton`/`MakeCanvas` | 외부 호출 0건(`MakeCanvas`는 `Make*` 내부에서만). 에디터 "Button" 메뉴는 빈 스텁. **단 `MakeImage`/`MakeText`/`MakeSpriteSheet`는 `HierarchyWindow`에서 5곳이 실제로 부른다** — 원안의 "`Make*` 호출 0회"는 정정한다(그 수치는 게임 스크립트 기준이었다) |

> **`Canvas::CanvasName`/`prevCanvasName`은 죽은 코드가 아니다(원안 정정).** 실제 조회 키는
> GameObject 이름이지만(`UIManager::AddCanvas`가 `canvas->ToString()`으로 `CanvasMap`을 채운다),
> `CanvasName`은 C# 바인딩(`CanvasGetName`/`CanvasSetName`)과 인스펙터가 읽고 쓰며 저작 자산
> 19개 전부가 값을 싣고 있다. **삭제 대상이 아니라 "조회에 쓰이지 않는데 편집 가능한 필드"라는
> 혼동 유발 대상**이다 — 새 설계에서 의미를 확정하거나(캔버스 조회의 정본으로 승격) 읽기
> 전용으로 강등한다.

### 1.5 정량 (2026-08-19)

- `UIManager.h` 98줄 / `UIManager.cpp` 708줄. 책임 6종(팩토리·입력·정렬·등록부·지연 연결·
  네비 플래그). `UIComponent` 85/99 · `Canvas` 92/150 · `RectTransformComponent` 189/387.
- **저작 자산**: 씬 12 · 프리팹 206 = 218개. 노드 2,070개 중 `m_gameObjectType: 5`(UI)
  **680개(32.9%)**, `6`(Canvas) **19개**. `m_ownerCanvasName`을 싣는 UI 컴포넌트 **692개**.
- **Navigation 링크**: `navObject`가 0이 아닌 항목 **39개**, 전부 프리팹, **5개 파일**
  (UI_CanvasesBoss 12 · UI_CanvasesIngame 12 · MenuSettingCanvas 8 · SettingCanvas 4 ·
  PauseBox 3). **39/39가 같은 파일 안의 `m_instanceID`로 해소된다**(파일 간 참조 0).
  중첩 프리팹은 MenuSettingCanvas·UI_CanvasesBoss 둘에 있지만 **nav 타깃이 중첩 서브트리에
  걸린 건은 0**이다(UI_CanvasesBoss의 중첩 노드 2개와 nav 타깃 8개가 교집합 없음).
- **`m_typeUUID`를 실은 자산: 0/218.** 컴포넌트 헤더는 아직 `<TypeName>: <typeID>` 한 줄이다.
  (CT4-b가 typeID 정본을 FNV-1a 64로 바꿨으므로 구 파일의 숫자는 어긋나고,
  `Meta::ExtractTypeFromYAML`이 "이름으로 수용 + 경고"로 받는다. 재저장이 치유한다.)
- **살아 있는 게임 소비자**: C# `GameScripts/` 5파일 517줄(`RectLayoutProbe` 154 ·
  `UiTextProbe` 112 · `UiNavProbe` 93 · `UiProbe` 89 · `TutorialUI` 69) + `ClrHost`의
  네이티브 UI API(Rect 6 · Image 9 · Canvas 4 · UiNav/Ui 6). **`Dynamic_CPP/Assets/Script/`의
  C++ UI 스크립트는 컴파일 대상이 아니다**(솔루션 7 프로젝트에 없음, `.vcxproj` 없음).
  단 `Dynamic_CPP/Assets/`의 **데이터**(Scenes·Prefabs·Shaders — `WorldSprite.hlsl` 포함)는
  살아 있는 프로젝트 자산이다.
- **C# 바인딩 공백**: `SpriteSheetComponent`가 `ScriptCore/Component.cs`의 타입 표에 없다
  (Image/Text/Canvas/UIButton 넷만 등록).
- 신규 위젯 타입 1개 추가 비용: 등록 정본 **4곳 동시 갱신**이 필수다 —
  `Lifecycle::Registry::RegisterAllComponents` · `ComponentTypeUUID::kTable` ·
  `RegisterReflectManual.h`의 `REFLECT_TYPE_LIST` · C# 바인딩(`Component.cs` + `Native.cs` +
  `UIComponents.cs`). 앞의 둘은 **개수 불일치**를 기동 시 `std::abort`로 잡고(`Count() !=
  kTable.size()`), 셋째 누락은 `Meta::Serialize`가 "typed ops 미등록 타입" 오류를 찍는다.
  **넷째(C#)는 아무도 안 잡는다** — 컴파일은 되는데 `GetComponent`가 조용히 null을 준다.
  ★ 개수 검사는 *개수*만 본다 — 한쪽에 A를 넣고 다른 쪽에 B를 넣으면 통과한다.

### 1.6 회귀 세트가 잡는 것과 못 잡는 것

실행: `pwsh -NoProfile -File Tools\regression\run-all.ps1` (**반드시 pwsh 7+**. Windows
PowerShell 5.1로 돌리면 한글 주석 인코딩이 깨져 파싱이 무너지고 거짓 실패가 난다.)

| 검사 | 잡는 것 |
|---|---|
| `ui_regression.txt` (UI 생성 순서) | 캔버스 없이 UI 먼저 / 캔버스 나중 추가 / 빈 캔버스 단독 / 재생·정지 반복에서의 **크래시**와 `UiTextProbe` 단정. 통과 조건은 "통과 ≥ 4회 · 실패 0 · 미처리 예외 없음 · 종료 코드 0" |
| `verify-authored-rects.ps1` | 런타임이 계산한 `worldRect`가 프리팹에 남은 저작 `m_worldRect` 다중집합과 일치하는지(프리팹 12종). **레이아웃 수학의 정본 검사** |
| `verify-resolution-sweep.ps1` | 해상도 6종 스위프에서 캔버스 rect · 원점 규약(−size/2) · `Canvas::ComputeScaleFactor` 로그 보간 · 자식 크기 · **버튼 히트박스가 rect와 일치**하는지 |
| 나머지(shutdown / crash-dump / bt / prefab·transform 왕복 / 리플렉션 골든 / 생명주기 기준선) | UI 직접 대상 아님. 단 **리플렉션 골든**과 **생명주기 기준선**은 이 계획이 컴포넌트를 추가·이관할 때 반드시 재기준선을 요구한다 |

**못 잡는 것 — 검증 항목을 쓸 때 이 사각을 먼저 볼 것**

1. **C1(클릭 0-나누기).** `ui.hitbox`는 `UIButton::GetCollider()`와 `RectTransformComponent::GetWorldRect()`를
   나란히 찍어 비교한다 — **둘 다 rect에서 나온 값이고, `CheckClick`의 좌표 변환은 한 번도
   실행되지 않는다.** "보이는 곳과 눌리는 곳"은 검사하지만 "실제로 눌리는가"는 검사하지 않는다.
   → U0의 C1 검증은 **`CheckClick`을 실제로 태우는 신규 콘솔 명령**(예: `ui.click <x> <y>`)이
   있어야 성립한다.
2. **렌더 결과.** 세트 어디에도 픽셀 비교가 없다. Image/Text/SpriteSheet가 실제로 그려지는지는
   dx12 자가검증 스위트(`Tools/dx12-validation/Invoke-Dx12Suite.ps1`) 쪽 일이다 — C4·U3c·U5의
   검증은 그쪽에 등록해야 한다. **Vulkan 백엔드는 또 별개다**(RhiBoundaryPlan).
3. **UIButton 등록/연결.** `ui.status`는 Image/Text/Sprite의 연결 수만 센다. 버튼은 어느
   레지스트리에도 없어 숫자에 나타나지 않는다(H-2가 조용한 이유).
4. **CanvasRenderMode 2모드.** 저작 자산에 `RenderMode` 키가 0개라 세트가 도는 동안
   ScreenSpaceCamera/WorldSpace 경로는 한 번도 실행되지 않는다(H-1).
5. **Create/Update 필드 동일성.** 두 경로를 대조하는 검사가 없다 — 드리프트 4건이 살아남은 이유.
6. **동순위 정렬 안정성.** 반복 실행 간 순서 비교가 없다.

---

## 2. 레퍼런스 판정 — 무엇을 왜 가져오고, 무엇을 왜 버리나 (원안 유지 · 근거 갱신)

**Unity에서 가져온다**: 저작 모델 전체(RectTransform 수학 · `ComputeScaleFactor` · 3모드
Canvas — 이미 동형으로 구현·검증됨), EventSystem/Raycaster/InputModule의 **분리 구조**
(입력을 프록시 파이프라인과 무관한 게임 스레드 시스템으로), Selectable 4상태
(Normal/Highlighted/Pressed/Disabled), 배칭 규칙((canvasOrder, layerOrder) 안정 정렬 + 연속
동일 텍스처 병합 — `EnhancedUIPass`가 이미 같은 결론을 자체 도출했고 주석에 그 이유까지
적어 뒀다: "UI는 순서가 곧 위아래이므로 텍스처로 전역 정렬하면 겹친 요소의 앞뒤가 뒤집힌다").

**Unreal에서 가져온다**: dirty 기반 인밸리데이션(전량 순회 폐지 — 단 서브트리 캐시가 아니라
이 엔진의 델타 발행 모델에 맞춘 dirty-root 집합으로), FReply식 입력 소비 모델(버블링 +
Handled에서 정지), 포인터 캡처(드래그 중 히트테스트 우회), 2-pass 레이아웃(desired size →
arrange)을 LayoutGroup 서브트리에 한정 적용.

**버린다**: UI Toolkit의 retained VisualElement 트리·USS 캐스케이드·Yoga(GameObject 모델과
근본 부정합), Slate의 별도 위젯 트리(듀얼 트리 동기화 비용만 지불), Slate의 "매 프레임 전체
draw 리스트 재생산"(델타 발행 모델의 이점 파괴), UMG식 UWidget↔SWidget 이중 구조(정당화
조건 부재).

**이번 재확인에서 추가된 제약**: 트랙 C3이 "컴포넌트가 스스로 도는 축"을 없앴으므로,
Unreal에서 가져오는 인밸리데이션은 **위젯이 자기 틱에서 dirty를 검사하는 형태로 구현할 수
없다.** 반드시 `UITickSystem`(또는 후속 시스템)의 조밀 배열 순회 안에서 dirty를 읽는 형태여야
한다 — 이것이 원안과 갈리는 유일한 실행 모델 차이이고, 전략(무엇을 가져올지)은 그대로다.

---

## 3. 채택 설계

### 3.0 생명주기 훅 배정 규칙 (신설 — 이 개정의 뼈대)

원안은 `Awake`/`OnDestroy` 한 쌍에 등록·연결·캐시를 전부 얹었다. 그 쌍이 사라졌고, 대신
들어온 6단계는 **게이트 유무가 서로 다르다**. 실측:

| 훅 | 부르는 자리 | 게이트 | DDOL이 씬을 건널 때 |
|---|---|---|---|
| `OnInitialized` | `Scene::RegistryDrainAwakeAndStart` 첫 루프 | `State_AwakeCalled` 비트 — **컴포넌트 평생 1회** | **다시 안 불림** |
| `OnAddedToScene` | 같은 루프(최초) + `Scene::AttachExistingGameObject`가 **직접** | 없음 | **매번 불림** |
| `OnBeginSimulation` | 같은 함수 둘째 루프 | `State_StartCalled` 비트 — 평생 1회 | 다시 안 불림 |
| `OnEndSimulation` | `Scene::FlushPendingDestroy` | 없음(파괴 시) | 해당 없음 |
| `OnRemovingFromScene` | `Scene::DetachGameObjectHierarchy` + `FlushPendingDestroy` | 없음 | **매번 불림** |
| `OnUninitializing` | `Scene::FlushPendingDestroy` | 없음(파괴 시) | 해당 없음 |
| `OnEnable`/`OnDisable` | `Component::SetEnabled` 전이 지점 | 상태 비교 | 무관(직교 축) |

또 하나: 드레인 루프는 `!component->IsEnabled()`이면 큐에 되돌려 **다음 프레임에 다시
시도**한다. 즉 비활성으로 태어난 컴포넌트의 `OnInitialized`는 켜질 때까지 미뤄진다.

**규칙 — 이 문서의 모든 등록·해지가 따른다.**

1. **씬 스코프 등록부**(`Canvas::WidgetRegistry`, `InputRouter`의 히트테스트 후보,
   `UITickSystem` 같은 시스템 조밀 배열) → **`OnAddedToScene` / `OnRemovingFromScene`**.
   게이트가 없어 짝이 항상 맞고, DDOL 이송에서도 빠졌다 들어온다. 트랙 C3이 UI 5종에
   이미 이 규칙을 적용했고 그 근거가 `UITickSystem.h` 상단에 적혀 있다.
2. **프로세스 스코프 등록부**(`RenderScene`의 프록시 커맨드 등록) → **`OnInitialized` /
   `OnUninitializing`**. 씬을 건너도 프록시는 하나면 되므로 평생 1회가 맞다. 현행
   `ImageComponent::OnInitialized`/`OnUninitializing`이 그렇게 돼 있고 유지한다.
3. **"게임이 시작됐다"에 반응하는 것**(트윈 시작, 초기 포커스 부여) → `OnBeginSimulation` /
   `OnEndSimulation`.
4. **캔버스 연결은 양방향 계약으로 닫는다** — 아래 3.1 참조. 한쪽 훅만으로는 못 닫는다.
5. **형제 컴포넌트 포인터를 캐시하지 않는다.** `PrefabUtility::ApplyComponentDiff`가
   컴포넌트를 그 자리에서 파괴하고 같은 타입을 다시 채우는 경로라, 어느 훅에서 잡아 두어도
   댕글링 창이 열린다. `GameObject::FindComponent`(K2 — 타입 마스크 선판정 + 소배열 선형
   탐색, 오브젝트당 컴포넌트 0~6개)가 이미 싸고, 현행 `TickLayout`들이 매 틱 재조회하며
   문제된 적이 없다. **원안의 "Awake 1회 rect 캐시"는 폐기한다** — 훅이 사라져서가 아니라
   측정된 조건이 그 최적화를 정당화하지 않기 때문이다.

**프레임 순서(재생 모드, `EditorMain` 실측)** — 설계가 어느 창에 들어가는지의 근거다:

```
InputManagement->Update
  → SceneManagers->Editor()
  → SceneManagers->Initialization()   : Scene::Awake(=드레인) → OnEnable → Start
  → SceneManagers->InputEvents()      : UIManager::Update (정렬 → CurCanvas 선정 → tryLink → CheckInput)
  → SceneManagers->Physics()          : Scene::FixedUpdate
  → SceneManagers->GameLogic()        : Scene::Update (UpdateUILayout … UITickSystem … LateAllUpdateWorldMatrix)
                                        Scene::LateUpdate → UpdateRenderData(프록시 커밋 + UI 가시성 푸시 + join)
  → SceneManagers->DisableOrEnable()  : Scene::OnDestroy → FlushPendingDestroy
```

두 가지가 여기서 곧장 나온다.
- **등록은 입력보다 먼저다.** 드레인(`OnAddedToScene`)이 `InputEvents`보다 앞이라, 새로
  태어난 위젯은 **같은 프레임에** 클릭 대상이 된다. 폴링 없이 닫을 수 있다는 뜻이다.
- **입력은 이번 프레임 레이아웃보다 먼저다.** 히트테스트가 읽는 rect는 **지난 프레임 값**이다.
  현행도 그렇고(원래 버그가 아니다) 새 설계도 그대로 둔다 — 다만 **의도된 1프레임 지연으로
  문서화**하고, 나타나자마자 눌리는 UI가 문제가 되면 그때 `InputRouter`를 `GameLogic` 뒤로
  옮기는 것을 별도 결정으로 다룬다(§5 R-6).

### 3.1 저작 계층 (uGUI 형상, `[[Property]]` 리플렉션)

- **`UIElement`** (`UIComponent` 대체, `Component` 파생 추상)
  - 상태: `m_layerOrder`, `m_raycastTarget`, `DirtyFlags{Layout|Paint}`.
  - 순수가상 `BuildDrawElement()`.
  - **rect는 캐시하지 않는다** — 필요할 때 `GetOwner()->GetComponent<RectTransformComponent>()`
    (규칙 5).
  - Navigation은 base에서 빼서 **`IUINavigable`** 인터페이스로 분리(현재는 base 강제 보유라
    Button/SpriteSheet가 항상 빈 벡터를 갖고, 네비 등록은 `ImageComponent` 전용이다).
  - **훅 계약(규칙 1·2)**:
    `OnInitialized` → 프로세스 스코프 프록시 등록 /
    `OnAddedToScene` → ① 조상 캔버스 탐색 후 `WidgetRegistry` 등록, ② 실패 시 "미연결"로
    남고 **경고를 컴포넌트당 1회** /
    `OnRemovingFromScene` → `WidgetRegistry` 해지(**계약으로 강제** — 현재 `UIButton`이
    캔버스 목록에서 빠지지 않아 죽은 weak_ptr이 남던 자리를 여기서 닫는다) /
    `OnUninitializing` → 프록시 해지.
- **`Canvas`** — RenderMode/ScaleMode/ReferenceResolution/MatchWidthOrHeight/PlaneDistance/
  CanvasOrder 승계(`ComputeScaleFactor` 로직 무변경). 추가는 **`WidgetRegistry`** 하나 —
  `(canvasOrder → layerOrder)` 정렬 상태를 유지하는 `vector<weak_ptr<UIElement>>`.
  **이것이 3중 장부를 대체하는 단일 진본이다.**
  - **양방향 연결 계약(규칙 4)**: 위젯의 `OnAddedToScene`은 위로 조상 캔버스를 찾고,
    **`Canvas::OnAddedToScene`은 아래로 자기 서브트리의 미연결 `UIElement`를 흡수한다.**
    두 방향을 다 걸어야 `ui_regression.txt`의 케이스 1·2("캔버스 없이 UI 먼저" / "캔버스를
    나중에 추가")가 **폴링 없이** 닫힌다. 지금 `UIManager::Update`의 `tryLink`가 매 프레임
    도는 이유가 정확히 그 두 케이스이고, 그래서 그 폴링을 이 계약이 대체한다.
  - **재부모화도 같은 계약을 탄다** — Hierarchy 드래그가 `Unmount → Mount`를 부른다(U6).
  - 이름 폴백(`m_ownerCanvasName`)은 **읽기 전용 마이그레이션 힌트로 강등**하고 런타임
    조회에서 뺀다(저작 자산 692개가 값을 싣고 있으므로 지우지는 않는다).
  - 정렬된 vector라 동순위 타이브레이크의 `unordered_map` 의존이 부수적으로 해소된다.
- **`ImageComponent`/`TextComponent`/`SpriteSheetComponent`** — `UIElement` 파생 리프.
  필드·`[[Property]]` 이름 최대 보존(콘텐츠 이식 비용 최소화 — 호환 강제가 아니라 비용 판단).
- **`Button`** (`UIButton` 대체) — "옆에 붙은 ImageComponent" 암묵 관례 폐기.
  `[[Property]] weak_ptr<ImageComponent> m_targetGraphic`(선택) + `IUIInputHandler` 구현 +
  Selectable 4상태 색. **`UIElement` 파생이므로 H-2(버튼만 있는 오브젝트가 연결 안 됨)가
  구조적으로 사라진다** — 연결이 Image 레지스트리가 아니라 `UIElement` 계약에 걸리기 때문이다.
- **`RectTransformComponent`** — 수학 무변경 승계(이미 uGUI 동형 · `verify-authored-rects`로
  검증됨). 추가: `m_rotationZ`/`m_localScale` `[[Property]]`(UI 회전 개념 부재 해소 —
  `UIButton::UpdateCollider` 주석이 요구하는 바로 그 조건), setter가
  `WidgetInvalidationTracker`에 dirty-root를 등록하는 훅.
- **신규 보조 컴포넌트**: `LayoutGroupComponent(H/V/Grid)`, `ContentSizeFitterComponent`,
  `RectMaskComponent`(§3.4의 clipRect 방식).

> ★ **위젯 타입을 하나 더할 때마다 등록 정본 4곳을 함께 갱신한다**(§1.5). 특히
> `ComponentTypeUUID::kTable`과 `Lifecycle::Registry::RegisterAllComponents`는 *개수*만
> 대조되므로, 두 표에 서로 다른 타입을 하나씩 넣으면 검사를 통과한다. 리뷰 항목으로 고정.

### 3.2 레이아웃 — 1-pass 앵커(승계) + 2-pass flow(신규) + dirty-root 순회

기본 경로는 현행 top-down 1-pass(`Scene::LayoutUINode`) 유지. **LayoutGroup 서브트리에서만**
Measure(리프→루트 desired-size 상향 전파) → Arrange(자식의 anchoredPosition/sizeDelta를
계산해 **대입**) 2-pass가 돌고, 결과는 다시 기존 1-pass에 흘러 worldRect 계산 경로는 하나로
유지된다.

순회는 Scene별 `dirtyLayoutRoots` 집합에서 시작하는 부분 순회로 전환한다. 현재는
`UpdateLayout`의 dirty가 **재계산만** 막고 **순회는 씬 루트부터 전량**이다.
서브트리 내부의 부모→자식 강제 전파 규칙(`LayoutUINode`의 `if (parentChanged) rect->MarkDirty();`)과
순환·깊이 가드(`TryEnterTraversal`)는 그대로 유지한다.

**틱 축이 없는 세계에서의 구현 형태(원안과 갈리는 지점).** "값이 바뀐 위젯이 스스로
알린다"는 Slate 아이디어를 **위젯의 틱**으로 구현할 수 없다 — `Component`에 틱이 없다.
따라서 형태는 이렇게 된다.

- 알림은 **setter에서** 일어난다(`RectTransformComponent`의 앵커/피벗/sizeDelta setter,
  `UIElement::MarkPaintDirty`). 여기엔 틱이 필요 없다.
- 소비는 **시스템 안에서** 일어난다. `Scene::UpdateUILayout`이 `dirtyLayoutRoots`만 돌고,
  `UITickSystem::Update`의 조밀 배열 순회는 각 원소에서 `Paint`/`Layout` dirty를 보고
  **early-out**한다. 즉 시스템을 없애는 것이 아니라 **시스템이 하는 일을 dirty로 줄인다.**
- **새 위젯을 `UITickSystem`에 더하지 않는다.** LayoutGroup·ContentSizeFitter·RectMask는
  매 프레임 할 일이 없다 — dirty가 섰을 때 레이아웃 순회가 부르면 된다. 반대로 현행 5종은
  줄일 수 있다: `Canvas`의 등록분이 하는 일은 `CanvasOrder` 변경 **폴링**(`TickCanvasOrder`)
  하나뿐이라 setter 훅으로 대체하면 **레인 자체가 사라진다**(U3a).

### 3.3 입력 — `InputRouter` (`UIManager::CheckInput` 전면 교체)

- **`ViewportContext` 단일 진입점**: 에디터는 `GameViewWindow`가 계산한 Game 패널 rect를
  그 자리에서 기록하고, 플레이어는 클라이언트 rect를 기록한다. 소비자는 이 하나만 읽는다 —
  C1의 재발을 구조로 봉쇄한다(변환 재구현 금지). **0 크기 방어를 값의 출처에서 한다**:
  size가 0이면 화면 크기로 폴백하고 경고를 1회 남긴다. 지금은 0으로 나눈 결과가 조용히
  NaN이 되어 아무도 모른다.
- **히트테스트**: `WidgetRegistry` 정렬 **역순**(위부터)으로 `m_raycastTarget` 요소를 검사.
  Overlay/Camera는 화면 rect, **World는 카메라 레이 ↔ 캔버스 평면 교차**(좌표계 버그 해소).
  **z-order 우선순위가 이 시점에 처음으로 켜진다** — 그리고 렌더가 이미 쓰고 있는
  `(canvasOrder, layerOrder)`와 **같은 키**를 쓰므로 "보이는 앞뒤 ≠ 눌리는 앞뒤"가 닫힌다.
- **캔버스 하나 제약을 푼다**: `CurCanvas` 단일 선택 대신 활성 캔버스 전부를 canvasOrder
  역순으로 순회한다. `CurCanvas`는 포커스(게임패드 네비의 현재 컨텍스트)로 의미를 좁힌다.
- **FReply 소비 모델**: 최심 요소에서 부모 체인으로 버블링, `Handled` 반환 지점에서 정지.
  만료 weak_ptr 순회 자체가 사라져 break 버그류가 원천 차단된다.
- **마우스와 게임패드 네비를 분리한다**(H-3): `EnableUINavigation`은 게임패드 모듈만 끈다.
- **Hover/Focus/Drag 신설**(현재 전무): OnPointerEnter/Exit(프레임 간 히트 diff),
  OnFocus/Blur + `InputRouter::SetFocus()`(현재 `ClrHost`의 `UiNavSetSelected`가
  `UIManagers->SelectUI`에 직접 대입하는 자리를 이 API 뒤로 옮긴다), OnDragBegin/Drag/End
  (포인터 캡처).
- **게임패드 네비게이션**: `IUINavigable` 옵트인으로 전 위젯 개방(현재 `ImageComponent`
  전용). 디바운스 타이머(`elapsed`) 누적은 캔버스 유무 체크 **뒤로** 이동.
- **도는 자리**: 현행과 같이 `SceneManager::InputEvents` 창을 유지한다(드레인 뒤 · 레이아웃
  앞). 1프레임 지연은 §3.0에 적은 대로 문서화된 한계다.

### 3.4 렌더 — 프록시 브리지 무변경, 생산자만 재편

- `RenderScene::m_uiProxyMap` · ProxyCommand variant · latest-wins 폴딩 · sceneEpoch 전부 유지.
- **`CanvasRenderMode` 3모드 배선은 흡수한다**(폐기 아님). 근거: ① 3모드가 프록시 구조에
  정확히 대응돼 Image 기준 배선 완료(`EnhancedSceneRendererLive`의 캔버스 평면 계산이
  Overlay/Camera/World를 다 탄다), ② RGHandle 기반이라 backend 중립 제약을 만족하고 실제로
  `LivePipeline`(DX12)·`VulkanLivePipeline` 둘 다 `EnhancedUIPass`를 멤버로 든다,
  ③ 배칭 설계가 uGUI CanvasRenderer와 같은 결론을 자체 도출했다.
  **단 저작 표면이 없어 그 배선이 한 번도 저작 데이터로 실행된 적이 없다**(H-1) — U0에서
  먼저 연다.
- **`BuildImageDrawData`/`BuildTextDrawData`/`BuildSpriteSheetDrawData` 순수 함수 3개로
  생산 단일화** — Create(`UIProxyBridge`)와 Update(`ProxyCommand`) 양쪽이 같은 함수를
  호출한다. 드리프트 4건(§1.3)의 재발을 구조로 차단. 호출은 Paint-dirty일 때만.
- **클리핑은 clipRect discard 방식**(uGUI안에서 접목): `ImageData`에 clipRect 필드 하나
  추가, 셰이더에서 discard — 스텐실 PSO가 필요 없어 **DX12·Vulkan 두 백엔드 확장 비용이
  0이다**(DX11 은퇴로 백엔드가 둘로 줄었지만 그만큼 둘 다 맞춰야 한다). 현재 실사용은
  진행률 클립(`CalculateClippedRects`)뿐이고 스크롤뷰·팝업 마스킹은 신규 기능이라 이걸로
  충분하다. **비사각형/정밀 마스크 요구가 로드맵에 생기면 그때 스텐실로 승급**(결정 지점을
  U3c에 명시).
- **정렬 소스를 결정적으로 만든다**: `EnhancedUIPass::PrepareFrame`의 `stable_sort`는 그대로
  두고(정렬 규칙이 옳다), 그 **입력**을 `RenderScene::GetUIProxySnapshot`의 `unordered_map`
  순회 대신 `WidgetRegistry`의 결정적 순서로 바꾼다 — 동순위 불안정성이 여기서 닫힌다.
- **씬 스코프 정책 통일**: 프록시 갱신을 `UIManagers->Images` 전역 리스트가 아니라 Canvas
  단위로 발행한다 — Canvas가 자기 Scene을 아니까 갱신·가시성 두 단계가 같은 가드를 공유하게
  된다(§1.3의 이중 정책 해소).
- **`RenderPassData`의 죽은 UI 큐 3종을 삭제한다**(§1.4) — 살아 있는 정렬 옆에 죽은 정렬
  API가 나란히 있으면 다음 사람이 그쪽을 고친다.

### 3.5 UIManager의 운명

| 현재 책임 | 이관처 |
|---|---|
| 팩토리 `Make*` 5종 | 에디터 전용 무상태 `UIObjectFactory` 자유함수. 실소비자는 `HierarchyWindow` 5곳(Image 2 · Text 2 · SpriteSheet 1)이고 `MakeButton`/`MakeCanvas`는 0. 암묵적 캔버스 자동 생성 폐지, Pos 기본값은 `ReferenceResolution/2`에서 파생 |
| 입력 `CheckInput` | `InputRouter` + `ViewportContext` |
| 정렬 `SortCanvas`/`needSort` | `Canvas::WidgetRegistry` 정렬 유지로 흡수(별도 상태 불필요) |
| 3종 레지스트리(`Images`/`Texts`/`SpriteSheets`) | 삭제 — `WidgetRegistry` 단일 진본. **`Scene::CommitRenderProxies`·`RenderProxyComponentCount`·`Scene::UpdateRenderData`가 이 셋을 직접 읽으므로 함께 옮긴다** |
| 씬-캔버스 지연 연결 `tryLink`(매 프레임 폴링) | `UIElement`/`Canvas` 양방향 Mount 계약(§3.1) — 폴링 폐지 |
| `EnableUINavigation` | `InputRouter`의 게임패드 모듈 플래그(마우스와 분리) |
| `CurCanvas`/`SelectUI` | `InputRouter` 포커스 상태(`SetFocus` API로만 접근). `ClrHost`의 `UiNavGetSelected`/`UiNavSetSelected`가 그 API를 부르도록 배선 |
| `Canvas::TickCanvasOrder`(CanvasOrder 폴링) | `SetCanvasOrder` setter 훅 — `UITickSystem`의 Canvas 레인 소멸 |

`UIManager.h/.cpp`는 U7에서 파일째 삭제. §1.4 죽은 코드는 전부 U0에서 삭제.

---

## 4. 단계

각 단계는 독립적으로 빌드·검증 가능해야 한다. 원안 대비 변경: **U0.5 폐지**(§1.1),
U0에 H-1 지혈과 C1 검증 수단 신설 추가, U1의 "3자 동시 갱신"을 **4자**로, U7에서
Dynamic_CPP C++ 이식 삭제 및 UUID 각인 선행 추가.

### U0 — 지혈 (구조 개편 착수 전, 현 시스템 위에서)

- **C1**: `GameViewWindow`가 계산한 Game 패널 rect를 `InputManager::m_gameViewPos`/`m_gameViewSize`에
  실제 대입(에디터) + `PlayerMain` 클라이언트 rect 배선(플레이어). **0 크기 폴백 + 1회 경고**를
  같이 넣는다.
- **C1 검증 수단 신설**: `CheckClick`을 실제로 태우는 콘솔 명령(`ui.click <x> <y>` — 히트한
  버튼 이름과 판정 좌표를 찍는다). **이것 없이는 C1 수정을 회귀로 고정할 수 없다**(§1.6-1).
- **C4 전반부**: `EnhancedUIPass::BuildRectsFromQueue`(+ 라이브 3D 평면 경로)에
  `SpriteSheetData` 소비 추가. 프레임 UV 계산만 필요하고 폰트 아틀라스와 무관하다 —
  주석이 Text와 한 문장으로 묶어 두는 바람에 함께 막혀 있었다. **TextData는 SDF 폰트
  아틀라스 선행 의존이라 U5로**(§7).
- **H-1 지혈**: `ImGuiDrawHelperCanvas`가 가리고 있는 `Canvas` 6필드(특히 `RenderMode`·
  `PlaneDistance`·`ReferenceResolution`)를 노출한다. 커스텀 드로어에 필드를 더하거나,
  더 나은 방향으로 커스텀 드로어를 걷고 제네릭 typed 드로어에 맡긴다(그러면 Undo도 덤으로
  붙는다). **Canvas 오브젝트에서 `Transform`도 함께 그린다** — RectTransform이 있으면
  Transform 드로어를 대신하는 `InspectorWindow`의 XOR 분기를 "Canvas는 둘 다"로 고친다.
  → 이후 모든 3모드 픽셀 회귀의 **선행 조건**이다.
- **H-3**: `CheckInput`에서 `isEnableUINavigation` 게이트를 게임패드 블록 안으로 내린다.
- `CheckInput`의 `break` → `continue` 수정(1줄), `elapsed += tick`을 캔버스 체크 뒤로 이동.
- §1.4 죽은 코드 일괄 삭제(`ICollision2D`+`m_clickEvent` · `UItype` · `MaxOreder` ·
  `SpriteFont.h` include · `RenderPassData`의 UI 큐 3종). `CompareLayerOrder`는 **남긴다** —
  U4에서 켠다.
- **하지 않는 것**: C3(프리팹 instanceID). SceneGraph P2가 "지금 걷으면 고치는 버그보다 넓게
  새로 낸다"고 판정했고 근거가 `Prefab.cpp` 주석에 있다. **U7이 대체 배선을 세운 뒤에** 건다.
- 검증: `ui.status` 등록↔연결 수치 일치 · **신규 `ui.click`으로 에디터/플레이어 클릭 스모크** ·
  SpriteSheet가 dx12 자가검증 스위트에서 실제 픽셀로 나오는 신규 검사 · Canvas 인스펙터에서
  RenderMode를 바꾼 씬이 저장→로드에서 값을 보존하는지(저작 표면이 처음 생기는 것이므로
  왕복부터 확인) · 회귀 세트 전체 통과.

### U1 — 신규 저작 표면 골격 (구 시스템과 병행 배치)

- `UIElement`/`IUINavigable`/`IUIInputHandler` 신설, `Canvas` 재설계(`WidgetRegistry`),
  Image/Text/SpriteSheet/Button을 `UIElement` 파생으로 이식(`[[Property]]` 이름 최대 보존).
- **§3.0의 훅 계약을 코드로 고정** — `UIElement`가 `OnAddedToScene`/`OnRemovingFromScene`에서
  Mount/Unmount하고, `Canvas::OnAddedToScene`이 서브트리를 흡수한다.
- `RectTransformComponent`에 `m_rotationZ`/`m_localScale` 필드 신설(계산 반영은 U3a부터).
- **등록 정본 4곳 동시 갱신**(원안의 "3자"는 무효 — `.generated.h`와 `RegisterReflect.def`는
  PHASE 18에서 소멸했다):
  `Lifecycle::Registry::RegisterAllComponents` · `ComponentTypeUUID::kTable`(**새 UUID는 손으로
  박고 이후 절대 바꾸지 않는다**) · `RegisterReflectManual.h` · C# 바인딩(`Component.cs` 타입 표 +
  `Native.cs` + `UIComponents.cs`). **`SpriteSheetComponent` C# 바인딩 누락도 이때 메운다.**
- 검증: 위젯 100개 등록/해제 스트레스에서 등록=해제 카운트 일치 · **DDOL 이송 왕복에서
  `WidgetRegistry` 항목이 정확히 한 번 빠지고 한 번 들어오는지**(규칙 1의 직접 시험) ·
  `verify-authored-rects`를 신규 클래스로 재실행해 동일 결과 · C# `GetComponent` 비-null 단위
  테스트 · **리플렉션 골든과 생명주기 기준선 재기준선**.

### U2 — draw 생산 단일화 + Paint dirty 게이팅

- `BuildXxxDrawData` 순수 함수 3개 신설, Create/Update 양쪽 교체.
- `MarkPaintDirty()` 도입, dirty일 때만 `BuildDrawElement` 호출.
- 프록시 발행을 Canvas 단위로 재편(씬 스코프 정책 통일).
- 검증: **Create/Update 양 경로 필드 동일성 회귀 신설** — 드리프트 4건(filpEffect ×2 ·
  fontSize×layoutScale · m_isEnabled)을 각각 단정으로 고정 · 정적 화면에서 프레임당
  ProxyCommand 발행 0건 계측.

### U3a — dirty-root 부분 순회 + 폴링 제거 (레이아웃 성능)

- `WidgetInvalidationTracker`(Scene별 `dirtyLayoutRoots`) + `RectTransformComponent` setter 훅.
- `Scene::UpdateUILayout`을 dirty-root 시작 부분 순회로 전환.
- **`UITickSystem` 조밀 순회에 early-out 부착**, `Canvas::TickCanvasOrder` 폴링을 setter 훅으로
  대체하고 `UITickSystem`의 Canvas 레인 제거.
- **골든 비교 하네스**: 기존 전량 순회와 신규 부분 순회를 같은 씬에 대해 A/B 자동 비교,
  worldRect 오차 0.01px 이내 — y-down 부호 실수류 회귀를 기계로 잡는다.
- 검증: 골든 하네스 100% 일치 · `verify-authored-rects`·`verify-resolution-sweep` 무변경 통과 ·
  정적 씬에서 프레임당 `LayoutUINode` 호출 수 감소 계측.

### U3b — LayoutGroup/ContentSizeFitter (신규 레이아웃 엔진)

- H/V/Grid LayoutGroup + ContentSizeFitter, Measure→Arrange 2-pass 경로.
- 검증: 중첩 LayoutGroup 스트레스 씬이 1프레임 내 진동 없이 수렴.

### U3c — RectMask 클리핑 (렌더 확장)

- `RectMaskComponent` + `ImageData`(필요 시 Text/SpriteSheet 동일 패턴)에 clipRect 필드,
  셰이더 discard. **스텐실 승급 여부는 이 단계에서 명시 결정**(비사각형 마스크 로드맵 유무).
- 검증: 부모 경계 밖 자식이 실제로 잘리는 픽셀 비교 회귀를 **DX12·Vulkan 양쪽**에 등록.

### U4 — InputRouter 전면 교체

- `ViewportContext` / 정렬 역순 히트테스트 + World 레이-평면 교차 / 캔버스 다중 순회 /
  FReply 버블 / Hover·Focus·Drag / `SetFocus` API(+ `ClrHost` 배선) / `IUINavigable` 게임패드
  네비 / 디바운스 이동.
- 구 `CheckInput` 경로는 이 단계에서 비활성화 — U0에서 이미 동작하게 고쳐 뒀으므로 A/B 비교가
  가능하다.
- 검증: 겹친 버튼 z-order 씬에서 항상 위쪽이 클릭(`ui.click`) · **렌더 정렬 순서와 클릭 순서가
  같은 키를 쓰는지 대조** · World 캔버스 클릭 전용 씬 · **Image 없이 Button만 붙인 오브젝트가
  클릭되는지**(H-2 회귀) · 게임패드 네비를 끈 상태에서 마우스가 살아 있는지(H-3 회귀) ·
  Hover/Focus/Drag 각 1개 이상 스모크.

### U5 — 렌더 소비 갭 마감

- `EnhancedUIPass` TextData 소비(**SDF 폰트 아틀라스 트랙 완료에 의존** — §7).
- 렌더 스냅샷 정렬 입력을 `WidgetRegistry` 결정적 순서로 교체.
- ScreenSpaceCamera 깊이 정책을 "문서화된 한계"로 확정(씬 지오메트리에 안 가려지는 현행 유지).
- 검증: Image/Text/SpriteSheet 3종 렌더 회귀를 dx12 자가검증 스위트에 모두 등록(가능하면
  Vulkan 대조도) · 동순위 위젯 수백 개 반복 등록/해제 후 정렬 순서가 매 실행 동일.

### U6 — 에디터 정합

- **인스펙터 Undo 배선**: Image/RectTransform 커스텀 드로어에 `Meta::MakeCustomChangeCommand`
  연결(Canvas는 U0에서 제네릭으로 갔다면 이미 붙어 있다) — 컴포넌트별 Undo 가능 여부가
  갈리던 불균형 해소.
- **재부모화 훅**: Hierarchy 드래그 시 `UIElement` Unmount/Mount 재실행 → 캔버스 소속·정렬
  즉시 갱신.
- `navigations`를 제네릭 typed 드로어에 노출(`vector<Navigation>` 분기 추가) — Text/Button에서도
  편집 가능하게.
- `HierarchyWindow` 5곳을 신규 `UIObjectFactory`로 교체, **Button 생성 스텁 실배선**.
- 다중 선택 기즈모 드래그의 Undo 누락(주 오브젝트만 기록)도 이때 수정.
- 검증: UI 컴포넌트 전부 인스펙터 편집 Ctrl+Z 동작 · 캔버스 간 드래그 직후 소속·정렬 즉시
  갱신 · 에디터에서 만든 Button이 곧바로 클릭되는지.

### U7 — 재설계: Navigation 로컬 참조 완료 / 구 시스템 은퇴는 후속 분리

원안은 Navigation 변환, C# 소비자 이식, `UIManager` 삭제를 한 슬라이스로 묶었다. 그러나
E7-c를 실제로 막던 것은 **Navigation의 전역 instanceID 참조 하나**였고, 저작 자산 폐기로
218파일 UUID 각인·일괄 변환 전제도 사라졌다. 따라서 U7을 다음 두 경계로 나눴다.

#### U7-N — Navigation 로컬 참조 (✅ 2026-08-21)

- **영속 형식**: `Navigation::navObject`를 제거하고, 소스 UI에서 공통 조상까지 오르는
  `parentHops`와 그 조상에서 타깃까지 내려가는 `childOrdinals`를 저장한다. 단순한
  프리팹 노드 번호가 아니므로 씬·일반 프리팹·중첩 프리팹에 동일하게 적용된다.
- **저작 변경 추적**: `UIComponent::SetNavi`는 런타임 weak 참조와 경로를 함께 갱신하고,
  typed YAML의 `OnBeforeSerialize` 훅이 저장 직전에 경로를 재계산한다. 링크를 만든 뒤
  reparent해도 낡은 경로가 저장되지 않는다.
- **구파일 승격**: 씬의 구 `navObject`는 로드 후 한 번 해소해 새 경로로 치유한다. 프리팹은
  원본 자산을 덮어쓰지 않고 인스턴스화용 YAML 복제본에서 구 ID를 로컬 경로로 바꾼다.
- **인스턴스 격리**: `Prefab::InstantiateRecursive`의 UI ID 예외를 제거해 UI도 매번 새
  instanceID를 받는다. Navigation은 전역 첫 매치를 찾지 않고 소스 오브젝트의 라이브
  계층에서만 타깃을 푼다.
- **E7-c 연계**: `m_gameObjectType` 멤버·리플렉션·`GetType()`과 저장 enum 판정을 제거했다.
  새 파일은 컴포넌트 조합으로 생성 형상을 추론하고 구 키만 읽기 호환 힌트로 소비한다.
- **검증**: `ui.navprobe`로 새 스키마, 동일 프리팹 2개 격리, ID 재발급, UI/Canvas 공간
  컴포넌트, 구 YAML 승격을 검증했다. 리플렉션 골든 77타입·diff 0, 전체 회귀 20개 섹션이
  통과했다. 기존 D3D12 resize 래칫(2/7)은 이 슬라이스 밖의 동일한 별건이다.

#### U7-L — 구 UI 런타임 은퇴 (⬜ 후속)

E7-c 완료 조건에서는 분리됐지만 UI 전면 재설계의 남은 범위다.

- C# `UiNavSetSelected`/`UiNavGetSelected`를 `InputRouter` 포커스 API로 이식하고
  `CanvasName` 계약을 확정한다.
- 살아 있는 `GameScripts/`·`ClrHost` 소비를 이식한 뒤 `UIManager.h/.cpp`와 구
  `UIComponent` 계열의 실제 무참조를 증명하고 삭제한다.
- `Dynamic_CPP/Assets/Script/`의 컴파일되지 않는 보존본은 이식 대상이 아니다.
- 검증은 C# UI 프로브, 구 심볼 참조 0건, 전체 회귀를 요구한다.

---

## 5. 리스크

**폐기(원안에 있었으나 근거가 사라진 것)**

- ~~"U0.5 판정이 '레이스 있음'일 경우 join 추가가 프레임 타이밍에 영향"~~ → **레이스 없음이
  확인됐다**(§1.1). join은 이미 있고 추가할 것이 없다.
- ~~"World→Screen 통합 시 7개 파일의 미세한 차이가 의도된 동작이었을 가능성"~~ → 그 7파일은
  `Dynamic_CPP/Assets/Script/`의 **컴파일되지 않는** C++ 스크립트다. 통합할 대상이 없다.
- ~~"U7 착수 시 39링크·5파일을 다시 세고 instanceID→프리팹 인덱스 일괄 변환"~~ → 저작
  자산이 폐기되어 변환 대상이 사라졌다. 살아 있는 구 YAML은 계층 경로로 메모리 승격한다.
- ~~"218파일 UUID 각인 왕복과 대규모 골든 검수"~~ → 같은 자산 폐기로 작업 자체가
  소멸했다. E7-c의 골든 변경은 Navigation 형식과 저장 enum 제거만 명시적으로 재기준했다.

**유지**

1. **1-pass/2-pass 공존** — 발동 조건(부모 체인의 LayoutGroup 유무)이 런타임에 바뀌면 진동 /
   1프레임 지연 가능. 발동 판정을 정적으로 유지하고 U3b 스트레스 씬으로 고정한다.
2. **TextData 소비가 SDF 트랙에 의존** — 이 재설계는 그 갭을 대신 메우지 않는다. U5가 외부
   트랙에 블로킹될 수 있음을 일정에 반영(§7). U0에서 SpriteSheet만 먼저 여는 이유이기도 하다.
3. **등록 정본 4곳 동시 갱신** — 어긋나면 "컴파일은 되는데 조용히 null". 앞의 둘은 기동
   `abort`가 잡지만 **개수만 대조**하고, C# 바인딩은 아무도 안 잡는다. U1 검증 항목으로
   고정했지만 이후 위젯 추가 시마다 반복되는 함정이다.
**신규 — 이번 재작성에서 보인 것**

4. **입력이 레이아웃보다 한 프레임 앞선다.** 현행 그대로이지만 새 설계에서 Hover/Drag가
   붙으면 체감이 커진다(드래그 중 커서와 위젯이 1프레임 어긋남). U4에서 드러나면
   `InputRouter`를 `GameLogic` 뒤 창으로 옮기는 것을 **별도 결정**으로 다룬다 — 그 이동은
   "입력이 물리·스크립트보다 뒤"라는 다른 계약을 건드리므로 이 계획 안에서 임의로 하지 않는다.
5. **`UIElement` 개명이 UUID 계약과 충돌할 수 있다.** `UIComponent` → `UIElement`는 타입
   이름을 바꾸는 일이다. `ComponentTypeUUID::kTable`의 `typeName`은 **문자열 키**이고
   `SerializeObjectInto`가 `meta::reflect<T>::identifier`로 조회한다. 이름을 바꾸면서 UUID를
   그대로 두려면 표의 `typeName`도 함께 바꿔야 하고, 그 순간 구 파일(이름 폴백으로 열리던
   것)이 이름으로는 못 열린다. 현 저작 자산은 폐기됐지만 앞으로 새 구형 이름 파일이 생길
   수 있으므로, U1 병행 배치 뒤 U7-L에서 이름 별칭 또는 UUID 승계 정책을 먼저 확정한다.
6. **H-1 지혈이 저작 데이터를 바꾼다.** Canvas 인스펙터에 `RenderMode` 등이 열리는 순간
   누군가 값을 저장하고, 그 파일은 지금까지 한 번도 실행된 적 없는 렌더 경로를 탄다.
   U0 검증에 "RenderMode를 바꾼 씬의 저장→로드 왕복"을 넣은 이유이고, **저작 가이드에
   'WorldSpace/Camera는 U3c·U5 전까지 실험용'이라고 명시**해야 한다.
7. **`Canvas::UIObjs`와 `WidgetRegistry`의 병행 기간.** U1은 구 시스템과 병행 배치다.
   두 장부가 공존하는 동안 "어느 쪽이 진본인가"가 슬라이스마다 다르다. 각 슬라이스 완료
   조건에 **"이 시점의 진본은 X"**를 한 줄로 못 박는다.

---

## 6. 콘텐츠 이식 비용 (2026-08-21 U7-N 착지 후 재산정)

저작 자산 폐기로 원안의 **2,070노드 변환, Navigation 39링크 일괄 재계산, 218파일 UUID
각인 왕복은 모두 0**이 됐다. U7-N은 런타임/로더 승격으로 끝났고, 남은 비용은 U7-L의
살아 있는 코드 소비자 이식뿐이다.

- **수작업(작음)** — C# 소비자 5파일 517줄 + `ClrHost` UI 바인딩. API 이름을 보존하면
  대부분 무변경이고, 실제 손은 ① 포커스 API 2곳(`UiNavSetSelected`/`UiNavGetSelected`),
  ② `Canvas::CanvasName` 의미 확정에 따른 2곳, ③ `SpriteSheetComponent` 바인딩 신설.
  **합쳐서 1인일 이내.**
- **에디터 손질(중간)** — `HierarchyWindow` 5곳의 `Make*` 교체 + Button 스텁 실배선,
  인스펙터 커스텀 드로어 3종의 Undo 배선. U6에 계상.
- **이식 안 함(의도)** — `UIManager::Make*` 팩토리의 게임 측 소비(0건), `ICollision2D`
  (콘텐츠 없음), `Dynamic_CPP/Assets/Script/`의 C++ 스크립트(컴파일 대상 아님 — 되살릴
  계획이 서면 그때 별도 과제).

**현재 산정: U7-N 완료. U7-L의 C#·에디터 이식과 구 런타임 제거 검증이 남았으며,
과거 자산 변환 도구 비용은 없다.**

---

## 7. 범위 밖 — 명시적 미해결 (침묵 누락이 아니라 결정)

| 항목 | 이유 | 귀속 |
|---|---|---|
| SDF 폰트 아틀라스·글리프 배치 | 텍스트가 화면에 나오려면 필수지만 UI 구조가 아니라 렌더 애셋 트랙 | 별도 트랙(`MaterialPipelinePlan` 인접) · **U5가 의존** |
| `GameObjectType::UI` 저장 필드 제거 | **완료.** U7-N이 선행을 제공하고 E7-c가 멤버·리플렉션·판정을 제거했다 | `SceneGraphRedesignPlan` **E7-c(완료)** |
| 프리팹 오버라이드 체계 전반 | Navigation은 계층 경로로 독립했지만 오버라이드 저장 구조 자체는 프리팹 트랙 소관 | `SceneGraphRedesignPlan` 트랙 P |
| UI 오브젝트의 공간 컴포넌트 정책 | S3가 이미 착지했다(UI = RectTransform만, Canvas = 둘 다). 이 계획은 그 결과를 **전제로 쓴다** — 다시 손대지 않는다 | `SceneGraphRedesignPlan` S3(완료) |
| ScreenSpaceCamera가 씬 지오메트리에 안 가려짐 | 의도적 단순화인지 미완성인지 근거 부재 — v1은 현행을 문서화된 한계로 유지 | U5에서 문서화, 결정은 보류 |
| Vulkan 백엔드의 UI 픽셀 동등성 | `EnhancedUIPass`는 두 파이프라인 모두에 배치돼 있지만 픽셀 대조는 이 계획의 자가 검증 범위 밖 | `RhiBoundaryPlan`(공유 패스 승격 진행분) |
| 트윈/애니메이션 상태 머신 공용화 | 새 화면 제작 비용의 큰 몫이지만 UI 구조와 독립 | 후속 후보(TweenSystem) |
| 멀티플레이어 UI 슬롯 공통 개념 | 게임 설계 결정 필요 | 후속 후보 |
| `Dynamic_CPP/Assets/Script/`의 C++ 스크립트 되살리기 | 컴파일 대상이 아니고 되살릴 계획도 서 있지 않다. 되살린다면 그 안의 World→Screen 재구현·위치 기반 자식 바인딩·전역 포커스 직접 대입이 **그때** 이식 대상이 된다 | 별도 과제(CoreCLR 레거시 은퇴 후속) |

---

## 부록 A — 코드에서 고쳐야 할 것으로 발견했으나 이 문서가 손대지 않은 것

원안 문서 재작성 때 발견한 항목의 기록이다. 2026-08-21 U7-N/E7-c 코드는 착지했지만,
아래 나머지는 각 슬라이스에 귀속된 별도 작업이다.

- `Canvas::AddUIObject`의 주석이 아직 "등록은 각 컴포넌트의 **Awake**가 자기 수명에 맞춰
  직접 한다"고 말한다. `Awake`는 없다 — `OnInitialized`로 고쳐야 한다(다른 UI 파일의
  주석 여러 곳도 같은 상태).
- `ImageComponent::OnInitialized` 본문 주석의 "레지스트리 등록은 수명의 시작(Awake)에서" 도 같다.
- `UIManager::Update`의 캔버스 선택 루프가 `canvasPtr->GetComponent<Canvas>()`의 결과를 널
  검사 없이 `canvas->IsEnabled()`로 역참조한다. `Scene::Canvases`에 Canvas 컴포넌트가 없는
  오브젝트가 들어가면 즉사한다.
- `EnhancedUIPass::BuildRectsFromQueue`의 스킵 주석("텍스트·스프라이트시트. 폰트 아틀라스가
  붙기 전까지 건너뛴다")이 SpriteSheet까지 폰트 의존으로 묶어 읽히게 한다. U0에서 코드와
  함께 고친다.
- `ScriptBinder/UIComponent.h`의 `pos`/`scale`은 `[[Property]]`가 아닌데 위젯 배치의
  중간 상태로 쓰인다(`TickLayout`이 rect에서 매 틱 다시 채운다). `UIElement`로 옮길 때
  "파생 상태"임을 타입으로 드러낼 자리다.
