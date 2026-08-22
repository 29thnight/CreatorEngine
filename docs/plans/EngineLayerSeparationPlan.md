# 엔진 레이어 분리 계획 — Runtime Core와 Editor의 물리적 분리

작성: 2026-08-08

재분석: 2026-08-21

상태: 실행 기준 문서

이 문서는 CreatorEngine을 **Runtime Core**, **Editor**, **Host**, **Project**로
분리하는 계획의 기준이다. 빌드·쿡·패키징 절차는 `BuildPipelinePlan.md`가 담당하고,
이 문서는 소스 소유권, 프로젝트 경계, 의존 방향, 런타임 수명주기만 다룬다.

핵심 목표는 Player의 파일 형식을 바꾸는 것이 아니다.

> **Editor가 Runtime Core를 사용하는 상위 애플리케이션이 되고, Runtime Core는
> Editor 타입·UI·실행 모드·경로 정책을 전혀 모르게 한다.**

Player의 DLL화나 저장소 폴더 재배치는 이 경계가 닫힌 뒤 선택할 수 있는 후속
패키징 작업이다.

---

## 1. 범위와 결정

### 1.1 이번 계획이 해결하는 것

- Core 프로젝트에 편입된 Editor 전용 구현을 별도 프로젝트로 이동한다.
- Core 안의 `EngineMode::IsEditor/IsPlayer`, `BUILD_FLAG` 정책 분기를 Host로 올린다.
- Editor의 play-mode, Undo, Selection, Prefab 편집 수명주기를 Scene runtime에서
  분리한다.
- Editor 렌더 pass와 Editor camera를 RenderCore에서 분리한다.
- 런타임 asset loading과 Editor asset authoring/import 기능을 분리한다.
- include뿐 아니라 `vcxproj` 소스 편입과 `ProjectReference`까지 단방향으로 검증한다.

### 1.2 이번 계획의 범위에서 제외하는 것

- `Player.exe`를 `Player.dll`로 바꾸는 일은 완료 조건이 아니다.
- 게임 쿡·pak·C# 배치·CI 패키징 구현은 `BuildPipelinePlan.md`의 범위다.
- 모든 Core 라이브러리를 하나의 거대한 DLL이나 public facade로 합치지 않는다.
- 폴더 이름 변경과 대규모 `git mv`는 의미 경계가 안정된 후의 선택 작업이다.
- `Debug`라는 이름이 붙었다는 이유만으로 런타임 진단 데이터까지 Editor로 옮기지
  않는다. 수집은 Core, 표시와 조작은 Editor가 기본 원칙이다.

---

## 2. 2026-08-21 현재 소스 기준선

과거 계획의 `TrainAsis`, `GameBuild.sln`, `SingletonManager`, `ManagedHeap`,
`Dx11Main` 전제와 과거 역방향 간선 수는 현재 구조를 설명하지 못하므로 제거했다.
현재 기준선은 다음과 같다.

### 2.1 물리적 빌드 구조

- `Academy_4Q.vcxproj`가 `EngineEntry`와 `EngineGUIWindow` 소스를 직접 컴파일한다.
  Editor가 별도 라이브러리 계층이 아니라 실행 파일 내부의 소스 묶음이다.
- `RenderEngine.vcxproj`가 `ImGuiHelper`와 `ScriptBinder`를 직접 참조한다.
- `Player.vcxproj`도 `ImGuiHelper`를 참조한다.
- `scripts/check_include_boundary.py`는 E0 첫 슬라이스에서 include뿐 아니라 프로젝트
  참조, Editor 소스 편입, 금지 include와 모드/매크로 occurrence까지 검사하도록
  확장됐다. 기존 부채는 `scripts/layer_boundary_allowlist.txt`에 수동 동결한다.

검색 기준 수치는 다음과 같다. 이 수치는 진행률 기준선이며 완료 판정은 고정 숫자가
아니라 자동 검사 결과로 한다.

| 항목 | 현재 값 |
|---|---:|
| 역방향·금지 `ProjectReference` | 2 |
| Core 프로젝트의 Editor 소스 편입 | 21 |
| Core의 Editor/ImGui 계열 금지 include occurrence | 54 |
| Core 4프로젝트의 `EngineMode::IsEditor/IsPlayer` 분기 | 8 |
| Core 4프로젝트의 `BUILD_FLAG` 조건부 지시문 | 13 |
| E0 물리 경계 부채 합계 | 98 |

Core 4프로젝트는 현재 `Utility_Framework`, `RenderEngine`, `Physics`,
`ScriptBinder`를 뜻한다.

### 2.2 혼합 책임 인벤토리

| 현재 위치 | Core에 남길 책임 | 상위 레이어로 옮길 책임 |
|---|---|---|
| `Utility_Framework/CoreWindow` | native window primitive가 필요하면 최소 구현만 | Editor/Player 창 정책, 메시지 중계 정책 |
| `Utility_Framework/RuntimeSettings` + `EngineEntry/EditorSettingsStore` | 실행 중인 backend·render pass·startup scene | Editor preference와 Player build 선택의 저작·저장. MSVC/MSBuild 설정은 별도 상태가 아니라 `build.ps1` 책임 |
| `Utility_Framework/PathFinder` | 주입받은 경로의 조회 | 프로젝트 선택, `%TEMP%` 패키지 경로 결정, 실행 모드 분기 |
| `RenderEngine/DataSystem` | asset cache/load, GUID/catalog 조회, bundle 유지 | source scan, watcher, meta, import/save, picker, FileDialog. ShellExecute/Explorer는 `EngineEntry/EditorPlatform`으로 이동 완료 |
| `EnhancedSceneRendererLive` | runtime view와 render pipeline 실행 | Editor camera, grid/gizmo/wireframe/icon pass 조립 |
| RenderEngine의 ImGui host/shell | RHI의 일반 presentation 계약만 | ImGui backend와 Editor texture adapter |
| RenderEngine의 self-test/benchmark | 런타임 계측 원시 데이터 | 테스트 실행기, benchmark UI, 개발자 명령 |
| `ScriptBinder/SceneManager` | scene load/unload, activation, simulation, 직렬화 primitive | Edit→Play→Stop 백업·복원, Undo/Selection, Editor 이벤트 |
| `ScriptBinder/PrefabEditor` | prefab runtime instantiate/serialize primitive | prefab 편집 세션 오케스트레이션 |
| BT/Animation node 자료형 | 런타임 graph 데이터 | node-editor pin/layout/build 자료 |
| `PhysicsDebug` | 필요 시 진단 데이터 수집 | 표시 UI와 Editor 조작 |
| `EngineBootstrap` | Core manager 초기화/종료 순서 | Undo 초기화, Editor/Player 정책과 adapter 설치 |

### 2.3 가장 큰 결합 지점

1. **`DataSystem`**은 runtime asset service와 Editor asset database/UI를 아직 한
   클래스에 묶고 있다. OS shell 책임은 `EditorPlatform`으로 분리했지만 watcher/meta
   writer와 picker/icon/font가 Core에 남아 있으므로 단순한 ImGui 제거로 끝나지 않는다.
2. **`SceneManager`**는 runtime scene lifecycle과 Editor play-mode transaction을
   함께 소유한다. Undo와 selection을 옮기려면 먼저 snapshot/restore primitive를
   분리해야 한다.
3. **`EnhancedSceneRendererLive`**는 Editor pass와 Editor camera를 직접 소유한다.
   기존 `LivePipelineDesc`를 확장 경계로 바꾸는 것이 최소 변경 경로다.
4. **`CoreWindow`·옛 `EngineSetting`·`PathFinder`**가 Host 정책을 Foundation에
   내려보내던 결합은 E1에서 절단 중이다. window/path와 설정 저장 책임은 이미
   Host·Editor 쪽으로 이동했고, 남은 판정은 새 배선의 빌드·제품 회귀와 E3/E6의
   공통 bootstrap·물리 프로젝트 경계다.

---

## 3. 목표 구조

의존 화살표는 위 레이어가 아래 레이어를 사용한다는 뜻이다.

```text
CreatorEditor.exe
 ├─ EditorUI
 ├─ EditorRuntime
 └─ EditorRender
          │
          ▼
HostRuntime ──────► EngineRuntime
                         ├─ AssetRuntime
                         ├─ SceneRuntime / ScriptRuntime
                         ├─ RenderCore
                         ├─ PhysicsCore
                         └─ Foundation

Player.exe 또는 Player.dll
 └─ PlayerRuntime ─────► HostRuntime / EngineRuntime

DeveloperTools / RenderTests
 └────────────────────► EngineRuntime / RenderCore
```

### 3.1 목표 프로젝트와 책임

| 레이어/프로젝트 | 책임 | 금지 의존 |
|---|---|---|
| `Foundation` | 자료구조, delegate, logging, reflection 기초 | Editor, Player, ImGui, project path 정책 |
| `RenderCore` | RHI, RenderGraph, runtime pass, render resource | Editor pass, ImGui backend, `ScriptBinder` concrete type |
| `PhysicsCore` | simulation과 query, 진단 데이터 수집 | Editor UI |
| `AssetRuntime` | packaged catalog 기반 runtime load/cache | watcher, meta authoring, FileDialog, ShellExecute |
| `SceneRuntime/ScriptRuntime` | GameObject/component, scene, script lifecycle | Undo, Selection, Prefab edit session, node-editor |
| `EngineRuntime` | 위 runtime subsystem의 초기화·tick·종료 조정 | Editor/Player 모드 판정 |
| `HostRuntime` | window/message loop, launch config, 공통 bootstrap 계약 | Editor window 구체 타입 |
| `EditorRuntime` | project session, asset database, play mode, Undo/Selection, prefab edit | Player |
| `EditorRender` | Editor camera, grid/gizmo/wireframe/icon, Scene View 기여 | Player |
| `EditorUI` | 현재 `EngineGUIWindow`의 창과 inspector | Player |
| `PlayerRuntime` | package 준비, startup scene 요청, smoke/종료 정책 | Editor, EditorRender, EditorUI |
| `DeveloperTools/RenderTests` | self-test, benchmark, 개발자 명령과 표시 | shipping runtime의 필수 초기화 경로 |

`AssetRuntime`, `SceneRuntime`, `RenderCore` 같은 이름은 먼저 **책임 경계**를 뜻한다.
초기 단계에서는 기존 static library를 유지해도 된다. 새 프로젝트는 실제 소스가
이동하는 슬라이스에서만 만들며 빈 껍데기 프로젝트를 미리 만들지 않는다.

### 3.2 경계 규칙

1. Editor는 Core 내부 API를 사용할 수 있다. 초기 분리에서 거대한 public engine
   facade를 먼저 만들지 않는다.
2. Core는 Editor 타입, 폴더, 매크로, ImGui UI, Editor/Player 실행 모드를 알 수 없다.
3. 동작 차이는 Core 내부의 모드 분기가 아니라 Host가 전달하는 설정·경로·adapter로
   만든다.
4. `#ifdef EDITOR`나 `BUILD_FLAG`로 Core 안의 Editor 코드를 가리는 것은 완료가
   아니다. 해당 코드가 Core 프로젝트에서 컴파일되지 않아야 한다.
5. 프로젝트 참조와 소스 편입이 실제 경계의 증거다. 폴더 위치만으로 판정하지 않는다.
6. 종료 순서는 생성 순서의 역순이며, Editor contributor와 delegate는 Core 종료 전에
   해제한다.
7. Editor의 GameObject/component, C# lifecycle, UI, DDOL 계약은 유지한다. 외부 엔진의
   구조를 복사하기보다 현재 계약에 필요한 수명주기 경계만 도입한다.

---

## 4. 필요한 경계 계약

새 계약은 실제 역방향 호출이 있는 지점에만 추가한다. 범용 service locator를 새로
만들지 않는다.

### 4.1 Host 설정과 경로

```cpp
struct EnginePaths
{
    std::filesystem::path runtimeRoot;
    std::filesystem::path assetRoot;
    std::filesystem::path managedRoot;
    std::filesystem::path cacheRoot;
};

struct EngineLaunchConfig
{
    EngineRunMode compatibilityRunMode;
    EnginePaths paths;
    RuntimeContentPrepare prepareRuntimeContent;
    HostSettingsInitialize initializeHostSettings;
    WindowDesc window;
};
```

- Editor와 Player가 경로·창·Host capability를 각각 결정한다.
- 공통 bootstrap은 Host가 runtime content를 준비한 뒤 Core `RuntimeSettings`를
  초기화하고, Editor만 `initializeHostSettings`로 저작 설정 store를 붙인다.
- Core 소비자는 `RuntimeSettings`의 값 snapshot/apply API만 사용한다. Host 저작
  설정이나 YAML writer를 Core config에 다시 넣지 않는다.
- `PathFinder`가 필요하면 전역 정책 결정자가 아니라 초기화 후 읽기 전용 view로
  축소한다.

윈도우 생성에는 `WindowDesc`를 사용하고, 메시지 전달은 `IWindowMessageSink` 또는
등록형 callback으로 역전한다. `CoreWindow`가 `WinProcProxy`나 Editor 여부를 직접
확인하지 않는다.

### 4.2 Asset 경계

- `AssetRuntime`: packaged catalog, cache, load, retain/release.
- `EditorAssetDatabase`: source scan, watcher, meta, import/reimport, authoring save.
- `EditorAssetPresentation`: picker, icon/font, texture type selector.
- `EditorPlatform`: FileDialog, Explorer, URL/IDE 실행. OS shell open/reveal과
  volume-profile FileDialog는 Editor Host로 이동 완료했다.

Editor가 import를 완료하면 runtime이 이해하는 catalog/asset 변경 이벤트만 전달한다.
Player는 source directory를 감시하거나 meta 파일을 생성하지 않는다.

### 4.3 Editor play-mode 경계

`SceneManager`에는 다음 primitive만 남긴다.

- scene load/unload/activate
- simulation start/stop
- scene snapshot serialize/restore
- 안전한 구조 변경 적용 지점

새 `EditorPlayModeController`가 이를 조합해 다음 transaction을 소유한다.

```text
Enter Play
  Editor scene snapshot → Undo/Selection 정리 → simulation start

Exit Play
  simulation stop → snapshot restore → prefab/selection 재연결
```

`PrefabEditor`는 UI 창이 아니라 Editor scene session 오케스트레이터이므로
`EditorUI`가 아니라 `EditorRuntime`에 둔다. runtime prefab instantiate/serialize
기능만 Core에 남긴다.

### 4.4 Render 확장 경계

기존 `LivePipelineDesc`를 일반적인 기여 지점으로 확장한다.

```cpp
struct IRenderFeatureContributor
{
    virtual void Contribute(
        LivePipelineDesc& pipeline,
        const RenderViewContext& view) = 0;
};
```

- `EditorRender`가 Editor pass instance와 Editor camera를 소유한다.
- Editor 초기화 때 contributor를 등록하고 Core 종료 전에 해제한다.
- `isEditorView`는 `RenderViewFlags` 또는 일반적인 view capability로 바꾼다.
- RenderCore는 concrete Editor pass나 `GizmoRenderer::GetActive()`를 호출하지 않는다.
- `EnhancedUIPass`는 게임 UI이므로 Editor로 이동하지 않고 runtime UI 경로로
  재배치한다.
- 장기적으로 RenderCore는 `ScriptBinder` concrete type 대신 render snapshot/packet을
  소비한다. 기존 RenderEngine→ScriptBinder 절단 트랙과 같은 방향이다.

### 4.5 ImGui 경계

`ImGui 사용 == Editor 전용`으로 일괄 분류하지 않는다.

- Inspector, picker, Editor overlay: `EditorUI` 또는 `EditorRender`.
- DX12/Vulkan ImGui backend: 과도기에는 `HostImGuiPresentation`.
- runtime 게임 UI: RenderCore의 `EnhancedUIPass` 계통.
- Player가 ImGui presentation을 필요로 하지 않게 되면 `Player.vcxproj`의
  `ImGuiHelper` 참조를 제거한다.

---

## 5. 실행 계획

각 소슬라이스는 독립 커밋으로 만들고, 의미 이동과 폴더 대이동을 같은 커밋에 섞지
않는다. 아래 순서는 의존성과 회귀 격리 순서다.

### E0 — 기준선과 경계 게이트 구축 ✅ 완료 (2026-08-21)

아래 실행 결과는 각 명시된 snapshot의 증거다. 이후 소스 변경은 같은 gate를 다시
통과해야 하며, 과거 성공을 현재 바이너리의 성공으로 간주하지 않는다.

1. 현재 Editor/Player 빌드와 smoke 기준선을 기록한다.
2. `check_include_boundary.py`에 다음 검사를 추가한다.
   - `ProjectReference` 방향
   - Core `vcxproj`의 Editor 경로 소스 편입
   - Core의 금지 include: Editor, EngineEntry, EngineGUIWindow, ImGui/node-editor
   - Core의 `EngineMode`, `BUILD_FLAG` 잔여 수
3. 삭제된 프로젝트를 아직 참조하는 CI 항목을 현재 솔루션과 맞춘다.
4. Core 4프로젝트만 빌드하는 레그와 비유니티 빌드 레그를 유지한다.

2026-08-21 첫 슬라이스:

- ✅ 프로젝트 참조·소스 편입·금지 include·`EngineMode`·`BUILD_FLAG` occurrence
  래칫 추가. 기존 부채 98건을 수동 baseline으로 동결했다.
- ✅ 가상 신규 `EngineMode` 분기 1건을 gate가 실패시키는 음성 테스트를 통과했다.
- ✅ CI에서 삭제된 `SingletonManager`·`ManagedHeap` target/산출물 참조를 제거했다.
- ✅ Utility와 변경 Editor/Player App TU의 비유니티 컴파일·재링크에 더해,
  Core 4프로젝트(`Utility_Framework`, `Physics`, `ScriptBinder`, `RenderEngine`)의
  Debug x64 비유니티 전수 컴파일·링크를 통과했다. Entity 전환 중 드러난
  `RenderProxy/PrimitiveProxyBridge` 연쇄 오류는 proxy 계약을 바꾸지 않고 필요한
  타입 정의를 직접 include해 닫았고, 함께 노출된 헤더 자급성과 잘못된 상대 include도
  수정했다.
- ✅ Editor/Player가 `Time->Tick` 전에 이전 프레임 delta를 읽던 순서를 고쳤다.
  현재 프레임 delta를 callback 첫머리에서 확정한 뒤 simulation에 넘기며, Player
  로그에서 첫 프레임 `PxScene::simulate(0)` 실패가 0건임을 확인했다.
- ✅ 재링크한 Editor는 첫 프레임 전 종료 6/6, script attach lifecycle 1회,
  DDOL 관리 훅/handle 유지, lifecycle 221사건 순서 대조를 모두 통과했다.
- ✅ 기존 pak은 1,532개짜리 8월 15일 산출물이라 8월 16일 추가된
  `WorldSprite.hlsl`이 없었다. 오류를 숨기던 DX12 pipeline 구축 분기에 원문
  로깅을 추가하고, smoke는 renderer가 영구 비활성화되면 timeout 대신 exit 4로
  실패하게 했다.
- ✅ 생성 정본인 `Tools/featuretest/build-scenes.ps1 -Only FT_Primitives`로 startup
  scene을 현재 `m_Entities`/`Entity`/component-owned `Transform` 스키마로 다시
  저작했다. 11개 Entity의 계층과 transform digest는 저장·재로드 뒤 동일했다.
- ✅ B2 자동화로 현재 Workspace 입력 170개를 독립 candidate에 패킹·검증한 뒤에만
  immutable release와 current pointer를 게시한다. 동일 입력 반복에서 content/runtime/
  distribution digest가 일치했고 startup scene, managed type 25종,
  `OnInitialized → OnBeginSimulation`, display/promotion 2회, exit 0을 통과했다.
- ✅ 최신 snapshot의 일반(non-smoke) Player를 `WM_CLOSE`로 종료했을 때 exit 0,
  PID별 runtime root를 실제로 관측한 뒤 제거됐고 숫자형 sibling PID root와 parent
  snapshot, Stage file-set/Pak/Player hash가 모두 보존됐다.
  runtime/StageRoot junction 선점과 AssetPacker의 중첩 출력·child junction 음성
  테스트도 격리된 project/target과 외부 sentinel을 보존하며 실패했다.
- ⚠️ 렌더 failure marker는 없었지만 PhysX GPU 미지원에 따른 software fallback과
  MeshOptimizer LOD 경고는 남는다. 이를 "physics/renderer 오류 0"으로 과장하지 않는다.
- ⚠️ canonical `FT_Primitives.creator`, runtime settings template, smoke probe와 패키지
  도구가 아직 HEAD에 없고 FMOD import/runtime 바이너리 공급도 저장소 밖이다.
  `Tracked`는 working tree fallback 없이 실패하고 기존 current를 보존한다. 따라서 현재
  증거는 Workspace와 Project 제품 gate까지이며 clean-checkout/CI 산성 테스트는 미완료다.
- ✅ 최신 Release Editor의 제품 진입 `--exec game.pak --exec quit`은 exit 0이었고
  `Project/BuildNative/WORKTREE`, startup/backend, verification/smoke와 digest가 채워진
  immutable release/current pointer를 게시했다.

판정:

- Core-only·비유니티·Editor 전체 빌드와 Editor 수명주기 회귀가 통과한다.
- Player 기준선은 startup scene load, display slot 2회 이상 promotion,
  `[SMOKE]` 완료 marker, exit 0을 모두 요구한다.
- renderer가 영구 비활성화되면 timeout이 아니라 원인 로그와 exit 4로 실패한다.
- 현재 경계 부채는 baseline으로 보이되 새 부채는 실패하며, include 0만으로
  완료 처리하지 않는다.
- Workspace 자동 package gate와 정상 종료/경계 음성 테스트가 통과한다.
- clean checkout의 동일 판정은 canonical 입력의 HEAD 편입과 FMOD 공급을 닫은 뒤
  `BuildPipelinePlan.md`의 `Tracked` gate로 판정한다.

### E1 — Host·설정·경로 정책 분리 ◐ 진행 중

1. `EngineLaunchConfig`, `EnginePaths`, `WindowDesc`를 도입한다.
2. `CoreWindow`의 Editor/Player 분기를 각 Host의 window policy로 옮긴다.
3. `WinProcProxy`를 Editor/Host presentation으로 이동하거나 message sink로 교체한다.
4. `EngineSetting`을 Core의 `RuntimeSettings`와 Editor의
   `EditorPreferences`·`BuildSettings`·단일 `EditorSettingsStore`로 분리한다.
   소비자가 없는 toolchain 상태는 새 singleton으로 옮기지 않고 제거한다.
5. package unpack과 startup scene 정책을 Player bootstrap으로 이동한다.
6. Undo 초기화·종료를 공통 bootstrap에서 Editor bootstrap으로 이동한다.

2026-08-21 첫 슬라이스:

- ✅ `EngineLaunchConfig`, `EnginePaths`, `WindowDesc`를 도입하고 Editor/Player
  composition root가 서로 다른 값과 capability를 구성한다.
- ✅ `CoreWindow`의 모드 분기와 ImGui/`WinProcProxy` 의존을 제거했다.
- ✅ `CoreWindow`를 공통 bootstrap 소유로 바꾸고 복사를 금지했다. App과 presentation이
  먼저 종료되고 HWND가 나중에 파괴된다.
- ✅ `WinProcProxy.cpp`는 Utility library에서 제외하고 Editor exe만 컴파일한다.
  실제 파일 이동은 E6의 프로젝트 물리 경계 확정 때 수행한다.
- ✅ `PathFinder`와 `EngineSetting`의 `EngineMode` 분기를 Host 경로와 capability
  주입으로 교체했다. Utility의 `EngineMode::IsEditor/IsPlayer` 참조는 0이다.
- ✅ pak 출력 위치도 실행 파일의 우연한 폴더 깊이 대신 Host가 넘긴 project root에서
  계산한다. B2는 live settings와 분리된 package 입력, runtime settings overlay,
  candidate 검증 후 원자 publish를 구현했다.
- ✅ Player Host가 `%TEMP%\CreatorEngine\Player` owner, `<PID>` process,
  `RuntimeContent`/`RuntimeData`를 한 번만 결정해 `EnginePaths`로 주입한다. Utility의
  `GetTempPathW`/PID/제품명 재조립과 미사용 legacy TEMP unpack overload는 제거했다.
- ✅ 삭제 capability는 임의 `(target, parent)`가 아니라 `EnginePaths + RuntimeCleanupScope`를
  받는다. packaged Host일 때만 owner→process→content/data와 project/assets 관계를
  case-insensitive exact component로 검증하고, target은 API 내부에서만 고른다. Editor는
  owner/process가 비어 있어도 기존 경로 계약으로 부팅한다.
- ✅ Player는 공통 bootstrap 전에 자신의 stale process root 전체를 정리하고, bootstrap은
  첫 runtime write 전에 ownership을 다시 검증한다. `RuntimeContent`와 `RuntimeData` 양쪽
  ancestor를 검사하며, log directory 준비/Log sink 초기화 실패는 terminate 대신 exit 2로
  닫힌다. 정상 종료 cleanup 실패는 더 이상 exit 0으로 숨지 않고 exit 5를 반환한다.
- ✅ 정상 종료 gate는 실제 PID root 생성·exact-root 제거·sibling/Stage/Pak 보존을 판정한다.
  owner junction 음성 gate는 smoke cleanup 생략 없이 exit 2, 명시적 cleanup 거부와 외부
  sentinel 보존까지 확인한다. Core TEMP 정책 재유입은 경계 래칫이 신규 부채로 실패시킨다.
- ⚠️ lineage 검증과 `remove_all` 사이 child-junction 교체 TOCTOU 및 실제 동시 두 Player
  수명 교차는 아직 자동 gate가 없다. exact-root 설계 증거를 race-free 삭제 증거로
  확대하지 않고 B2 운영/보안 후속으로 유지한다.
- ✅ pak unpack 책임은 Player Host로 이동했다. Player가 명시적인 pak/extract 경로를 쓰는
  `prepareRuntimeContent` capability를 주입하고, 공통 bootstrap은 PathFinder·Log·dump 준비 뒤
  `RuntimeSettings` 로드 전에 이 Host callback의 안전한 실행 시점만 보장한다.
- ✅ `EngineSettingLaunchOptions::preparePackagedAssets`, Core의 `PakHelper` include와 unpack
  분기를 제거했다. 준비 실패는 초기화 실패(exit 2)로 닫고, 실패한 smoke도 PID process root를
  정리한다. package smoke·제품 `game.pak`·runtime junction·missing-pak·정상 종료 gate가
  새 배선을 통과했다.
- ✅ 설정 물리 분리와 전체 build·제품 gate가 끝났다.
  `EngineSetting.h/.cpp`와 TU 전역 raw-pointer alias를 제거하고, Core에는 명시적으로
  초기화·종료하는 `RuntimeSettings`만 남겼다. Editor의 preference와 build 선택은 값 객체이며
  네 번째 `EditorToolchainSettings` singleton을 만들지 않는다. 소비자가 없던
  `MSVCVersion`/vswhere 경로와 `MSBuildHelper`를 제거하고 native toolchain 선택은
  `Tools/build.ps1` 한 곳만 소유한다. Debug Core 비유니티와 Release Editor 비유니티가
  오류 0, Editor 즉시 종료 6/6, packaging boundary 6종, Workspace/Product Player smoke가 통과했다.
- ✅ live `EngineSettings.asset`의 유일한 writer는 Editor의 `EditorSettingsStore`다.
  기존 YAML map에 known map을 재귀 overlay하므로 root와 `renderPassSettings` 하위의 알 수 없는
  key를 보존하고, PID/sequence가
  붙은 candidate를 flush한 뒤 `MoveFileExW(REPLACE_EXISTING|WRITE_THROUGH)`로 교체한다.
  `RuntimeSettings`와 Player에는 저장 API가 없다. 6회 저장/종료에서 candidate 잔여 0과
  기존 미소유 root key 보존을 확인했다. replace 실패 주입 전용 자동 gate만 후속이다.
- ✅ `RenderPassSettings&` 전역 참조도 제거했다. Core 정본은 mutex 아래 값 snapshot/full apply를
  제공하고 skybox 부분 수정은 같은 잠금 아래 해당 필드만 갱신해 GT Volume 적용과 Editor
  presentation 저장/수정 사이의 data race와 lost update를 막는다.
- ✅ 옛 설정 singleton이 잘못 소유하던 세션 상태도 실제 수명 주인에게 돌렸다. frame delta는
  각 `EditorMain`/`PlayerMain`, minimized 상태는 각 `App`, Game View와 단일 terrain brush는
  `EditorSessionState`가 소유한다. gizmo collider 수집 스위치는 bridge 내부 atomic으로
  이동해 Editor UI와 render 소비자의 data race를 제거했다.
- ◐ Player의 startup scene load/start 요청은 이미 `PlayerMain`이
  `RuntimeSettings::GetStartupSceneName()`으로 수행한다. Editor `BuildSettings`의 선택은
  패키징 인자로 전달되고 runtime overlay의 `startupSceneName`으로 materialize된다.
  E3에는 이 정책을 다시 옮기는 일이 아니라 공통 frame orchestration과 `SceneManager`의
  남은 mode 분기를 제거하는 일이 남는다.
- ✅ backend key 계약을 단방향으로 고쳤다. live Editor YAML에서
  `render.backend`는 Editor preference, `build.render.backend`는 Player build 선택이다.
  패키징이 선택값을 pak의 `render.backend`로 투영하며 Player의 `RuntimeSettings`는 이
  effective runtime key만 읽고 `build.render.backend`를 직접 읽지 않는다. 현재 template의
  build key 중복은 preflight 호환용 과도기 자료이지 Player API가 아니다. CLI backend를
  생략해도 build key를 runtime key로 투영하고 preflight가 두 값의 일치를 강제하며 manifest는
  실제 runtime key를 기록한다.
- ⬜ Undo는 `ReflectionUndo.h`의 inline 전역 포인터가 정적 초기화 때 Player에서도
  singleton을 만들고 있어 호출만 옮길 수 없다. accessor 전환과 소비자 이동을 E3에서
  함께 수행한다.

판정:

- Foundation에 window/path/toolchain의 실행 모드 분기 0.
- Editor와 Player가 같은 Core 초기화 API를 서로 다른 config로 호출한다.
- 창 생성, resize, backend 초기화, 종료 순서가 두 실행 파일에서 정상이다.

### E2 — AssetRuntime과 Editor asset 기능 분리 ◐ source intake/domain writer 1차 분리 완료

1. ✅ `DataSystem`의 GUID catalog를 startup read-only scan/query와
   `RuntimeAssetChange` 소비 계약으로 고정한다. runtime load/cache는 유지하고 public
   `RegisterFileGuid`/`UnregisterFilePath`와 material picker/icon/font API는 제거했다.
2. ✅ ShellExecute/Explorer/URL 열기와 확장자별 open 정책을 `EditorPlatform`으로
   이동한다. 호출자가 없던 `OpenSolutionAndFile`은 옮기지 않고 제거한다.
3. ✅ Core의 Terrain/Foliage/Prefab이 meta writer를 직접 소유하지 않도록 Host가
   설치하는 좁은 asset-authoring port를 사용한다. Player는 no-op 구현을 넣지 않고
   handler 미설치 상태로 둔다.
4. ✅ source watcher/meta와 material/volume save 구현·수명을 `EditorAssetDatabase`로
   이동한다. Core에는 thread-safe GUID catalog와 read-only startup scan만 남긴다.
5. ✅ source import/reimport/copy writer를 `EditorAssetDatabase`로 옮기고 runtime
   reload 요청만 Core API로 남긴다. 외부 model/texture source intake, model `.asset` 게시,
   embedded texture 인코딩/게시와 Terrain height/splat/layer texture/descriptor transaction을
   이동했다. runtime type은 값 스냅샷만 만들며 Player에는 writer handler가 없다.
6. ✅ picker/file icon/font/texture selector를 `EditorAssetPresentation`으로 이동했다.
   gizmo icon은 `ScriptBinder`가 Editor 객체를 역참조하지 않고, Host가 제공한 공유 소유
   render 입력을 frame packet이 소비 완료까지 운반한다.
7. ✅ Editor가 import를 완료하면 `CatalogUpsert`/`ContentReload`/`Removed` 중 하나의
   `RuntimeAssetChange`만 전달한다. Runtime은 source/meta writer를 알지 않는다.

2026-08-21 안전 슬라이스:

- ✅ Player Host는 asset-authoring capability를 false로 주입한다. `DataSystem`,
  `ModelLoader`, `RHIShaderCompiler`, `RuntimeSettings`, `PhysicsManager`의 현재 부팅·종료
  경로는 source copy/meta/cache/settings/collision-matrix 쓰기를 차단한다.
- ✅ package smoke의 unpack residue는 0이고, 로그·dump·cache는 source/Stage가 아니라
  PID별 `RuntimeData`로 향한다.
- ◐ 이것은 호출 안전 게이트이지 소유권 분리가 아니다. Scene/prefab/terrain/
  blackboard/animator/input-map 등 여러 public writer와 watcher/import 구현이 여전히 Core
  프로젝트에 있으므로, "Player UI가 호출하지 않는다"를 최종 계약으로 삼지 않는다.

2026-08-22 첫 물리 슬라이스:

- ✅ `EngineEntry/EditorPlatform`이 file open, Explorer reveal, URL open과 prefab open
  override를 소유한다. `DataSystem`과 RenderEngine에서 ShellExecute/CreateProcess 및
  platform callback 상태를 제거했다.
- ✅ 호출자가 없던 `DataSystem::OpenSolutionAndFile`과 그 detached wait thread를
  제거했다. 종료 뒤 registry/watcher를 재스캔할 수 있던 수명 경로도 함께 사라졌다.
  향후 IDE 빌드 완료 후 rescan이 필요하면 `EditorAssetDatabase`의 명시적 요청으로
  다시 추가한다.
- ✅ 경계 래칫은 Core의 ShellExecute/CreateProcess/FileDialog 신규 호출을 막는다.
  `DataSystem.cpp`의 `BUILD_FLAG` 조건부 부채는 12→9로 감소했고, 남은 Editor platform
  API 허용 항목은 `CreateVolumeProfile`의 FileDialog 1건뿐이다.
- ✅ Release 비유니티 `Academy_4Q`와 `Player` 빌드가 통과했다. 기존 Vulkan delay-load,
  PhysX PDB, Terrain wchar 변환 경고 외 새 오류는 없다. Editor 즉시 종료는 3/3,
  workspace package `Dynamic_CPP-b28dff85dc2b4ec9932e3c77c41717c5`의 smoke와 정상
  종료(코드 0, runtime root 정리, Stage/PAK/Player 불변)도 통과했다.

2026-08-22 두 번째 물리 슬라이스:

- ✅ `AssetMetaWather`를 RenderEngine 프로젝트에서 제거했다. 새
  `EngineEntry/EditorAssetDatabase`가 efsw watcher, meta 생성·정리·이동, 지원 확장자,
  material/volume 저장과 FileDialog를 소유하며 PresentationThread join 뒤,
  RenderThread drain 전에 명시적으로 종료된다.
- ✅ `DataSystem`은 startup에서 `.meta`를 읽기만 하는 catalog가 됐다. registry는
  `shared_mutex`로 조회/등록/해제를 보호하고 GUID↔path bijection을 유지한다.
- ✅ `AssetAuthoringPort`를 통해 Terrain/Foliage/Prefab의 meta 생성과 GUID 등록을
  파일 flush 직후 동기로 끝낸다. 기존 100 ms/1 s sleep과 meta 재읽기를 제거했고,
  prefab watcher 경쟁으로 filename GUID가 먼저 생겨도 루트 `m_fileGuid`로 교정한다.
- ✅ Contents Browser, material inspector, volume inspector 호출은 Editor database로
  향한다. Core의 ShellExecute/CreateProcess/FileDialog 호출은 0, `DataSystem.cpp`의
  `BUILD_FLAG` 조건부 부채는 최초 12→5로 감소했다. 경계 래칫은 watcher/meta/save
  구현의 Core 재유입도 허용 항목 없이 차단하며 현재 91/91이다.
- ✅ Editor 빌드는 `efsw.dll`을 Editor 출력에만 배치한다. Player PE 의존성과 패키지
  payload에서는 efsw를 제거했다.
- ✅ Release 비유니티 `Academy_4Q`와 `Player` 빌드, Editor 즉시 종료 3/3,
  prefab 저장·재로드 및 prefab/meta GUID 일치, workspace package
  `Dynamic_CPP-a04406e375b84504884e0b468796ac8e` smoke를 통과했다. 일반 Player는
  종료 코드 0, runtime root 정리, sibling TEMP 보존, Stage/PAK/Player 불변이었다.

2026-08-22 세 번째 물리 슬라이스:

- ✅ 외부 model/texture source intake를 `EngineEntry`로 올렸다. `App`과 `model.load`는
  `EditorAssetDatabase::ImportSourceAsset`로 source를 Assets 하위 정규 위치에 복사하고
  meta 생성·registry 등록을 같은 authoring mutex 구간에서 끝낸 뒤, `DataSystem`에는
  import된 경로의 runtime load만 요청한다.
- ✅ `DataSystem::LoadModel`/`LoadCashedModel`/`LoadSharedTexture`는 더 이상 파일을
  복사하지 않는다. 전달 경로가 실제 파일이면 그대로 읽고, 아니면 기존 Assets의
  type별 경로를 read-only fallback으로 해석한다. 호출자가 없던 `MonitorFiles`,
  `LoadModels`, `LoadTextures`, `LoadMaterials`와 copy helper도 제거했다.
- ✅ texture pending queue와 `TextureType Selector` ImGui context를 새
  `EditorAssetPresentation`으로 이동했다. PresentationThread join 뒤 context를 먼저
  unregister하고, 그 다음 `EditorAssetDatabase` watcher를 종료한다.
- ✅ 외부 OBJ fixture로 최초 import와 reimport를 실행했다. 두 번 모두 Editor 종료 코드
  0, 목적 파일은 갱신된 source와 SHA-256이 일치했고 meta GUID
  `d354b74d-7d4f-50e2-a43f-49f60b68613d`는 유지됐다. probe 파일은 검증 뒤 제거했다.
- ✅ 첫 package smoke가 `ModelLoader::GenerateMaterial`의 `Materials` 무잠금 접근으로
  `0xC0000005`를 재현했다. asset bundle 병렬 로딩 중 map 조회/삽입이 겹친 것이 원인이며,
  `DataSystem::FindCachedMaterial`/`RegisterImportedMaterial`과 model-cache 잠금으로
  정본 접근을 고정했다. material clone/create 경로도 같은 mutex 규약으로 맞췄고,
  Editor/CLI의 직접 map 순회·조회는 `FindCachedModel`과 model/material/texture snapshot API로
  교체해 UI가 캐시 mutex를 잡은 채 그리지 않도록 했다.
- ✅ Release 비유니티 `Academy_4Q`와 `Player`, Editor 즉시 종료 3/3, 경계 래칫
  91/91을 통과했다. 최종 workspace package
  `Dynamic_CPP-eb96198c51c9465d82da593db2764ee8`는 170개 항목, smoke 종료 코드 0,
  GT 7051 frames, promotions 2, managed types 25다. content digest는
  `38a0e992ec7b5d342c5b1d5764642ed8d749049c028b1596e1e69bb13776b6c7`이며,
  payload와 Player PE 의존성 모두 `efsw.dll` 0이다.

2026-08-22 네 번째 물리 슬라이스:

- ✅ `ModelLoader`의 model-cache 직렬화는 메모리 payload 생성까지만 담당한다. 실제
  `.asset` 파일 게시와 GLB embedded encoded/raw texture의 PNG 인코딩·게시는
  `EditorAssetDatabase`가 설치하는 `AssetAuthoringPort` writer가 소유한다. Player는 세
  writer를 설치하지 않는다.
- ✅ `TerrainComponent::Save`의 layer texture 복사도 목적 경로만 계산한 뒤 Editor
  adapter에 요청한다. `ScriptBinder/Terrain.cpp`의 `copy_file`과 `ModelLoader.cpp`의
  `ofstream`/`SaveToWICFile`/directory writer는 0이다.
- ✅ model cache와 encoded embedded texture는 sibling `.tmp`에 완전히 flush한 다음
  `MoveFileExW(REPLACE_EXISTING | WRITE_THROUGH)`로 게시한다. watcher는 `.tmp`를 무시하며
  실패 시 임시 파일을 회수하므로 runtime reader가 반쪽 artifact를 보지 않는다.
- ✅ `verify-asset-authoring-ownership.ps1`이 고유 GLB probe의 embedded image 이름을
  같은 길이로 치환해 최초 import에서 `.asset` 890 bytes와 PNG를 생성하고, 두 번째
  실행에서는 cache/PNG hash와 timestamp가 유지되는 것을 Release/Debug Editor에서
  확인했다. model/material/temp probe 잔여는 모두 0이다. 실행 전 `efsw.dll`도 명시적으로
  검사해 Editor runtime 배치 누락을 소유권 회귀로 오판하지 않는다.
- ✅ Release `Academy_4Q`와 `Player` 링크가 통과했고 Player LTCG 재컴파일은
  722/71,481(1.0%)였다. `build.ps1 -BuildNative`의 Debug 전체 파이프라인도
  `Dynamic_CPP-9e798fce515445b3a3e07a35f4052764`, 480 entries, smoke exit 0,
  GT 765 frames, promotions 2, managed lifecycle true로 통과했다. 기존 Terrain wchar,
  Vulkan delay-load, PhysX PDB 경고 외 새 오류는 없다.

2026-08-22 다섯 번째 물리 슬라이스:

- ✅ material picker, 파일 분류/아이콘, Verdana 12/10 폰트와 camera/light gizmo PNG의
  초기화·소비·해제를 `EditorAssetPresentation`으로 옮겼다. 사용되지 않던 Folder icon은
  이동하지 않고 제거했고, 같은 PNG를 쓰는 파일 분류는 하나의 `Texture` 신원을 공유한다.
- ✅ material picker의 선택 전달 상태도 `DataSystem`에서 제거했다. inspector는 presentation의
  `TakeSelectedMaterial`을 소비하며, redo lambda가 이미 비워진 전역 전달 슬롯을 다시 읽던
  문제도 선택한 `shared_ptr<Material>`을 값으로 붙드는 방식으로 고쳤다.
- ✅ `EnhancedGizmoSceneBinding`은 더 이상 `DataSystems`를 역참조하지 않는다. Editor Host가
  `EnhancedGizmoIconTextures`를 render 입력으로 설치하고, GT가 만든
  `EnhancedGizmoSceneData`가 같은 `shared_ptr`을 packet에 보관해 queued RenderThread가 raw
  pass pointer를 쓰는 동안 CPU texture 수명을 보장한다. Player는 이 입력을 설치하지 않는다.
- ✅ `DataSystem.h/.cpp`의 ImGui, file/gizmo icon, font, file-presentation map, material picker
  transfer 상태는 0이다. `verify-asset-presentation-boundary.ps1`이 Core 재유입,
  ScriptBinder의 Editor 역참조, Player의 presentation 설치를 정적으로 차단한다.
- ✅ Release 비유니티 `Academy_4Q`와 `Player`, Editor 첫 프레임 전 종료 3/3을 통과했다.
  workspace package `Dynamic_CPP-4e004c68cc3c4e81a7d5290489d374e6`는 pak 170 entries,
  smoke 종료 코드 0, promotions 2, managed types 25이며 content digest는
  `38a0e992ec7b5d342c5b1d5764642ed8d749049c028b1596e1e69bb13776b6c7`다. 기존 Terrain
  wchar, Vulkan delay-load, PhysX PDB 경고 외 새 오류는 없다.

2026-08-22 여섯 번째 물리 슬라이스:

- ✅ `RuntimeAssetChange`를 Core가 이해하는 유일한 Editor→Runtime asset 변경 계약으로
  추가했다. `CatalogUpsert`는 GUID↔path만 갱신하고, `ContentReload`는 게시가 끝난 경로의
  기존 cache generation을 lookup에서 분리한 뒤 catalog를 갱신하며, `Removed`는 둘을
  함께 제거한다. 자산 종류를 명시하지 않은 watcher 경로는 확장자와 canonical asset
  directory로 model/material/texture/UI/sprite를 판별한다.
- ✅ `EditorAssetDatabase`의 meta scan/create/move/delete와 source import, embedded texture,
  Terrain layer texture 게시가 이 계약만 호출한다. public `RegisterFileGuid`와
  `UnregisterFilePath`는 제거했고, `PrefabUtility`의 중복 직접 등록도 제거했다. Player에는
  change producer가 없다.
- ✅ model/texture/material의 legacy raw 참조가 남아 있으므로 reload 시 이전 generation을
  즉시 파괴하지 않고 runtime teardown까지 pin한다. 이후 load는 새 generation을 받지만
  기존 component/frame은 자신이 보던 generation을 안전하게 마친다. raw asset 참조가 모두
  shared ownership으로 전환되면 이 pin은 reclamation 가능한 generation retire queue로
  축소할 수 있다.
- ✅ 고유 GLB probe의 writer/캐시 검증에 같은 Editor 프로세스의 연속 `model.load`를
  추가했다. 두 번째 import가 `runtime-cache=reloaded`로 다른 `Model` generation을 설치했고,
  model cache 890 bytes와 embedded PNG의 기존 게시/재사용 검사도 함께 통과했다.
- ✅ Release 비유니티 `Academy_4Q`와 `Player`가 통과했다. 기존 Terrain wchar 변환,
  Vulkan delay-load, PhysX PDB 및 관리 코드 trimming 경고 외 새 오류는 없다. Editor
  첫 프레임 전 종료는 3/3을 통과했다. workspace package
  `Dynamic_CPP-87706828d4684f95b21f9ffac1c53e54`는 pak 170 entries, smoke 종료 코드 0,
  promotions 2, managed types 25이며 content digest는
  `38a0e992ec7b5d342c5b1d5764642ed8d749049c028b1596e1e69bb13776b6c7`다.

2026-08-22 일곱 번째 물리 슬라이스:

- ✅ `TerrainComponent::Save`는 `PresentationThread`가 scene structure mutex로 component
  수명을 보호하는 동안 height/layer 값을 한 번 복사해 `TerrainAuthoringRequest`로 넘긴다.
  `this`를 캡처한 전역 worker 작업과 `NotifyAllAndWait`, PNG/JSON/filesystem writer는 제거했다.
- ✅ `EditorAssetDatabase`가 height float bit PNG, 레이어별 splat PNG, diffuse texture와
  descriptor/meta를 하나의 authoring mutex 구간에서 저장한다. 모든 payload는 `.tmp`
  staging directory에서 완성한 뒤 immutable generation directory로 rename하고,
  `.terrain` descriptor를 `MoveFileExW(REPLACE_EXISTING | WRITE_THROUGH)`로 마지막에 게시한다.
  기존 descriptor/meta는 rollback 사본을 보관해 commit 뒤 meta 갱신 실패도 이전 세대로
  되돌릴 수 있다.
- ✅ 목적지는 authoring root의 `Terrain` 아래로 제한했다. descriptor가 Terrain 루트에 있으면
  generation 경로를 기존 loader 계약대로 상대 경로로 쓰고, 하위 폴더 저장은 기존 loader가
  기대하는 절대 경로를 쓴다. 호출자가 없던 별도 `.tbin` writer `BuildOutTrrain`은 이 경계를
  우회할 수 있어 이동하지 않고 제거했으며 runtime `.tbin` loader는 유지했다.
- ✅ `verify-asset-authoring-ownership.ps1`은 Release Editor에서 2×2 height/splat/diffuse/
  descriptor/meta commit을 실행하고 PNG signature와 참조 경로를 확인한다. 이어 누락 texture로
  transaction을 거부시켜 기존 descriptor SHA-256과 generation 수가 그대로이며 `.tmp` 잔여가
  0임을 확인한다. Player는 Terrain writer를 설치하지 않는다.
- ✅ Release `Academy_4Q`와 `Player` 링크, runtime asset-change 경계, Editor 첫 프레임 전
  종료 3/3을 통과했다. workspace package
  `Dynamic_CPP-a7b678a7eb6c438fa8120f6e0567867f`는 pak 170 entries, smoke 종료 코드 0,
  promotions 2, managed types 25이며 content digest는
  `38a0e992ec7b5d342c5b1d5764642ed8d749049c028b1596e1e69bb13776b6c7`다. 기존 Vulkan
  delay-load, PhysX PDB와 Terrain wchar 변환 경고 외 새 오류는 없다.

2026-08-22 여덟 번째 물리 슬라이스:

- ✅ `FoliageComponent::SaveFoliageAsset`의 파일 쓰기를 걷어냈다. 컴포넌트는 YAML
  payload까지만 만들고 `AssetAuthoringPort::WriteFoliage`로 넘긴다. 목적 경로 확정,
  `.foliage` 확장자 정책, 원자적 게시와 meta 생성은 `EditorAssetDatabase`가 authoring
  mutex 아래에서 소유하며 목적지는 authoring root의 `Foliage` 아래로 제한한다. Player는
  이 writer를 설치하지 않는다.
- ✅ 이관하면서 기존 결함 둘이 함께 닫혔다. 저장 대화상자를 취소하면 빈 경로가 그대로
  넘어가 실행 폴더에 `.foliage`라는 이름의 파일이 생겼고, meta GUID를 이미 flush된 YAML
  노드에 써 넣어 파일에는 끝내 기록되지 않는 죽은 코드가 있었다.
- ✅ `foliage.authoring.probe` CLI를 신설해 검증이 정적 단정에 그치지 않게 했다.
  `verify-asset-authoring-ownership.ps1`이 Release Editor에서 커밋 1회(자산·meta 게시,
  `.tmp` 잔여 0), Foliage 루트 이탈 거부 1회(루트 밖 미기록, 기존 커밋 SHA-256 불변),
  대체 데이터 스트림 이름 거부 1회(기반 파일 미생성)를 확인한다.
- ✅ 착지 전 적대적 검토(정확성·동시성/수명·경계/보안·게이트 품질 4렌즈, 지적마다 반증
  전용 판정)에서 5건이 확정돼 4건을 고쳤다.
  - 타입·인스턴스가 0개면 손대지 않은 `MetaYml::Node`가 0바이트를 내보내 새 빈-payload
    거부에 걸렸다. 이관 전에는 저장에 성공했지만 그렇게 만들어진 0바이트 자산은
    `LoadFoliageAsset`이 다시 열지 못했다. 빈 시퀀스를 명시해 양쪽을 함께 닫았다.
  - 취소 시 호출부가 `return`으로 함수를 빠져나가 같은 프레임의 브러시 UI가 통째로
    빠졌다. 바로 위 Save Terrain과 같은 지역 가드로 바꿨다.
  - `IsSafeAssetName`이 `Foo:hidden` 같은 이름을 통과시켰다. `std::filesystem`은 평범한
    파일명으로 보지만 NTFS는 대체 데이터 스트림으로 연다. 금지 문자·제어 문자·후행
    점/공백·예약 장치명을 막았고, Terrain도 같은 헬퍼를 쓰므로 함께 강화됐다.
  - escape 음성 경로가 실제로 회귀하면 게이트가 Terrain 루트의 잔여를 지우지 않았다.
- ⚠ 남긴 한계: `WriteFoliage`는 자산을 원자적으로 교체한 뒤 `CreateMetaLocked`가 실패하면
  이전 자산으로 되돌리지 않는다. Terrain은 descriptor/meta rollback 사본을 갖고 있어
  비대칭이다. 이관 전 코드도 같은 성질이었고(오히려 더 약했다) 발생 조건이 좁아
  이번 슬라이스에서 확대하지 않았다 — Blackboard·Animator 이관 때 같은 모양의 writer가
  모이면 rollback을 공통 헬퍼로 한 번에 도입한다.
- ⚠ 비유니티 빌드가 직전 슬라이스의 누락 include를 드러냈다. 7c13263f가
  `ConsoleCommandSystem.cpp`에 `StringToWstring`을 도입했는데 `StringHelper.h`를 넣지
  않았고, 저장소에서 그 헤더를 include하는 파일은 `EditorAssetDatabase.cpp` 하나뿐이라
  같은 유니티 청크에 묶일 때만 컴파일됐다. `Academy_4Q.vcxproj`가 스스로
  `EnableUnitySupport=true`를 켜므로 기본 빌드로는 절대 드러나지 않는다. include를 추가해
  닫았고, 이후 슬라이스는 커밋 전 비유니티 레그를 반드시 돌린다.
- ✅ Release 비유니티 `Academy_4Q`와 `Player`가 통과했고 경계 래칫은 86/86이다. 기존
  PhysX PDB 경고 외 새 오류는 없다.

2026-08-22 아홉 번째 물리 슬라이스:

- ✅ `BlackBoard::Serialize`의 파일 쓰기를 걷어냈다. 컴포넌트는 YAML payload까지만
  만들고 `AssetAuthoringPort::WriteBlackBoard`로 넘긴다. 반환형을 `void`→`bool`로 바꿔
  실패를 예외가 아니라 값으로 돌려주고, 호출부 2곳(`MenuBarWindow`의 Create·Save)이
  `try/catch` 대신 반환값을 검사한다. Player는 이 writer를 설치하지 않는다.
- ✅ Foliage와 달리 `Deserialize`(런타임 읽기)가 Core에 남아 같은 이름→경로 규약을
  쓴다. 쓰기만 옮기면 규약이 두 벌이 되어 조용히 갈라지므로 `ResolveBlackBoardPath`
  하나로 모으고 읽기·쓰기가 모두 그것만 쓰게 했다. 게이트가 Core와 Editor 양쪽에서
  규약 철자가 각각 한 번씩만 나타나는지 센다.
- ✅ 세 번째 복사본을 만드는 대신 Editor 쪽 게시 경로를 `PublishTextAssetLocked`로
  묶었다. 이름 검증·authoring root 제한·`.tmp` staging·원자 교체·meta 생성을 한 곳이
  담당하고 Foliage도 그 위로 되돌렸다. request/result는 `TextAssetAuthoringRequest/
  Result`로 통합했지만 port handler는 도메인별로 남겨 경계 계약과 게이트 단정이
  도메인마다 유지된다.
- ✅ `blackboard.authoring.probe` CLI는 Foliage probe의 한계를 넘는다. 고정 payload가
  아니라 실제 `BlackBoard` 객체를 직렬화해 게시한 뒤 같은 이름으로 다시 읽어 값
  왕복까지 확인하므로 Core→port 배선과 write/read 규약 일치가 함께 증명된다.
- ✅ 착지 전 적대적 검토(4렌즈)에서 4건이 확정돼 전부 고쳤다.
  - 이관 전에는 예외가 실패한 파일 경로를 실어 날랐는데 지금은 자산 이름만 남았다.
    실제 I/O 실패를 내는 `WriteBinaryFileLocked`의 네 분기가 아무 로그도 남기지 않던
    것이 뿌리라, 공통 배관에 경로와 `GetLastError`를 남기게 했다 — Foliage·Terrain·
    model cache·embedded texture가 함께 좋아진다.
  - 이름이 비면 파일명이 `.blackboard`가 되는데 선행 점만 있는 이름은 `stem()`이 이름
    전체를 돌려주므로 Editor가 확장자를 한 번 더 붙여 `.blackboard.blackboard`로
    저장된다. 읽기는 `.blackboard`를 보므로 조용히 갈라진다. Core가 먼저 막는다.
  - 두 handler 별칭이 같은 함수 포인터 타입이라 서로 바꿔 설치해도 컴파일된다.
    타입이 못 막으므로 게이트가 설치 짝을 문자로 못 박는다.
  - 규약 1회 단정이 Core 파일만 보고 Editor 쪽 철자를 확인하지 않았다.
- ✅ Release 비유니티 `Academy_4Q`·`Player` 빌드, 경계 래칫, 실행 게이트(값 왕복·빈
  blackboard·빈 이름 거부)를 통과했다.

판정 갱신:

- `DataSystem`의 source copy/import queue/picker/icon/font/texture selector,
  `ModelLoader`의 filesystem writer, Terrain·Foliage·BlackBoard의 전체 filesystem
  writer는 0이다. 정적 경계 검사로 재유입을 막는다.
- import 완료 후 runtime reload/change 계약과 public catalog mutation primitive 제거까지
  완료했다. `DataSystem`은 source/meta 작성 방법을 알지 않는다.
- Core에 남은 콘텐츠 저작 writer는 5종이다(전수 조사 7종 중 Foliage·BlackBoard 완료).
  `SceneManager::SaveScene`, `PrefabUtility::SavePrefab`,
  `Animator::SerializeControllers`, `InputActionManager::SerializeMap`,
  `TagManager::Save`, `PhysicsManager::SaveCollisionMatrix`.
- 다음은 Animator와 InputMap이다. 둘 다 YAML이 아니라 `nlohmann::json` dump이지만
  본문 하나와 meta로 끝나는 모양은 같으므로 `PublishTextAssetLocked`를 그대로 쓴다.
  Animator는 `NodeEditor`가 `ImGuiHelper`에서 별도로 레이아웃 json을 쓰고 이름을
  바꾸므로(그 프로젝트는 Core가 아니다) 저작 자산과 편집기 레이아웃의 경계를 먼저
  가른 뒤 옮긴다.
- `TagManager`와 `PhysicsManager`는 Core
  라이프사이클(Initialize/Finalize/Shutdown)이 스스로 저장을 트리거하므로 GUI 호출부
  목록만 봐서는 안 잡힌다 — `TagManager::Save`는 `EngineMode::IsEditor()` 게이트를 달고
  있어 이관하면 Core의 `EngineMode` 분기 8건 중 7건이 함께 사라진다.
- ⚠ Scene과 Prefab은 마지막에 둔다. `SceneManager`는 `SaveScene`(저작)과
  `CreateEditorOnlyPlayScene`(런타임 primitive)이 한 파일에 있는데, 후자는 이름과 달리
  **Player가 실제로 호출한다** — Player가 씬 로드 직후 `EngineMode::IsPlayer()` 분기로
  `SetGameStart(true)`를 켜고, 매 루프의 `ApplyPendingSceneStructureChange`가 이를
  실행한다. 반면 `DeleteEditorOnlyPlayScene`은 Editor 경로에서만 불린다. 이름과 `Save*`
  패턴에 기대 파일 단위로 옮기면 Player의 씬 시작 경로가 깨진다. Prefab은
  `PrefabEditor::Close`가 Core 안에서 `SavePrefab`을 직접 트리거하므로 `PrefabEditor`가
  `EditorRuntime`으로 가는 E3와 함께 다뤄야 한다.
- `ScriptBinder/LifecycleTrace.cpp`의 `Trace::Dump`(`fopen_s`)는 콘텐츠 저작이 아니라
  회귀 진단 로그다. `DumpHandler`·`LogSystem`과 같은 범주로 이관 대상에서 제외한다 —
  판단을 남겨 두면 다음 조사가 같은 항목을 다시 발굴한다.

### E3 — Editor scene lifecycle 분리

1. Scene snapshot/restore와 simulation primitive를 `SceneManager`에 명시한다.
2. `EditorPlayModeController`를 만들고 Edit→Play→Stop transaction을 이동한다.
3. Undo, Selection, Editor play event를 `EditorRuntime`으로 이동한다.
4. `PrefabEditor`를 `EditorRuntime`으로 이동한다.
5. BT/Animation runtime graph와 node-editor layout/pin/build 자료를 분리한다.
6. `PlayerMain`이 이미 소유한 startup load/start 요청을 명시적인 runtime primitive로
   고정하고, `SceneManager`의 남은 Player mode 분기를 제거한다.
7. Editor/Player에 복제된 frame orchestration을 `EngineRuntime` primitive로
   고정한다. `Time->Tick` 안에서 현재 frame delta 확정 → pre-physics →
   physics → game logic → post-physics 순서를 한 곳이 소유하고, 두 Host는
   Editor hook과 presentation만 조립한다.

판정:

- Edit→Play→Stop 뒤 scene, hierarchy, prefab 연결, selection이 복원된다.
- DDOL과 C# Awake/OnEnable/Start/OnDisable/OnDestroy 순서가 유지된다.
- `SceneManager`가 Undo, Selection, PrefabEditor, Editor mode를 include하지 않는다.
- Editor와 Player가 같은 현재-frame delta와 simulation phase order를 사용하며,
  delta 0은 pause 상태에서만 허용된다.

### E4 — Editor 렌더링 분리

1. `EnhancedUIPass`를 runtime UI 위치로 먼저 분류·이동한다.
2. `IRenderFeatureContributor`와 중립적인 view flags를 도입한다.
3. Grid/Gizmo/Wireframe/GizmoIcon pass를 `EditorRender`로 이동한다.
4. `GizmoRenderer`, `EnhancedGizmoSceneBinding`, Editor texture adapter를 이동한다.
5. Editor camera 소유권을 `EditorRenderContext`로 이동하고 view로 전달한다.
6. DX12/Vulkan ImGui shell을 RenderCore 밖의 presentation layer로 이동한다.
7. RenderEngine→ScriptBinder concrete 참조를 render snapshot/bridge 방향으로 줄인다.

판정:

- DX12와 Vulkan에서 Game View 결과가 분리 전과 동일하다.
- Grid/Gizmo는 Scene View에만 기여하고 Player pipeline에는 node 자체가 없다.
- RenderCore가 Editor pass, Editor camera, `isEditorView`, ImGui backend를 소유하지 않는다.

### E5 — DeveloperTools와 테스트 분리

1. RenderEngine에 편입된 RHI self-test와 benchmark 실행기를 별도 프로젝트로 옮긴다.
2. Console command와 debug window는 DeveloperTools API를 호출한다.
3. Physics/render 진단 데이터 수집은 Core에 남기고 UI는 Editor로 이동한다.

판정:

- Core-only 빌드가 테스트/benchmark UI 없이 링크된다.
- 기존 self-test와 benchmark는 DeveloperTools 경로에서 계속 실행 가능하다.

### E6 — 프로젝트 물리 경계 확정

실제 소유권 이동에 맞춰 다음 프로젝트를 만든다. 프로젝트 이름은 구현 중 조정할 수
있지만 책임을 다시 합치지는 않는다.

- `HostRuntime.vcxproj`
- `EditorRuntime.vcxproj`
- `EditorRender.vcxproj`
- `EditorUI.vcxproj`
- 필요 시 `DeveloperTools.vcxproj` 또는 `RenderTests.vcxproj`

`Academy_4Q`는 entry/resource와 조립 코드만 가진 얇은 `CreatorEditor.exe`가 된다.
기존 runtime static library의 이름 변경은 필수가 아니다.

판정:

- Core 프로젝트의 Editor 소스 편입 0.
- Core→Editor/EngineEntry/EngineGUIWindow/ImGuiHelper 프로젝트 참조 0.
- Player→Editor 프로젝트 참조 0.
- Core의 `BUILD_FLAG`, `EngineMode::IsEditor/IsPlayer` 0.

### E7 — 선택 후속 작업

모든 경계와 런타임 검증이 닫힌 뒤에만 수행한다.

- `Engine/`, `Editor/`, `Projects/` 폴더 재배치.
- `ScriptBinder`를 `SceneRuntime/ScriptRuntime`으로 재명명 또는 분할.
- `RenderEngine`을 `RenderCore`로 재명명.
- Player thin exe + game module DLL 구조 또는 Player DLL export 구조 검토.

이 단계는 E0~E6의 가치를 만들기 위한 선행 조건이 아니다.

---

## 6. 검증 게이트

각 단계는 영향 범위에 맞는 아래 게이트를 통과해야 한다.

| 게이트 | E0 | E1 | E2 | E3 | E4 | E5 | E6 |
|---|:---:|:---:|:---:|:---:|:---:|:---:|:---:|
| Core-only build | ● | ● | ● | ● | ● | ● | ● |
| CreatorEditor build/start/exit | ● | ● | ● | ● | ● | ● | ● |
| Player `--smoke` | ● | ● | ● | ● | ● | ● | ● |
| Player 정상 종료·TEMP 정리 |  | ● | ● | ● | ● | ● | ● |
| Pak/Stage reparse·경로 음성 테스트 |  | ● | ● | ● | ● | ● | ● |
| 비유니티 빌드 | ● | ● | ● | ● | ● | ● | ● |
| Asset import/authoring |  |  | ● |  |  |  | ● |
| Edit→Play→Stop round-trip |  |  |  | ● | ● |  | ● |
| Prefab·Undo·Selection |  |  |  | ● |  |  | ● |
| DX12/Vulkan Scene/Game View |  | ● |  |  | ● |  | ● |
| 프로젝트 참조·소스 편입 검사 | ● | ● | ● | ● | ● | ● | ● |

실행 가능한 패키지 검증은 `BuildPipelinePlan.md`의 smoke/산성 테스트를 재사용한다.
레이어 이동을 이유로 별도 빌드 파이프라인을 만들지 않는다.

---

## 7. 최종 완료 기준

1. Core 프로젝트에서 Editor/EngineEntry/EngineGUIWindow/ImGui/node-editor 직접 include 0.
2. Core 프로젝트에 Editor 경로의 source/header 편입 0.
3. Core→Editor 및 Player→Editor 프로젝트 참조 0.
4. Core의 `EngineMode::IsEditor/IsPlayer`, `BUILD_FLAG` 0.
5. Core-only 빌드와 비유니티 빌드 성공.
6. Editor Edit→Play→Stop scene/prefab/selection 복원 성공.
7. Player smoke와 C# lifecycle 검증 성공.
8. DX12/Vulkan Game View가 분리 전과 동등하고 Editor overlay는 Scene View에만 존재.
9. Player 실행 중 source watcher/meta authoring/FileDialog/ShellExecute 호출 0.
10. 프로젝트 참조 그래프가 CI에서 단방향으로 강제된다.

Player 산출물의 ImGui 심볼 0은 Host presentation 교체까지 끝났을 때 추가로 닫는다.
이는 Editor/Core 물리 분리의 강한 최종 검증이지만, runtime 게임 UI 제거를 뜻하지
않는다.

---

## 8. 구현 원칙과 금지 사항

- Core에 `#ifdef EDITOR`를 추가해서 임시 완료 처리하지 않는다.
- `DataSystem`을 이름만 바꿔 Editor와 Runtime이 계속 공동 소유하게 하지 않는다.
- `SceneManager`에 Editor callback 몇 개만 주입하고 play-mode 상태 소유권을 그대로
  남기지 않는다.
- RenderCore가 Editor contributor의 lifetime을 소유하지 않는다.
- Editor pass 이동과 RHI 대개편을 한 슬라이스에 섞지 않는다.
- 모든 debug 기능을 Editor 전용으로 오판하지 않는다.
- 외부 엔진의 module layout을 그대로 복제하지 않는다. CreatorEngine의
  GameObject/component, UI, DDOL, C# 계약을 유지한다.
- 현재 워킹트리의 다른 변경을 정리하거나 되돌리는 작업과 레이어 분리를 섞지 않는다.

---

## 9. 관련 문서

- `BuildPipelinePlan.md`: Player 빌드, 쿡, pak, C# 배치, smoke/CI의 기준.
- `EnginePackagingPlan.md`: 과거 Core 내부 경계 분석 자료. 현재 실행 순서는 이 문서가
  우선한다.
- `RhiBoundaryPlan.md`: RHI/backend 경계와 RenderGraph 작업. E4는 그 계획의 hot zone을
  존중해 별도 소슬라이스로 진행한다.
- `SceneGraphRedesignPlan.md`, `UISystemRedesignPlan.md`: E3에서 수명주기 계약을 검증할
  때 사용하며, 레이어 분리를 이유로 해당 구조를 다시 설계하지 않는다.

과거 L0/L1 완료 기록과 L2/L2'/L4' 승계 문구는 현재 실행 계획과 중복되고 삭제된
프로젝트·진입점을 전제로 하므로 본문에서 제거했다. 필요한 이력은 Git history와
`BuildPipelinePlan.md`에 남아 있고, 이후 Editor/Core 분리 진행 상태는 E0~E7에만
기록한다.
