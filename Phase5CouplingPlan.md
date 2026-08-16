# PHASE 5 구조적 커플링 제거 — 실행 설계 (5-1)

작성: 2026-08-06 · 근거: include 그래프 실측 (`scripts/check_include_boundary.py`)

## 1. 경계 원칙

- **금지 방향**: RenderEngine → ScriptBinder include. 렌더는 게임플레이를 모른다.
- **허용 방향(당분간)**: ScriptBinder → RenderEngine. 게임플레이가 렌더 리소스/씬을 참조하는 것은
  현 구조의 공식 방향이다 (29파일, 별도 페이즈에서 재검토).
- **공식 데이터 경계 = 프록시/스냅샷**: `PremitiveRenderProxy`·`UIRenderProxy`·렌더 큐 +
  DX12 라이브 경로의 `CaptureLiveFrame` 밀봉본. 3-9에서 이 경계로 렌더링이 성립함을 이미 증명했다.
- **CI 게이트(래칫)**: `scripts/check_include_boundary.py` — 기존 간선은
  `scripts/include_boundary_allowlist.txt`에 동결, 새 간선은 CI 실패. 절단이 진행되면 목록을 줄인다.

## 2. 실측 현황 (2026-08-06 기준)

- RenderEngine → ScriptBinder: **55파일 · 154간선(파일×헤더)**
- 최다 인용: `Scene.h`(26) · `RenderableComponents.h`(13) · `SceneManager.h`(12) ·
  `LightProperty.h`(8) · `GameObject.h`(7) · `Terrain.h`(7)
- ScriptBinder → RenderEngine(역방향, 허용): 29파일 — `RenderScene.h`(8) · `DataSystem.h`(6) 등

## 3. 절단 전략 — 4개 카테고리

### A. 죽은 include 제거 (빠름, 위험 낮음)

심볼 사용 분석으로 32간선이 후보(예: 대부분의 패스가 `Scene.h`를 include하지만 실제로는
`RenderScene&`만 사용). 슬라이스 단위로 제거 → 컴파일 검증. 유니티 빌드가 관대하게 통과시킬 수
있으므로 최종 판정은 빌드 그린 여부.

추가: `RenderEngine/Transform.cpp`는 어느 vcxproj에도 물려 있지 않은 고아(ScriptBinder 사본의
낡은 복제) — 삭제.

### B. 렌더 데이터 헤더의 소속 정정 → `RenderEngine/Interfaces/`

다음 헤더는 게임플레이 개념이 아니라 렌더 데이터인데 ScriptBinder에 있다.
`RenderEngine/Interfaces/`(RenderEngine.vcxproj에 include 경로 기등록)로 이관하고,
ScriptBinder는 역방향(허용)으로 include한다.

| 헤더 | 내용 | 외부 소비자 |
|---|---|---|
| `LightProperty.h` | Light cbuffer, LightType, ShadowMapRenderDesc | 없음 |
| `TerrainBuffers.h` | 지형 버퍼 POD | EngineEntry/EngineSetting.h |
| `LightMapping.h` | 라이트맵 POD | 없음 |
| `FoliageInstance.h` | 폴리지 인스턴스 POD | 없음 |
| `FoliageType.h` | 폴리지 타입(이미 Model.h를 역방향 include) | 없음 |

`Navigation.h`는 분리 대상: `ClipDirection`·`TextAlignment`·`ImageInfo`(렌더 데이터)를
`Interfaces/UIRenderTypes.h`로 추출하고, 게임플레이 개념(`Navigation`, `Direction`)은 잔류.
리플렉션 어노테이션(AUTO_REGISTER_ENUM, *.generated.h)이 있으므로 이동 시 MetaGenerator
스캔 경로 확인 필수.

### C. 행위 커플링 — 인터페이스/이동 (본체, 슬라이스 반복)

1. **C1 프록시 생성 지점** — `ProxyCommand.cpp`·`MeshRendererProxy.cpp`·`UIRenderProxy.cpp`·
   `RenderScene.cpp`의 컴포넌트 읽기 코드(MeshRenderer/Decal/Foliage/Terrain/UI 컴포넌트 →
   프록시 변환)를 ScriptBinder 측 어댑터 파일로 이동. 프록시 **타입**은 렌더 소유,
   프록시 **생성**은 게임플레이 소유가 원칙.
2. **C2 씬 질의** — `SceneRenderer.cpp`(GetActiveScene/GetMeshRenderers/CreateGameObject 등)의
   씬 순회를 스냅샷 경계 뒤로 이동. DX12 라이브 경로가 참조 구현.
3. **C3 애니메이션** — `Animation.cpp`·`AnimationJob.cpp`의 Animator/AnimationController 직접
   접근. 애니메이션 갱신은 게임플레이 도메인이므로 잡 코드를 ScriptBinder 측으로 이동하는
   방향이 우선, 렌더에는 본 팔레트 결과만 전달.
4. **C4 DataSystem** — `DataSystem.cpp` → PrefabEditor/PrefabUtility/SceneManager 참조는
   5-3(에디터 UI 분리)과 한 몸. 5-3에서 함께 해소.
   같은 5-3 묶음: `Utility_Framework/ReflectionImGuiHelper.h`(924줄, 에디터 전용인데 코어에 있으며
   `SceneManager.h`·`InputManager.h`를 역참조) → `EngineGUIWindow`로 이관.
   근거·범위는 [ReflectionRetentionDecision.md](ReflectionRetentionDecision.md) §4-R1.
5. **C5 ModelLoader 물리 부착** — MeshCollider/RigidBody 생성은 콜백/팩토리 역전으로 게임플레이
   측에 위임.
6. **C6 기타** — `Camera.cpp`(InputManager: 에디터 카메라 조작 — 입력 인터페이스 추출),
   `Socket.cpp`/`Model.cpp`/`ModelLoader.cpp`(GameObject 생성 — 게임플레이 팩토리로),
   `Texture.cpp`·`ShaderSystem.cpp`(경미 — 개별 처리).

### 발견된 구조 부채 (Slice A 실행 중 실측)

- **Scene.h는 자급자족적이지 않다**: `GameObject::Index`·`GameObjectType`을 기본 인자로 쓰면서
  GameObject.h를 include하지 않는다 — includer가 GameObject.h를 먼저 include해야만 컴파일된다.
  Scene.h에 GameObject.h를 넣으면 `Scene.h → GameObject.h → GameObject.inl →
  IRegistableEvent.h → Scene.h` 순환이 닫혀서 불가. 임시 조치: Scene.h를 먼저 만나는 TU에
  GameObject.h 선행 include 삽입(LightMap·LightMapPass·PositionMapPass·SceneRenderer·
  ShadowMapPass). 근본 해소는 C2에서 IRegistableEvent의 Scene 의존(이벤트 등록 람다)을
  분리할 때 함께 — RegistableEvent<T> 본문을 별도 .inl로 빼서 순환을 끊는다.
- **암묵 전이 include 의존**: ScriptBinder/Scene.cpp가 SpriteRenderer 완전 타입을,
  RenderEngine/Camera.h가 ShadowMapConstant를 전이 include로만 받고 있었다 — 직접 include로 정정.

### D. 검증

각 슬라이스마다: ① 전체 솔루션 빌드 그린 ② `check_include_boundary.py` 간선 수 감소 확인
③ `Tools/regression` 통과(렌더 행위 변화 없음) ④ 허용 목록 축소 커밋.

★ **④를 `--update`로 하지 않는다(2026-08-09 정정).** 그 옵션은 죽은 항목을 걷어 주는
동시에, 그 순간 워킹트리에 있는 간선을 전부 허용으로 흡수한다. 같은 트리에서 다른
세션이 작업 중이면 남의 미완성 중간 상태가 래칫에 박히고, 그 뒤로는 게이트가 그것을
정상으로 취급한다(실제로 한 번 그렇게 됐다). 줄일 때는 줄을 손으로 지운다.

## 4. 실행 순서

A(죽은 include) → B(데이터 헤더 이관) → C1 → C2 → C3 → C5·C6 → (C4는 5-3에서).
각 단계는 독립 커밋. 5-2 완료 기준: 허용 목록 0줄 + CI 게이트 상시 통과.

## 5. 진행 (2026-08-09 기준)

```
간선  154 ──A── 122 ──(DX11 은퇴 배당)── 43 ──B·C1── 23 ──소형 클러스터── 14
```

| 카테고리 | 상태 |
|---|---|
| A 죽은 include | 완료 (30간선 + 고아 2파일) |
| B 데이터 헤더 이관 | 완료 (`Interfaces/`로 5종 + generated 3종) |
| C1 프록시 생성 지점 | 완료 (`ProxyCommand`·`PrimitiveProxyBridge`·`UIProxyBridge`·`RenderSceneBridge`) |
| C2 씬 질의 | 착수 (`GameObjectIndex.h` 분리로 `Scene.h` 자급자족 — 위 §발견된 구조 부채의 순환이 닫혔다) |
| C3 애니메이션 | 완료 (`AnimationJob`·`AnimationEventBridge`) |
| C5 모델 씬 인스턴스화 | 완료 (`ModelSceneBridge`) |
| C6 기타 소형 | 대부분 완료 (8간선 절단) |
| C4 DataSystem | 5-3으로 이관 |

**남은 14간선.** 이것이 허용 목록의 전부이기도 하다.

| 출발 | 도착 | 몫 |
|---|---|---|
| `DataSystem.cpp` | GameObject · PrefabEditor · PrefabUtility · Scene · SceneManager (5) | C4 → 5-3 |
| `RenderScene.cpp` | Animator · Scene · SceneManager · UIManager (4) | C2 |
| `RenderScene.h` | GameObject (1) | C2 |
| `Camera.cpp` | InputManager · MeshRenderer (2) | C6 (에디터 입력 인터페이스 추출) |
| `ShaderSystem.cpp` | ImageComponent (1) | C6 |
| `Skeleton.cpp` | Socket (1) | C6 |

★ **허용 목록을 26 → 14로 조였다(2026-08-09).** 죽은 항목이 12개 남아 있었다 —
슬라이스 B가 `*PassSetting.h`를 `RenderEngine/Interfaces/`로 옮긴 뒤, 옛 경로를
가리키던 것들이다. 그동안 게이트가 실제보다 12만큼 헐거웠다는 뜻이고,
지금은 목록 크기와 실측 간선 수가 정확히 같아 간선 하나만 늘어도 CI가 잡는다.
