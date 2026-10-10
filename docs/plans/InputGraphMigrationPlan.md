# InputGraph 입력 시스템 전환 작업 기록

## 범위와 기준

사용자 승인된 [HTML 구현 계획](InputGraphImplementationPlan.html)의 공개 계약과 LX 저작·C++/C# 소비 경로를 구현한다.

- 구현 기준 master: `d3c85faf182e93842217dd46eee3d3aabf4afc13`
- 설계 조사 기준: `b2f918c196d02c648eef5ef064e11ae4f8d7846b` — HTML의 소스 사실은 이 기준이다.
- 공개 명칭: `InputGraph`, `InputSignal<T>`, `InputBinding`, `InputLayer`, `InputSession`, `InputFrame`, `SignalEvent<T>`
- 작은 커밋으로 개발하지만 제품 전환은 자산·cook·런타임·에디터·스크립트 계약을 함께 바꾸는 단일 전환이다.
- 빌드·컴파일·런타임·회귀 테스트 실행은 이번 작업에서 수행하지 않는다. 테스트 소스 작성과 정적 검토를 실행 검증으로 표시하지 않는다.

## 구현 계약

1. LX는 공통 저작·편집 기반이며 입력은 별도 CPU 실행 정의로 준비한다. Material Slang 실행이나 범용 VM을 입력 처리에 재사용하지 않는다.
2. 불변 정의만 공유하고 사용자·평가 도메인별 상태와 장치 연결 세대는 분리한다.
3. 입력 기록과 정책 변경 기록은 공통 시간선에서 순서대로 해석한다. 이미 확정한 결과는 UI 정책이 늦게 도착해도 소급 변경하지 않는다.
4. 고정 틱마다 고유한 입력 결과를 물리 이전에 소비한다. UI 처리는 고정 틱 루프 밖에서 동작하지만 GT 자체가 막히면 함께 지연될 수 있다.
5. 취소는 정상 해제와 다르다. 포커스·계층·장치·정의 변경은 영향 신호를 취소하고 중립 복귀 전 재작동을 제한한다.
6. LX의 숫자 enum·variant 직렬화 ID를 보존하며 Vector2와 저장 형식을 확장한다. 화면 배치 변경은 의미 변경이나 판정 초기화로 취급하지 않는다.
7. C#은 생성된 타입 접근자와 버전이 명시된 POD 경계로 같은 입력 결과를 읽는다. C# 그래프 저작 DSL은 범위에 포함하지 않는다.
8. 애매한 구 자산 타입·문자열 콜백은 오프라인 변환에서 진단한다. 제품 런타임에 구 입력 경로 fallback을 두지 않는다.

## 작업 상태

2026-10-10 소스 제출 기준이다. 아래의 **소스 구현**은 작성된 경로를 뜻하며, 빌드 성공이나 제품 수용 완료를 뜻하지 않는다.

| 작업 | 작성·전환된 주요 소스 경로와 범위 | 소스 상태 | 실행 검증 |
| --- | --- | --- | --- |
| INP0 | 이 기록, `ScriptCore/InputGraph.md`, `Tools/input-migration/README.md`: 공개 계약·소유권·역사 자산의 불확정 항목과 이전 경계 | 기록 작성 | NOT RUN |
| INP1 | `Engine/SceneRuntime/InputManager.{h,cpp}`, `Engine/Utility_Framework/CoreWindow.h`: GameInput 콜백, 물리 scan code, 단조 시간·sequence, 유한 기록 보존, 장치/할당 세대, 정책 기록과 누락 복구 | 소스 구현 | NOT RUN |
| INP2 | `Engine/Utility_Framework/InputGraphTypes.h`, `InputGraph.{h,cpp}`; `Lattice/Core/LXGraph.{h,cpp}`, `Lattice/Input/LXInputGraph.{h,cpp}`, `LXInputCompiler.{h,cpp}`: 강한 ID, Vector2 저장 형식, 입력 도메인·타입 검증, 제한된 LX 낮춤, 불변 CPU 준비 | 소스 구현 | NOT RUN |
| INP3 | `Engine/SceneRuntime/InputSession.{h,cpp}`, `InputSessionComponent.{h,cpp}`, `InputSubscriptions.{h,cpp}`: 사용자/도메인별 상태, 순서 있는 프레임·이벤트, Hold/Tap/Chord/repeat, 취소·중립 재작동, 타입 구독 수명 | 소스 구현 | NOT RUN |
| INP4 | `Engine/Utility_Framework/InputAccessorGenerator.{h,cpp}`; `Engine/SceneRuntime/ScriptInputABI.h`, `ClrHost.{h,cpp}`; `ScriptCore/Input.cs`, `Native.cs`, `ScriptRegistry.cs`: 타입 접근자 생성, POD ABI·API table v41, 소유한 managed 프레임 복사, simulation-scope 구독 | 소스 구현 | NOT RUN |
| INP5 | `Engine/SceneRuntime/InputSessionSystem.{h,cpp}`, `RuntimeFrame.{h,cpp}`, `ScenePhysicsSimulation.{h,cpp}`, `UIManager.{h,cpp}`, `SceneManager.{h,cpp}`; `Editor/EngineEntry/App.{h,cpp}`, `EditorMain.cpp`, `EditorPlayModeController.cpp`; `Player/PlayerApp.{h,cpp}`: UI/game 별도 경계, 실제 고정 물리 step 이전 소비, PT 정책 게시와 Play/Stop 정리 | 소스 구현 | NOT RUN |
| INP6 | `Engine/RenderEngine/AssetDepot/InputGraphAssetRuntime.{h,cpp}`, `AssetDepot.cpp`, `AssetLink.h`, `DataSystem.{h,cpp}`, `Experiment/Cooked/CookedInputGraph.{h,cpp}`, `CookedAssetManifest.{h,cpp}`; `Tools/AssetCooker/AssetSetBuild.cpp`; `Editor/EngineEntry/EditorAssetDatabase.{h,cpp}`, `Editor/EngineGUIWindow/InputGraphWindow.{h,cpp}`, `ImGuiDrawHelperInputSession.cpp`; `Engine/Utility_Framework/InputBindingOverrideArchive.{h,cpp}`: 정식 GUID/meta, CEIG/CEIO, 비동기 lease, cook, 마지막 정상 정의 보존, 재바인딩과 사용자 profile | 소스 구현 | NOT RUN |
| INP7 | `GameScripts/InputProbe.cs`, `GameplayInputConsumer.cs`, `Generated/GameplayInputs.cs`; `Dynamic_CPP/Assets/InputGraph/Gameplay.inputgraph`, `.meta`, `Generated/GameplayInputs.h`; `Tools/input-migration/`; `BuildTool/PackageInputs.cs`, `RuntimeBootstrap.cs`, `Tools/AssetCooker/AssetCooker.cpp`: 타입 소비 예제·오프라인 변환·패키징 전환. 구 `ActionMap`, `InputAction`, `InputActionManager`, `PlayerInput`, `PlayerInputSystem` 실행 파일과 연결 제거 | 소스 전환, 역사 콜백 이전 제외 | NOT RUN |
| INP8 | `Tools/regression/InputGraphContractTests.cpp`, `InputGraphAuthoringTests.cpp`, `InputGraphScriptProbe/`, `verify-inputgraph-corpus.ps1`, `verify-asset-authoring-ownership.ps1`, `verify-editor-command-surface.ps1`; `Editor/RenderTests/InputGraph/InputGraphAssetCodecSelfTest.{h,cpp}`와 CLI 등록: 검증 소스·후속 실행 절차 | 작성 및 정적 검토, 수용 대기 | NOT RUN |

프로젝트 편입 경로는 `Engine/Utility_Framework/Utility_Framework.vcxproj`, `Engine/RenderEngine/RenderEngine.vcxproj`, `Engine/SceneRuntime/SceneRuntime.vcxproj`, `Editor/Editor.vcxproj`와 해당 filters다. RenderEngine은 SceneRuntime을 참조하지 않고 Utility_Framework의 불변 정의 계약을 사용한다.

## 실제 LX 지원 문법과 실패 경계

정본 저작 형식은 `LXINPUT 1 "<UUID>"` 헤더와 LXG10 문서이며, 기존 LXG9 숫자 enum/variant 의미를 유지하면서 Vector2를 추가한다. 현재 낮춤은 범용 그래프 VM이 아니라 다음 명시적 입력 문법이다.

- 물리 Key/MouseButton, MouseDelta/PointerPosition/Wheel, GamepadButton, Vector2 stick 및 Float trigger 소스; Axis2D에는 직접 연결된 물리 Button 네 개가 필요하다.
- Button/Float/Vector2 signal 출력, layer, 순서 있는 Deadzone/Normalize/Scale/Invert/Clamp processor, Sum/MaximumMagnitude/MostRecent/Priority 결합과 우선순위.
- Press/Hold/Tap/Chord 및 analog threshold, signal 참조를 쓰는 gate/chord. 한 출력의 모든 대안은 같은 interaction과 gate를 공유해야 한다.
- 결합 전 binding processor와 결합 후 전체 signal processor를 구분한다. 전체 결합 뒤의 Normalize는 지원하지만, 처리된 일부 결합을 다시 다른 결합 안에 넣는 형태는 순서를 바꿔 평탄화하지 않고 거절한다. 중첩 결합 정책도 일치해야 한다.
- 상태를 갖는 group 확장, 임의 signal 값을 일반 값 노드처럼 재사용하는 연결, 알 수 없는 정의·속성 형식·지원 범위 밖 물리 코드, cycle 또는 표현할 수 없는 처리 순서는 진단하고 게시를 거절한다.

`InputGraphWindow`는 저작·진단·저장·접근자 내보내기·재바인딩 경로를 제공한다. 이 소스 범위를 계획의 모든 임의 DAG 표현이나 실제 UI 동작 검증 완료로 해석하지 않는다.

## 자산·해시·재게시 계약

- AssetDepot은 불변 `InputGraphProgram` lease를 제공한다. BuildTool과 native AssetPacker는 `.inputgraph` 및 구 `.inputmap`을 패키징에서 제외하고 `.ceig`를 유지한다. Player는 CEMF v3의 InputGraph kind와 CEIG만 읽고 LX/YAML/문자열 콜백을 런타임에서 해석하지 않는다. CEIG 읽기는 명시적 endian, UUID, schema/compiler/API 버전, capability, collection/byte 경계와 semantic hash를 검사하고 파생 CPU 표를 한 번 준비한다.
- Interface hash는 graph/layer/signal 안정 ID, signal 타입, schema/ABI 계약을 식별한다. binding·processor 재설정은 기존 타입 접근자를 무효화하지 않는다. Semantic hash는 실제 실행 설정과 순서 있는 processor를 식별한다. 두 해시는 역할이 다르다.
- LX 배치·뷰·표시 제목 변경은 semantic identity에서 제외한다. 정상 저작 세대와 의미가 같은 배치 저장은 재컴파일·런타임 재게시를 생략하고 cook receipt도 정규화된 의미를 사용한다. 안정 signal ID의 타입 변경은 거절한다.
- Save와 watcher reload는 입력 전용 mutex로 직렬화하고 최신 게시 revision을 확인한다. 잘못된 새 정의는 기존 session lease를 교체하지 않는다. 첫 source 저장 뒤 meta 실패는 기존/후보의 동일 UUID를 검증한 재시도로 복구하며, InputGraph sidecar는 staging 후 교체한다.
- CEIO profile은 사용자·graph·안정 binding ID와 버전으로 검사한다. 잘못된 사용자/graph, orphan/중복/호환되지 않는 source는 거절한다. 컴포넌트는 실제 적용된 세대가 확인된 override만 사용자 profile에 저장하는 경로를 갖는다. 실제 디스크 실패·재시작 동작은 아직 실행하지 않았다.

## 구독 소멸 경계

Native subscriber는 `InputSubscriptionScope`를 소유하고 disable/world removal 시 무효화한다. 콜백 도중 자기 자신이나 입력 컴포넌트를 제거하는 경우 즉시 구독 해제로 처리하여 이후 콜백과 재진입 terminal callback을 호출하지 않는다. 일반적인 외부 제거·graph/user 교체는 보유한 불변 final frame으로 취소를 전달한 뒤 세대를 무효화한다. 파괴된 수신자로의 취소 전달을 보장하지 않는다.

## 역사 자산과 새 타입 예제의 구분

기존 `ControllCamera`, `Player`, `PlayerKeyBoard`, `PlayerMenuKey`, `PlayerPerchase`, `PlayerSelect` 여섯 `.inputmap`은 원본 바이트를 `Tools/input-migration/fixtures/legacy/`에 보존했다. Assets 밖의 오프라인 입력이며 패키징 대상이 아니다.

정적 조사에서 26개 action 모두 문자열 callback 참조를 포함했다. 참조하는 `Player`, `CameraMove`, `MenuKeyObserver`, `ItemUIPopup`, `InputDeviceDetector`의 해당 구현은 추적된 script 소스에 없고, 추적된 sample scene/prefab에도 이 map 참조가 발견되지 않았다. 또한 `Player.Move`, `PlayerKeyBoard.Move`, `ControllCamera.CameraMove`, `PlayerSelect.PlayerSelectUIMove` 네 Value action은 저장된 타입이 없다. 누락된 gameplay 코드를 추측하거나 콜백을 버려 자동 이전했다고 주장하지 않는다.

`Tools/input-migration/migrate_inputmap.py`는 정확한 원본 SHA-256, 명시적 타입·도메인·claim·dispatch, 검토된 consumer 위치와 필요 시 반대 키 중립 정책을 요구한다. 불명확한 VK/OEM 키나 지원하지 않는 변환은 거절한다. 생성되는 consumer 계약 보고서는 실제 gameplay 코드 작성의 대체물이 아니다. 변환기도 이번 작업에서 실행하지 않았다.

새 `Gameplay.inputgraph`는 독립된 타입 예제다. GUID `9ad58e30-9ff7-4f2a-a8c2-435a3c126801`의 Gameplay layer에 Move(Vector2/WASD), Jump(Button/Space), Look(Vector2/상대 mouse delta)를 선언한다. C++/C# 접근자와 `GameplayInputConsumer.cs`가 이 동일한 ID 계약을 사용한다. `Tools/input-migration/Gameplay.assetset`은 source-free cook을 위한 명시적 예제 정의이며, cook·패키징·Player 실행을 완료했다는 뜻이 아니다. 체크인 접근자의 fingerprint는 정적 계약에서 산출했으며 실제 LX export 실행도 미실행이다.

## 작성된 검증과 남은 실행 게이트

**다음은 모두 AUTHORED, NOT RUN이다. 통과 결과가 아니다.**

- Native 계약 probe: `InputGraphContractTests.cpp`의 22개 scenario group. schema/ID/해시, 같은 tick의 다중 전이, delta·processor 순서, 시간 경계·지연 기록, Hold/Tap/Chord, layer claim, 사용자/도메인 분리, 취소·재동기화·overflow, reload·보유 frame, native 구독 재진입·변경·수명을 다룬다.
- LX probe: `InputGraphAuthoringTests.cpp`. LXG9 의미 보존, Vector2·Undo/Redo, archive round-trip, 배치/의미 identity, 지원하지 않는 문법과 결합 후 processor 순서를 다룬다.
- Managed probe: `InputGraphScriptProbe/`. POD layout, 소유한 snapshot, 타입 lookup, 이벤트 다중성, stale accessor, 구독·session/reload 수명을 다룬다. deterministic native test double을 사용하며 실제 ClrHost 검증이 아니다. trimmed NativeAOT 설정도 실행 결과가 아니다.
- Codec probe: `InputGraphAssetCodecSelfTest.cpp`. CEIG/CEIO round-trip, 잘린/지원하지 않는 payload, 마지막 정상 결과 보존과 override 거절을 다룬다. 오프라인 변환 계약 소스는 `Tools/input-migration/test_migrate_inputmap.py`다. `BuildTool/Tests/Program.cs` 및 `verify-pak-source-exclusion.ps1`에는 입력 원본 제외·CEIG 보존의 대소문자 fixture를 추가했으며 이 역시 미실행이다.
- CLI: `input.graph.inspect <path.inputgraph>`, `input.graph.roundtrip <path.inputgraph>`, `input.graph.selftest`, `input.graph.authoring.probe <save|verify> <CE_InputProbe_name>`. 마지막 명령과 회귀 harness는 실제 EditorAssetDatabase 저장·재시작 후 source/meta UUID 확인 경로를 작성한 것이며 실행하지 않았다.

모든 Debug/Release/Shipping·non-unity 빌드, native/managed 테스트, 회귀 script, converter, NativeAOT publish/run, 실제 Editor UI·물리 장치·고정 step·UI/game 라우팅·hot reload·사용자 profile·packaged Player 실행, sanitizer와 benchmark는 **NOT RUN**이다. 메모리 할당량, latency, 처리량 또는 프레임 예산 충족에 대한 성능 주장은 없다. managed 안전 복사 경로는 publication마다 할당하며 allocation-free로 표시하지 않는다.

정적 소스 검토와 공백 검사는 실행 검증과 별개다. 실제 도구·환경·revision·결과를 기록할 때까지 HTML 계획의 **T01–T28은 전부 수용 대기**다. PR 게시, 프로젝트 편입 또는 검증 소스 작성만으로 어느 항목도 passed로 올리지 않는다.
