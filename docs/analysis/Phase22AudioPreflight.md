# PHASE 22 오디오 백엔드 사전 정찰 (2026-09-16)

- 기준 트리: `81b90288` + 미커밋 변경(오디오 계층에는 변경 없음)
- 대조 정본: [AudioBackendModernizationPlan.md](../plans/AudioBackendModernizationPlan.md)(기준선 2026-08-27),
  [RefactoringPlanDashboard.html](../RefactoringPlanDashboard.html) PHASE 22 AU0~AU9,
  [BuildPipelinePlan.md](../plans/BuildPipelinePlan.md) B5,
  [EngineDistributionAndLauncherPlan.md](../plans/EngineDistributionAndLauncherPlan.md) DL5
- 묻는 것 셋. ① 계획서 §1 기준선(3주 전)이 지금도 서 있는가. ② AU0~AU9 가 지목한 대상이 실제로 그 자리에
  있는가. ③ 지금 착수할 수 있는가, 착수 전에 정해야 할 것은 무엇인가.
- 방법: 소스·프로젝트·스크립트·산출물 JSON·PE import 를 직접 읽었다. 문서 간 대조는 근거로 세지 않았다.
  실행 검증(재생·장치·성능)은 하지 않았다 — §9 에 재지 않은 것을 적었다.

---

## 0. 결론

1. **현재 FMOD 경로는 소리를 한 번도 적재하지 못한다.** 로더가 스캔하는 `Sounds\` 디렉터리가 저장소에
   존재하지 않아 `LoadSounds()` 가 영원히 실행되지 않고(§2.1), `sounds` 맵은 영구 공집합이다. 저작
   데이터의 오디오 참조도 0건, 저장소의 오디오 파일도 0개다. 계획서가 AU0/AU4 에 둔 **"유효 FMOD 항목
   A/B" 의 유효 집합은 사실상 공집합**이다 — AU0 은 "기존 동작 분류" 가 아니라 **합성 fixture 로 새
   골든을 세우는 일**로 다시 써야 한다(§2.5).
2. **FMOD 표면은 계획서 목록보다 넓고, 동시에 계획서가 세지 않은 오탐 둘을 포함한다.** 살아 있는 참조
   파일은 15개(계획서 7개), 그중 `AnimationJob.cpp`·`ImGuiDrawHelperTerrainComponent.cpp` 의 매치는
   C 표준 `fmod()` 와 지역변수 `fModes[]` 로 **오디오와 무관**하다(§1.2). 진짜 vendor 진입점은
   `SoundDefinition.h:5-6` **한 곳**이고 ABI 누출은 `ChannelPair` 와 `Get2D/3DChannel` **둘**뿐이다(§1.3).
3. **AU2 는 "PHASE 17 정본을 소비" 가 아니라 파이프라인 신설이다.** 128-bit identity 전제는 참이지만
   (`Uuid16`, `static_assert(sizeof==16)`), 오디오에는 importer·`RuntimeAssetType`·`CookedAssetKind`·
   `.meta` 오디오 필드·pak 범위 read·C# identity 타입이 **전부 없다**(§3.2). 확장자 allowlist 는 세 곳에
   중복 하드코딩돼 있어 한 곳만 고치면 조용히 어긋난다(§3.3).
4. **AU8 이 지목한 대상이 이사했다.** `Tools/build.ps1` 에 FMOD 는 0건이며(22줄짜리 호환 진입점으로
   축소), 실제 배치는 `Tools/runtime/deploy-runtime.ps1` 의 **PE import 폐포**가 한다(§4.1~4.2).
   FMOD 는 어느 목록에도 이름으로 등록돼 있지 않다 — 소스에서 심볼이 사라지면 폐포에서 자동으로
   떨어진다. 반대로 **지금 그것을 재는 게이트가 0개**다(§4.4).
5. **신규 발견 — 라이선스 공백.** 배포본에 `fmodL.dll`(FMOD 로깅 빌드)이 실리는데
   `Licenses/ThirdParty/` 에 Fmod 항목이 없다. `x64-Debug-Shipping` 구성의 Player 매니페스트도
   `fmodL.dll` 을 싣는다(§4.3). 독점 SDK라 PHASE 23 DL5/DL9 감사 전에 이미 처분이 필요하다.
6. **의존 페이즈 전제 셋 중 둘이 어긋난다.** `PHASE 17 D2 → AU2` 의 D2 는 `stopped`(PHASE 3.75 이관)
   이지만 identity 실물이 있으므로 실질 무해하고, `PHASE 14 provider → AU5/AU9` 의 P5 는 `todo` 라
   **발행할 provider 가 없다**. `PHASE 12.5 B2/B3 → AU0` 의 B3 는 `todo` 이나 AU0 은 실측상 B2/B3 없이
   진행 가능하다(§5).
7. **착수 가능하다.** AU0(재정의)·AU1·AU3 은 지금 시작할 수 있다. 착수 전 결정 2건과 재추정은 §8.

---

## 1. FMOD 표면 재측정

### 1.1 살아 있는 참조 15개 (계획서 §1.1 은 7개)

| 파일 | 건수 | 성격 | 계획서 §1.1 |
|---|---|---|---|
| `Engine/SceneRuntime/SoundManager.cpp` | 67 | 구현 본체 | 있음 |
| `Engine/SceneRuntime/SoundManager.h` | 18 | 공개 헤더 · ABI 누출 | 있음 |
| `Editor/EngineGUIWindow/InspectorWindow.cpp` | 19 | 채널 직접 조작 5블록 | 있음 |
| `Engine/SceneRuntime/SoundComponent.h` | 9 | 공개 헤더 · ABI 누출 | 있음 |
| `Engine/SceneRuntime/SoundComponent.cpp` | 6 | 구현 | 있음 |
| `Engine/SceneRuntime/SoundDefinition.h` | 2 | **vendor 헤더 유일 진입점** | **없음** |
| `Engine/SceneRuntime/SoundSystem.h` | 1 | 전방 선언 | **없음** |
| `Editor/CreatorEditor.vcxproj` | 10 | include·lib·link | 있음 |
| `Player/Player.vcxproj` | 6 | include·lib·link | 있음 |
| `Editor/Editor.vcxproj` | 2 | include only(솔루션 6번째 프로젝트, 오늘도 커밋됨) | **없음** |
| `Engine/SceneRuntime/SceneRuntime.vcxproj` | 2 | include only | **없음** |
| `ScriptCore/SoundComponent.cs` | 2 | 주석 | **없음** |
| `Tools/regression/verify-experiment-contract.ps1` | 8 | **링크 입력**(동작 단정 아님) | **없음** |
| `Tools/runtime/deploy-runtime.ps1` | 1 | DLL 소스 루트 | **없음**(build.ps1 로 적힘) |
| `.github/workflows/build.yml` | 2 | 제외 사유 주석 | 있음 |

`Artifacts/phase21-*/` 아래 4건은 UI 작업의 before 스냅샷이라 제외했다. `ThirdParty/Fmod` 자체와
`vcpkg_installed`(`fmt/chrono.h` 등의 `fmod` 수학 함수)도 제외했다.

**FMOD Studio 소비자는 0이다.** `FMOD::Studio`·`EventInstance`·`EventDescription`·`.bank` 0건이고,
`ThirdParty/Fmod/inc` 에 `fmod_studio.h` 가 **아예 없다**. 계획서의 "Core 재생 계층만" 판정은 유효하다.

### 1.2 오탐 둘 — 오디오 소비자가 아니다

- `Engine/SceneRuntime/AnimationJob.cpp:490,512,554,571,604,626` — C 표준 `fmod()`(애니메이션 시간
  랩어라운드). 같은 파일에 `sound`/`audio` 0건.
- `Editor/EngineGUIWindow/ImGuiDrawHelperTerrainComponent.cpp:299` — foliage 모드 콤보의 지역변수
  `fModes[]`. `FMOD`/`Sound`/`Audio` 0건.

대소문자 무시 grep 으로 FMOD 표면을 세면 이 둘이 항상 섞인다. AU8 의 "symbol 0" 판정은 `fmod(`
호출과 `fModes` 를 제외하는 규칙을 명시해야 한다.

### 1.3 vendor 격리 실태 — 진입점 1 · ABI 누출 2 · 전이 오염 6

- **유일한 vendor 헤더 진입점**: `Engine/SceneRuntime/SoundDefinition.h:5-6`
  (`#include "../../ThirdParty/Fmod/inc/fmod.hpp"`, `fmod_errors.h`). 상대경로 하드코딩.
- **진짜 ABI 누출 둘**
  - `SoundManager.h:8-12` `struct ChannelPair { FMOD::Channel* ch2D; FMOD::Channel* ch3D; }` —
    `playFromSourceBlended`(`:54`)·`playOneShotPooled`(`:59-64`) 반환 타입.
  - `SoundComponent.h:66-67` `Get2DChannel()`/`Get3DChannel()` — Editor 가 이걸로 채널을 직접 조작한다.
  - 부수: `setListenerAttributes(const FMOD_VECTOR&, ...)`(`SoundManager.h:31-34`),
    `getListenerPosition(FMOD_VECTOR&)`(`:71`), `SoundComponent.h:95-96` 의 `FMOD_VECTOR` 멤버.
- **전이 오염**: `SoundDefinition.h` 가 직렬화 enum `ChannelType`(`:8-16`)과 한 몸이라, `SoundComponent.h`
  를 include 하는 TU 가 전부 FMOD 헤더를 끌고 온다 — `RegisterReflectManual.h:34,146`,
  `SceneManager.cpp:23`, `InspectorWindow.cpp:37`, `InspectorIconList.h:14`, `ComponentFactory.cpp:24`,
  `LifecycleRegistry.cpp:30`, `ClrHost.cpp:11`.
- **managed ABI 는 깨끗하다**: `ClrHost.cpp:142-153` 의 오디오 함수 12개는 전부
  `int/float/char*/ScriptObjectHandle` 다. vendor 누출은 헤더 레벨에서 멈춘다. 계획서 §3.1 의
  "native pointer 를 managed 에 숫자로 노출하지 않는다" 는 **이미 지켜지고 있다**.

### 1.4 죽은 공개 표면

호출부 0인 public API: `setListenerAttributes`(`SoundManager.cpp:145`), 외부 `shutdown()`,
`unloadSound`(`:189`), `setGroupMaxVoices`/`setGroupStealPolicy`/`setGroupPreemptSameClip`(`:206-208`),
`configureVoicePool`/`clearVoicePool`(`:341,383`), `SoundComponent::EditorSet()`(`SoundComponent.cpp:161`),
`SoundDefinition.h:47` `ComputeEqualPower`(같은 수식이 `SoundComponent.cpp:83-84` 와
`SoundManager.cpp:211` 에 각각 재구현돼 있다).

AU1/AU5 가 "이관" 으로 계산하면 안 되는 부분이다. **정책 API 는 이름만 있고 소비자가 없다.**

---

## 2. 현재 오디오 경로는 소리를 내지 못한다

### 2.1 `Sounds\` 부재 → `LoadSounds()` 영구 미실행

`SoundManager::SoundLoaderThread()`(`SoundManager.cpp:99-121`)는 1초마다 `PathFinder::Relative("Sounds\\")`
를 재귀 순회해 `.mp3/.wav/.ogg` 개수를 세고, **개수가 바뀐 경우에만** `LoadSounds()` 를 부른다(`:117`).

- `PathFinder::Relative(x)` 의 기준은 `DataPath = paths.assetsRoot`(`PathFinder.h:93,191`), 즉
  `Dynamic_CPP/Assets`(`App.cpp:136`).
- `Dynamic_CPP/Assets` 아래에 `Sounds` 가 **없다**. 저장소 전 트리에 `Sounds` 디렉터리 0건이고,
  `PathFinder` 의 자동 생성 디렉터리 목록(`PathFinder.h:128~`)에도 없다.
- 따라서 `recursive_directory_iterator` 가 매 초 예외를 던지고 `catch (...) {}`(`SoundManager.cpp:113`)가
  삼킨다 → `cnt = 0` 이 초기값 `_currSoundCount = 0`(`SoundManager.h:104`)과 같아 `LoadSounds()` 는
  **한 번도 실행되지 않는다**.
- 그러므로 `sounds` 맵(`SoundManager.h:108`)은 영구 공집합이고, 모든 재생은
  `sounds.find(clipKey)`(`SoundManager.cpp:318`) 실패 → 에러 로그 후 return(`:319`) 이다.

### 2.2 종료는 영구 정지 조건을 품고 있다

`_isSoundLoaderThreadRunning` 은 초기값 **true**(`SoundManager.h:114`)이고, 값을 내리는 유일한 경로가
`LoadSounds()` 의 끝(`SoundManager.cpp:141`)이다. §2.1 로 그 경로가 닫혀 있으므로
`~SoundManager()`(`:10-13`)의 `while (_isSoundLoaderThreadRunning) sleep(10ms)` 는 **조건상 영원히 돈다**.
계획서 §1.3 의 "초기값 true 에서 내려가지 않는다" 는 맞지만, 재현 조건은 *폴더 부재* 라는 한 줄에
달려 있다 — AU0 실패 canary 는 이 조건을 명시해야 재현된다(폴더를 만들고 파일을 넣으면 canary 가
조용히 초록이 된다).

동시에 스레드는 `detach()`(`:21`) 된 `while (true)` 라 종료 수단이 없다. 소멸자가 통과하는 경우(로드가
한 번이라도 돌아 플래그가 내려간 경우)에는 `shutdown()` 이 `system->release()` 를 하고, 1초 뒤 깨어난
스캔 스레드가 해제된 `system` 을 만진다. **hang 과 use-after-free 가 상호배타적으로 둘 다 열려 있다.**

### 2.3 리스너 0 · Play 모드 update 0

- `setListenerAttributes` 는 리스너를 갱신하는 유일한 경로인데 호출부가 0이다(§1.4). 반면
  `getListenerPosition`(`SoundManager.cpp:419`)은 `SoundComponent.cpp:72` 가 소비해 `Rolloff::Custom`
  감쇠를 계산한다 — **항상 원점 리스너 기준으로 3D 감쇠를 계산한다.**
- Editor 의 `Sound->update()` 는 `InputEvent` 람다 안에 있고(`EditorMain.cpp:291`), 그 람다는
  `InputOwner::Game` 이면 맨 앞에서 `return` 한다(`EditorMain.cpp:278`). 즉 **Play 모드 동안
  `SoundManager::update()` 가 호출되지 않는다.** Player 의 같은 람다에는 그 가드가 없다
  (`PlayerMain.cpp:218-222`). `InputEvent` 자체는 프레임마다 broadcast 된다(`SceneManager.cpp:451`,
  `RuntimeFrame.cpp:70`).
- 컴포넌트 틱은 살아 있다: `Scene.cpp:2688,2747` → `SoundSystem`(`SoundSystem.cpp:30-68`).

### 2.4 저작 데이터의 오디오 참조 0건

`Dynamic_CPP` 의 `.creator` 14개·`.prefab` 9개에서 `clipKey`·`SoundComponent` 출현 **0건**. 저장소에서
`clipKey` 를 담은 파일 둘은 리플렉션 골든 스냅샷이지 저작 데이터가 아니다. 오디오 파일도 전 트리
**0개**(`.wav/.mp3/.ogg/.flac`).

### 2.5 그래서 AU0 의 A/B 축이 무너진다

계획서 AU0 은 기존 동작을 `유효 / 고장 / 미검증` 으로 나누고 **유효 항목에만** FMOD A/B 를 쓰라고
적었다. 실측 결과 분류는 이렇게 된다.

| 계획서 분류 | 항목 | 실측 |
|---|---|---|
| 유효(A/B 근거) | play/stop/loop/stream/bus | **관측 불가** — 클립이 적재되지 않아 실행 경로가 첫 줄에서 끊긴다 |
| 고장 | shutdown/lifetime | 확정(§2.2), + Play 모드 update 미호출(§2.3) |
| 미검증 | listener/spatial/reverb | 확정 — listener 호출부 0, reverb 는 Inspector 람다에만 있고 재생 시 재적용 경로 없음 |

**조치**: AU0 의 판정문에서 "유효 FMOD 항목 A/B" 를 빼고, ① 합성 fixture ② offline/golden 기대값
③ 실패 canary ④ 절대 성능 예산만 남긴다. FMOD 와의 비교는 *참고 수치* 로 강등하고, 비교를 하려면
먼저 `Sounds\` 를 만들고 fixture 를 넣어 **현재 코드가 처음으로 소리를 내게 하는 작업**이 선행돼야
한다는 것을 AU0 안에 명시한다.

---

## 3. 자산 파이프라인 전제 (AU2)

### 3.1 서 있는 전제 — canonical 128-bit identity

`Engine/Utility_Framework/Uuid.h:151-170` `Uuid::Uuid16 { std::array<uint8_t,16> }` +
`static_assert(sizeof(Uuid16) == 16)`. 저작/런타임 래퍼는 `TypeTrait.h:117` `FileGuid`, 쿠킹 경계는
`Experiment/ModelData.h:80-86` `AssetId`. v4 생성(`TypeTrait.h:130-158`)과 v5 유도(`Uuid.h:190-204`)가
모두 있다. 계획서 §3.1 의 "D2 가 확정한 중립 asset identity" 는 **실물로 존재한다**.

### 3.2 없는 것 — 여섯 축이 전부 신설이다

| 축 | 현재 | 근거 |
|---|---|---|
| 오디오 importer | 없음. `IAssetImporter` 구현은 glTF·FBX 둘뿐 | `Experiment/Import/ImportedScene.h:519-526`, `GltfImporter.h:23`, `FbxImporter.h:17` |
| 런타임 자산 타입 | `RuntimeAssetType` 에 오디오 없음 → `.wav` 는 `CatalogOnly` 로 떨어진다 | `DataSystem.h:33-43`, `DataSystem.cpp:118-145` |
| 쿠킹 종류 | `CookedAssetKind{Model,Material,Texture,ShaderMeta,Scene,Prefab}` — 오디오 없음 | `Cooked/CookedAssetManifest.h:31-39`, `Tools/AssetCooker/AssetCooker.cpp:329` |
| `.meta` 오디오 필드 | 스키마는 `guid` + `importSettings.{extension,timestamp}` 3필드뿐 | `EditorAssetDatabase.cpp:1542-1560`, 실물 `Dynamic_CPP/Assets/Cloud/Cloud.png.meta` |
| pak 범위 read | `readAll()`/`readByIndex()` 전량 읽기만. 내부 청크 인덱스는 있으나 범위 API 미노출 | `Paklib.hpp:392,425-445`, 청크 `:255,263` |
| C# identity 타입 | 없음. 오디오 managed 표면은 **문자열 clipKey** | `Native.cs:97-98,677-688` |

`.wav` 가 Assets 에 들어오면 `.meta` 사이드카는 받지만(`EditorAssetDatabase.cpp:1493-1592`) importer 는
돌지 않고 런타임 등록도 되지 않는다. **AU2 는 "PHASE 17 정본을 소비" 가 아니라 위 여섯 축의 신설이고,
그중 pak bounded reader 는 오디오 밖(스트리밍 일반)에도 영향을 준다.** 7일 추정은 이 폭을 반영하지
않았다.

### 3.3 확장자 allowlist 는 3중 미러다

정본은 `Editor/EngineEntry/EditorAssetDatabase.cpp:1716-1733` 의 하드코딩 집합
(`.wav`, `.mp3`, `.ogg` 등록 · `.flac` 없음). 같은 목록이 두 곳에 복제돼 있다.

- `Tools/regression/verify-asset-guid-contract.ps1:181-193` — 주석이 "`m_registeredFiles` 와 같은
  범위여야 한다" 고 명시
- `Tools/migration/Repair-AssetSidecarIdentities.ps1:55`

AU2 의 "allowlist 를 WAV/MP3/FLAC 으로 고정하고 OGG 를 명시 거부" 는 **세 곳을 함께 바꾸는 일**이며,
어긋남을 잡는 게이트가 `verify-asset-guid-contract.ps1` 하나뿐이라는 것도 같이 확인했다(이미 `.slang`
누락으로 drift 가 나 있다 — 오디오와 무관한 기존 결함).

표시 계층에도 구멍이 있다: `EditorAssetPresentation.cpp:262-277` 의 `m_extensionTypes` 에 `.wav`·`.mp3`
만 있고 **`.ogg` 가 빠져** `FileType::Unknown` 아이콘으로 보인다.

---

## 4. 빌드 · 배포 · 게이트 (AU8/AU9)

### 4.1 AU8 이 지목한 대상이 이사했다

- `Tools/build.ps1` 은 22줄 호환 진입점으로 축소됐고 FMOD 0건이다(`:1`, `:18`, `:22` — `BuildTool/
  CreatorBuildTool.csproj` 로 포워딩). 계획서 AU8 의 "`Tools/build.ps1` 의 fmodL.dll stage 제거" 는
  **대상이 없다**.
- 실제 배치는 `Tools/runtime/deploy-runtime.ps1:30-31` 이 `ThirdParty\Fmod\bin\x64` 를 소스 루트로
  등록하고, 그룹 판정(`:75-79`)에서 Common 으로 떨어뜨려 `Bin/x64-<Config>/Runtime/Common/fmod.dll`
  (Debug 는 `fmodL.dll`)로 복사한다.
- `BuildTool/`(GamePackager·PlayerVerification·PackageInputs·AssetCooking)에 `fmod|audio|sound` 0건 —
  **패키징 코드는 FMOD 를 이름으로 알지 못한다.**

### 4.2 폐포는 PE import 가 닫는다

`deploy-runtime.ps1:54-86` 이 호스트 exe 의 import 를 시드로 BFS 를 돌려 의존을 닫고, 파서는
`Tools/runtime/RuntimeLayout.psm1:33-74` `Get-EnginePeImports`(import·delay import 양쪽 순회)다.
FMOD 는 `SceneRuntime` → 호스트 import 로 폐포에 들어온다. 실측:

| 바이너리 | fmod import |
|---|---|
| `Bin/x64-Release/Editor/CreatorEditor.runtime.dll` | `fmod.dll` (import 44개 중) |
| `Bin/x64-Debug/Editor/CreatorEditor.runtime.dll` | `fmodL.dll` (import 35개 중) |
| `Bin/x64-Release/Editor/CreatorEditor.exe` | 없음(얇은 런처, import 2개) |

**따라서 AU8 의 실제 작업은 "목록에서 FMOD 를 지우는 일" 이 아니라 "소스에서 심볼을 없애는 일"이고,
폐포·매니페스트는 자동으로 따라온다.** 반대로 그것을 확인해 주는 게이트는 지금 없다(§4.4).

### 4.3 ★ 라이선스 공백 — 배포본이 FMOD 로깅 빌드를 싣고 있다

- 발행본 `Build/Distributions/local-0.0.0.0-win-x64-Debug-.../Bin/x64-Debug/Runtime/Common/fmodL.dll`
  이 실물로 들어 있다.
- 같은 배포본 `Licenses/ThirdParty/` 는 `DotNetHost · ImViewGuizmo · Mathematics · Slang · fastgltf ·
  mikktspace · ufbx · README.md` 로 **Fmod 항목이 없다**. 수집 규칙이
  `^(LICENSE|COPYING|NOTICE|README)` 파일명 기준인데(`BuildTool/EnginePublisher.cs:108-109`)
  `ThirdParty/Fmod/` 에는 그런 파일이 하나도 없기 때문이다.
- `x64-Debug-Shipping` 구성의 `Runtime/Manifests/Player.json:50` 도 `Runtime/Common/fmodL.dll` 을
  싣는다 — **출하 구성에 개발용 로깅 빌드가 들어간다.**
- 저장소에 SBOM 파일은 없다. FMOD 를 문장으로 언급하는 곳은 `ThirdParty/README.md:10`(독점 SDK 명시,
  이 README 가 규칙에 걸려 배포본에 복사되는 유일한 FMOD 표기)과 `Tools/distribution/README.md:72`
  ("현재 오디오 closure 는 FMOD 를 포함하며 PHASE 22 완료를 뜻하지 않는다") 둘이다.

이건 PHASE 22 완료를 기다릴 사안이 아니다. **AU8 을 기다리지 말고 지금 처분해야 한다**(§8.2 결정 B).

### 4.4 게이트 0 · CI 제외 타깃

- `Tools/regression/` 202개 엔트리 중 오디오를 재는 게이트 **0개**. `run-all.ps1` 의 `Run-Step` 115개
  중 오디오 축 **0개**(등록은 배열이 아니라 소스 하드코딩 나열, `run-all.ps1:19-24`).
- `verify-experiment-contract.ps1:205-214` 의 FMOD 는 **링크 전제**다 — 단정은 "라이브러리 파일이
  존재하는가" 한 줄(`:211-213`)이고 오디오 동작 단정은 없다. AU8 이 FMOD 를 지우면 **이 게이트의
  링크 인자도 같이 손봐야 한다**(계획서에 없는 항목).
- `.github/workflows/build.yml` 은 엔진 라이브러리 4종(`Utility_Framework`·`Physics`·`SceneRuntime`·
  `RenderEngine`, `:102-107`)만 빌드한다. 제외 대상은 **exe 타깃 전부와 게임 패키지 레그**이며 사유는
  `ThirdParty/Fmod/lib/x64/*.lib` 가 `.gitignore`(`:88 x64/`, `:94 [Bb]in/`)로 저장소에 없다는 것이다
  (`build.yml:6-13`). 추적되는 FMOD 파일은 `inc/*` 헤더 11개뿐.
- 즉 **B5 차단은 "FMOD 라이선스 때문에 lib 을 저장소에 못 넣는다" 이고, AU8 이 그것을 푼다**는
  계획서 기술이 지금도 정확하다.

---

## 5. 의존 페이즈 전제

| 계획서 의존 | 대시보드 실측 | 판정 |
|---|---|---|
| `PHASE 17 D2/D5 → AU2` | D2 `stopped`(PHASE 3.75 이관) · D5 `done` | **문장은 어긋났으나 실질 무해** — identity 실물이 있다(§3.1). 계획서의 "D2 가 확정한" 표현을 `Uuid16`/`FileGuid` 실물 참조로 바꾼다 |
| `PHASE 12.5 B2/B3 → AU0` | B2 `progress`(잔여에 "FMOD-free Release" 명시) · B3 `todo` | **AU0 은 B2/B3 없이 가능** — AU0 산출물은 fixture·canary·예산이며 패키징에 의존하지 않는다. 의존을 AU8 로 옮긴다 |
| `AU8/AU9 → PHASE 12.5 B5` | B5 `blocked`(사유가 AU8 을 지목) | 순환이 아니라 정상 — B5 가 AU8 을 기다린다 |
| `AU5/AU9 → PHASE 14 provider` | P1 `progress` · **P5 `todo`** | **발행처가 없다.** AU5 판정에서 "counter 를 provider 에 발행" 을 분리해 P5 이후로 미루고, AU5 는 backend-neutral counter 의 *값* 까지만 판정한다 |

---

## 6. 외부 기준 재확인 (miniaudio)

- 최신 안정 릴리스 **0.11.25**(2026-03-03 발행). 계획서가 적은 후보값이 지금도 최신이다.
- 저장소에 miniaudio 흔적 **0건**(소스·ThirdParty·vcpkg 매니페스트 전부).
- 라이선스는 GitHub API 상 `NOASSERTION`(public domain / MIT-0 이중 선택이라 SPDX 단일 식별자가 붙지
  않는다) — AU3 에서 **둘 중 어느 쪽을 택했는지 파일로 남기는** 절차가 필요하다.

---

## 7. 계획서 정정 목록

| 계획서 문장 | 실측 | 조치 |
|---|---|---|
| §1.1 "`Tools/build.ps1` 이 fmodL.dll stage" | build.ps1 FMOD 0건. 배치는 `deploy-runtime.ps1` + `Runtime/Manifests` | AU8 대상 교체 |
| §1.1 FMOD 소비 파일 7개 | 살아 있는 참조 15개(오탐 2 별도) | §1.1 표로 교체 |
| §1.1 "Editor/Player 프로젝트" | 프로젝트 4개(`CreatorEditor`·`Editor`·`SceneRuntime`·`Player`) | AU8 목록 보정 |
| §1.2 "`.flac` 은 등록 목록에 없다" | 맞다. 추가로 **allowlist 가 3중 미러**이고 `.ogg` 는 아이콘 맵에서 누락 | AU2 에 미러 동기화 항목 추가 |
| §1.2 "실파일 0개" | 맞다. 추가로 **`Sounds` 디렉터리 자체가 없다** | AU0 canary 재현 조건 명시 |
| §1.3 "플래그가 내려가지 않는다" | 맞다. 단 원인은 폴더 부재 → `LoadSounds` 미실행 | canary 기술 보정 |
| §1.4 "listener 호출부 확인 안 됨" | **호출부 0 확정**. 반대편 `getListenerPosition` 은 소비자 있음 | "미검증" → "부재" 로 확정 |
| §6 "PHASE 17 D2 → AU2" | D2 `stopped` · identity 는 실재 | 참조를 실물 타입으로 교체 |
| §6 "PHASE 12.5 B2/B3 → AU0" | AU0 은 독립 가능 | 의존을 AU8 로 이동 |
| AU0 "유효 FMOD 항목 A/B" | 유효 집합이 사실상 공집합 | A/B 를 참고로 강등, 합성 골든 중심으로 재작성 |
| AU5 "counter 를 PHASE 14 provider 에 발행" | P5 `todo` | 판정에서 분리 |
| (없음) | Editor Play 모드에서 `update()` 미호출 | AU0 "고장" 목록에 추가 |
| (없음) | 배포본 FMOD 라이선스 미등재 · Shipping 에 로깅 빌드 | §8.2 결정 B 로 분리 착수 |
| (없음) | `verify-experiment-contract.ps1` 이 FMOD lib 을 링크 | AU8 항목 추가 |

---

## 8. 착수 판정과 순서

### 8.1 지금 착수 가능한 것

- **AU0(재정의)** — 합성 fixture(WAV/MP3/FLAC) 생성기, 실패 canary(폴더 부재 조건 포함), 절대 성능
  예산, **오디오 회귀 게이트 한 축 신설**(현재 0개). FMOD A/B 는 참고로만.
- **AU1** — backend-neutral 계약과 Null backend. 현재 정책 API 가 죽어 있으므로(§1.4) 이관이 아니라
  설계 신설로 잡는다.
- **AU3** — miniaudio 0.11.25 벤더링과 미배선 구현. 제품 경로를 건드리지 않는다.

### 8.2 착수 전 결정 2건

- **결정 A — AU2 의 폭.** pak bounded byte source(`Paklib` 범위 read)는 오디오 전용이 아니다. ①
  AU2 안에서 오디오가 여는가, ② 별도 슬라이스로 떼어 스트리밍 일반으로 여는가. 선택에 따라 AU2 가
  7일에서 크게 벌어진다.
- **결정 B — FMOD 라이선스 처분 시점.** 배포본이 `fmodL.dll` 을 라이선스 등재 없이 싣고 있다(§4.3).
  ① PHASE 22 밖에서 지금 처분(등재 또는 배포 제외), ② AU8 까지 현상 유지. 독점 SDK라 ②는
  PHASE 23 감사까지 위험을 끌고 간다.

### 8.3 재추정

초기 45 인일 대비 방향만 적는다(정밀 수치는 AU0 스파이크 뒤 갱신).

| 슬라이스 | 초기 | 재추정 | 근거 |
|---|---|---|---|
| AU0 | 3 | **4~5** | 게이트 축 신설이 없던 일로 추가(§4.4). A/B 축 소멸로 분류 작업은 줄어든다 |
| AU2 | 7 | **10~13** | 여섯 축 신설 + allowlist 3중 미러(§3.2~3.3). 결정 A 에 따라 상단 |
| AU5 | 6 | 6 | provider 발행분만 분리(§5) |
| AU7 | 5 | **4** | managed ABI 가 이미 깨끗하고(§1.3) 죽은 표면이 많다(§1.4) |
| AU8 | 3 | **3~4** | 대상 이사(§4.1) + `verify-experiment-contract` 링크 + 라이선스(결정 B 가 ②면 여기) |
| 합계 | 45 | **≈ 50~54** | |

---

## 9. 이 정찰이 재지 않은 것

- **실행 검증 0.** 재생·장치·성능·종료를 실제로 돌리지 않았다. §2 의 "소리가 나지 않는다" 는 코드
  경로 판정이며, 에디터를 띄워 확인한 것이 아니다. AU0 의 첫 작업이 이 판정을 실측으로 확정하는 것이다.
- **`SoundComponent::Play()` 의 equal-power 재작성 의혹**(계획서 §1.4)은 읽지 않았다. 클립이 없어
  어차피 실행되지 않으므로 AU0 fixture 이후에 판정한다.
- **Debug/Release 링크 구성 차이**를 링크 로그로 확인하지 않았다. PE import 로만 확인했다.
- **`Editor/Editor.vcxproj` 와 `CreatorEditor.vcxproj` 의 FMOD 경로 중복**이 의도된 분업인지
  (lib 프로젝트는 include only, exe 는 link) 확인만 했고, 정리 필요 여부는 판정하지 않았다.
