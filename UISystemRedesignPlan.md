# UI 시스템 전면 재설계 — UIManager 해체와 "uGUI 저작 · Slate 런타임" 하이브리드 (PHASE 16)

작성: 2026-08-16 · 계기: UIManager와 관련 구조의 전면 재설계 요청. 레퍼런스는 유니티·언리얼
양쪽을 조사해 장점을 접목하기로 했다. **기존 구조 재활용은 요구사항이 아니다**(사용자 명시).
따라서 아래 설계에서 무언가를 승계하는 항목은 전부 "다시 만들 이유가 없어서"이지
"호환 때문"이 아니다 — 승계 여부는 항목마다 실측 근거로 판정했다.

방법: 병렬 조사 13기 — 현 시스템 7개 축 실측(매니저·캔버스/레이아웃·위젯·렌더·입력·게임
소비자·에디터/직렬화), Unity(UGUI + UI Toolkit)·Unreal(UMG + Slate) 이식성 조사 2건,
독립 설계안 3종(uGUI 충실 계승 / Slate 위젯트리 / 하이브리드) 생성 후 심판 채점.
결과: **하이브리드 83점 / uGUI 74점 / Slate 66점 → 하이브리드 채택**, 단 심판 교정 3건과
탈락안 접목 5건을 반영했다(§3, §4에 반영 완료).

관련 문서: `RefactoringPlanDashboard.html`(Phase 6 — 3중 장부가 크래시 3건의 근본 원인이라는
선행 실측), `SceneGraphRedesignPlan.md`(프리팹 트랙 P — instanceID 정책이 겹친다),
`RhiBoundaryPlan.md`(멀티 RHI 중립 제약), `BuildPipelinePlan.md`(트랙·게이트 관례 승계).

---

## 0. 전략 요약

**경계는 "누가 만지는가"로 긋는다.**

- **저작 표면 = uGUI 형상.** 디자이너·게임 스크립트가 만지는 것(GameObject + RectTransform +
  Canvas + 위젯 컴포넌트, `[[Property]]` 직렬화)은 uGUI 모델을 따른다. 실측 결과 이 엔진의
  RectTransform(앵커/피벗/sizeDelta/16종 프리셋, y-down 규약)과 CanvasScaler(로그 보간까지),
  CanvasRenderMode 3종은 **이미 uGUI와 동형으로 수렴해 있고 검증돼 있다** — 재발명은 손해다.
- **런타임 내부 = Slate 실행 모델.** 값이 바뀐 위젯만 dirty로 자신을 알리고(전량 순회 폐지),
  draw 데이터 생산은 단일 함수로 통일해 프록시 커맨드로 발행하며, 입력은 정렬된 히트테스트
  + leaf→root 버블/소비(FReply) 모델로 재편한다.
- **프록시 브리지는 무변경.** 조사에서 확인된 핵심: 이 엔진의 ProxyCommand 델타 발행 모델은
  Slate의 draw-element 스냅샷 철학과 **이미 구조적으로 같다**. Slate의 "매 프레임 위젯 트리
  재순회 + 서브트리 캐시" 실행 모델을 통째로 이식하면 오히려 델타 발행의 이점을 깎아먹는다.
  가져올 것은 게임 스레드 쪽(2-pass 레이아웃, 입력 버블링, 인밸리데이션 아이디어)뿐이다.
- **Slate식 별도 위젯 트리는 만들지 않는다.** 이 엔진은 UI가 GameObject와 1:1이고
  RectTransform이 자동 부착된다(`Scene.cpp:1806-1813`, `GameObject.cpp:36-39`). Unreal에서
  UWidget↔SWidget 분리를 정당화하는 조건(액터와 독립된 위젯 수명)이 여기엔 없다 — 별도
  트리를 만들면 "듀얼 트리 동기화 누락"이라는 새 버그 클래스만 산다(Slate안 탈락 사유).
- **순서: 지혈(U0) → 레이스 검증 스파이크(U0.5) → 구조 재편.** 실측된 CRITICAL을 먼저 죽여야
  이후 회귀를 "재설계 탓"과 "원래 버그 탓"으로 구분할 수 있다.

---

## 1. 실측 — 2026-08-16 전수 조사

### 1.1 즉시 지혈 대상 (CRITICAL)

| # | 결함 | 근거 |
|---|---|---|
| C1 | **마우스 클릭 히트테스트가 항상 실패한다.** `UIButton::CheckClick`의 좌표 변환이 읽는 `InputManager::m_gameViewPos/m_gameViewSize`는 리포 전체에서 **대입이 0회**라 항상 (0,0) — `screenSize.x/gameViewSize.x`가 0-나누기로 Inf/NaN이 되어 판정이 사실상 항상 false. `GameViewWindow`는 Game 패널 rect를 매 프레임 계산만 하고 아무 데도 기록하지 않는다 | `UIButton.cpp:32-40`, `InputManager.h:97-98`, `GameViewWindow.cpp:14-37` |
| C2 | **navigations가 로드마다 2배로 불어난다.** `ComponentFactory::LoadComponent`의 Image/Text/SpriteSheet 분기 3곳이 `Meta::Deserialize`로 채운 뒤 같은 노드를 clear() 없이 다시 push_back — 씬 로드·프리팹·Play 진입 복제 전 경로에서 재현되는 데이터 손상 | `ComponentFactory.cpp:552-561, 591-600, 623-632` |
| C3 | **UI 프리팹을 두 번 배치하면 instanceID가 충돌한다.** `Prefab::InstantiateRecursive`가 `GameObjectType::UI`만 새 ID 재발급을 건너뛴다. `FindInstanceID`는 선형 탐색 첫 매치라 두 번째 인스턴스의 Navigation 링크가 첫 인스턴스로 오염된다 | `Prefab.cpp:129-156`, `GameObject.cpp:309-327`, `UIComponent.cpp:61-78` |
| C4 | **Text/SpriteSheet는 등록만 되고 그려지지 않는다.** `EnhancedUIPass::BuildRectsFromQueue`는 ImageData만 소비, TextData/SpriteSheetData의 소비처는 리포 전체 0곳. 옛 DX11 SpriteBatch 경로는 이미 삭제됨 — "배선 완료"와 "화면에 나옴"이 별개의 보장인 구조가 기존 타입 2종에서 실증됐다 | `EnhancedUIPass.cpp:81-93`, `UIRenderProxy.cpp:37-44` |
| C5 | **(잠재)** `ICollision2D` 소멸자가 싱글톤 원시 포인터를 `GetIfAlive()` 가드 없이 역참조 — `DLLAcrossSingleton.h:27-40` 자신의 경고 위반. 현재 상속자 0이라 휴면 상태 | `UIManager.h:77-94` |

### 1.2 구조 결함 (HIGH)

- **3중 장부.** 같은 UI가 ① `UIManager::Images/Texts/SpriteSheets`(raw pointer 전역 평면
  리스트 — 실제 프록시 갱신 소스), ② `Canvas::UIObjs`(weak_ptr — 히트테스트·일시정지 갱신
  소스), ③ `Scene::Canvases/CanvasMap`(이름 키)에 서로 다른 시점·조건으로 등록된다.
  `RefactoringPlanDashboard.html` Phase 6가 이미 크래시 3건의 근본 원인으로 지목한 구조다.
- **히트테스트 break 버그.** `UIManager::CheckInput` 루프의 break가 `if(uiObjPtr)` 블록
  밖이라, 만료된 weak_ptr 하나를 만나면 그 프레임의 남은 클릭 판정이 통째로 스킵된다
  (`UIManager.cpp:407-419`).
- **World 캔버스 클릭 좌표계 불일치.** `CheckClick`이 RenderMode 분기 없이 worldRect를 항상
  화면 픽셀로 취급 — Overlay/Camera는 우연히 맞고 World는 어긋난다(`UIButton.cpp:12-70`,
  `RectTransformComponent.cpp:214-229`).
- **컴포넌트→프록시 매핑 중복(3~4벌).** `UIProxyBridge.cpp`(Create)와 `ProxyCommand.cpp`
  (Update)에 동일 필드 매핑이 복붙돼 있고 **이미 실제로 갈라졌다** — TextComponent의
  `filpEffect`가 Update 경로에서 누락(`UIProxyBridge.cpp:53-77` vs `ProxyCommand.cpp:256-285`).
- **씬 스코프 이중 정책.** `Scene::UpdateRenderData`의 프록시 갱신 단계는 씬 필터가 없고
  (`Scene.cpp:486-509`), 가시성 푸시 단계에는 `scene==this || DDoL+활성씬` 가드가 있다
  (`Scene.cpp:527-554`) — additive 로드 시 비활성 씬 UI도 매 프레임 갱신 대상.
- **클릭 z-order 부재.** `Canvas::UIObjs`는 삽입 순서 그대로이고 `CompareLayerOrder`는 정의만
  있고 **호출처 0곳** — 겹친 버튼 중 "먼저 등록된 쪽"이 클릭을 가로챈다(`UIComponent.h:41-50`).
- **에디터 Undo 불균형.** Canvas/Image/RectTransform 인스펙터는 손짠 위젯이라
  `Meta::MakeCustomChangeCommand` 호출이 0 — 필드 편집 Undo 불가. Text/UIButton/SpriteSheet만
  제네릭 드로어로 스칼라 Undo가 된다. UI 생성·재부모화도 Undo 밖(`InspectorWindow.cpp:1361-1616`).
- **재부모화가 캔버스 소속을 안 바꾼다.** Hierarchy 드래그는 부모 인덱스만 갱신하고
  `m_ownerCanvasObject`/`Canvas::UIObjs`는 방치. tryLink는 `GetOwnerCanvas() != null`이면
  즉시 return이라 씬 리로드 전까지 옛 캔버스 소속으로 남는다(`HierarchyWindow.cpp:331-353`,
  `UIManager.cpp:581-583`).
- **부모 Rect 클리핑(마스킹) 부재.** 진행률(Filled) 클립 5종뿐, RectMask2D류는 리포 전체 0건
  — 스크롤뷰·팝업 마스킹을 만들 수단이 없다(`UIClipping.h:40-84`).
- **게임 코드가 프레임워크 공백을 손으로 메우고 있다.** World→Screen 변환을 7개 파일이 카메라
  행렬 곱으로 각자 재구현(미세하게 다르게), 자식 바인딩을 5개 파일이 "항상 첫 자식" 위치
  순서로 수행(프리팹 구조 변경에 조용히 깨짐), 전역 포커스(`CurCanvas/SelectUI`)를 스크립트가
  직접 대입 2곳(`HPBar.cpp:86-102` 외, `ItemUIPopup.cpp:24-59` 외, `SettingWindowUI.cpp:89-90`).

이 밖에 MEDIUM급: raw pointer 레지스트리가 lifecycle 이벤트에만 의존(씬 언로드 시 명시적
clear는 `SelectUI` 하나뿐, `SceneManager.cpp:807`), 게임패드 네비게이션이 ImageComponent 전용
(UIButton은 Awake/OnDestroy가 없어 등록 자체가 안 됨), 동순위 정렬이 unordered_map 순회
순서에 암묵 의존(`RenderScene.h:55`), Canvas 파괴 정리가 무관한 `CurCanvas.expired()` 조건에
걸림(`Canvas.cpp:45-56`), `elapsed` 디바운스가 캔버스 부재 구간에도 누적(`UIManager.cpp:384-388`).

### 1.3 죽은 코드 (전부 삭제 대상)

| 항목 | 근거 |
|---|---|
| `ICollision2D` + `m_clickEvent` | 상속자 0, 발행처 0 — 완성된 모양의 죽은 브로드캐스트 설계 |
| `UItype` enum | 주석부터 "아직안씀", setter만 있고 읽는 곳 0. GENERATED_BODY 생성자는 아예 안 채워 SpriteSheet/Button은 항상 None |
| `extern float MaxOreder` | 오타 포함, 정의 외 참조 0 |
| `UIManager.h`의 `<DirectXTK/SpriteFont.h>` | D4에서 은퇴한 기능의 잔재 |
| `Canvas::CanvasName`/`prevCanvasName` | 실제 조회 키는 GameObject 이름 — 인스펙터에서 편집해도 무효인 죽은 필드 |
| `MakeButton`/`MakeCanvas` | 외부 호출 0건. 에디터 "Button" 메뉴는 빈 TODO 스텁(`HierarchyWindow.cpp:175-178`) |
| `UIComponent::CompareLayerOrder` | 정의만 있고 호출 0 — 새 설계에서 "처음으로 켜지는" 기능임을 명시하고 배선 |

### 1.4 정량

UIManager.h 99줄 / .cpp 709줄, 책임 6종(팩토리·입력·정렬·등록부·지연 연결·네비 플래그).
신규 위젯 타입 1개 추가 비용 = **12~14개 파일**(그중 다수가 "등록"만 보장, "그려짐" 미보장).
게임 소비자: UI 스크립트 13개 1,747줄 전수 — Rect API 77회/15파일, Image/Text/Nav API
240회/53파일, `GetComponent<UI계열>` 87회/34파일, **`Make*` 팩토리 호출 0회**,
`GameObject::Find` 181회/65파일(UI 범위 밖, §7). 미커밋 진행분: `CanvasRenderMode.h`,
`EnhancedSpritePass.h/.cpp`, `WorldSprite.hlsl` — Image 기준 3모드 전부 배선 완료 상태.

---

## 2. 레퍼런스 판정 — 무엇을 왜 가져오고, 무엇을 왜 버리나

**Unity에서 가져온다**: 저작 모델 전체(RectTransform 수학·CanvasScaler·3모드 Canvas — 이미
동형으로 구현·검증됨), EventSystem/Raycaster/InputModule의 **분리 구조**(입력을 프록시
파이프라인과 무관한 게임 스레드 시스템으로), Selectable 4상태(Normal/Highlighted/Pressed/
Disabled), 배칭 규칙((canvasOrder, layerOrder) 안정 정렬 + 연속 동일 텍스처 병합 — 신규
EnhancedUIPass가 이미 같은 결론을 자체 도출, `EnhancedUIPass.h:33-56`).

**Unreal에서 가져온다**: dirty 기반 인밸리데이션(전량 순회 폐지 — 단, 서브트리 캐시가 아니라
이 엔진의 델타 발행 모델에 맞춘 dirty-root 집합으로), FReply식 입력 소비 모델(버블링 +
Handled에서 정지), 포인터 캡처(드래그 중 히트테스트 우회), 2-pass 레이아웃(desired size →
arrange)을 LayoutGroup 서브트리에 한정 적용.

**버린다**: UI Toolkit의 retained VisualElement 트리·USS 캐스케이드·Yoga(무거운 GameObject
모델과 근본 부정합), Slate의 별도 위젯 트리(듀얼 트리 동기화 비용만 지불), Slate의 "매 프레임
전체 draw 리스트 재생산"(델타 발행 모델의 이점을 파괴), UMG식 UWidget↔SWidget 이중 구조
(정당화 조건이 이 엔진에 없음).

---

## 3. 채택 설계

### 3.1 저작 계층 (uGUI 형상, `[[Property]]` 리플렉션)

- **`UIElement`** (UIComponent 대체, Component 파생 추상) — `m_rect`(Awake 1회 캐시, 매 프레임
  GetComponent 재조회 금지), `m_layerOrder`, `m_raycastTarget`, `DirtyFlags{Layout|Paint}`,
  순수가상 `BuildDrawElement()`. Navigation은 base에서 빼서 **`IUINavigable`** 인터페이스로
  분리(현재는 base 강제 보유라 Button/SpriteSheet가 항상 빈 값을 갖고, 네비 등록은
  ImageComponent 전용이던 문제 해소). **`OnDestroy`가 반드시 `Canvas::Unregister`를 호출하는
  계약**(현재 UIButton만 누락돼 죽은 weak_ptr이 남던 버그를 계약 수준에서 차단).
- **`Canvas`** — RenderMode/ScaleMode/ReferenceResolution/MatchWidthOrHeight/PlaneDistance/
  CanvasOrder 승계(`ComputeScaleFactor` 로직 무변경). 추가는 **`WidgetRegistry`** 하나 —
  (canvasOrder→layerOrder) 정렬 상태를 유지하는 `vector<weak_ptr<UIElement>>`. **이것이 3중
  장부를 대체하는 단일 진본이다.** 등록은 조상 계층 탐색만, 이름 폴백(`m_ownerCanvasName`)은
  폐기. 정렬된 vector라 동순위 타이브레이크의 unordered_map 의존도 부수적으로 해소된다.
- **`ImageComponent`/`TextComponent`/`SpriteSheetComponent`** — UIElement 파생 리프. 필드·
  `[[Property]]` 이름 최대 보존(콘텐츠 이식 비용 최소화 — 호환 강제가 아니라 비용 판단).
- **`Button`** (UIButton 대체) — "옆에 붙은 ImageComponent" 암묵 관례 폐기.
  `[[Property]] weak_ptr<ImageComponent> m_targetGraphic`(선택) + `IUIInputHandler` 구현 +
  Selectable 4상태 색.
- **`RectTransformComponent`** — 수학 무변경 승계(이미 uGUI 동형·검증 완료). 추가:
  `m_rotationZ`/`m_localScale` `[[Property]]`(UI 회전 개념 부재 해소), setter가
  WidgetInvalidationTracker에 dirty-root 등록하는 훅.
- **신규 보조 컴포넌트**: `LayoutGroupComponent(H/V/Grid)`, `ContentSizeFitterComponent`,
  `RectMaskComponent`(§3.4의 clipRect 방식).

### 3.2 레이아웃 — 1-pass 앵커(승계) + 2-pass flow(신규) + dirty-root 순회

기본 경로는 현행 top-down 1-pass 유지. **LayoutGroup 서브트리에서만** Measure(리프→루트
desired-size 상향 전파) → Arrange(자식의 anchoredPosition/sizeDelta를 계산해 **대입**) 2-pass가
돌고, 결과는 다시 기존 1-pass에 흘러 worldRect 계산 경로는 하나로 유지된다.

순회는 Scene별 `dirtyLayoutRoots` 집합에서 시작하는 부분 순회로 전환(현재는 재계산만 dirty
스킵하고 **순회는 매 프레임 전량**, `Scene.cpp:1938-1963`). 서브트리 내부의 부모→자식 강제
전파 규칙(`Scene.cpp:1913-1918`)은 유지.

### 3.3 입력 — `InputRouter` (CheckInput 전면 교체)

- **`ViewportContext` 단일 진입점**: 에디터는 GameViewWindow가 계산한 Game 패널 rect를 그
  자리에서 기록, 플레이어는 클라이언트 rect — C1의 재발을 구조로 봉쇄(변환 재구현 금지).
- **히트테스트**: WidgetRegistry 정렬 역순(위부터)으로 `m_raycastTarget` 요소를 검사.
  Overlay/Camera는 화면 rect, **World는 카메라 레이↔캔버스 평면 교차**(좌표계 버그 해소).
  z-order 우선순위가 이 시점에 처음으로 켜진다.
- **FReply 소비 모델**: 최심 요소에서 `m_parentIndex` 체인으로 버블링, `Handled` 반환 지점에서
  정지. 만료 weak_ptr 순회 자체가 사라져 break 버그류가 원천 차단된다.
- **Hover/Focus/Drag 신설**(현재 전무): OnPointerEnter/Exit(프레임 간 히트 diff),
  OnFocus/Blur + `InputRouter::SetFocus()` API(스크립트의 `CurCanvas/SelectUI` 직접 대입 대체),
  OnDragBegin/Drag/End(포인터 캡처).
- **게임패드 네비게이션**: `IUINavigable` 옵트인으로 전 위젯 개방. 디바운스 타이머는 캔버스
  유무 체크 뒤로 이동.

### 3.4 렌더 — 프록시 브리지 무변경, 생산자만 재편

- `RenderScene::m_uiProxyMap`·ProxyCommand variant·latest-wins 폴딩·sceneEpoch 전부 유지.
- **진행 중인 CanvasRenderMode·EnhancedSpritePass·WorldSprite.hlsl은 흡수한다**(폐기 아님).
  근거: ① 3모드가 프록시 구조에 정확히 대응돼 Image 기준 배선 완료, ② RGHandle 기반이라
  backend 중립 제약을 이미 만족, ③ 배칭 설계가 uGUI CanvasRenderer와 같은 결론을 자체 도출.
- **`BuildImageDrawData`/`BuildTextDrawData`/`BuildSpriteSheetDrawData` 순수 함수 3개로
  생산 단일화** — Create(UIProxyBridge)와 Update(ProxyCommand) 양쪽이 같은 함수를 호출.
  filpEffect류 드리프트의 재발을 구조로 차단. 호출은 Paint-dirty일 때만.
- **클리핑은 clipRect discard 방식**(uGUI안에서 접목): `ImageData`에 clipRect 필드 하나 추가,
  셰이더에서 discard — 스텐실 PSO가 필요 없어 멀티 RHI 확장 비용 0. 현재 실사용은 진행률
  클립뿐이고 스크롤뷰·팝업 마스킹은 신규 기능이라 이걸로 충분하다. **비사각형/정밀 마스크
  요구가 로드맵에 생기면 그때 스텐실로 승급**(결정 지점을 U3c에 명시).
- 렌더 스냅샷 정렬 소스를 WidgetRegistry의 결정적 순서로 교체(동순위 불안정성 해소).
- 씬 스코프 정책 통일: 프록시 갱신을 전역 리스트가 아닌 Canvas 단위로 발행 — Canvas가 자기
  Scene을 아니까 갱신·가시성 두 단계가 같은 가드를 공유하게 된다.

### 3.5 UIManager의 운명

| 현재 책임 | 이관처 |
|---|---|
| 팩토리 `Make*` 5종 | 에디터 전용 무상태 `UIObjectFactory` 자유함수(소비자가 HierarchyWindow 5곳뿐). 암묵적 캔버스 자동 생성 폐지, Pos 기본값은 ReferenceResolution/2에서 파생 |
| 입력 `CheckInput` | `InputRouter` + `ViewportContext` |
| 정렬 `SortCanvas`/`needSort` | `Canvas::WidgetRegistry` 정렬 유지로 흡수(별도 상태 불필요) |
| 3종 레지스트리 | 삭제 — WidgetRegistry 단일 진본 |
| 씬-캔버스 지연 연결 tryLink | `UIElement::Awake` 시점 즉시 연결(조상 탐색), 매 프레임 폴링 폐지 |
| `EnableUINavigation` | InputRouter의 게임패드 모듈 플래그 |
| `CurCanvas`/`SelectUI` | InputRouter 포커스 상태(`SetFocus` API로만 접근) |

`UIManager.h/.cpp`는 U7에서 파일째 삭제. §1.3 죽은 코드는 전부 U0에서 삭제.

---

## 4. 단계

각 단계는 독립적으로 빌드·검증 가능해야 한다. 심판 교정 반영: 원안의 U3 과적재를
U3a/U3b/U3c로 분리, WorkerPools 레이스 검증을 U0.5 스파이크로 명시 스케줄, 에디터 Undo·
재부모화 갱신을 U6 정식 작업으로 추가.

### U0 — 지혈 (구조 개편 착수 전, 현 시스템 위에서)
- C1: GameViewWindow 계산 rect를 `InputManager::m_gameViewPos/Size`에 실제 대입(에디터) +
  PlayerMain 클라이언트 rect 배선(플레이어)
- C2: ComponentFactory 3곳의 navigations 수동 재푸시 루프 제거
- C3: `Prefab::InstantiateRecursive`의 UI 타입 instanceID 재발급 스킵 제거
- C4(전반부): EnhancedUIPass에 SpriteSheetData 소비 추가(프레임 UV 계산만 필요 — 저비용).
  TextData는 SDF 폰트 아틀라스 선행 의존이라 U5로(§7 참조)
- CheckInput break→continue 수정(HIGH지만 1줄)
- §1.3 죽은 코드 일괄 삭제
- **완료 조건 체크리스트에 "DX11 UI 지원 여부 결정" 포함**(Slate안 접목 — DX11 SpriteBatch
  경로가 이미 삭제된 상태를 공식 승인할지, 계속 지원할지 명시 결정)
- 검증: `ui.status` 등록↔연결 수치 일치 · 에디터/플레이어 클릭 스모크 · 씬 저장→로드 5회
  반복해 navigations 크기 불변 diff · 동일 UI 프리팹 2개 배치 후 ID 충돌 없음 · SpriteSheet가
  dx12 자가검증 스위트에서 실제 픽셀로 나오는 신규 검사

### U0.5 — WorkerPools 레이스 검증 스파이크 (조사 전용, 최소 투자)
- `Scene::UpdateRenderData`의 UI 푸시(WorkerPools 비동기, raw 컴포넌트 포인터 캡처)와 같은
  프레임 뒤쪽 `DisableOrEnable()`의 컴포넌트 파괴 사이에 join/wait가 있는지 실측 —
  이번 조사에서 **미확인**으로 남은 유일한 스레드 안전성 항목(`Scene.cpp:535-582`)
- 산출물: "레이스 있음/없음" 판정 + 있으면 join 지점 1곳. **이 판정 없이 U1 착수 금지**
  (늦게 검증하면 그 전 단계 전체가 재작업 위험을 안는다 — Slate안의 자인된 실수를 회피)

### U1 — 신규 저작 표면 골격 (구 시스템과 병행 배치)
- `UIElement`/`IUINavigable`/`IUIInputHandler` 신설, Canvas 재설계(WidgetRegistry),
  Image/Text/SpriteSheet/Button을 UIElement 파생으로 이식(`[[Property]]` 이름 최대 보존)
- RectTransform에 `m_rotationZ`/`m_localScale` 필드 신설(계산 반영은 아직)
- **`.generated.h`·`RegisterReflect.def`·`ScriptCore/UIComponents.cs` 3자 동시 갱신** —
  하나라도 어긋나면 컴파일은 되는데 GetComponent가 조용히 null을 주는 실패 모드
  (SpriteSheetComponent가 C# 바인딩 누락으로 이미 실증). SpriteSheet C# 바인딩도 이때 신설
- 검증: 위젯 100개 등록/해제 스트레스에서 등록=해제 카운트 일치 · 기존 RectTransform 회귀
  스위트를 신규 클래스로 재실행해 동일 결과 · C# GetComponent 비-null 단위 테스트

### U2 — draw 생산 단일화 + Paint dirty 게이팅
- `BuildXxxDrawData` 3개 순수 함수 신설, Create/Update 양쪽 교체
- `MarkPaintDirty()` 도입, dirty일 때만 BuildDrawElement 호출
- 프록시 발행을 Canvas 단위로 재편(씬 스코프 정책 통일)
- 검증: **Create/Update 양 경로 필드 동일성 회귀 신설**(filpEffect 재발 방지 고정) ·
  정적 화면에서 프레임당 ProxyCommand 발행 0건 계측

### U3a — dirty-root 부분 순회 (레이아웃 성능)
- `WidgetInvalidationTracker`(Scene별 dirtyLayoutRoots) + RectTransform setter 훅
- `Scene::UpdateUILayout`을 dirty-root 시작 부분 순회로 전환
- **골든 비교 하네스**(Slate안 접목): 기존 전량 순회와 신규 부분 순회를 같은 씬에 대해 A/B
  자동 비교, worldRect 오차 0.01px 이내 — y-down 부호 실수류 회귀를 기계로 잡는다
- 검증: 골든 하네스 100% 일치 · 정적 씬에서 프레임당 UpdateLayout 호출 수 감소 계측

### U3b — LayoutGroup/ContentSizeFitter (신규 레이아웃 엔진)
- H/V/Grid LayoutGroup + ContentSizeFitter, Measure→Arrange 2-pass 경로
- 검증: 중첩 LayoutGroup 스트레스 씬이 1프레임 내 진동 없이 수렴

### U3c — RectMask 클리핑 (렌더 확장)
- `RectMaskComponent` + `ImageData`(필요 시 Text/SpriteSheet 동일 패턴)에 clipRect 필드,
  셰이더 discard. **스텐실 승급 여부는 이 단계에서 명시 결정**(비사각형 마스크 로드맵 유무)
- 검증: 부모 경계 밖 자식이 실제로 잘리는 픽셀 비교 회귀(dx12 자가검증 스위트 추가)

### U4 — InputRouter 전면 교체
- ViewportContext / 정렬 역순 히트테스트 + World 레이-평면 교차 / FReply 버블 /
  Hover·Focus·Drag / `SetFocus` API / IUINavigable 게임패드 네비 / 디바운스 이동
- 구 CheckInput 경로는 이 단계에서 비활성화(U0에서 이미 동작하게 고쳐뒀으므로 A/B 비교 가능)
- 검증: 겹친 버튼 z-order 씬에서 항상 위쪽이 클릭 · World 캔버스 클릭 전용 씬 ·
  ImageComponent 없는 위젯의 게임패드 선택 · Hover/Focus/Drag 각 1개 이상 스모크

### U5 — 렌더 소비 갭 마감
- EnhancedUIPass TextData 소비(SDF 폰트 아틀라스 트랙 완료에 의존 — §7)
- 렌더 스냅샷 정렬을 WidgetRegistry 결정적 순서 기반으로 교체
- ScreenSpaceCamera 깊이 정책을 "문서화된 한계"로 확정(씬 지오메트리에 안 가려지는 현행 유지)
- 검증: Image/Text/SpriteSheet 3종 렌더 회귀를 dx12 자가검증 스위트에 모두 등록 ·
  동순위 위젯 수백 개 반복 등록/해제 후 정렬 순서가 매 실행 동일

### U6 — 에디터 정합 (심판이 지적한 누락 2건 포함)
- **인스펙터 Undo 배선**: Canvas/Image/RectTransform 커스텀 드로어에
  `Meta::MakeCustomChangeCommand` 연결(Slate안 U5 작업 정의 이식) — 컴포넌트별 Undo 가능
  여부가 갈리던 불균형 해소
- **재부모화 훅**: Hierarchy 드래그 시 캔버스 소속 재판정(UIElement Mount/Unmount 계약에
  재부모화 경로 추가)
- navigations를 제네릭 드로어에 노출(`vector<Navigation>` 분기 추가) — Text/Button에서도 편집
  가능하게
- HierarchyWindow 5곳을 신규 `UIObjectFactory`로 교체, Button 생성 스텁 실배선
- 다중 선택 기즈모 드래그의 Undo 누락(주 오브젝트만 기록)도 이때 수정
- 검증: 4개 UI 컴포넌트 전부 인스펙터 편집 Ctrl+Z 동작 · 캔버스 간 드래그 직후 소속·정렬
  즉시 갱신 확인

### U7 — 구 시스템 은퇴 + 콘텐츠 이식
- 씬/프리팹 YAML 자동 변환 도구(타입명 치환 + Navigation.navObject를 instanceID→프리팹-로컬
  인덱스로 재계산). **중첩 프리팹·오버라이드 엣지 케이스를 검증 항목에 명시**(Slate안 리스크
  인식 이식)
- Dynamic_CPP 이식(강제 작업): 위치 기반 자식 바인딩 5개 파일을 이름/명시 참조로,
  **World→Screen 공용 API 신설**(Camera에 `WorldToScreen` — 7개 파일 중복 제거),
  `CurCanvas/SelectUI` 직접 대입 2곳을 `SetFocus`로
- `UIManager.h/.cpp`·구 UIComponent 계열 파일째 삭제
- 검증: 변환 전후 worldRect 자동 스냅샷 diff · UI 스크립트 13개 게임플레이 스모크 ·
  구 심볼 참조 리포 전체 0건 grep

---

## 5. 리스크

1. **1-pass/2-pass 공존** — 발동 조건(부모 체인의 LayoutGroup 유무)이 런타임에 바뀌면 진동/
   1프레임 지연 가능. 발동 판정을 정적으로 유지하고 U3b 스트레스 씬으로 고정.
2. **TextData 소비가 SDF 트랙에 의존** — 이 재설계는 그 갭을 대신 메우지 않는다. U5가 외부
   트랙에 블로킹될 수 있음을 일정에 반영(§7).
3. **리플렉션·C# 3자 동시 갱신** — 어긋나면 "컴파일은 되는데 조용히 null". U1 검증 항목으로
   고정했지만 이후 위젯 추가 시에도 반복되는 함정.
4. **YAML 변환 도구의 Navigation 재계산** — 중첩 프리팹·오버라이드에서 조용히 잘못된 링크
   생성 가능. U7 검증에 엣지 케이스 명시.
5. **World→Screen 통합** — 7개 파일의 미세한 차이(clip.w≤0 처리, 오프셋 부호)가 의도된
   동작이었을 가능성. 통합 전 파일별 차이를 표로 만들고 화면 추적 UI(체력바)로 확인.
6. **U0.5 판정이 "레이스 있음"일 경우** — join 지점 추가가 프레임 타이밍에 영향을 줄 수 있어
   ProfilingCapturePlan의 계측으로 전후 비교 필요.

## 6. 콘텐츠 이식 비용 (정직 산정)

호환 유지는 요구가 아니므로(사용자 명시) 이것은 제약이 아니라 **비용표**다.

- **자동(낮음)**: RectTransform 필드는 이름·의미 무변경이라 YAML 값 그대로 복사. Canvas/
  위젯 4종도 타입명 치환 1회로 대부분 자동.
- **자동+검수(중간)**: Navigation instanceID→프리팹-로컬 인덱스 재계산 도구. 단순 계층은 완전
  자동, 중첩 프리팹은 수작업 검수.
- **수작업(산정)**: 게임 스크립트 13개 1,747줄 중 API 표면 대부분(240회/53파일 포함)은 이름
  보존으로 무변경 컴파일. 실제 수작업은 ① 위치 기반 바인딩 5파일(파일당 1~2시간, 총 1인일
  이내), ② World→Screen 교체 7파일(파일당 30분, 반나절 — 코드가 줄어드는 방향), ③ 전역 포커스
  대입 2곳(각 30분). **총 2~3인일** + 변환 도구 완성도 리스크.
- **이식 안 함(의도)**: `Make*` 팩토리(게임 실사용 0건), ICollision2D(콘텐츠 자체가 없음).

## 7. 범위 밖 — 명시적 미해결 (침묵 누락이 아니라 결정)

| 항목 | 이유 | 귀속 |
|---|---|---|
| SDF 폰트 아틀라스·글리프 배치 | 텍스트가 화면에 나오려면 필수지만 UI 구조가 아니라 렌더 애셋 트랙 | 별도 트랙(MaterialPipelinePlan 인접), U5가 의존 |
| `GameObject::Find` 181회/65파일 (GameManager 재탐색) | UI만의 문제가 아니라 씬 컨텍스트 주입/서비스 로케이터 설계 전반 | 오브젝트 모델 트랙 후보 — 3개 설계안 공통 누락이었음을 기록 |
| ScreenSpaceCamera가 씬 지오메트리에 안 가려짐 | 의도적 단순화인지 미완성인지 근거 부재 — v1은 현행을 문서화된 한계로 유지 | U5에서 문서화, 결정은 보류 |
| 트윈/애니메이션 상태 머신 공용화 | 6개 스크립트가 각자 재구현 중(MEDIUM) — 새 화면 제작 비용의 큰 몫이지만 UI 구조와 독립 | 후속 후보(TweenSystem) |
| 멀티플레이어 UI 슬롯 공통 개념 | 스크립트별 자체 필드로 재구현 중 — 게임 설계 결정 필요 | 후속 후보 |
| 프리팹 오버라이드 체계 전반 | instanceID 수정(C3)은 이 계획이 하지만 오버라이드 저장 구조는 프리팹 트랙 소관 | SceneGraphRedesignPlan 트랙 P |
| UI 오브젝트의 3D Transform 제거(공간 컴포넌트를 RectTransform 하나로) | UI 오브젝트마다 죽은 Transform이 실리는 문제는 UI 구조가 아니라 씬 그래프 소관 — 그쪽 S3가 이 계획의 레이아웃 트랙과 경계를 조율한다 | SceneGraphRedesignPlan 트랙 S(S3) |
