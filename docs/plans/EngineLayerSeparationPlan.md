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
- clean checkout의 동일 판정은 canonical 입력의 HEAD 편입과
  `AudioBackendModernizationPlan.md` PHASE 22 AU8/AU9의 FMOD 은퇴·miniaudio package gate를 닫은 뒤
  `BuildPipelinePlan.md`의 `Tracked` gate로 판정한다.

### E1 — Host·설정·경로 정책 분리 ✅ 완료 (2026-08-24 항목 6 완결로 전 항목 종료)

1. `EngineLaunchConfig`, `EnginePaths`, `WindowDesc`를 도입한다.
2. `CoreWindow`의 Editor/Player 분기를 각 Host의 window policy로 옮긴다.
3. `WinProcProxy`를 Editor/Host presentation으로 이동하거나 message sink로 교체한다.
4. `EngineSetting`을 Core의 `RuntimeSettings`와 Editor의
   `EditorPreferences`·`BuildSettings`·단일 `EditorSettingsStore`로 분리한다.
   소비자가 없는 toolchain 상태는 새 singleton으로 옮기지 않고 제거한다.
5. package unpack과 startup scene 정책을 Player bootstrap으로 이동한다.
6. Undo 초기화·종료를 공통 bootstrap에서 Editor bootstrap으로 이동한다.
   ✅ (2026-08-24 — 아래 마지막 불릿의 물리 이관으로 완결)

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
- ✅ Undo 수명 이관 완결(2026-08-24, 94810190 — 항목 6). `ReflectionUndo.h`를
  EngineEntry(EditorRuntime)로 물리 이동하고 Core 리플렉션 사슬에서 절단했다.
  실결합은 include만이 아니었다 — `ReflectionFunction.h`의
  `MakeCustomChangeCommand`가 UndoManager를 실사용했고(빌드가 잡음, 호출자는
  전부 에디터 UI 6파일이라 헬퍼째 이관), inline 전역 `UndoCommandManager`가
  정적 초기화 때 Player에서도 싱글턴을 만들던 뿌리였다(제거 — 소비자는
  GetInstance() 직접 호출). 공통 bootstrap의 init 1·finalize 7곳을 걷고
  수명은 EditorMain이 소유한다. 파괴가 리플렉션 정리보다 앞으로 와 옛
  순서보다 안전하다. `verify-play-mode-policy-boundary`의 Core 부재 단정을
  ReflectionFunction.h까지 확장(주석 관용 필터, 실코드 주입 음성 테스트).

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

소부채 정리 슬라이스 (2026-08-23, e48e1305) — 래칫 부채 8 → 2:

- ✅ `TagManager`의 태그·레이어 정의 저작 mutator 4종에서 `EngineMode::IsEditor`
  가드를 걷었다(Core의 마지막 실행 모드 분기). "저작은 에디터에서만"은 가드가
  아니라 **호출자 부재**로 보장된다 — 호출자는 Inspector와 에디터 CLI뿐이고
  Player는 그 층(EngineEntry·EngineGUIWindow)을 링크하지 않는다
  (ProjectReference 실측). `Load`는 mutator를 거치지 않고 컨테이너를 직접
  채우며 C# 노출도 0. E3-6과 같은 원리(정책은 부르는 쪽이 소유)다.
- ✅ `DataSystem`의 `BUILD_FLAG` 조건부 2건(소멸자 `Finalize`·`LoadMaterial`의
  느슨한 `.asset` 폴백)을 걷었다.
- ⚠ **실측: `BUILD_FLAG`는 도달 불가능한 죽은 매크로다.** GameBuild|x64 구성
  전용인데 그 구성은 `CreatorEngine.sln`의 구성 목록에 없고(Debug/Release뿐)
  어떤 빌드 스크립트도 참조하지 않는다 — `*/x64/GameBuild` 산출물은 8-8 잔재.
  따라서 `#ifndef BUILD_FLAG` 걷기는 전처리 결과 불변의 등가 변환이다.
- ✅ 동종 후속(2026-08-24, aff87fe0): **DYNAMICCPP_EXPORTS도 전면 제거** —
  170파일 373줄. 옛 C++ 핫리로드 모듈의 export 매크로로 정의처 0(E3 실측),
  모듈 은퇴(9-4) 후 전부 항상-참 가드였다. LifecycleTrace.h의 #else 스텁은
  자기 주석("모듈이 은퇴하면 이 분기도 함께 사라진다")의 이행. 파일별
  전처리 균형을 제거 전후 대조해 검증했다.
- ✅ 후속 스윕 완료(458f824d): 나머지 잔재를 전면 걷었다. 활성 분기는
  실측 결과 Profiler 계열 4파일뿐이었고(GizmoRenderer·EditorRenderer·
  PlayerApp의 매치는 "옛 가드를 걷었다"는 역사 주석 — 유지), 거기서 죽은
  매크로 셋(BUILD_FLAG·DYNAMICCPP_EXPORTS·무효화된 WITH_PROFILING)을 함께
  걷었다. vcxproj 10파일의 GameBuild 구성 블록 92개와 산출물 6디렉터리도
  제거(802줄 삭제). 잔여 검증에서 `GameBuilderSystem`이 부분 문자열로 두 번
  오탐 — substring-match 함정의 재연. profile.selftest가 스텁 분기 제거 후
  실구현으로 통과.
- 잔여 부채 2건: `ICustomEditor.h` 편입(E6)·RenderEngine→ScriptBinder
  프로젝트 참조(E4-7)만 남았다.

### E3 — Editor scene lifecycle 분리 ✅ 완료 (2026-08-23)

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
   ✅ (설계 정정으로 완료 — transaction의 물리 이동은 하지 않는다. 씬
   스냅샷·phase 전이는 런타임 primitive라 Core에 남고, 에디터 정책(Undo
   폐기)만 `PlayModeEvent` 구독으로 컨트롤러가 소유한다. 다섯 번째 슬라이스
   기록 참고. `UndoManager` 클래스의 물리 이관은 include 사슬 재설계가
   선행이라 E6 이후 후속.)
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
5. BT/Animation runtime graph와 node-editor layout/pin/build 자료를 분리한다. ✅

   E3-5a — Animation (2026-08-23, 4d4114ff):

   - ✅ `AnimationController`의 `NodeEditor*` 소유(리플렉션 필드!)를 걷어냈다.
     Core(Animator)가 생성 4곳·소멸 1곳을 갖고 있었고, 실소비자는 에디터 창
     `ImGuiDrawHelperAnimator.cpp` 한 파일뿐이라 컨트롤러별 사이드 테이블
     (`GetControllerNodeEditor`)로 옮겼다. NodeEditor의 프레임 간 상태는
     SettingsFile 경로에 결속된 컨텍스트뿐이고 `MakeEdit`이 경로 변화 시
     재생성·매 프레임 Nodes/Links 리빌드하므로 동작 등가다.
   - ⚠ **정찰이 "소비자 0"으로 오판했었다.** `grep -v Animator.cpp` 필터가
     `ImGuiDrawHelperAnimator.cpp`를 부분 문자열로 삼켰다. Release 빌드가
     C2039 열다섯 개로 잡았다 — 제거 단정을 만드는 정찰에서 경계 없는 문자열
     비교 금지(메모리 substring-match-false-results).
   - ⚠ 리플렉션 필드 제거로 직렬화에서 `m_nodeEditor: ~` 한 줄이 사라진다.
     현존 자산 등장 0건 실측(씬들의 컨트롤러 목록이 전부 `~`), 로드는 부재 키
     스킵. 리플렉션 골든을 재생성했고 diff가 정확히 그 한 줄 삭제뿐임을
     눈검산했다. 덤: 소멸자가 전방선언뿐인 불완전 타입에 SafeDelete를 부르고
     있었다.

   E3-5b — BT (2026-08-23, 6300f751):

   - ✅ `BTBuildNode`의 `ed::PinId` 2필드·`ImVec2 PositionEditor`, `BTEnum`의
     ImVec2 변환·핀 헬퍼·`namespace ed` 별칭, `BTBuildGraph`의 핀 세팅·
     `ed::BreakLinks` 호출을 에디터 층 신설 `EngineGUIWindow/BTEditorBridge.h`
     (`BTEd`)로 옮겼다. `BehaviorTreeComponent.h`의 imgui include는 죽어 있어
     함께 걷었다. **Core의 ImGui/node-editor 결합 0** — 경계 부채 12 → 8.
   - ✅ PinId는 저장할 것이 아니었다 — 노드 ID에서 순수 유도된다(input=ID<<1,
     output=ID<<1|1, 역함수 >>1·&1). 편집기 창(MenuBarWindow)이 호출 시
     유도하고, 화면 좌표는 창의 사이드 테이블(`s_nodeScreenPos`)이 소유한다.
   - ⚠ `BTBuildGraph::DeleteNode`의 `ed::BreakLinks`는 중복이었다 — 유일한
     호출부가 이미 직전에 직접 부른다. Core 자료구조가 편집기 세션 API를
     부르던 유일한 지점이 사실 죽은 호출이었다.
   - ⚠ 직렬화 형식 불변 — 핀·화면 좌표는 원래 리플렉션 밖이라 `.bt` YAML에
     없다. BT 관리측 재설계(9-8)의 "기존 에셋이 수정 없이 열린다" 제약과
     정합. BT 스모크가 핀 없는 자료형으로 픽스처를 로드해 트리 3개·틱 546을
     돌렸다.
   - ⚠ **부수 발견: BT 스모크 픽스처가 유실돼 있었다.** 8ae17250(8-21, 레거시
     BT 제거)이 손으로 만든 게이트 픽스처 `BTProbe.bt`/`.blackboard`까지 git에서
     쓸어 갔고, 워킹 트리 사본으로 게이트가 연명하다 이날 사본마저 지워져
     세트가 붉어졌다. CLI에 BT 그래프 저작 표면이 없어 재생성 불가 — 삭제 직전
     커밋에서 복원해 재커밋했다(eada47cb). .meta는 에디터가 재발급.
   - ⚠ 한계: BT 편집기 창의 드래그·링크 상호작용은 회귀 세트가 안 여는 UI
     경로다. 이번 변경의 그 부분 검증은 등가 변환 논증(순수 유도·중복 제거)과
     스모크(자산 로드·틱)에 기댄다.
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

### E4 — Editor 렌더링 분리 ✅ 완료 (2026-08-24, isEditorView 중립 개명만 E7 이관)

2026-08-23 착수 전 실측:

- ⚠ **판정 기준 "Player pipeline에는 node 자체가 없다"가 지금 거짓이다 — 그런데
  생각보다 나쁘다.** `LivePipelineDesc::DeclareAll`은 `if (!node.IsActive()) continue;`로
  비활성 노드를 건너뛰지만(`EnhancedLivePipelineDesc.cpp:195`), **`InitializeAll`에는
  그 필터가 없다**(101-133행). 즉 Player도 Grid/GizmoIcon/GizmoLine/UI 노드의
  `Initialize`를 돌려 PSO·버퍼를 만든다. "노드가 없다"가 아니라 "노드가 있고 자원도
  만드는데 그리기만 건너뛴다"가 현재 상태다.
- ⚠ **Game View 픽셀 동일성을 잴 수단이 없다.** `run-all.ps1`에 렌더 결과를 비교하는
  검사가 없고(해상도 스위프뿐), `dx12.*`/`vk.*` 35종은 격리된 합성 씬의 자기 일관성만
  잰다 — "리팩터 전/후 대조"가 아니다. `EnhancedLiveDisplayEntrySnapshot`은 메타데이터만
  갖고 픽셀·핸들이 없다. E3에서 두 번 겪은 "판정을 잴 수단이 0"이 여기서도 반복된다.
- ⚠ **`dx12.uploadring`이 비결정적이다.** E4 기준선을 뜨다 발견했다 — 같은 바이너리로
  단독 9회에 8통과·1실패, 전체 스위트에서도 한 번 실패. 실패 줄의 `worker 생성 0`이
  핵심으로, 링이 worker 세그먼트를 늘리는 경로가 경합에 의존해 매번 태워지지 않는다.
  무효·겹침은 0이라 정확성 위반이 아니라 **커버리지 누락**이다. 이것 하나로 문서
  기준선 `통과 28 · 실패 2`가 `통과 27 · 실패 3`으로 보여 회귀로 오인하기 쉽다.
  `Tools/dx12-validation/README.md`에 적어 두었다. 기준선 대조를 게이트로 쓸 때는
  제외하거나 불일치 시 재시도해야 한다.
- ⚠ **계획서 항목 4의 `EnhancedGizmoSceneBinding`은 대상이 아니다.** 헤더는
  `RenderEngine/`, 구현은 `ScriptBinder/EnhancedGizmoSceneBinding.cpp`
  (`ScriptBinder.vcxproj` 등록)로 **"타입은 render 소유, 생성은 gameplay 소유"** 브리지
  패턴이다. Editor 파일이 아니므로 "Editor로 이동"하면 이미 굳힌 경계를 역행시킨다.
  이 항목의 실제 잔여는 `GizmoRenderer`와 `EditorImGuiTexture`뿐이다.

첫 슬라이스 — E4-1 `EnhancedUIPass` 재분류:

- ✅ `EnhancedUIPass.{h,cpp}`를 `Render/Passes/Editor/` → `Render/Passes/UI/`로,
  그 테스트 `EnhancedUITest.cpp`를 `RHI/DX12/Tests/Editor/` → `Tests/UI/`로 옮겼다.
  로직 변경 0, 순수 재분류다. **경계 부채 75 → 68(7건 감소).**
- ✅ 이 패스는 **런타임 UI를 그린다.** 헤더가 스스로 밝히듯 DX11 `UIPass`의
  `m_UIRenderQueue`(게임 uGUI 스프라이트)를 인스턴싱으로 다시 쓴 것이지 에디터 UI가
  아니다. `Editor/` 밑에 있던 것이 오분류였다. 테스트 디렉터리도 패스 카테고리를
  미러링하므로(`Geometry`/`Lighting`/`PostProcess`/`Editor`) 테스트도 함께 옮겼다.
- ⚠ **파일 하나 옮겼는데 7건이 줄어든 이유**: 경계 검사기의 `is_forbidden_core_include`가
  `is_editor_path`를 재사용해서, 경로에 `Editor/`가 들어간 헤더를 **include하는 것만으로**
  금지 간선이 된다(`check_include_boundary.py:254-257`). 그래서
  `#include "../Passes/Editor/EnhancedUIPass.h"` 자체가 위반이었다. 이동으로
  `EnhancedSceneRenderer.cpp` 1→0, `VulkanUITest.cpp` 1→0, `EnhancedUITest.cpp` 1→0,
  `EnhancedSceneRendererLive.cpp` 6→5, 소스 멤버십 3건이 한 번에 사라졌다.
  (`Live`의 6번째는 `RHI/IImGuiHost.h` — 이름에 imgui가 들어가서다.)

두 번째 슬라이스 — 파이프라인 구성 게이트 신설:

- ✅ `pipeline.nodes` CLI(Editor)와 `[SMOKE] pipeline.node` 로그(Player), 그리고
  `verify-pipeline-composition.ps1`을 신설했다. 둘 다 `LivePipelineDesc::Dump()`의
  같은 값을 낸다 — 그 Dump는 이미 있었고 디버그 스냅샷에도 실려 있었는데 아무도
  찍지 않았다.
- ✅ **픽셀 캡처를 만들지 않기로 했다.** 처음에는 `render.capture` CLI로 Game View
  PNG를 떠서 전후 대조할 계획이었는데, 실측하니 중복이었다 — Editor 패스 자가 검사
  (`EnhancedGridTest` 등)가 이미 리드백으로 픽셀을 읽어 임계값·좌표까지 단정한다.
  E4-1의 파일 이동에서 dx12 스위트가 기준선과 정확히 일치한 것이 그 증거다.
  **빈 구멍은 패스 내부가 아니라 조립이었다.** 판정 기준 2번이 묻는 것도 조립이다.
- ⚠ **실측 결과가 착수 전 가정보다 나쁘다. Editor와 Player의 파이프라인이 완전히
  동일하다** — 둘 다 19노드이고 그 안에 `Grid|always`·`GizmoIcon|always`·
  `GizmoLine|always`가 있다. `always`는 `active` 술어가 아예 없다는 뜻이라
  `IsActive()`가 참이고, 따라서 **`DeclareAll`이 건너뛰지 않는다.** 즉 출하 게임이
  에디터 그리드·기즈모 패스를 매 프레임 렌더 그래프에 선언한다. 술어를 가져 실제로
  건너뛰는 것은 `WireFrame`과 `VolumetricFog` 둘뿐이다.
  (앞서 "노드가 있고 자원만 만들고 그리기는 건너뛴다"고 적었는데 그건 절반만 맞았다 —
  `InitializeAll`에 필터가 없는 것도 사실이지만, 애초에 `DeclareAll`도 안 건너뛴다.)
- Player 구성은 CLI가 없어 스모크 로그로 읽는다. 실제로 게임 패키지를 빌드해
  `Player.exe --smoke 120`을 돌리고 런타임 HTML 로그에서 19노드를 확인했다.
  ⚠ `Debug->LogDebug`는 stdout이 아니라 그 HTML 로그로 간다 — `%TEMP%\CreatorEngine\
  Player\<pid>\RuntimeData\Log\`. stdout만 보면 마커가 없어 보인다.
- 음성 테스트: 프로브가 Grid 노드를 빠뜨리게 만들고 빌드해 게이트가 붉어지는 것을
  확인했다(정확한 메시지까지). 무의미성 방지 셋(노드 0개·요약 없음·행/요약 불일치)도
  각각 잡는 것을 확인했다.

세 번째 슬라이스 — E4-2 `IRenderFeatureContributor` 도입과 에디터 노드 기여 이관:

- ✅ `Render/Core/RenderFeatureContributor.h` 신설 — Host가 파이프라인 조립 시점
  (UI 노드 뒤, live_present 앞)에 자기 노드를 끼워 넣는 계약이다.
  `LiveFrameBinding.isEditorView`는 중립 비트 `viewFlags`
  (`LiveViewFlags::kScreenSpaceUI`/`kSceneOverlay`)로 교체했다 — UI 노드는 전자,
  기여 노드는 후자를 본다. 블랙보드 슬롯 어휘(`LiveSlots`)는 기여자도 같은
  계약으로 이어야 하므로 .cpp 익명 네임스페이스에서 `EnhancedLivePipelineDesc.h`로
  올리되, `kGizmoDepth`는 기여 노드끼리만 잇는 슬롯이라 Core 어휘에서 빼고
  기여자 파일로 옮겼다.
- ✅ Grid/WireFrame/GizmoIcon/GizmoLine의 노드 등록·패스 멤버(DX12·Vulkan 양쪽
  파이프라인 구조체)·SetOutputFormat 8줄·프레임별 feed 2줄·WireFrame 술어의
  `GizmoRenderer::GetActive()` 호출이 전부 Core에서 사라졌다. §4.4의 "RenderCore는
  `GizmoRenderer::GetActive()`를 호출하지 않는다"가 이 슬라이스로 달성됐다
  (`EnhancedSceneRendererLive.cpp`의 GizmoRenderer 참조 0). 경계 부채 68 → **64**
  (Live.cpp 금지 include 5→1, 남은 1은 `IImGuiHost.h`).
- ✅ `EngineEntry/EditorSceneOverlayContributor`가 같은 자리에 같은 이름·순서·술어로
  4노드를 기여한다. 패스 인스턴스는 Contribute 호출마다 새로 만드는 묶음
  (shared_ptr)이 소유하고 노드 람다가 붙들어 desc와 수명을 같이한다 — 파이프라인
  둘(DX12·Vulkan)이 공존해도, 리사이즈 재구축이 이어져도 섞이지 않고, Vulkan의
  SPIR-V ScopedOutput 아래에서 초기화되는 것도 다른 노드와 같은 기계를 타므로
  자동이다. 설치는 `EditorMain`이 렌더러 초기화(렌더 스레드 기동) **전에**, 해제는
  `StopLiveRenderThread` 뒤에 한다. 프레임별 기즈모 데이터는
  `RenderFeatureContext.gizmoScene`(LiveState 멤버의 안정 주소)으로 넘기고
  GizmoIcon/GizmoLine 노드의 custom prepare가 예전 feed와 같은 시점(PrepareAll)에
  물린다 — 소비자가 하나뿐이라 범용 사이드밴드 채널은 만들지 않았다.
- ⚠ 계약 헤더를 처음 `Interfaces/`에 두었다가 래칫이 상향 간선으로 잡았다.
  `RenderEngine/Interfaces`는 의사층 1(순수 데이터, 모든 층이 include 가능)이라
  RHI 헤더(`RHIFormat.h`)를 물 수 없다. 계약이 `LivePipelineDesc` 어휘와 결합돼
  있으므로 `Render/Core/`가 맞는 집이다.
- ✅ **판정 기준 2가 조립 수준에서 달성됐다.** Editor 런타임 구성은 전후 동일
  (19노드, Grid/GizmoIcon/GizmoLine=always, WireFrame=inactive — 게이트 기대값
  무변경 통과가 곧 기여자 발화의 런타임 증거)이고, **Player는 15노드로 에디터
  오버레이 노드 자체가 없다**(신선 패키지의 `--smoke 240` 런타임 로그 실측,
  exit 0). InitializeAll도 노드 목록을 돌므로 Player는 이제 그 4패스의 PSO·버퍼도
  만들지 않는다. 패스 소스의 물리 이동(E4-3)은 별개로 남는다.
- ✅ `verify-pipeline-composition.ps1`에 판정 C(조립 소유권 정적 단정)를 추가했다.
  부재 단정 앞에 존재 단정(기여자의 4노드 등록·EditorMain 설치·설치가 초기화보다
  앞)을 두고, 순서 단정은 등장 횟수를 먼저 못 박았다(E3-2·E3-7의 거짓 실패 재발
  방지). 주석은 걸러내고 코드만 본다(E3-4 교훈). 음성 테스트 4갈래(Core 등록
  주입·기여자 등록 누락·설치 제거·Player 설치 주입) 전부 검출을 확인했다.
- ⚠ **첫 Player 실측이 낡은 로그를 재고 있었다.** 스모크 로그는 종료 시 PID
  루트와 함께 정리되므로 실행 중 스냅샷을 떴는데, 이전 세션(10:49)의 잔존 PID
  루트가 같은 경로에 남아 있어 감시 루프가 그것을 먼저 집었다 — 변경 전 19노드가
  나와 이관 실패로 보일 뻔했다. 로그 파일명의 생성 시각으로 걸러냈고, 실행 PID를
  특정한 재실측으로 15노드를 확정했다. "게이트가 낡은 바이너리를 잰다"의 로그판 —
  Player 런타임 관측은 반드시 PID를 특정하라.
- ✅ Release 비유니티 `Academy_4Q`·`Player`(오류 0, 기존 PhysX PDB 경고뿐)와 Debug
  빌드, 경계 래칫 64/64, 첫 프레임 전 종료 6/6, lifecycle 221사건 순서 동일,
  workspace package `Dynamic_CPP-0a16c02446f24d42a4a4e69d958efc91`(verification
  passed, smoke exit 0), dx12 스위트 집계 통과 28·완료 4·실패 2·무판정 1
  (문서화된 기준선과 정확히 일치)을 통과했다.

1. `EnhancedUIPass`를 runtime UI 위치로 먼저 분류·이동한다. ✅
2. `IRenderFeatureContributor`와 중립적인 view flags를 도입한다. ✅
   에디터 4노드의 조립·수명 소유권 이관을 포함해 위 세 번째 슬라이스로 완료.
3. Grid/Gizmo/Wireframe/GizmoIcon pass를 `EditorRender`로 이동한다. ✅
   조립·수명은 E4-2, 값 타입 절단은 E4-3a, 물리 이동은 E5-2(아래)가 완결했다 —
   `EditorRender.vcxproj`(층 5, E6 목표 프로젝트의 선행 신설)가 8파일을 소유하고
   Academy_4Q(기여자)와 RenderTests(자가 검증)가 참조한다. 이동하며 Grid/Icon
   .cpp의 죽은 DX12 구현 include 7건(정찰 실측: 본문 사용 0)도 걷었다.
   ⚠ 이 이동은 두 결합을 먼저 끊어야 한다: ① Core의 수집이 패스 타입·인스턴스에
   의존한다 — **E4-3a로 절단 완료(아래)**. ② dx12·vk 자가 검증 테스트 9파일이
   RenderEngine 안에서 패스를 직접 물고 있어, 패스만 옮기면 Core→Editor 상향
   include가 새로 생긴다 — 테스트 이동은 E5(self-test 분리) 소관이므로 이 항목의
   완결은 E5와 맞물린다. 이제 남은 블로커는 ②뿐이다.

   여섯 번째 슬라이스 — E4-3a 기즈모 값 타입·선 수식을 Core로 추출 (2026-08-23):

   - ✅ `EnhancedGizmoSceneTypes.h` 신설 — `EnhancedGizmoIcon`(구
     `EnhancedGizmoIconPass::Icon`)·`EnhancedGizmoLineVertex`(구
     `EnhancedGizmoLinePass::Vertex`)·`EnhancedGizmoLineCollector`가 Core 소유가
     됐다. 도형→선 수식(약 180줄, DX11 이식 — 세그먼트 수까지 그대로)은
     `EnhancedGizmoLineCollector.cpp`로 자리만 옮겼고 "수식의 집은 하나"라는
     패스 주석의 규칙이 새 집에서 그대로 성립한다.
   - ✅ GT 수집(ScriptBinder의 `EnhancedGizmoSceneBinding.cpp`)이 더 이상 에디터
     패스를 인스턴스화하지 않는다 — `BuildEnhancedGizmoSceneData`가
     `EnhancedGizmoLineCollector&`를 받고, `Capture...`의 지역 수집기도 Core
     타입이다. `EnhancedGizmoSceneBinding.h`의 에디터 패스 include 2건이
     사라졌다. 경계 부채 58 → **56**.
   - ✅ 패스는 타입 alias(`using Vertex/Icon = ...`)와 위임 Add* 래퍼로 기존
     API를 유지한다 — 자가 검증 테스트 9파일이 무변경으로 컴파일되고, E5가
     테스트를 옮기면 래퍼를 함께 걷을 수 있다. 유일한 테스트 수정은
     `EnhancedGizmoSceneTest.cpp`의 Build 호출부(수집기로 받은 뒤
     `SetVertices`로 싣는다 — 수집분 먼저, 주입분 나중이라 정점 순서 불변).
   - ⚠ python 텍스트 왕복이 두 소스 파일의 CRLF를 통째로 LF로 뒤집었다
     (universal newline 읽기 + `newline=''` 쓰기). 바이너리 재정규화로 복원했다 —
     원자 writer 줄바꿈 함정의 편집판이다. 소스 편집 스크립트는 바이트로
     읽고 바이트로 써야 한다.
   - ✅ Release 비유니티 `Academy_4Q`·`Player`, Debug 빌드 오류 0, 경계 래칫
     56/56, dx12 스위트 28·4·2·1(기즈모 5종 — gizmoicon·gizmoline·gizmoscene·
     grid·wireframe 전부 통과, 픽셀·정점 수 단정이 수식 이관의 등가성을 증명),
     구성 게이트 PASS, 첫 프레임 전 종료 6/6을 통과했다.

   네 번째 슬라이스 — E4-4a `GizmoRenderer` 물리 이동 (2026-08-23):

   - ✅ `RenderEngine/GizmoRenderer.{h,cpp}`(84줄)를 `EngineEntry/`로 옮겼다.
     E4-2가 Core의 유일한 코드 참조(WireFrame 술어의 `GetActive()`)를 기여자로
     옮긴 뒤라 이동이 풀렸다 — 남은 Core 쪽 등장은 전부 주석이다
     (`EnhancedWireFramePass.h:18`, `EnhancedGizmoSceneTest.cpp` 2곳). 실소비자는
     Editor 4곳(`EditorMain`, `SceneViewWindow`, 기여자 술어, 자기 자신)뿐이었다.
   - ✅ 죽은 가드 둘을 함께 걷었다. `DYNAMICCPP_EXPORTS` 전체 가드(정의처 0 —
     E3-4에서 전수 확인)와 `ShowGridSettings`의 `#ifndef BUILD_FLAG`(Academy_4Q는
     BUILD_FLAG를 정의하지 않아 항상 참). 게임 빌드 배제는 이제 매크로가 아니라
     프로젝트 편입이 보장한다 — Player가 링크하는 RenderEngine에서 이 파일이
     빠졌다.
   - ✅ 경계 부채 64 → **62**(GizmoRenderer.cpp의 BUILD_FLAG 조건부 1건,
     Editor/ImGui include 1건 소멸). E4 항목 4의 잔여는 `EditorImGuiTexture`뿐이다.
   - ✅ Release 비유니티 `Academy_4Q`·`Player`, Debug 빌드(오류 0, 기존 PhysX PDB
     경고뿐), 경계 래칫 62/62, 구성 게이트 PASS(WireFrame=inactive — 이동된
     클래스의 `GetActive()` 경로를 기여자 술어가 실제로 부른 결과), 첫 프레임 전
     종료 6/6을 통과했다.

   다섯 번째 슬라이스 — E4-4b `EditorImGuiTexture` 물리 이동 (2026-08-23):

   - ✅ `RenderEngine/EditorImGuiTexture.{h,cpp}`(82줄)를 `EngineEntry/`로 옮겼다.
     Core 쪽 등장은 `Texture.cpp`의 T6 이력 주석 1건뿐이고, 실소비자는 Editor
     6곳(`EditorAssetPresentation`, `ContentsBrowserWindow`, `InspectorWindow`,
     `MenuBarWindow`, draw helper 2종)이다. 소비자 include가 전부 bare
     `"EditorImGuiTexture.h"`라 include 경로만으로 새 위치가 해결된다.
   - ✅ 죽은 `DYNAMICCPP_EXPORTS` 전체 가드를 함께 걷었다(정의처 0).
   - ✅ 경계 부채 62 → **58**(editor-source-membership 2건, Editor/ImGui include
     2건 소멸). **E4 항목 4가 닫혔다** — `EnhancedGizmoSceneBinding`은 착수 전
     실측대로 이동 대상이 아니다(render 소유 타입·gameplay 소유 생성의 브리지).
   - ✅ Release 비유니티 `Academy_4Q`·`Player`, Debug 빌드 오류 0, 경계 래칫
     58/58, 구성 게이트 PASS, 첫 프레임 전 종료 6/6을 통과했다.
4. `GizmoRenderer`, `EnhancedGizmoSceneBinding`, Editor texture adapter를 이동한다. ✅
   실제 이동 대상은 `GizmoRenderer`(E4-4a)와 `EditorImGuiTexture`(E4-4b)였다.
   `EnhancedGizmoSceneBinding`은 착수 전 실측대로 대상이 아니다.
5. Editor camera 소유권을 `EditorRenderContext`로 이동하고 view로 전달한다. ✅

   일곱 번째 슬라이스 — E4-5 (2026-08-23):

   - ✅ 새 `EditorRenderContext` 클래스는 만들지 않았다 — shared_ptr 하나를 위한
     전용 컨텍스트는 speculative generality다. 이미 있는 `EditorSessionState`가
     에디터 카메라를 소유하고, 생성(RegisterContainer·avoid 플래그 포함)은
     `EditorMain::Initialize`, 반납은 `EditorMain::Finalize`가 한다. 반납은
     `ShutdownLive`의 `CameraManagement->Finalize()`보다 앞이어야 한다(컨테이너가
     닫힌 뒤의 DeleteCamera는 무효) — Core가 지키던 순서를 Editor가 그대로 잇는다.
   - ✅ `BuildLiveFramePacket`이 `Camera* const*` 대신
     `EnhancedLiveViewRequest{camera, sceneOverlayView}` 배열을 받는다. 씬 오버레이
     뷰 판정이 "Core 소유 카메라와의 항등 비교"에서 **Host 선언**으로 바뀌었다 —
     Editor는 자기 카메라에 true, Player는 게임 카메라에 false를 싣는다.
     `EnhancedSceneRenderer::GetEditorCamera()`와 `LiveState.editorCamera`는
     제거됐다. CLI(`camera.editor` status/follow)와 Editor 창들은
     `EditorSessionState`를 본다.
   - ✅ 시각 검증: 변경 전후 에디터 전체 화면 캡처(capture-window.ps1)가 동일하다 —
     씬 뷰에 그리드·방향광 아이콘이 그대로 서고 게임 뷰는 깨끗하다. 오버레이
     뷰 플래그 배선이 Host 선언으로 넘어간 뒤에도 기여 노드가 같은 뷰에만
     그린다는 픽셀 증거다. `camera.editor` status는 에디터 카메라 index 0과
     기본 pose(0,1,-10)를 그대로 보고했다.
   - ⚠ 캡처 도구는 `--console` 모드에서 창을 못 찾는다(MainWindowHandle 0) —
     일반 실행 + CloseMainWindow로 찍어야 한다.
   - ⚠ 남긴 것: packet/내부 필드 이름(`EnhancedLiveViewPacket.isEditorView`,
     `CameraView.isEditorView`)과 표시 대상 enum(`EnhancedLiveDisplayTarget::
     Editor/Game`)은 그대로다. 값의 결정권은 Host로 넘어갔고 이름만 남았다 —
     중립 개명은 표시 스냅샷 공개 API(Editor 창·PlayerMain 소비)까지 걸리는
     기계적 스윕이라 별도 마무리 슬라이스로 둔다.
   - ✅ 게이트 판정 C-5 신설(존재 단정: 세션 설치·Host 뷰 요청 / 부재 단정:
     Core의 editorCamera·GetEditorCamera·Player의 세션 참조). 음성 테스트
     3갈래 검출 확인 — 첫 시도에서 'SetEditorCameraX' 치환이 부분 문자열로
     여전히 매치돼 거짓 음성이 났다. 음성 테스트는 심볼 개명이 아니라 **호출
     제거**로 주입해야 한다.
   - ✅ Release 비유니티 `Academy_4Q`·`Player`, Debug 빌드 오류 0, 경계 래칫
     56/56, 구성 게이트 PASS, 첫 프레임 전 종료 6/6, lifecycle 221사건 순서
     동일을 통과했다.
6. DX12/Vulkan ImGui shell을 RenderCore 밖의 presentation layer로 이동한다. ✅
   E4-6a(호출 역전)·E4-6b(HostImGuiPresentation 물리 이동)·E4-6c(참조 절단)로
   완료. §4.5의 마지막 문장(Player ImGuiHelper 참조)만 ScriptBinder Profiler
   이관에 걸려 후속이다 — 아래 열 번째 슬라이스 참고.

   2026-08-23 착수 전 실측 (E4-6 정찰):

   - **Core→셸 결합은 단 2개 파일군·4개 지점뿐이다.**
     `EnhancedSceneRendererLive.cpp`(24행 include; 585행 Vulkan 리드백의
     `SubmitCpuRgbaFrame` push; 4210-4224행 `GetLiveDisplayImTextureId`의 ID 해석;
     4528행 진단 문자열의 `GetBackendName`)와
     `EnhancedSceneRendererLiveDX12Adapter`(cpp 5행 include;
     `OpenDisplayTexture(IImGuiHost&, DisplayToken)` 공개 API). ScriptBinder·
     ImGuiHelper에는 셸 참조가 0건이다.
   - **공짜 절단 1건**: `RenderScene.cpp:2`의 `#include "ImGuiRegister.h"`는
     `ImGui::` 사용이 0건인 죽은 include다 — RenderEngine→ImGuiHelper 결합의
     실체 중 하나가 죽은 참조였다.
   - **셸은 자체 디바이스 섬이다.** ImGuiDx12Shell/ImGuiVulkanShell은 라이브
     렌더러와 별개의 자기 DX12DeviceResources/VulkanDeviceResources(디바이스+
     스왑체인)를 세우고, 그래서 interop이 DX12 공유 핸들(`CreateSharedHandle`→
     `OpenSharedTexture`)·Vulkan CPU 리드백(`SubmitCpuRgbaFrame`)이다. 셸의
     RenderEngine 내부 의존(DeviceResources·TextureCache·RetireQueue·Texture)이
     깊으므로 물리 이동 시 새 프로젝트가 RenderEngine을 참조해야 한다 —
     presentation 층이 Core 위라 방향은 합법이다.
   - **Core에 반드시 남는 것**: 표시 수명 오케스트레이션(`displayLifetimeMutex`
     아래 BeginDisplaySnapshot/PublishDisplayResultLocked/Invalidate... — GT/RT/CE
     세 스레드가 만나는 지점)과 표시 텍스처 3동사 계약(만든다/폐기한다/연다).
     `RegisterTexture(Texture*)`는 라이브 표시와 별개 계약(에셋 텍스처 → ImGui
     ID)이라 이 좁히기에서 제외한다.
   - **소비자 지형**: Editor는 EditorRenderer(오케스트레이션: Initialize/Begin/
     End/RebuildFontAtlas)+EditorImGuiTexture(RegisterTexture)+CLI 진단, Player는
     PlayerMain이 직접 최소 소비(Initialize/일치 가드/Begin/End/Shutdown) —
     ImGuiRegister 펌프도 Win32 입력도 설계상 없다. 두 소비자 모두 Editor/Host
     계층이라 셸 이동 후에도 새 위치를 include하면 끝난다.
   - **테스트·게이트는 셸 생존에 구조적으로 비의존**: dx12.ui/vk.ui는 ImGui 참조
     0의 오프스크린 테스트, dx12 스위트의 정규식은 셸 의존 유일 명령
     (render.livecheck)을 애초에 안 뽑는다. 단 resolution-sweep의 커버리지 상한
     (해상도 7중 2)은 `ImGuiDx12Shell::Resize` 크래시라는 기지 결함이 만든다 —
     셸 재편 때 함께 볼 것.
   - **ImGuiHelper는 imgui 코어가 아니다** — vcpkg가 imgui 코어·백엔드를 공급하고
     ImGuiHelper.vcxproj는 헬퍼(NodeEditor·Profiler·widgets) 7파일만 컴파일한다.
     Player 소스는 ImGuiHelper 심볼 0건이라 Player의 ImGuiHelper 직접 참조는
     제거 후보다.

   슬라이스 계획: **E4-6a** Core→셸 호출 역전 — Core 소유의 좁은
   `IDisplayPresentationSink`(OpenSharedTexture/SubmitCpuFrame/GetCpuFrameTextureId)
   를 Host가 설치(E4-2 contributor와 같은 패턴)하고 Core의 IImGuiHost 참조를 0으로.
   RenderScene.cpp 죽은 include도 함께 걷는다(부채 56→53). 과도기의 호스트별 위임
   어댑터 중복(각 ~20줄)은 E4-6b가 흡수한다. **E4-6b** 셸 6파일+어댑터를 새
   `HostImGuiPresentation.vcxproj`(StaticLibrary, ImGuiHelper.vcxproj 본보기)로 물리
   이동, RenderEngine을 참조하며 Academy_4Q·Player가 소비(부채 −15).
   **E4-6c** Player.vcxproj의 ImGuiHelper 직접 참조 제거와 RenderEngine→ImGuiHelper
   project-reference 재검토.

   여덟 번째 슬라이스 — E4-6a Core→셸 호출 역전 (2026-08-23):

   - ✅ `RHI/IDisplayPresentationSink.h` 신설(IsActive/GetName/OpenSharedTexture/
     SubmitCpuFrame/GetCpuFrameTextureId — imgui 언급 없는 중립 계약). Host가
     `SetDisplayPresentationSink`로 설치하고, Core의 네 호출 지점이 전부 sink로
     역전됐다: Vulkan 리드백 push는 `PromoteCompleted`가 sink를 **매개변수**로
     받고(구조체 메서드라 LiveState가 스코프에 없다 — 호출자가 mutex 아래 복사해
     넘긴다), `GetLiveDisplayImTextureId`는 표시 수명 락 아래에서 sink로 ID를
     해석하며, DX12 어댑터 `OpenDisplayTexture`는 `IDisplayPresentationSink&`를
     받는다. 진단 문자열은 sink 이름/"none"이다.
   - ✅ **Core(RenderEngine)의 GetImGuiHost/IImGuiHost 코드 참조가 0이 됐다**
     (셸 자신 6파일 제외). `RenderScene.cpp`의 죽은 `ImGuiRegister.h` include도
     걷었다. 경계 부채 56 → **53**(Live.cpp·DX12 어댑터·RenderScene 각 1건).
   - ✅ 어댑터 구현은 Host별 ~20줄 위임(EditorMain·PlayerMain의 익명 네임스페이스,
     유니티 빌드 충돌을 피해 이름을 달리했다). 설치는 렌더러 초기화 전, 해제는
     `StopLiveRenderThread` 뒤 — 셸이 Initialize 전이어도 위임은 안전하다
     (비활성 셸은 no-op/0). E4-6b가 이 중복을 흡수한다.
   - ✅ **픽셀 증거 3종** — sink 오배선은 스모크·게이트가 못 잡고 화면 검정으로만
     드러나므로 창 캡처로 검증했다. ① DX12 에디터: 씬 뷰(그리드+광원 아이콘)·
     게임 뷰 모두 기준 캡처와 동일. ② **Vulkan 에디터**: 설정을 백업하고
     `render.backend: vulkan`으로 부팅해 두 뷰가 표시됨을 확인(SubmitCpuFrame→
     GetCpuFrameTextureId 경로의 실증), exit 0, 설정 복원 확인. ③ 게시 패키지
     `Dynamic_CPP-32d07924...`(verification passed)의 Player 창: FT_Primitives
     씬이 정상 표시되고 에디터 오버레이는 없다, exit 0.
   - ✅ 게이트 판정 C-6 신설(두 Host의 설치 존재 ≥2회 / Core 두 파일의
     GetImGuiHost·IImGuiHost 부재)과 음성 테스트 2갈래 검출 확인. Release
     비유니티 `Academy_4Q`·`Player`와 Debug 빌드 오류 0, 경계 래칫 53/53,
     구성 게이트 PASS, 첫 프레임 전 종료 6/6, lifecycle 221사건 순서 동일.
   - 남은 것: 셸 6파일의 물리 편입(부채 15건)은 E4-6b, Player의 ImGuiHelper
     직접 참조는 E4-6c. Editor 층(EditorRenderer·EditorImGuiTexture·CLI·
     PlayerMain)의 GetImGuiHost 소비는 Host 계층이라 그대로 옳다.

   아홉 번째 슬라이스 — E4-6b 셸 물리 이동, `HostImGuiPresentation` 신설 (2026-08-23):

   - ✅ 새 정적 라이브러리 `HostImGuiPresentation.vcxproj`(ImGuiHelper.vcxproj
     본보기, GUID {D4C2A7F1-...})를 만들고 셸 7파일(IImGuiHost.h·
     IImGuiRendererBackend.h·ImGuiHost.cpp·ImGuiDx12Shell.{h,cpp}·
     ImGuiVulkanShell.{h,cpp})을 `HostImGuiPresentation/RHI/` 구조 보존으로
     옮겼다 — 소비자 5곳의 `#include "RHI/IImGuiHost.h"`가 include 경로 추가만으로
     그대로 산다. 새 프로젝트는 RenderEngine·Utility를 참조한다(presentation 층이
     Core 위 — 방향 합법). Academy_4Q·Player가 참조·include 경로를 얻었고
     RenderEngine은 셸을 더 이상 컴파일하지 않는다.
   - ✅ 죽은 `DYNAMICCPP_EXPORTS` 가드 7개를 이동하며 걷었고, 셸 cpp의 상대
     include(같은 폴더 기대)를 RenderEngine 루트 기준으로 고쳤다.
     `check_include_boundary.py`에 새 프로젝트를 층 5(ImGuiHelper와 동급
     presentation)로 등록했다 — Core(3)가 이 헤더를 물면 이제 **상향 간선**으로
     잡힌다(전에는 allowlist 부채였다).
   - ✅ 경계 부채 53 → **38**(셸 forbidden-core-include 15건 소멸). E0 기준선
     98에서 누적 60건이 내려갔다.
   - ⚠ **유니티 재청크가 잠복 충돌을 깨웠다.** 셸 3 cpp가 RenderEngine에서
     빠지자 유니티 청크가 재편되며 `DX12PSOManager.cpp`와 `RHIShaderCompiler.cpp`의
     익명 네임스페이스 심볼(kCacheMagic·CacheHeader·HashBytes)이 같은 청크에서
     병합돼 Debug 유니티만 붉었다(Release 비유니티는 초록 — 검증 순서가 유니티를
     뒤에 두면 놓친다). PSOManager 쪽을 Pso 접두로 개명해 닫았다 — "유니티
     빌드에서 익명 네임스페이스 이름은 고유하게"라는 저장소 규칙의 재확인이고,
     파일 편입 변경은 반드시 유니티 레그도 태워야 한다.
   - ✅ Release 비유니티 `Academy_4Q`·`Player`, Debug 유니티 빌드 오류 0, 경계
     래칫 38/38(상향 0), 구성 게이트 PASS, 첫 프레임 전 종료 6/6, lifecycle
     221사건 순서 동일, Editor·Player 창 캡처가 E4-6a 기준과 동일, 게시 패키지
     `Dynamic_CPP-38e80a82...` verification passed·Player exit 0.
   - 남은 것(E4-6c): Player.vcxproj의 ImGuiHelper 직접 참조 제거,
     RenderEngine→ImGuiHelper project-reference 재검토(셸이 나가며 RenderEngine의
     ImGuiHelper 실소비가 남았는지 전수 확인), 호스트별 sink 어댑터 ~20줄 중복의
     새 프로젝트 합류.

   열 번째 슬라이스 — E4-6c 참조 절단·어댑터 합류 (2026-08-23):

   - ✅ **RenderEngine→ImGuiHelper project-reference를 제거했다.** 셸이 나간 뒤
     RenderEngine의 ImGuiHelper 헤더 소비를 전수 확인하니 0건이었다(직접 include
     0 — "Profiler" 매치는 전부 자기 소유 IRHIGpuProfiler/DX12GpuProfiler였다).
     include 경로의 `$(SolutionDir)ImGuiHelper\` 3곳도 걷었다. 경계 부채 38 →
     **37**. 시도 중 정규식 이스케이프 실수로 vcxproj 수정 없이 allowlist만
     줄었는데 래칫이 그 자리에서 초과 1건으로 붉어졌다 — 래칫이 자기 오류를
     잡는 실증.
   - ⚠ **Player의 ImGuiHelper 참조는 제거할 수 없다 — 링크 필수다.**
     `ScriptBinder.vcxproj`는 ProjectReference가 0개라(전 프로젝트 include
     경로만으로 컴파일) `Scene.cpp`·`SceneManager.cpp`의 `Profiler.h`(ImGuiHelper
     소속) 심볼을 exe의 직접 참조가 링크로 해소한다. Player 소스 자체는
     ImGuiHelper 심볼 0건이지만 ScriptBinder.lib의 미해결 심볼이 남는다.
     절단하려면 ScriptBinder의 Profiler 소비를 먼저 옮겨야 하고, 그것은
     ProfilingCapturePlan(P축)의 프로파일러 소유권 문제다 — 여기서 확대하지
     않는다.
   - ✅ 호스트별 sink 어댑터 중복을 `HostImGuiPresentation/RHI/
     ImGuiHostPresentationSink.h`(공용 header-only 타입)로 합쳤다. E4-6a의
     ~20줄×2가 하나가 됐고 두 Host가 같은 타입을 설치한다.
   - ✅ Release 비유니티 `Academy_4Q`·`Player`, Debug 유니티 빌드 오류 0, 경계
     래칫 37/37, 구성 게이트 PASS, 첫 프레임 전 종료 6/6.

판정 갱신(E4-6): 항목 6의 실체였던 "RenderCore가 ImGui backend를 소유하지
않는다"가 닫혔다 — Core의 셸 호출 0(E4-6a), 셸 물리 편입 0(E4-6b),
RenderEngine→ImGuiHelper 참조 0(E4-6c). §4.5의 마지막 문장(Player의 ImGuiHelper
참조 제거)만 ScriptBinder Profiler 이관에 걸려 남는다.
7. RenderEngine→ScriptBinder concrete 참조를 render snapshot/bridge 방향으로 줄인다. ✅

   열한 번째 슬라이스 — E4-7 마지막 부채 소멸 (2026-08-24):

   - ⚠ **착수 전 실측이 규모를 정정했다 — "줄인다"가 아니라 이미 0이었다.**
     MSVC 해석 규칙(현재 파일 디렉터리 → /I 순서, /I에서 ScriptBinder가
     Utility보다 앞)대로 RenderEngine 전 소스의 `#include "..."`를 재해석한
     결과 ScriptBinder 헤더로 해석되는 include 0건, 동명 헤더 겹침 0건.
     앞 슬라이스들(프록시 브리지 C1 — "타입은 렌더 소유, 생성은 gameplay
     소유", Socket 절단, E4-2~6)이 소스 결합을 전부 끊어 놓았고 남은 것은
     vcxproj 잔재뿐이었다. 남은 "ScriptBinder/" 등장 3건은 전부 그 절단
     이력을 적은 주석이다.
   - ✅ RenderEngine.vcxproj의 ScriptBinder ProjectReference와 include 경로
     (`$(SolutionDir)ScriptBinder\`)를 제거했다. 같은 실측에서
     `$(SolutionDir)EngineEntry\` include 경로도 해석 0건으로 드러나 함께
     걷었다 — Core(3)가 에디터 특권층(6)으로 가는 문이 /I에 조용히 열려
     있던 셈이다. 죽은 include 경로는 미래 상향 include를 무통보로 허용하는
     문이라 부채 항목이 아니어도 닫는다.
   - ⚠ **전이 링크 함정을 사전 검증했다.** MSBuild 정적 lib의 ProjectReference
     는 exe까지 전이 링크되므로, ScriptBinder.lib를 RenderEngine 경유로만
     받던 exe가 있으면 제거가 링크를 깬다 — 전수 확인 결과 exe 둘
     (Academy_4Q·Player)이 모두 직접 참조라 안전했다.
   - ✅ **allowlist 마지막 항목 삭제 — 경계 부채 0/0.** E0 기준선 98건에서
     완주다. 허용 목록이 완전히 비었으므로 "빈 집합을 성공으로 읽는" 함정을
     경계해 음성 테스트를 했다: 제거한 참조를 재주입하니 정확히 그 항목으로
     붉어지고(초과 1), 복원 후 초록 — 빈 목록에서도 게이트가 살아 있다.
   - ✅ Release 비유니티 `Academy_4Q`(리빌드)·`Player`, Debug 유니티 빌드
     오류 0, 래칫 0/0, 회귀 세트 전체 통과(lifecycle 221사건 순서 동일).

판정:

- DX12와 Vulkan에서 Game View 결과가 분리 전과 동일하다. ✅ (E4-2·E4-5·
  E4-6a의 픽셀 캡처 3종 — DX12/Vulkan 에디터 두 뷰·게시 패키지 Player 창)
- Grid/Gizmo는 Scene View에만 기여하고 Player pipeline에는 node 자체가 없다.
  ✅ (E4-2 — Player 15노드 실측, InitializeAll도 4패스 자원을 만들지 않는다)
- RenderCore가 Editor pass, Editor camera, `isEditorView`, ImGui backend를
  소유하지 않는다. ✅ — 패스는 E4-3/E5-2, 카메라는 E4-5, ImGui backend는
  E4-6, 프로젝트 참조는 E4-7이 닫았다. 단 `isEditorView`는 **결정권**이
  Host로 넘어갔고 필드 이름만 남았다(E4-5의 남긴 것) — 중립 개명은 표시
  스냅샷 공개 API까지 걸리는 기계적 스윕이라 E7 개명·정리 소관으로 넘긴다.

### E5 — DeveloperTools와 테스트 분리 ✅ 완료 (2026-08-23)

1. RenderEngine에 편입된 RHI self-test와 benchmark 실행기를 별도 프로젝트로 옮긴다. ✅
2. Console command와 debug window는 DeveloperTools API를 호출한다. ✅

   E5 항목 2 스윕 (2026-08-23, 9fdaac75):

   - ✅ 테스트 32종·벤치 2종의 선언을 facade에서 걷어내
     `RenderTests/RHI/DX12/Tests/DX12SelfTest.h`의 자유 함수(namespace
     `DX12Test`)로 옮겼다. 정의 26파일 재한정, CLI 호출 35곳 전환, 스택
     인스턴스 35개 제거. Vulkan(VulkanSelfTest.h)과 소유 구조가 대칭이 됐다.
   - ⚠ 전환이 안전한 근거(실측): 정의 34종 전부 renderer 멤버 상태 접근 0 —
     각자 `DX12DeviceResources`를 스택에 세워 돌고, CLI도 빈 인스턴스를 매번
     만들어 부르고 있었다. 애초에 멤버일 이유가 없었다.
   - ⚠ `RunLiveDisplayRegression`은 옮기지 않았다 — 라이브 러너의 정적 상태를
     잠그고 검사하는 진단 수집(Core 소유, `GetLiveDebugSnapshot` 계열)이다.
   - ⚠ 위 문단의 "선언 35종"은 오계상이었다 — 주석 언급(RunPixelCompareTest·
     RunSkySceneTest) 2건이 섞였고 실측은 32+벤치 2=34종이다. grep으로 수를
     셀 때 주석 언급이 섞이는 문제의 재연.
   - ⚠ `EnhancedSceneRenderer`는 이제 인스턴스 멤버 0의 순수 정적 facade다.
     클래스→namespace 정리는 E7의 개명·정리 소관으로 미룬다.
   - 검증: Release 비유니티·Debug 유니티 오류 0, 래칫 통과(부채 8 불변),
     dx12 스위트 집계 28·4·2·1 기준선 정확 일치 — CLI 34종이 새 자유 함수로
     실제 실행됐다.
3. Physics/render 진단 데이터 수집은 Core에 남기고 UI는 Editor로 이동한다. ✅
   2026-08-23 실측 — **이미 완료 상태라 옮길 것이 없다.** `PhysicsDebug`는 빈
   스텁(참조 0), 실제 물리 진단(`ColliderDebugData`·PVD)은 Core 소유에 소비 UI가
   없거나 외부 PVD 앱 직결이고, render 진단은 `GetLiveDebugSnapshot`/
   `GetLiveTuning`/`SetLiveTuning` 값 API로 이미 수집=Core/표시=Editor다.
   Core의 남은 ImGui 결합(ScriptBinder 4건)은 진단이 아니라 BT/애니메이션
   노드 편집기 결합 — E3-5 소관.

2026-08-23 착수 전 실측과 첫 슬라이스 (E5-1):

- 실측: 테스트의 실체는 Tests/ 트리 36파일(19,935줄)만이 아니었다.
  `EnhancedSceneRenderer.cpp` **4,931줄 전체가 self-test 구현**(Run* 멤버 9종만
  정의, 프로덕션 심볼 0)이고, Vulkan 선언·오케스트레이터(`VulkanSelfTest.{h,cpp}`)
  도 Tests/ 밖에 있었다. DX12는 "선언은 facade 헤더, 구현은 흩어진 파일" —
  Vulkan은 자유 함수 헤더로, 두 백엔드의 소유 구조가 비대칭이다.
- ✅ 새 정적 라이브러리 `RenderTests.vcxproj`(층 5, HostImGuiPresentation 본보기)
  로 39파일을 옮겼다: Tests/ 트리 36 + VulkanSelfTest.{h,cpp} +
  `EnhancedSceneRenderer.cpp`(→ `Tests/EnhancedSceneRendererSelfTest.cpp`로 개명
  이동). include 339건을 스크립트로 재작성(이동 세트 내부는 상대 유지, RenderEngine
  잔류분은 프로젝트 경로로)하고 재작성 후 전 include 해석을 검산(미해결 0),
  죽은 DYNAMICCPP 가드 39개를 걷었다. CCS의
  `#include "RHI/Vulkan/VulkanSelfTest.h"`는 include 경로 추가만으로 그대로 산다.
- ⚠ **래칫이 정찰이 놓친 역결합을 잡았다.** 프로덕션 패스 파일 둘의 말미에
  테스트가 내장돼 있었다 — `EnhancedForwardPass.cpp`의 `RunForwardPlusTest`
  (297줄), `EnhancedSSGIPass.cpp`의 `RunSSGITest`(724줄). 둘 다 테스트 헤더
  (`DX12TestTextureRegistration.h`)를 물고 있어 이동 직후 상향 간선 2건으로
  붉어졌다. 두 구획을 잘라 `EnhancedForwardPlusTest.cpp`/`EnhancedSSGITest.cpp`로
  분리했다(파일 지역 헬퍼 `CompileSsgiShader`는 테스트 쪽 사본으로 이름을
  고유화 — 유니티 병합 대비).
- ✅ **Player 링크 안전을 사전 검증했다.** 정적 링크는 obj 단위로 견인하므로
  Core 잔류 파일이 이동한 Run* 심볼을 참조하면 Player가 깨진다 — 전수 확인
  결과 `EnhancedSceneRendererLive.cpp`의 매치 2건은 주석뿐이었다.
- ✅ 경계 부채 37 → **20**(Tests 관련 14줄/17count 소멸). Release 비유니티
  `Academy_4Q`·`Player`와 Debug 유니티 빌드 오류 0, 래칫 20/20(상향 0),
  **dx12 스위트 28·4·2·1 기준선 정확 일치**(이동 후에도 전 검사 동일),
  `vk.grid 통과`·`dx12.selftest 통과`(Vulkan 경로와 DX12 종단 오케스트레이터의
  새 집 실행 실증), 구성 게이트 PASS, 첫 프레임 전 종료 6/6.

두 번째 슬라이스 (E5-2 = E4-3 완결, 2026-08-23):

- ✅ `EditorRender.vcxproj` 신설(층 5) — 에디터 씬 오버레이 패스 8파일이
  `EditorRender/Render/Passes/Editor/`로 이동했다. 경로 보존이라 소비자
  (EngineEntry 기여자·RenderTests 9파일)는 include 경로 추가만으로 소스
  무변경이다. RenderTests가 EditorRender를 참조한다(5→5, 합법).
- ✅ 경계 부채 20 → **12건(항목 8개)** — E0 기준선 98에서 누적 86건 감소.
  RenderEngine의 Editor 소스 편입은 이제 `ICustomEditor.h` 1건뿐이다(구현체 0의
  dynamic_cast 훅 — EditorUI 층 재료라 E6 소관).
- ✅ Release 비유니티 `Academy_4Q`·`Player`, Debug 유니티 빌드 오류 0, 래칫
  12/12(상향 0), dx12 스위트 28·4·2·1 기준선 일치(기즈모 5종 —
  EditorRender.lib에서 링크된 패스로 픽셀 검사 통과), 구성 게이트 PASS,
  첫 프레임 전 종료 6/6, 에디터 창 캡처 기준과 동일(씬 뷰 그리드+아이콘,
  게임 뷰 깨끗).

판정:

- Core-only 빌드가 테스트/benchmark UI 없이 링크된다. ✅ (RenderEngine에서
  테스트 소스 39파일·41 편입 항목이 빠졌다)
- 기존 self-test와 benchmark는 DeveloperTools 경로에서 계속 실행 가능하다. ✅
  (dx12 스위트 기준선 일치 + vk/selftest 프로브)

### E6 — 프로젝트 물리 경계 확정 ✅ 완료 (2026-08-24, 판정 4/4 — 마지막은 P1a가 닫음)

실제 소유권 이동에 맞춰 다음 프로젝트를 만든다. 프로젝트 이름은 구현 중 조정할 수
있지만 책임을 다시 합치지는 않는다.

- `HostRuntime.vcxproj` — **만들지 않는다**(아래 E6-2 실측)
- `EditorRuntime.vcxproj` ✅ E6-2
- `EditorRender.vcxproj` ✅ E5-2에서 선행 신설
- `EditorUI.vcxproj` ✅ E6-1
- 필요 시 `DeveloperTools.vcxproj` 또는 `RenderTests.vcxproj` ✅ RenderTests는 E5-1

`Academy_4Q`는 entry/resource와 조립 코드만 가진 얇은 `CreatorEditor.exe`가 된다.
기존 runtime static library의 이름 변경은 필수가 아니다.

E6-1 — EditorUI.vcxproj 신설 (2026-08-23, c8d7cf17):

- ✅ EngineGUIWindow 소스 17·헤더 17을 Academy_4Q 직접 편입에서 신설 정적
  라이브러리(EngineGUIWindow/ 소재, 층 6)로 옮겼다. 파일 물리 이동 없음 —
  폴더 재배치는 E7 소관이고 E6의 본질은 컴파일 소유권과 링크 경계다.
- ⚠ 항목 메타데이터 승계 두 건: `EnhancedRenderDebugWindow.cpp`의 유니티
  제외(넣으면 청크 재편으로 Scene.h 전방선언이 깨진다 — 이유 주석째 승계),
  InspectorWindow의 개별 /bigobj는 프로젝트 전역 /bigobj로 흡수. 정찰 때
  자기닫힘 한 줄만 세는 스크립트가 이 멀티라인 블록 둘을 놓쳐 수 검증이
  잡았다.
- ⚠ `ReflectionTypedDraw.h`는 Academy_4Q에 미등록이던 실존 헤더 — 함께 등록.

E6-2 — EditorRuntime.vcxproj 신설·WinProcProxy 이동 (2026-08-23, 1b802a7f):

- ✅ EngineEntry의 에디터 런타임 소스 11종을 신설 정적 라이브러리
  (EngineEntry/ 소재, 층 6)로. E1이 미뤄 둔 `WinProcProxy.{cpp,h}`도
  Utility_Framework → EngineEntry로 물리 이동해 함께 편입(하향 의존만 있고
  소비자는 조립 코드 둘뿐이라 include 해석 무변경).
- ✅ **Academy_4Q가 얇아졌다**: 남은 것은 App(wWinMain)·EditorMain·
  ShutdownFinalMarker(init_seg 종료 마커 — 유니티 제외 메타 유지)와 조립·
  부트스트랩 헤더 9종·리소스뿐이다. exe 개명은 후속 스윕에서 완료했다
  (2026-08-24, 6d5bc284): TargetName=CreatorEditor로 산출물만 개명(프로젝트
  파일·sln 이름은 안정성 유지), 게이트·스크립트 41파일의 exe 참조 40곳과
  캡처 ProcessName 갱신, 옛 산출물 삭제(낡은 바이너리 함정 예방). 소스
  9파일의 언급은 전부 주석이고 C#(Native.cs)은 이름 무관 방식이라 코드
  영향 0 실측. 회귀 세트 전량과 dx12 스위트(28·4·2·1 기준선 일치)가 새
  exe로 통과.
- ⚠ **HostRuntime.vcxproj는 만들지 않는다.** 공통 Host 계약의 실체는
  `EngineBootstrap.h`(264줄)·`EngineLaunchConfig.h` 헤더 온리이고 .cpp가
  없다 — Editor·Player가 EngineEntry include 경로로 공유하는 현 구조가
  이미 그 역할이다. 빈 프로젝트를 만드는 것은 speculative generality.
- ⚠ `verify-prefab-editor-ownership` 게이트가 붉어졌다 — 착지로 전제가
  뒤집힌 것(소유: Academy_4Q 편입 → EditorRuntime). 게이트를 "에디터 층이
  컴파일하고 Player 링크 사슬에 없다"로 갱신하고 Player→EditorRuntime
  부재 단정을 추가했다. 음성 테스트(항목 제거 주입→붉음→복원→통과) 확인.

E6-3 — ICustomEditor.h 이동 (2026-08-23, 85d53583):

- ✅ Core에 마지막으로 편입돼 있던 에디터 소스(커스텀 인스펙터 인터페이스 —
  상속자 0, 소비자는 InspectorWindow 하나)를 EngineGUIWindow로 이동,
  EditorUI에 등록. 경계 부채 2 → 1.

판정:

- Core 프로젝트의 Editor 소스 편입 0. ✅ (E6-3)
- Core→Editor/EngineEntry/EngineGUIWindow/ImGuiHelper 프로젝트 참조 0.
  ✅ (실측: Core 4프로젝트의 참조는 Physics→Utility,
  RenderEngine→ScriptBinder·Utility뿐 — 에디터 계열 0.
  RenderEngine→ScriptBinder는 층 역방향 부채로 E4-7 소관.)
- Player→Editor 프로젝트 참조 0. ✅ (2026-08-24, P1a로 완결 —
  ProfilingCapturePlan P1a 참고. Profiler 수집 코어가 신설
  `EngineDiagnostics`(층 1)로 물리 이관되며 ScriptBinder의 링크 의존이
  하위 층으로 내려갔고, Player는 ImGuiHelper 참조를 EngineDiagnostics로
  교체했다. include 경로의 ImGuiHelper는 의도적 유지 — PlayerMain의
  `"imgui.h"`가 ImGui.h 래퍼로 해석되는 컴파일 결과 보존, 헤더 온리라
  링크 무관.)
- Core의 `BUILD_FLAG`, `EngineMode::IsEditor/IsPlayer` 0. ✅ (소부채 정리
  슬라이스)

### E7 — 선택 후속 작업 ◐ 재배치·개명 1단 완료 (2026-08-24)

모든 경계와 런타임 검증이 닫힌 뒤에만 수행한다.

- `Engine/`, `Editor/`, `Projects/` 폴더 재배치. ✅ (E7-b)
- `ScriptBinder`를 `SceneRuntime/ScriptRuntime`으로 재명명 또는 분할. — 판단
  자료 완성(2026-08-24, docs/analysis/ScriptBinderSplitAnalysis.md): 도메인
  분할은 SceneCore 양방향 강결합(49+42 등)으로 기각, ClrScript 분할은 씬
  수명 훅 7지점 역전이 필요해 **BT 관리 이관(9-8)·SceneGraph E1 착지 후
  재평가**. 단순 개명(→SceneRuntime)은 분할 결정과 독립이라 즉시 가능 —
  안 2로 가더라도 그 이름은 살아남는다.
- `RenderEngine`을 `RenderCore`로 재명명. — 보류(동일 사유는 아니나 함께 결정)
- Player thin exe + game module DLL 구조 또는 Player DLL export 구조 검토. — 미착수

이 단계는 E0~E6의 가치를 만들기 위한 선행 조건이 아니다.

E7-a — 산출물 디렉터리 규약화 + filters 정리 (2026-08-24, bc44845a):

- ✅ 중간물 `Build\Obj\<프로젝트>\<플랫폼>-<구성>\`, 정적 lib
  `Build\Lib\<플랫폼>-<구성>\` — Directory.Build.props 한 곳에서(.vcxproj
  한정 조건 — C# 3프로젝트의 OutDir 오염 차단). 전에는 IntDir이 전 프로젝트
  미지정으로 소스 트리 18곳에 x64\가 흩어졌고(이중 중첩 5곳 포함), Release
  lib 12개가 exe와 함께 Bin\Editor\에 뒤섞였다.
- ✅ exe는 위치 불변 — `EngineOutput.props`의 `EngineOutDir`이 이미 단일
  진실(C# Managed·nethost 배치가 참조)이라 exe 프로젝트의 OutDir 명시를
  `$(EngineOutDir)` 참조로 통일했다. 게이트 32파일(x64\Debug 참조)이
  무변경으로 산다.
- ⚠ **$(ProjectName)은 Directory.Build.props 평가 시점에 미정의다**
  (Cpp.Default.props가 나중에 채운다). 첫 시도에서 전 lib의 obj가
  `Build\Obj\x64-Debug\` 한 솥에 섞였다 — 동명 cpp가 서로 덮어쓰는 잠복
  결함. 예약 속성 `$(MSBuildProjectName)`으로 정정, 리빌드로 13개 분리 확인.
- ✅ filters 13파일: 빈 필터 24개 제거(대부분 E6 이동 잔재), 항목·필터
  알파벳 정렬, BOM·CRLF·2-space 표준 포맷 통일. diff 대조로 항목 소실 0 검증.

E7-b — Engine/Editor 물리 재배치 (2026-08-24, 1d73e2c3, 738파일):

- ✅ Engine\ 5(Utility_Framework·EngineDiagnostics·Physics·RenderEngine·
  ScriptBinder) / Editor\ 6(ImGuiHelper·HostImGuiPresentation·RenderTests·
  EditorRender·EngineEntry·EngineGUIWindow) + CreatorEditor.vcxproj(Editor
  직하 — 항목 경로 `EngineEntry\...`가 상대 해석으로 그대로 산다).
  Player·Tools·ThirdParty·Dynamic_CPP는 루트 유지(게이트 참조 보존).
  **폴더 이름은 유지, 위치만** — 이동과 개명을 한 커밋에 섞지 않는다.
- ⚠ L5'가 경고한 "경로 없는 include 약 1,000곳"의 실체: bare include는
  vcxproj /I 갱신이 전부 흡수하고, 프로젝트 간 상대 include 55건도 같은
  그룹 이웃 관계가 유지되면 그대로 산다. **실제 소스 재작성은 그룹 경계를
  넘는 5건뿐이었다**(ScriptBinder→ThirdParty 2·EngineEntry→Engine 2·
  HostImGui→Utility 1).
- ✅ sln 가상 폴더(Engine/Editor/Tools) + 13 프로젝트 중첩.
  ⚠ **중첩된 프로젝트는 MSBuild sln 타깃 이름이 `폴더\이름`이 된다** —
  /t:Academy_4Q가 MSB4057로 죽었다. 명시 타깃 호출 전수(3곳: build.ps1의
  AssetPacker, CI $targets의 lib 4개)를 갱신하고 CI 타깃 형식
  (`Engine\Utility_Framework`)을 로컬로 실검증했다. 이후 수동 빌드는
  `/t:Editor\CreatorEditor` 형식이다. Player는 진입점 안정성을 위해
  중첩하지 않았다.
- ⚠ 재배치가 잠자던 검사를 깨웠다 — 검사기 내부의 예외 경로 리터럴
  (`Utility_Framework/FileDialog.h`·DataSystem 2건)이 표 갱신에서 빠져
  editor-platform-api 오탐. 경로 리터럴은 표 밖에도 있다.
- ⚠ EngineVersion.h는 gitignore된 생성 파일이라 git mv에 안 딸려간다 —
  옛 위치 잔재를 확인 후 삭제(새 위치 실존·생성 로직이 재생성).
- ✅ 게이트·CI 16파일의 소스 경로 참조를 기계 치환(오탐 0 눈검). 빌드
  5레그(CI 형식 포함) 오류 0, 래칫 0/0, 회귀 세트 전체, dx12 스위트
  28·4·2·1 기준선 일치.

E7-c — CreatorEditor 개명 (2026-08-24, 5f99f365):

- ✅ 이름 3중 분열(파일 Academy_4Q·RootNS Academy4Q·산출물 CreatorEditor)
  해소 — 파일명·RootNamespace·sln 표시명 통일, ProjectName·TargetName
  명시 제거(파일명=산출물명이라 기본값과 일치). GUID 불변, 외부 참조는
  검사기 1곳뿐(실측). rc·ico 등 자산 파일명은 유지(항목 경로 불변).

E7-d — 에디터 vcxproj 통폐합 (2026-08-24, eddbea16):

- ✅ 에디터 전용 4개(ImGuiHelper·EditorRender·EditorRuntime·EditorUI)의
  컴파일 소유권을 `Editor\Editor.vcxproj` 하나로(cpp 38·h 57). 프로젝트
  14 → **11**. 소스 폴더는 유지 — include 층 검사(PROJECTS)가 폴더
  기준이라 ImGuiHelper(2)·EditorRender(5)·EngineEntry(6)·EngineGUIWindow(6)
  층 구분 래칫이 통합 후에도 그대로 산다(E6 기법의 역방향).
- ⚠ **통합 가능 범위는 링크 경계가 가른다.** Player가 링크하는 6종
  (Engine 5 + HostImGuiPresentation)은 에디터 계열과 합치면 "Player→에디터
  0" 판정이 무너지므로 제외. RenderTests는 개발자 도구 격리(E5 정신)로
  유지하되 통합 Editor(6층) 참조를 위해 층 5→6 조정(소비자가 CreatorEditor
  뿐인 도구라는 근거).
- ✅ 유니티 청크가 폴더 경계를 넘지 않게 했다
  (CombineFilesOnlyFromTheSameFolder) — 통합으로 다른 폴더의 익명
  네임스페이스가 병합되는 것(E4-6b에서 실제로 터진 종류)을 구조로 막는다.
  EnhancedRenderDebugWindow의 유니티 제외 메타는 이유 주석째 승계.
- ✅ verify-prefab-editor-ownership을 새 구조로 갱신(+실코드 주입 음성
  테스트). CreatorEditor 참조 11→8. 빌드 4레그 오류 0, 래칫 0/0, 회귀
  세트 전체 통과.

명명 확인(2026-08-24 실측): 나머지 프로젝트는 파일·폴더·RootNS 정합.
사소 불일치 1(Utility_Framework의 RootNS가 UtilityFramework). 재명명 후보
둘(ScriptBinder·RenderEngine)은 위 보류 사유 — 검사기 resolve_owner의
폴더 이름 힌트, 문서·메모리 전반의 이름 정합까지 걸리므로 분할 결정과
함께 별도 슬라이스로.

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
