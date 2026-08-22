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

DedicatedServer.exe (PHASE 20 N9, 선택 제품)
 └─ ServerRuntime ─────► HostRuntime / SceneRuntime / PhysicsCore / NetCore
                         (EditorUI/EditorRender/RenderCore/authoring parser 없음)

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
| `ServerRuntime` (PHASE 20 N9) | fixed simulation host, cooked startup, connection/replication 조정 | Editor, ImGui, RenderCore, authoring parser |
| `DeveloperTools/RenderTests` | self-test, benchmark, 개발자 명령과 표시 | shipping runtime의 필수 초기화 경로 |

`AssetRuntime`, `SceneRuntime`, `RenderCore` 같은 이름은 먼저 **책임 경계**를 뜻한다.
초기 단계에서는 기존 static library를 유지해도 된다. 새 프로젝트는 실제 소스가
이동하는 슬라이스에서만 만들며 빈 껍데기 프로젝트를 미리 만들지 않는다.
`ServerRuntime`도 이 문서 E0~E7의 완료 조건이 아니다. E6가 source/link 경계를
세우면 PHASE 20 N9가 실제 제품 요구가 있을 때 target과 PE import gate를 소유한다.

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

2026-08-22 열 번째 물리 슬라이스:

- ✅ `PhysicsManager::SaveCollisionMatrix`의 파일 쓰기를 걷어냈다. YAML payload만 만들고
  `AssetAuthoringPort::WriteCollisionMatrix`로 넘기며 반환형은 `bool`이다. 호출자가 0인
  `PhysicsManager::Shutdown` 안의 저장 호출은 함께 제거했다(커밋 직전 재확인에서도
  정의 외 호출자 0).
- ✅ 프로젝트 설정 자산에는 기존 저작 자산 경로를 쓰지 않는다. `ProjectSetting` 폴더에는
  `.asset` 3종만 있고 `.meta`가 하나도 없다 — GUID로 참조되지 않는 파일에 사이드카를
  만들기 시작하면 안 되므로 meta를 만들지 않는 `PublishProjectSettingLocked`를 신설했다.
  게이트가 그 함수 본문에 `CreateMetaLocked`가 없는지까지 검사한다.
- ✅ 이름 왕복을 없앴다. BlackBoard에서 `stem()` 왕복이 빈 이름을 깨뜨렸으므로, 여기서는
  Core가 읽기와 같은 규약(`PathFinder::ProjectSettingPath`)으로 최종 경로를 만들어 넘기고
  Editor는 그것이 설정 루트 **바로 아래 한 칸**인지만 검증한다. 쪼갰다 붙이는 과정이
  없으니 갈라질 여지도 없다.
- ✅ `collisionmatrix.authoring.probe` CLI는 값을 뒤집어 저장하고 메모리를 되돌린 뒤
  파일에서 다시 읽어 왕복을 확인한다 — 디스크를 실제로 거치지 않으면 통과할 수 없다.
  이어서 원래 값으로 복원하고, 게이트가 저장소 자산의 SHA-256 불변·`.meta` 미생성·설정
  루트 밖 목적지 거부를 확인한다.
- ⚠ `PhysicsManager::SetCollisionMatrix`는 어떤 게이트도 없이 public이다. 현재는 호출자가
  Editor UI뿐이라 안전하지만, 새 GUI나 C# 바인딩이 이를 노출하면 Player 쓰기 방어선이
  뚫린다. 이번 범위 밖으로 두되 기록한다.

2026-08-22 열한 번째 물리 슬라이스:

- ✅ `TagManager::Save`의 파일 쓰기를 걷어냈다. YAML payload만 만들고
  `AssetAuthoringPort::WriteTagManager`(meta 없는 프로젝트 설정 경로)로 넘긴다.
  이름→경로 규약은 `ResolveTagManagerPath` 하나로 모아 `Load`와 공유한다.
- ✅ **핵심은 호출 순서였다.** 태그 저장은 asset database가 authoring handler를 설치한
  뒤부터 걷기 전까지만 성공하는데, 기존 배치는 두 저장이 모두 그 창 **밖**에 있었다.
  `TagManagers->Initialize()`는 handler 설치보다 90줄 앞에서, `Finalize()`는 handler
  해제 뒤에서 돌았다. 그대로 port로 옮겼다면 첫 실행 기본 태그 생성과 종료 시 저장이
  둘 다 조용히 사라졌을 것이다 — 빌드도 기존 게이트도 잡지 못하는 형태다.
  `Initialize()`를 asset database 초기화 뒤·`CreateScene()` 앞으로, `Finalize()`를
  asset database 종료 앞으로 옮겼다.
- ✅ 착륙 지점은 실행 경로 전수 확인으로 정했다. 그 사이 구간의 태그 참조는 전부
  지연 실행이다 — `newSceneCreatedEvent` 람다의 `SetTag("MainCamera")`,
  `CollisionMatrixPopup` 람다의 `GetLayers()`, `InspectorWindow`의 드로우 헬퍼가
  그렇고, `InspectorWindow` 생성자의 TagManager 참조는 0건이며 **RenderEngine 전체에
  TagManager 참조가 0건**이다. 첫 실질 읽기는 `CreateScene()`이다.
- ✅ `EngineMode::IsEditor()` 분기는 7건→4건이 됐다. writer 관련 3건(시딩 조건·Finalize
  저장·`Save` 본문)이 사라졌다. 시딩은 이제 파일 존재 여부만 보고 실제 쓰기는 handler
  유무가 가른다 — Player는 handler가 없어 메모리 기본값만 갖고 지나간다(pak에 자산이
  빠진 방어 경로이며 빈 표보다 낫다).
- ⚠ 남은 4건(`AddTag`·`AddLayer`·`RemoveTag`·`RemoveLayer`)은 writer 가드가 아니라
  **저작 mutator 가드**다. 태그 어휘를 편집하는 것과 오브젝트에 태그를 부여하는 것은
  층위가 달라 후자(`AddTagToObject` 등)에는 애초에 가드가 없다. 이들을 Host 주입
  capability로 바꾸는 일은 별도 슬라이스로 둔다. 따라서 이 슬라이스로 Core의
  `EngineMode` 분기는 8→5이며 0이 아니다.
- ✅ 게이트에 이번 함정의 회귀 방지를 넣었다. 정적으로는 `EditorMain.cpp`의 호출 줄
  번호를 실제로 비교해 순서가 창 밖으로 나가면 실패시킨다. 실행으로는 태그를 추가하고
  **정상 종료**한 뒤 자산 해시 변화를 확인하고, **Editor를 다시 켜서** 그 태그가
  로드되는지 본 다음 제거·재종료로 원래 해시로 돌아오는지까지 확인한다 — 한 프로세스
  안에서 검사하면 메모리 상태만 보게 되므로 재기동이 필수다.

2026-08-22 열두 번째 물리 슬라이스:

- ✅ 카탈로그에 등록되지 않는 자산의 게시 경로를 일반화했다. 요청 타입을
  `ProjectSettingAuthoringRequest`→`UncatalogedAuthoringRequest`로 정정하고(더 이상 설정
  전용이 아니다) `PublishProjectSettingLocked`→`PublishUncatalogedLocked(label, root,
  request)`로 루트를 매개변수화했다. 루트는 요청이 아니라 handler가 정하므로 요청으로
  벗어날 수 없다. 이 부분은 동작 변화가 없고 기존 게이트가 그대로 통과했다.
- ✅ `InputActionManager::SerializeMap`의 파일 쓰기를 걷어냈다. JSON payload만 만들고
  `AssetAuthoringPort::WriteInputActionMap`으로 넘긴다. `.meta`는 만들지 않는다.
- ✅ 기존 결함 둘이 함께 닫혔다. `replace_extension(".json")`은 이름에 `.`이 있으면 그
  뒤를 통째로 잘라내 `Player.v2` 맵이 `Player.json`으로 저장되며 다른 맵을 덮어썼다 —
  문자열 접합으로 바꿨다. 그리고 `SaveManager`가 맵마다 저장하면서 결과를 전혀 보지
  않았다 — 실패를 호출자에게 돌려주되 한 맵이 실패해도 나머지는 계속 쓰는 기존 동작을
  유지한다.
- ✅ `inputmap.authoring.probe`는 이 도메인의 고유 성질을 겨냥한다. 맵마다 파일이 하나이고
  읽기가 디렉터리 스캔이므로, `.`이 든 이름으로 저장한 뒤 **Editor를 다시 켜서** 스캔이
  그 파일을 찾아내는지 확인하고 `.meta` 미생성도 함께 본다.
- ✅ Release 비유니티 `Academy_4Q`·`Player`, 경계 래칫, 실행 게이트를 통과했다.

2026-08-22 열세 번째 물리 슬라이스:

- ✅ `Animator::SerializeControllers`의 파일 쓰기를 걷어냈다. JSON payload만 만들고
  `AssetAuthoringPort::WriteAnimatorController`로 넘기며 meta는 만들지 않는다.
  InputMap과 같은 `replace_extension` 이름 절단 결함도 문자열 접합으로 함께 닫았다.
- ✅ 호출부(`ImGuiDrawHelperAnimator`의 Save 버튼)가 반환값을 검사한다.
- ⚠ **이 슬라이스로 "Core→ImGuiHelper 참조 0"은 달성되지 않는다.**
  `ScriptBinder/Animator.cpp`가 `NodeEditor.h`를 직접 include하고
  `AnimationController`가 `NodeEditor*`를 리플렉션 필드로 소유한다 — Core 타입이 편집기
  객체를 소유하는 더 근본적인 결합이며 writer 이관과는 별개 슬라이스다. 매 프레임
  `ImGuiDrawHelperAnimator`가 부르는 `NodeEditor::MakeEdit`(레이아웃 json,
  `Assets/NodeEditor`)와 컨트롤러 이름 변경 시의 `ReNameJson`은 저작 트랜잭션을 우회하는
  경로로 그대로 남는다. 이관 완료로 오독하지 않도록 기록한다.
- ⚠ 값 왕복 probe는 만들지 않았다. `AnimationController::Serialize`가 상태 0개일 때
  `m_curState` 키를 생략하는데 `DeserializeControllers`는 무조건 읽어 예외가 나고,
  파싱 실패 시 `return` 없이 통과해 기존 컨트롤러를 지운다 — 둘 다 이관 이전부터 있던
  별개 결함이라 왕복을 태우면 게이트가 그 결함으로 붉어진다. 대신 저장·이름 보존·
  `.meta` 미생성·루트 이탈 거부만 잰다.
- ⚠ `DeserializeControllers`는 쓰기 쪽 `AnimatorjsonPath` 규약을 쓰지 않고 파일
  다이얼로그가 준 경로를 그대로 연다. 그래서 다른 도메인과 달리 "규약이 한 번만
  나타나는지" 정적 단정을 붙이지 않았다 — 대상이 없어 항상 무의미하게 통과한다.

판정 갱신:

- `DataSystem`의 source copy/import queue/picker/icon/font/texture selector,
  `ModelLoader`의 filesystem writer, Terrain·Foliage·BlackBoard의 전체 filesystem
  writer, `PhysicsManager`·`TagManager`의 프로젝트 설정 writer,
  `InputActionManager`의 맵 writer와 `Animator`의 컨트롤러 writer는 0이다. 정적 경계
  검사로 재유입을 막는다.
- 남은 writer는 `SceneManager::SaveScene`과 `PrefabUtility::SavePrefab` 2종이다. 둘 다
  마지막으로 미뤘고 이유는 아래 E3 항목에 있다 — Scene은 같은 파일에 진짜 런타임
  primitive가 섞여 있고, Prefab은 `PrefabEditor::Close`가 Core 안에서 저장을 직접
  트리거하므로 `PrefabEditor`가 `EditorRuntime`으로 가는 E3와 함께 다뤄야 한다.
- import 완료 후 runtime reload/change 계약과 public catalog mutation primitive 제거까지
  완료했다. `DataSystem`은 source/meta 작성 방법을 알지 않는다.
- Core에 남은 콘텐츠 저작 writer는 5종이다(전수 조사 7종 중 Foliage·BlackBoard 완료).
  `SceneManager::SaveScene`, `PrefabUtility::SavePrefab`,
  `Animator::SerializeControllers`, `InputActionManager::SerializeMap`,
  `TagManager::Save`, `PhysicsManager::SaveCollisionMatrix`.
- ⚠ 위 문단은 실측 전에 적은 것이고 **틀렸다**(2026-08-22 정정). Animator·InputMap을
  `PublishTextAssetLocked`(meta 생성)로 옮기면 지금까지 `.meta`가 없던 파일들에 처음으로
  사이드카가 생긴다 — `AnimatorController` 17개, `InputMap` 6개, `NodeEditor` 14개 전부
  `.meta`가 0개이고 코드 어디에도 이들을 가리키는 GUID 필드가 없으며 `.json`은 등록
  확장자도 아니다. CollisionMatrix·TagManager가 명시적으로 피한 것과 같은 함정이므로
  meta를 만들지 않는 경로를 쓴다.
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

### E3 — Editor scene lifecycle 분리 ◐ 게이트 선행 착수

2026-08-22 착수 전 실측과 첫 슬라이스:

- ⚠ 착수 전 회귀 커버리지가 0이었다. 세트 60여 종 어디에도 Edit→Play→Stop 왕복,
  Undo, Selection, PrefabEditor Open/Close를 구동·단정하는 검사가 없었고, 재생 왕복은
  UI 로그 통과 횟수 같은 프록시로만 봤다. E3의 판정 기준(당시 문구: "재생 후
  scene·hierarchy·prefab 연결·selection이 복원된다")을 잴 수단 자체가 없는 상태였다.
  그래서 첫 슬라이스를 이관이 아니라 **게이트 신설**로 잡았다.
  (그 문구의 selection 부분은 2026-08-23에 정정됐다 — 아래 판정 절 참고. 복원이 아니라
  해제이고, 잴 수단이 생기고 나서야 그것이 드러났다. 재려 하지 않았다면 계획서의
  틀린 문구를 그대로 목표로 삼아 없는 기능을 구현하려 했을 것이다.)
- ✅ `verify-play-roundtrip.ps1`과 `play.state` CLI를 신설했다. 재생 전이가 실제로
  일어났는지를 상태 플래그로 확인한 뒤에야 복원을 단정하고, 재생 중 오브젝트를 하나
  만들어 정지 후 사라지는지까지 본다. 스위트 편입 후 음성 테스트(생성 생략)로 실제
  붉어지는 것을 확인했다.
- ⚠ 게이트를 만들며 설계 오류 하나를 스스로 잡았다. 처음에는 "재생 중 digest가 편집
  상태와 달라야 한다"로 전이를 재려 했는데 그건 틀린 관측이다 — 재생 진입은 좌표를
  바꾸지 않고 `m_scenePhase`만 바꾸므로 스크립트가 움직이지 않는 한 digest는 같다.
  이 검사가 없었다면 "재생을 아예 안 했는데 통과"하는 게이트 위에서 E3 전체를
  진행했을 것이다.
- ⚠ **실측 발견: 재생 왕복 후 엔티티 슬롯 인덱스가 재배정된다.** 내용은 완전히
  보존되는데 열거 순서가 바뀐다(Main Camera↔Directional Light). 정지가 엔티티를
  파괴하고 백업에서 되살리므로 슬롯 배정이 달라지는 것이다. 엔진의 transform digest
  해시는 순서에 민감해 이대로는 왕복 비교에 쓸 수 없고, 인덱스를 가로질러 참조를
  들고 있는 코드가 있다면 왕복 뒤 어긋난다. 게이트는 내용 집합으로 비교하고 슬롯
  순서는 PASS 줄에 남겨 변화가 드러나게 했다. E3가 이 성질에 의존하거나 바꾸지
  않도록 주의할 것.
- ⚠ 이 게이트의 커버리지는 얕다. 시작 씬 엔티티가 3개뿐이라 계층 깊이·프리팹 연결·
  DDOL을 태우지 못한다. 더 풍부한 씬으로 왕복을 태우는 것이 후속이다.

착수 순서(조사 기준): 게이트 → 죽은 include 3건 제거 → E3-1 primitive 경계 명시 →
E3-2+3(Controller+Undo, 반드시 함께) → E3-6 → E3-7. E3-4(PrefabEditor)와
E3-5(BT/Animation)는 앞의 사슬과 파일을 공유하지 않아 별도 트랙으로 병렬 가능하다.

두 번째 슬라이스 — 죽은 include 3건 제거:

- ✅ `SceneManager.cpp`의 `PrefabEditor.h`, `AnimationController.h`의
  imgui-node-editor, `ComponentFactory.cpp`의 `NodeEditor.h`를 제거했다. 세 곳 모두
  해당 헤더가 주는 심볼 실사용 0건이고, 전이 include에 기대던 심볼도 없다
  (`SceneManager.cpp`의 `Prefab`/`PrefabUtility` 30·8건은 11행의 `PrefabUtility.h`가
  직접 준다). 경계 부채 83 → 80.
- ⚠ **제거 전 이미 링커가 버리고 있었다.** 변경 전 `Player.exe`에 PrefabEditor 흔적이
  0건이다 — `PrefabEditor.h`는 본문이 통째로 `DYNAMICCPP_EXPORTS`로 가드돼 있어 그
  경로로는 애초에 아무 코드도 나오지 않았다. 즉 이 슬라이스가 줄인 것은 바이너리가
  아니라 **경계 간선과 재컴파일 폭**이다. "죽은 include를 지웠으니 Player가 가벼워졌다"고
  적으면 거짓이 된다.
- ⚠ 위험은 심볼 실사용이 아니라 **전이 include에 얹혀 있던 소비자**였다. 특히
  `AnimationController.h`는 `Animator.h`가 물고 있어 파급 범위가 넓다. 실측 결과
  `ed::`를 쓰는 11개 파일 중 직접 include가 없는 셋(`MenuBarWindow.cpp`,
  `BlueprintBuilder.cpp`, `BTBuildGraph.h`)은 전부 `BTBuildNode.h`/`BTEnum.h`/
  `BlueprintBuilder.h` 경로로 받고 있어 이 사슬과 무관했다. `MenuBarWindow.cpp`는
  `ed::`를 700줄 쓰지만 `BTBuildGraph.h`→`BTBuildNode.h`가 준다.
- ⚠ 유니티 빌드는 이 판정을 못 한다. 청크 안에서 형제 TU의 include가 서로를 가려주기
  때문에, 비유니티(`/p:EnableUnitySupport=false`)와 유니티 양쪽을 다 태워야 한다.
  이 저장소는 같은 함정으로 이미 한 번 깨졌다(`StringToWstring` 누락 include).
- ⚠ **이 슬라이스를 검증하다 세트가 이미 붉은 상태였음이 드러났다.** 커밋 `76be6ff1`이
  저작 writer를 Editor로 옮기며 `ModelLoader`의 `std::ofstream& outfile`을
  `std::ostream& output`으로 바꿨는데, `verify-hierarchy-read-boundary.ps1`의 표현식
  단위 허용이 스트림 변수 이름 `outfile`을 규칙에 박아 두고 있어 허용 2건이 조용히
  위반으로 뒤집혔다. 게이트가 지키려는 성질은 "어떤 필드를 만지는가"이지 "지역 변수
  이름이 무엇인가"가 아니므로 스트림 이름을 `\w+`로 풀고 필드·`sizeof` 대상은 그대로
  못 박아 두었다. 느슨해져서 통과한 것이 아님을 음성 테스트(허용 밖 `m_parentIndex`
  접근 주입)로 확인했다 — 주입하면 붉어지고 되돌리면 통과한다.
- ⚠ 그 붉음을 처음에 못 봤다. `pwsh run-all.ps1 | tail -60`으로 돌려서 **종료 코드가
  pwsh가 아니라 `tail`의 것**이 됐고 출력도 27종 중 7종만 남았다. 파이프로 자르면
  "exit 0"은 아무것도 뜻하지 않는다. 세트는 파일로 받고 종료 코드를 따로 봐야 한다.



세 번째 슬라이스 — E3-1 snapshot/restore·simulation primitive 명시:

- ✅ `CaptureSceneSnapshot` / `RestoreSceneSnapshot` / `HasSceneSnapshot` /
  `DiscardSceneSnapshot` / `SetSimulationPhase`를 공개 primitive로 드러내고,
  합성 함수는 `BeginPlayTransaction` / `EndPlayTransaction`으로 이름을 고쳤다.
  E3-2가 transaction을 `EditorPlayModeController`로 들어내려면 런타임 몫에 먼저
  이름이 있어야 한다.
- ⚠ **옛 이름이 사실과 어긋나 있었다.** `CreateEditorOnlyPlayScene`은 씬을 만들지
  않고 에디터 전용도 아니다 — Player의 유일한 재생 진입 경로가 이 함수다.
  Player는 씬 로드 시 `EngineMode::IsPlayer()` 분기가 `SetGameStart(true)`를 부르고
  다음 프레임의 `ApplyPendingSceneStructureChange`가 같은 코드를 탄다. 이름만 믿고
  통째로 Editor로 옮겼다면 Player는 씬을 로드하고도 스크립트가 한 번도 돌지 않는
  정지화면이 됐을 것이다.
- ⚠ **실측 발견: Player의 스냅샷은 아무도 읽지 않는다.** `m_isGameStart`에 쓰는 곳은
  `SetGameStart` 하나뿐이고 Player는 `true`만 부른다. 따라서 Player에서
  `EndPlayTransaction`은 영영 불리지 않고, 시작할 때 뜬 씬 전체 직렬화는 그대로
  버려진다(그 YAML 노드는 프로세스 수명 내내 메모리에 남는다). E3-6이 Player 분기를
  걷어낼 때 이 죽은 직렬화도 함께 없애야 한다. 비용은 Release로 재지 않았으므로
  수치는 적지 않는다.
- ⚠ **리팩터가 동작을 하나 바꿨다 — 조용하지 않게 만들었다.** 옛 코드는 직렬화가
  예외 없이 끝났지만 엔티티 노드가 비어 있어도 phase를 올려 재생에 들어갔고, 정지할
  때 비로소 "백업이 없어 복원하지 못했다"가 떠서 편집 내용을 잃었다. `CaptureSceneSnapshot`은
  그 경우 재생을 거부한다. 이것은 "새 가드가 옛 성공을 무통보 실패로 바꾼다"는
  이 저장소의 반복 양식이라, 거부 시 에러 로그를 반드시 남기게 했다 — 없으면
  "재생 버튼이 안 먹는다"로만 보인다.
- 남은 Editor 정책: `BeginPlayTransaction`의 `UndoCommandManager::ClearGameMode/Clear`와
  `EndPlayTransaction`의 `resetSelectedObjectEvent.Broadcast`. 둘 다 지금은 Player도
  탄다(출하 게임이 Undo를 비운다). E3-2/E3-3 소관.
- ⚠ 선택(Selection)은 Editor 단독이 아니다. `resetSelectedObjectEvent`를 **Core의**
  `Scene.cpp:73`이 구독해 `Scene::ResetSelectedEntity`를 건다. E3-3은 이벤트 발행처만
  옮기는 것으로 끝나지 않고 Scene이 든 선택 상태까지 봐야 한다.
- ⚠ **두 번째 의도적 차이 — 적대적 검토가 잡았다.** `CaptureSceneSnapshot`의 예외
  catch가 스냅샷을 비운다. 옛 catch는 건드리지 않고 그냥 return 했는데, 그러면 직전
  재생 세션의 백업이 남아 있다가 다음 정지에서 "백업이 있다" 검사를 통과해 **지금
  씬과 무관한 과거 오브젝트를 현재 씬에 섞어 넣는다.** 도달 경로가 실재한다:
  `LoadSceneImmediate`가 `m_activeScene = nullptr`(623행) 뒤 `Scene::LoadScene`(636행)이
  던지면 바깥 catch가 로그만 남기고 널을 남기고, 그 상태로 정지하면 널-씬 조기
  반환이 백업을 남긴 채 빠진다. 비우는 쪽이 옳고, 거부는 로그로 드러낸다.
- ⚠ **선행 결함(신구 양쪽): 스냅샷이 자기 씬보다 오래 산다.** `m_editorSceneBackup`을
  비우는 곳은 정상 복원 경로뿐이다. `EndPlayTransaction`의 널-씬 조기 반환도, 씬 교체
  (`LoadSceneImmediate`)도 비우지 않는다. 그래서 A 씬에서 뜬 백업이 B 씬에 복원될 수
  있다. 위 catch 변경이 한 갈래를 좁혔을 뿐 뿌리는 그대로다 — 스냅샷에 출처 씬을
  묶고 불일치 시 거부하는 것이 옳은 고침이고, transaction을 소유하게 될 E3-2 소관이다.
  회귀 게이트가 없으므로 고칠 때 게이트부터 만들어야 한다.

1. Scene snapshot/restore와 simulation primitive를 `SceneManager`에 명시한다. ✅
2. `EditorPlayModeController`를 만들고 Edit→Play→Stop transaction을 이동한다.
3. Undo와 Editor play event를 Editor 소유로 옮긴다. ✅
   ⚠ 2026-08-23 정정 — 원래 "Undo, Selection, Editor play event를 `EditorRuntime`으로
   이동"이었다. 실측 결과 둘을 덜어야 했다:
   · **Selection은 옮길 것이 없다.** 위 판정 항목의 정정 참고 — `SceneManager`의 관여
     4건 중 옮길 수 있는 것이 0건이고, 유일한 구독자가 Core 자신이라 "Editor 정책"의
     실체가 없다.
   · **`UndoManager` 클래스의 물리 이관은 하지 않는다.** `ReflectionFunction.h:7`이
     `ReflectionUndo.h`를 직접 물고 그 헤더는 사실상 모든 리플렉션 소비 TU가 거치는
     사슬이라, 옮기려면 include 사슬 전체를 다시 설계해야 한다. 판정 기준이 요구하는
     것은 "`SceneManager`가 Undo를 include하지 않는다"이고 그것은 충족됐다.
   · 실제로 한 일: 재생 진입의 Undo 폐기를 `EditorPlayModeController`로 옮기고
     `SceneManager`의 Undo 참조를 0건으로 만들었다(커밋 2ace8a67).

   네 번째 슬라이스 — E3-2+3 게이트 선행 (2026-08-23):

   - ✅ `verify-play-selection-undo.ps1`과 CLI 프로브 4종(`undo.state`, `undo`/`redo`,
     `object.create.undoable`, `scene.selection`), `UndoManager`의 깊이 접근자 4종을
     신설했다. 착수 전 세트 전체에 selection/undo 단정이 **0건**이었다.
   - ⚠ **계획서의 판정 문구가 코드와 어긋난다.** 위 판정은 "재생 후 selection이
     **복원된다**"고 적었지만 코드는 복원하지 않고 **해제**한다. `EndPlayTransaction`이
     `resetSelectedObjectEvent`를 던지고 유일한 구독자 `Scene::ResetSelectedEntity`가
     포인터를 널로 만들 뿐이다. 애초에 선택은 씬 YAML에 실리지 않아(`.creator` 파일에
     `m_selectedEntit` 0건) 스냅샷에 담기지도 않으므로 복원될 경로 자체가 없다.
     게이트는 실측대로 **해제**를 단정한다. "복원"은 리팩터가 아니라 기능이며, 하려면
     선택을 `Entity*`가 아니라 instanceID/EntityHandle로 들어야 한다.
   - ⚠ **음성 테스트가 크래시를 냈다 — 이 슬라이스의 가장 중요한 제약이다.**
     `EndPlayTransaction`의 `resetSelectedObjectEvent.Broadcast()` 한 줄을 주석 처리하고
     돌리니 에디터가 `ACCESS_VIOLATION`(0xC0000005)으로 죽었다. 정지 후
     `m_selectedEntity`가 파괴된 엔티티를 가리키기 때문이다. 즉 그 호출의 위치
     (`AllDestroyMark()` **이전**)는 장식이 아니라 **댕글링 방지 안전 속성**이다.
     "Editor 정책이니 Controller로 옮긴다"고 기계적으로 처리해 순서가 뒤집히면
     재생 정지마다 크래시한다. 이 슬라이스 이전에는 어떤 검사도 이걸 못 잡았다.
   - ⚠ **`m_isGameMode`는 이름과 다르다.** 저장소 전체에서 이 필드에 쓰는 곳은
     `MenuBarWindow.cpp:469` 한 줄뿐이라, 실제 의미는 "게임 모드"가 아니라 "에디터
     UI의 Play 버튼을 눌렀는가"다. CLI로 재생하면 영원히 `false`다(실측:
     `gameStart=1 isGameMode=0`). `PrefabUtility.cpp:374-376`이 이미 같은 이유로 이
     필드 대신 `IsGameStart()`를 본다고 주석에 적어 두었다. 게이트가 "유효한 스택"
     하나만 봤다면 편집 스택을 보면서 게임 스택을 검사한다고 착각했을 것이라,
     편집·게임 스택을 따로 찍고 이 어긋남 자체를 PASS 줄에 남긴다.
   - ⚠ **게이트 둘이 낡은 바이너리를 검사하고 있었다.** `verify-play-roundtrip.ps1`과
     새 게이트의 기본 `-Exe`가 `Bin\Editor\Academy_4Q.exe`인데 빌드 산출물은
     `x64\Debug\Academy_4Q.exe`다(실측: Bin 쪽이 하루 낡음). `run-all.ps1`이 `-Exe`로
     덮어써 주므로 세트 실행은 무사했지만, **단독 실행은 옛 바이너리를 재고 있었다** —
     새 CLI 명령이 "알 수 없는 명령"으로 뜨면서 드러났다. 나머지 22종은 처음부터
     `x64\Debug`를 가리킨다. 둘 다 맞췄다.
   - ⚠ **범위 정정: "Core 30건 + Editor 53건"은 이관 대상이 아니다.** 그 수는 선택 심볼
     **소비자 전수**이고, 판정 기준이 요구하는 것은 `SceneManager`가 Selection을 모르는
     것이다. `SceneManager`의 관여는 4건뿐(델리게이트 선언·전역 별칭·
     `BeforeAwakeSceneLoad`의 `ResetSelectedEntity()`·`EndPlayTransaction`의 Broadcast).
     나머지 26건은 `Scene`의 필드·메서드이고 계획서 어디에도 "Scene이 selection을
     몰라야 한다"는 요구는 없다. 선택 소유권을 Scene 밖으로 빼는 것은 별개의 큰 일이다.
   - ⚠ `UndoManager` 클래스 자체의 물리 이관(`Utility_Framework` → EditorRuntime)은
     이 슬라이스에서 하지 않는다. `ReflectionFunction.h:7`이 `ReflectionUndo.h`를 직접
     물고 있고 그 헤더는 사실상 모든 리플렉션 소비 TU가 거치는 사슬이라, 옮기려면
     include 사슬 전체를 다시 설계해야 한다. 판정 기준은 "`SceneManager`가 Undo를
     include하지 않는다"이지 "클래스가 Utility_Framework 밖에 있다"가 아니다.

   다섯 번째 슬라이스 — E3-2 Undo 정책 이관 (2026-08-23):

   - ✅ `EngineEntry/EditorPlayModeController.{h,cpp}` 신설. `SceneManager::PlayModeEvent`를
     구독해 재생 진입에서 Undo 이력을 버린다. `BeginPlayTransaction` 안에 있던
     `UndoCommandManager->ClearGameMode()/Clear()` 두 줄이 여기로 왔다.
     **`SceneManager`의 Undo 참조가 0건이 됐다** — 판정 기준 하나 충족.
   - ✅ 그래서 **출하 게임이 더 이상 Undo 이력을 비우지 않는다.** `BeginPlayTransaction`은
     Player의 유일한 재생 진입 경로라, 예전에는 Player가 시작할 때마다 쓰지도 않는
     Undo 스택 넷을 비웠다. Player는 `EngineEntry`를 링크하지 않으므로 구독자가 없고,
     구독자 없는 `Broadcast`는 아무 일도 하지 않는다. (정직하게: 빈 스택을 비우는
     비용은 무시할 만하다. 이 이관의 값어치는 성능이 아니라 소유권이다.)
   - ✅ 포트가 아니라 델리게이트를 썼다. E2의 `AssetAuthoringPort`는 Core가 Editor의
     **결과값**을 써야 해서(파일이 실제로 써졌는지) 반환값 있는 함수 포인터가 필요했다.
     여기는 Core가 응답을 쓰지 않는 단방향 통지라, 이미 선언돼 있던 `PlayModeEvent`로
     충분하다. 그 이벤트는 선언·전역 별칭·셧다운 `Clear()`만 있고 **Broadcast 0건·
     구독 0건인 죽은 코드**였어서 `Delegate<void>` → `Delegate<void, bool>` 시그니처
     변경에 깨질 소비자가 없었다.
   - ✅ `verify-play-mode-policy-boundary.ps1` 신설. Player에서 "아무 일도 안 일어남"은
     런타임으로 재기 어렵다 — 정상이 곧 무동작이라 관측할 것이 없다. 정적으로 못 박되,
     부재 단정만 두면 대상을 못 찾아도 0건이 나오므로 **찾을 수 있어야 하는 것을 먼저
     찾는다**(컨트롤러가 실재하고 Undo를 다루는가, Core가 통지를 던지는가).
     네 갈래 음성 테스트로 실제 검출을 확인했다.
   - ⚠ **그 게이트가 처음엔 거짓 실패를 냈다.** 선택 해제가 `AllDestroyMark`보다 앞인지
     보는 단정에서 `IndexOf`를 파일 전체에 걸어, `AllDestroyMark`의 **첫** 등장(462행,
     종료 경로)과 비교했다. 실제 코드는 1438 < 1441로 올바른데 게이트가 붉었다.
     `AllDestroyMark`는 이 파일에 5번 나온다 — 순서 단정은 반드시 `EndPlayTransaction`
     본문으로 한정해야 한다. 거짓 실패는 거짓 통과만큼 나쁘다.
   - 셧다운 순서 확인: `m_playModeController.Shutdown()`(EditorMain.cpp:386)이
     `SceneManagers->Decommissioning()`(398행, 델리게이트 `Clear()` 연쇄를 도는 곳)보다
     먼저다. 구독을 걷은 뒤에 델리게이트가 비워진다.
   - ⚠ **예외 전파가 달라졌다 — 도달 불가에 가깝지만 설계 성질이다.** `Core::Delegate::Broadcast`는
     콜백마다 `try/catch`로 감싸 예외를 로그로 흘리고 다음 콜백으로 넘어간다
     (`Delegate.inl:140-147`). 옛 코드는 Undo 폐기를 `BeginPlayTransaction`의 `try` 안에서
     직접 했으므로 던지면 재생 진입 자체가 중단됐다. 지금은 중단되지 않는다.
     `Clear()`가 던지는 경로는 사실상 없고(소멸자는 암묵 `noexcept`라 던지면 `terminate`가
     먼저다) Undo 폐기는 편의 기능이라 실패가 재생을 막을 이유도 없어 그대로 둔다.
     다만 **씬 무결성에 관한 일을 이 훅에 추가하면 안 된다** — 실패가 조용히 지나간다.
     컨트롤러 헤더에 적어 두었다.
   - 남긴 것: `MenuBarWindow.cpp:469`의 `m_isGameMode` 대입은 그대로 뒀다. 그것을
     컨트롤러로 옮기면 즉시(버튼 클릭) → 다음 프레임(배리어)으로 타이밍이 한 프레임
     밀린다. `m_isGameMode`가 CLI 재생을 못 따라가는 결함은 실재하지만 그 수정은
     동작 변경이라 리팩터 슬라이스에 섞지 않는다 — 게이트가 현재 결함을 못 박아 두었으니
     고치면 PASS 줄 표기가 바뀌어 드러난다.
4. `PrefabEditor`를 Editor로 이동한다. ✅

   여덟 번째 슬라이스 — E3-4 (2026-08-23):

   - ✅ `ScriptBinder/PrefabEditor.{h,cpp}`(103줄)를 `EngineEntry/`로 옮겼다.
     **allowlist 4줄이 한 번에 사라졌다** — 소스 멤버십 2건과 ImGui include 2건.
     경계 부채 79 → **75**.
   - ✅ 이동이 깨끗했던 이유: **Core의 `PrefabEditor` 참조 4건이 전부 주석이었다.**
     `Prefab.cpp`·`PrefabUtility.cpp`·`PrefabUtility.h`·`SceneManager.cpp` 어디에도
     심볼 사용이 0건이고, 실제 소비자는 Editor 3곳(`ConsoleCommandSystem.cpp`,
     `EditorMain.cpp`, `HierarchyWindow.cpp`)뿐이다. 셋 다 같은 프로젝트
     (`Academy_4Q.vcxproj`)에 있고 그 프로젝트가 `$(SolutionDir)EngineEntry\`를
     include 경로에 두므로 `#include "PrefabEditor.h"`가 그대로 해결된다.
   - ⚠ **바이너리는 이 이관 전후로 같다.** 링커가 참조되지 않는 코드를 이미 버려서
     옮기기 전에도 `Player.exe`에 PrefabEditor 흔적은 0건이었다(E3 두 번째 슬라이스에서
     실측). 바뀐 것은 **컴파일 대상과 층 경계**다 — 저작 도구가 Player가 링크하는
     정적 라이브러리(`ScriptBinder`)에서 빠졌다. "Player가 가벼워졌다"고 적으면 거짓이다.
   - ⚠ **죽은 가드를 걷었다.** 옛 `PrefabEditor.h`는 본문 전체가 `#ifndef
     DYNAMICCPP_EXPORTS`로 감싸여 있었는데, 그 매크로를 정의하는 곳이 저장소에 하나도
     없다(vcxproj·props·targets·sln·소스 전수 확인, 솔루션에 Dynamic_CPP 프로젝트
     자체가 없다). 그런데 `SceneManager.cpp`의 주석은 "그 가드 때문에 이 헤더를 쓸 수
     없다"고 적혀 있었다 — **이미 틀린 설명이 코드 결정의 근거로 남아 있었다.**
     주석도 함께 고쳤다. 이제 이유는 가드가 아니라 층이다.
   - ⚠ 헤더의 `ImGuiRegister.h`도 죽은 include였다(ImGui 심볼 사용 0건). 그것이 이
     파일의 유일한 ImGui 경계 간선이었다. 지우기 전에 소비자 셋이 전이로 받고 있는지
     확인했다 — `HierarchyWindow.cpp`가 `ImGui::`를 87번 쓰면서 직접 include가 0건이라
     위험해 보였지만, 자기 헤더 `HierarchyWindow.h:3`이 물고 있었다.
   - ✅ `verify-prefab-editor-ownership.ps1` 신설. **주석은 걸러내고 코드만 본다** —
     Core의 여러 파일이 "PrefabEditor가 하던 일"을 설명하는 주석을 갖고 있고, 그것까지
     위반으로 세면 설명을 지워야 통과하는 게이트가 된다. (이 저장소의 include 경계
     검사가 실제로 그 문제를 안고 있어 별도 과제로 띄워 두었다.) 네 갈래 음성 테스트로
     검출과 **주석 관용**을 둘 다 확인했다.
5. BT/Animation runtime graph와 node-editor layout/pin/build 자료를 분리한다.
6. `PlayerMain`이 이미 소유한 startup load/start 요청을 명시적인 runtime primitive로
   고정하고, `SceneManager`의 남은 Player mode 분기를 제거한다. ✅

   여섯 번째 슬라이스 — E3-6 (2026-08-23):

   - ✅ `LoadSceneImmediate` 안의 `if (EngineMode::IsPlayer()) { SetGameStart(true);
     "Scene loaded" }` 분기를 `PlayerMain`으로 옮겼다. **Core의 마지막 Player mode
     분기가 사라졌다** — 남은 `EngineMode` 분기는 `TagManager`의 저작 가드 4건뿐이고
     그것은 "저작은 에디터에서만"이라는 별개 정책이다. `SceneManager.cpp`의
     `EngineMode.h` include도 죽어서 함께 걷었다. 경계 부채 80 → 79.
   - 새 API를 만들지 않았다. `SetGameStart`가 이미 공개된 런타임 primitive다.
     계획서가 말한 "명시적인 runtime primitive로 고정"의 요지는 새 함수를 만드는 것이
     아니라 **요청이 Player에서 나오게 하는 것**이다 — 씬 로더가 실행 모드를 캐묻는
     대신, 정책을 가진 쪽이 primitive를 부른다. 감싸개를 더하는 것은 speculative
     generality다.
   - 이동이 정확히 등가임을 확인했다: 옛 분기는 `LoadSceneImmediate`의 직선 경로
     끝(로그·이벤트 브로드캐스트 뒤, `Reset()` 앞)에 있었고 그 뒤로 던질 수 있는 코드가
     없다. 따라서 "마커가 찍힘 ⟺ 함수가 non-null 반환"이 성립하고, 새 코드의
     `else` 절과 같다. `Scene::Reset()`은 **빈 함수**라(C++ 핫리로드 은퇴 잔재)
     `SetGameStart`가 그 앞이든 뒤든 차이가 없다.
   - ⚠ **"Scene loaded" 문구는 스모크 판정 마커다.** `Tools/build.ps1:962`가
     `'Scene loaded:[^\r\n]*' + escape(StartupScene)`으로 찾는다. Core가 찍던 것은
     함수에 넘어온 경로 문자열(`loadSceneName = name.data()`)이고 Player가 넘긴 값이
     `scenePath.string()`이므로, Player에서 같은 값을 찍어 문구가 보존된다. 이걸 놓치면
     게임 빌드 검증이 조용히 깨진다.
   - `<iostream>`을 `PlayerMain.cpp`에 명시적으로 넣었다. `std::cout`을 이 파일에서
     처음 쓰는데(기존은 전부 `std::printf`), 전이 include에 기대면 비유니티 빌드에서
     깨진다 — 이 저장소가 이미 두 번 겪은 함정이다.
   - ⚠ **Player 스모크는 `SetGameStart(true)`를 검증하지 못한다.** 처음에는
     `[SMOKE] managed OnBeginSimulation` 마커가 재생 시작을 증명한다고 봤는데 틀렸다.
     그 마커는 `Scene::DrainPendingLifecycle`의 `State_StartCalled` 가드에서 나오고,
     그 드레인은 `SceneManager::Initialization`이 **재생 여부와 무관하게 매 프레임**
     부른다(`SceneManager.cpp:362`, `PlayerMain.cpp:354`). 즉 재생을 안 켜도 그대로
     찍힌다. 스모크가 이 슬라이스에 대해 실제로 지키는 것은 `Scene loaded` 마커뿐이다.
     `SetGameStart(true)`가 빠지면 조용히 망가지는 것: 시뮬레이션 phase 전이, 입력
     (`InputActionManager.cpp:8`·`PlayerInput.cpp:13`이 `!IsGameStart`면 즉시 return),
     애니메이션 잡. **스모크가 전부 통과한 채 입력이 죽은 게임이 출하된다.**
     그래서 `verify-play-mode-policy-boundary.ps1`에 정적 단정 둘을 더했다 —
     `PlayerMain`이 `SetGameStart(true)`를 부르는가, `Scene loaded` 마커를 찍는가.
     둘 다 음성 테스트로 검출을 확인했다. 런타임 커버리지는 여전히 없다(Player에는
     콘솔 명령 계층이 없어 상태를 물어볼 수단이 없다) — 후속 과제로 남긴다.
7. Editor/Player에 복제된 frame orchestration을 `EngineRuntime` primitive로
   고정한다. `Time->Tick` 안에서 현재 frame delta 확정 → pre-physics →
   physics → game logic → post-physics 순서를 한 곳이 소유하고, 두 Host는
   Editor hook과 presentation만 조립한다. ✅

   일곱 번째 슬라이스 — E3-7 (2026-08-23):

   - ✅ `ScriptBinder/RuntimeFrame.{h,cpp}` 신설. `Runtime::ResolveFrameDelta`가
     delta를 정하고 `Runtime::TickSimulationFrame`이 재생 중 순서를 소유한다.
     두 Host의 루프에서 그 구간이 통째로 빠졌다(EditorMain −80/+11, PlayerMain −50/+7).
   - ⚠ **실측: 두 Host의 재생 구간은 이미 완전히 같았다.** 관리(CLR) 틱을 감싸는
     `TickScripts`·`TickScriptsPrePhysics`는 두 파일에서 주석만 다르고 본문이
     **글자 그대로 동일**했고(주석 제거 후 토큰 비교로 확인), phase 순서도 같았다.
     즉 이 슬라이스는 갈라진 것을 맞춘 게 아니라 **갈라질 수 있는 것을 막은 것**이다.
     복제는 한쪽만 고치면 조용히 갈라지고, 그러면 "에디터에서는 되는데 빌드하면
     안 된다"가 된다 — 두 Host를 같은 시나리오로 나란히 태우는 하네스가 없어
     런타임으로는 못 잡는다.
   - ✅ **판정 기준 "delta 0은 pause 상태에서만 허용된다"가 이미 깨져 있었다.**
     `EditorMain`의 편집 모드 경로가 `SceneManagers->GameLogic()`을 인자 없이 불렀고,
     선언이 `float deltaSecond = 0`이라 delta 0이 조용히 들어갔다. 그때는 일시정지가
     아니다. 기본 인자를 없애고 호출부가 `GameLogic(0.0f)`라고 적게 했다 — 편집 모드는
     일시정지가 아니지만 시간도 진행하지 않는 **제3의 상태**이고, 그 사실이 호출부에
     드러나야 한다. 규약을 바꾼 게 아니라 위반이 보이게 만든 것이다.
   - ✅ `verify-frame-orchestration.ps1` 신설. 단계 순서·일시정지 분기 위치·두 Host가
     primitive를 부르는지·Host가 `Physics`/관리 틱을 직접 부르지 않는지·`GameLogic`
     기본 인자 부활 여부를 본다. 다섯 갈래 음성 테스트로 검출을 확인했다.
   - ⚠ **그 게이트도 처음엔 거짓 실패를 냈다.** 단계 순서를 파일 전체에서 찾아
     관리 틱의 **정의**(앞쪽 익명 네임스페이스)를 호출로 오인했다. `TickSimulationFrame`
     본문으로 한정해 고쳤다. E3-2의 게이트와 **같은 실수를 두 번** 했다 — 소스에서
     순서를 단정할 때는 범위를 먼저 좁히는 것이 규칙이어야 한다.
   - Editor에 남은 것: `SceneManagers->Editor()`(선택·프리뷰 상태 머신)와 편집 모드
     루프. 둘 다 런타임에 없는 상태라 primitive 밖이 맞다.
   - ⚠ `ResolveFrameDelta`를 처음에 `float`로 썼다가 `double`로 되돌렸다. 두 Host의
     `m_frameDeltaTime`이 `double`이라 좁혔다 넓히는 왕복이 생겼는데, 소비자 둘이
     즉시 `static_cast<float>`를 하므로 결과는 비트 동일이다 — 관측 차이는 없었다.
     그래도 좁힘의 책임이 옛 코드와 같은 자리에 있어야 해서 맞췄다.
   - 검토가 확인한 것: 일시정지를 두 번 읽는 것(`ResolveFrameDelta`와
     `TickSimulationFrame`)은 **옛 코드에도 그대로 있던 성질**이다(EditorMain 494/516,
     PlayerMain 346/356). 새로 생긴 레이스가 아니다.
   - **핵심 판정**: `verify-lifecycle-baseline.ps1`이 `221 사건 · 순서 동일`로 통과했다.
     프레임 루프를 통째로 갈아끼운 뒤에도 Editor의 생명주기 이벤트 순서가 한 건도
     달라지지 않았다는 뜻이다. Player 쪽은 게임 패키지 빌드의 스모크가 통과했다.

판정:

- Edit→Play→Stop 뒤 scene, hierarchy, prefab 연결이 복원되고, **selection은 해제된다.**
  ⚠ 2026-08-23 정정. 원래 "selection이 복원된다"로 적었는데 코드가 그러지 않는다.
  선택은 씬 YAML에 실리지 않아(`.creator`에 `m_selectedEntit` 0건) 스냅샷에 담기지
  않으므로 복원될 경로 자체가 없고, `EndPlayTransaction`이 `resetSelectedObjectEvent`를
  던져 **해제**한다. 그 해제는 선택 사항이 아니다 — 선택이 `Entity*` 원시 포인터인데
  정지가 엔티티를 전부 파괴하므로, 해제를 빼면 `ACCESS_VIOLATION`으로 죽는다(실측).
  "복원"을 원한다면 선택을 instanceID/EntityHandle로 들어야 하고, 그것은 리팩터가
  아니라 기능이라 이 PHASE 밖이다.
  게이트: `verify-play-selection-undo.ps1`.
- DDOL과 C# Awake/OnEnable/Start/OnDisable/OnDestroy 순서가 유지된다.
- `SceneManager`가 Undo, PrefabEditor, Editor mode를 include하지 않는다.
  ⚠ 2026-08-23 정정 — Selection을 이 목록에서 뺀다. `SceneManager`의 선택 관여는
  실측 4건인데, 둘은 델리게이트 선언·전역 별칭이고 나머지 둘은 옮길 수 없다:
  `EndPlayTransaction`의 Broadcast는 위에서 밝힌 안전장치이고,
  `SceneManager.cpp`의 씬 교체 경로 `oldScene->ResetSelectedEntity()`는 Core가 자기
  소유 상태를 치우는 것이다. 유일한 구독자도 Core의 `Scene::ResetSelectedEntity`라
  Editor가 얹은 정책의 실체가 없다. 진짜 이관은 `Scene`이 든 `m_selectedEntity`/
  `m_selectedEntities`를 빼는 것인데, 그것은 이 계획서가 요구한 적 없고 Editor 53곳
  재배선이 필요한 별개 작업이다. Player의 선택 소비자는 이미 0건이라 이득도 없다.
  게이트: `verify-play-mode-policy-boundary.ps1`.
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
- `SerializationPlan.md`: ryml authoring과 cooked runtime의 Archive/source 경계.
- `NetworkFrameworkPlan.md`: E6의 runtime 물리 경계를 소비해 N9에서 headless Server를
  만든다. 네트워크를 이유로 E0~E7의 완료 범위를 확장하지 않는다.

과거 L0/L1 완료 기록과 L2/L2'/L4' 승계 문구는 현재 실행 계획과 중복되고 삭제된
프로젝트·진입점을 전제로 하므로 본문에서 제거했다. 필요한 이력은 Git history와
`BuildPipelinePlan.md`에 남아 있고, 이후 Editor/Core 분리 진행 상태는 E0~E7에만
기록한다.
