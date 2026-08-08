# 엔진 코어 분리와 패키지화 — 실행 설계

작성: 2026-08-08 · 계기: "렌더러·네이티브 컴포넌트·자원을 이펙트와 동일하게
별도 프로젝트로 옮겨 엔진 코어에서 분리한다"

---

## 1. 먼저 정정 — 프로젝트는 이미 나뉘어 있다

| 프로젝트 | 파일 | 줄 |
|---|---:|---:|
| RenderEngine | 237 | 63,872 |
| Utility_Framework | 99 | 37,957 |
| ScriptBinder | 227 | 32,176 |
| EffectSystem | 54 | 19,507 |
| ImGuiHelper | 23 | 10,050 |
| Physics | 40 | 5,980 |
| SingletonManager · ManagedHeap | 14 | 646 |
| **Academy_4Q(exe)** | **46** | — |

실행 파일이 직접 컴파일하는 것은 `EngineEntry`(21) + `EngineGUIWindow`(25)
**46파일뿐**이고 나머지는 전부 정적 라이브러리다. 즉 **"별도 프로젝트로
옮긴다"는 이미 끝나 있다.**

EffectSystem이 41차 슬라이스에서 얻은 것도 프로젝트 경계였고, 그래서
PHASE 10-3(패키지화)이 여전히 남아 있다. **프로젝트 경계 ≠ 떼어낼 수 있음.**

## 2. 진짜 문제 — 코어가 코어가 아니다

계층을 거슬러 올라가는 include(하위 → 상위)를 전부 셌다. **56간선.**

| 방향 | 간선 | 성격 |
|---|---:|---|
| RenderEngine → ScriptBinder | 23 | PHASE 4-2가 줄이는 중(154 → 23) |
| **Utility_Framework → ScriptBinder** | 9 | 코어가 게임플레이를 안다 |
| **Utility_Framework → RenderEngine** | 7 | 코어가 렌더를 안다 |
| **Utility_Framework → EngineEntry** | 4 | **코어가 실행 파일을 안다** |
| **RenderEngine → EngineEntry** | 4 | 라이브러리가 실행 파일을 안다 |
| ScriptBinder → EffectSystem | 4 | 게임플레이가 이펙트 구현을 안다 |
| ScriptBinder → EngineEntry | 3 | 〃 |
| Utility_Framework → EffectSystem · ImGuiHelper | 2 | 〃 |

★ **`Utility_Framework`는 이름이 코어인데 ScriptBinder · RenderEngine ·
EngineEntry · EffectSystem · ImGuiHelper를 전부 참조한다.** 코어가 위로
손을 뻗고 있으면 위를 떼어낼 방법이 없다 — 떼는 순간 코어가 안 선다.
**이것이 패키지화를 막는 실제 장애물이고, 프로젝트 파일과는 무관하다.**

### 2.1 범인은 소수 파일에 몰려 있다

| 파일 | 역방향 간선 | 비고 |
|---|---:|---|
| `Utility_Framework/EngineBootstrap.h` | **12** | 부트스트랩이 코어에 있다. 소비자는 셋뿐 |
| `Utility_Framework/ReflectionImGuiHelper.h` | 3 | 에디터 전용. PHASE 4 C4가 이미 지목 |
| `RenderEngine/DataSystem.{h,cpp}` | 6 | 자산·에디터 시스템이 렌더러 안에 있다 |
| `EngineEntry/EngineSetting.h`를 향한 참조 | 7 | 설정이 최상위인데 모두가 읽는다 |
| `RenderEngine/Socket.{h,cpp}` | 4 | 게임 오브젝트 생성 |
| `RenderEngine/RenderScene.{h,cpp}` | 4 | 씬 순회 |

56간선 중 **36간선이 위 여섯**에 있다.

## 3. 목표 계층

```
[6] EngineEntry · EngineGUIWindow      실행 파일 · 에디터 셸
[5] EffectSystem · Terrain(신설)       떼어낼 수 있는 패키지
[4] ScriptBinder                       네이티브 컴포넌트 · 씬 · 생명주기
[3] RenderEngine                       렌더러 · 자원
[2] ImGuiHelper · Physics              외부 계통 래퍼
[1] Utility_Framework                  ★ 코어 — 위를 몰라야 한다
[0] SingletonManager · ManagedHeap     토대
```

규칙 하나: **화살표는 아래로만.** 위가 필요하면 코어에 인터페이스를 두고
위가 구현을 등록한다(EffectSystem의 `IEffectSystem`이 PHASE 10-3에서 쓰려는
그 방식과 같다).

## 4. 절단 순서

착수 비용이 낮고 간선을 많이 줄이는 것부터. 각 슬라이스는 독립 커밋이고,
판정은 **① 전체 빌드 ② 역방향 간선 수 감소 ③ 회귀 통과**다.

### P1 — 부트스트랩을 위로 (12간선)

`EngineBootstrap.h`를 `Utility_Framework/` → `EngineEntry/`로 옮긴다.
부트스트랩은 "모든 것을 아는 자리"라 최상위에 있어야 한다. 소비자가
`EngineEntry/App.cpp` · `EngineEntry/ShutdownFinalMarker.cpp` ·
`TrainAsis/GameApp.cpp` 셋뿐이고, TrainAsis는 이미 `$(SolutionDir)EngineEntry\`를
포함 경로에 갖고 있어 이동만으로 끝난다.

### P2 — 설정을 아래로 (7간선)

`EngineSetting`이 최상위에 있는데 코어·렌더·게임플레이가 다 읽는다.
설정 **데이터**를 코어로 내리고, 설정 **파일 입출력·에디터 UI**는 위에 남긴다.
`RenderPassSettings`·`TerrainBuffers`처럼 이미 아래에 있는 것들과 합류한다.

### P3 — 에디터 전용을 셸로 (3 + 6간선)

`ReflectionImGuiHelper.h` → `EngineGUIWindow/`(PHASE 4 C4가 이미 지목).
`DataSystem`의 에디터 UI 부분 → `EngineGUIWindow/`(PHASE 4-3과 한 몸).
남는 자산 로딩부는 렌더가 아니라 코어 소속인지 다시 본다.

### P4 — 렌더가 게임플레이를 놓는다 (23간선)

PHASE 4-2가 진행 중. `Socket`·`RenderScene`의 GameObject 생성·씬 순회를
게임플레이 측 팩토리로 역전한다(PHASE 4 C2·C6).

### P5 — 패키지 경계 세우기

여기까지 오면 코어가 위를 모른다. 그때 비로소 "빼도 도는가"를 물을 수 있다.

| 패키지 | 인터페이스 | 페이즈 |
|---|---|---|
| EffectSystem | `IEffectSystem` | 10-3 |
| Terrain | `ITerrainSystem` | 11-4 |
| Physics | 이미 얇다 — 확인만 |
| RenderEngine | ★ 뺄 수 없다 — 아래 참고 |

★ **렌더러는 패키지가 아니다.** 이펙트·지형은 없어도 엔진이 돌지만 렌더러가
없으면 화면이 없다. 렌더러에 대해 의미 있는 목표는 "떼어내기"가 아니라
**백엔드 교체 가능**이고, 그것이 PHASE 3의 R축(RHI 경계)이다. 이미 진행 중이다.

## 4.1 지형 패키지 — 실측과 순서 (2026-08-08)

### 규모

| 파일 | 줄 | 현재 소속 |
|---|---:|---|
| `Terrain.cpp` · `Terrain.h` · `TerrainBrush.h` | 1,549 | ScriptBinder |
| `TerrainCollider.{h,cpp}` | 135 | ScriptBinder |
| `TerrainMaterial.{h,cpp}` · `TerrainMesh.h` | 444 | RenderEngine |
| `TerrainBuffers.h` | 84 | RenderEngine/Interfaces |
| `ImGuiDrawHelperTerrainComponent.cpp` | 357 | EngineGUIWindow |
| **합계** | **2,569** | 네 프로젝트에 흩어짐 |

셰이더 둘(`TerrainTexture.cs` · `TerrainDebugBrush.ps`)도 함께 간다.

### ★ 파일을 옮기기 전에 의존을 역전해야 한다

지금 옮기면 **프로젝트 순환**이 닫힌다:

```
Terrain → ScriptBinder   Terrain.cpp가 Scene.h · SceneManager.h를 include
ScriptBinder → Terrain   Scene.h가 std::vector<TerrainComponent*>를 멤버로 든다
```

엔진이 지형을 **타입으로 아는 자리**를 세었다 — 열 곳이다:

| 바깥 | 지형에 대한 앎 |
|---|---|
| `ScriptBinder/Scene.h` | `m_terrainComponents` 멤버 + `Collect/UnCollect/GetTerrainComponent` |
| `RenderEngine/RenderScene.h` | `RegisterCommand`·`InvaildCheckTerrain`·`UpdateCommand`·`MakeProxyCommand`의 `TerrainComponent*` 오버로드 |
| `RenderEngine/ProxyCommand.h` | `ProxyCommand(TerrainComponent*)` 생성자 |
| `RenderEngine/MeshRendererProxy.h` | `PrimitiveProxyType::TerrainComponent` + 지형 전용 필드 넷 |
| `ScriptBinder/PhysicsManager.h` | `AddCollider/RemoveCollider(TerrainColliderComponent*)` |
| `ScriptBinder/FoliageComponent.h` | `AddInstanceFromTerrain` 등 셋 — 폴리지가 지형에 직접 의존 |
| `EngineEntry/EngineSetting.h` | `TerrainBrush* terrainBrush` 전역 |
| `Utility_Framework/PathFinder.h` | `TerrainSourcePath` |
| `RenderEngine/DataSystem.h` | `FileType::TerrainTexture` |
| `EngineGUIWindow/InspectorWindow.cpp` · `SceneViewWindow.cpp` | 에디터 배선 |

이것이 **11-4(패키지화)의 실제 내용**이고, 프로젝트 파일 만들기가 아니다.

### ★ 순서를 뒤집는다 — 패키지를 먼저 세운다

원래 PHASE 11은 `11-1 자료 정리 → 11-2 렌더 패스 → 11-3 폴리지 → 11-4 패키지화`
였다. **패키지화를 앞으로 당긴다.**

근거는 EffectSystem이다. 그쪽은 엔진 안에서 다 만든 뒤 41차 슬라이스에서
54파일을 떼어냈고, 그래서 10-3(패키지화)이 아직 남아 있다 — 나중에 자르는
것이 처음부터 밖에서 쓰는 것보다 비쌌다. 지형은 **렌더 경로가 아직 없으므로**
그 실수를 반복할 이유가 없다: 경계를 먼저 세우고 `EnhancedTerrainPass`를
그 안에서 쓰면, 경계를 넘는 코드가 애초에 생기지 않는다.

바뀐 순서:

1. **의존 역전** — 위 열 곳에서 지형 타입을 걷는다(등록·인터페이스로).
2. **프로젝트 신설과 이동** — `Terrain` 정적 라이브러리, 2,569줄 이동.
3. **CPU 진실 정리**(구 11-1) — 조각 결과가 `m_vertices`에 반영되게.
4. **렌더 패스 신규 작성**(구 11-2) — 패키지 **안에서** 쓴다.
5. **폴리지·편집 기즈모**(구 11-3) — 폴리지도 같은 패키지로.

## 5. 완료 기준

1. 역방향 간선 **0** — `scripts/check_include_boundary.py`가 계층 규칙 전체를
   검사하도록 확장(지금은 RenderEngine → ScriptBinder 한 쌍만 본다).
2. `Utility_Framework`가 `SingletonManager`·`ManagedHeap` 외에 아무것도
   include하지 않는다.
3. EffectSystem·Terrain 프로젝트를 솔루션에서 빼도 나머지가 빌드되고 실행된다.
4. 각 패키지가 자기 자가 검증을 들고 있고, 빼면 그 검증도 함께 빠진다.
