# 오디오 백엔드 현대화 — FMOD Core 은퇴 · miniaudio 내재화 (PHASE 22)

- 수립일: 2026-08-24
- 재검토일: 2026-08-27 — efsw 유지 결정과 소스 재감사 반영
- 상태: **계획 수립 · 구현 미착수**
- 배치: PHASE 17 직렬화·Asset/Cook 경계와 PHASE 12.5 package gate 뒤, PHASE 23 MSI·Launcher 제품화 앞
- 초기 추정: **45 인일**. AU0 기준선과 device/backend 스파이크 뒤 갱신
- 확정 포맷: **WAV · MP3 · FLAC만 지원**. OGG/Vorbis와 그 밖의 포맷은 importer에서 명시적으로 거부
- 백엔드 결정: **miniaudio를 소스 벤더링한 첫 `IAudioBackend` 구현체로 채택**

관련 정본:

- [RefactoringPlanDashboard.html](../RefactoringPlanDashboard.html) — PHASE 22 진행 상태
- [SerializationPlan.md](SerializationPlan.md) — `.meta`, `AssetId`, authoring/cooked 경계
- [BuildPipelinePlan.md](BuildPipelinePlan.md) — clean-checkout·CI·Game package gate
- [EngineLayerSeparationPlan.md](EngineLayerSeparationPlan.md) — Runtime / Editor / Host 의존 방향
- [ProfilingCapturePlan.md](ProfilingCapturePlan.md) — Audio counter와 capture provider 소비자
- [EngineDistributionAndLauncherPlan.md](EngineDistributionAndLauncherPlan.md) — PHASE 23 MSI·SBOM·최종 dependency 감사

외부 기준:

- [miniaudio 공식 저장소](https://github.com/mackron/miniaudio) — high-level engine, resource manager, node graph, WAV/FLAC/MP3 decoder, source 통합 방식
- [miniaudio Programming Manual](https://miniaud.io/docs/manual/index.html) — engine·3D spatialization·resource manager·threading 계약
- [miniaudio Releases](https://github.com/mackron/miniaudio/releases) — 채택 tag와 변경 이력 확인
- [miniaudio LICENSE](https://github.com/mackron/miniaudio/blob/master/LICENSE) — public domain 또는 MIT No Attribution
- [FMOD Legal](https://www.fmod.com/legal) — 상용 엔진/toolset 재배포 시 별도 계약 검토가 필요한 기존 제약

---

## 0. 결정 요약

1. CreatorEngine의 제품 오디오 입력은 **대소문자를 구분하지 않는 `.wav`, `.mp3`, `.flac` 세 종류**다.
   `.ogg`, `.oga`, Vorbis, Opus, AAC 등은 fallback decoder를 붙이지 않는다. 발견 즉시 경로와 지원 포맷을
   포함한 importer 오류를 낸다. 조용히 무시하거나 확장자만 바꾸는 동작은 금지한다.
2. miniaudio는 `ma_sound*`를 게임·컴포넌트·Editor API에 노출하는 대체 SDK가 아니다.
   CreatorEngine의 backend-neutral 계약 뒤에 숨은 **첫 구현체**다.
3. `SoundComponent`는 `AudioClipId`와 generation이 있는 `AudioVoiceHandle`만 보유한다.
   `FMOD::Channel*`, `ma_sound*`, `void* ownerTag`는 공개·직렬화·managed 경계를 넘지 않는다.
4. 오디오 서비스는 Runtime Host가 소유한다. 새 process-global registry나 새 singleton을 만들지 않는다.
   기존 `SoundManager` singleton은 이행 façade로만 쓰고 AU7에서 소비자를 옮긴 뒤 은퇴한다.
5. miniaudio는 DLL로 배포하지 않는다. 채택 시점 stable tag를 exact commit/hash와 함께 고정하고
   `miniaudio.c` 한 translation unit을 Runtime 오디오 모듈에 직접 컴파일한다. 현재 계획 기준 후보는
   `0.11.25`이며 AU0에서 다시 확인한다.
6. filename stem 기반 `clipKey`와 `Sounds/BGM` 폴더 추론을 정본으로 쓰지 않는다. `.meta`가 부여한
   `AssetId`와 명시적 `AudioClipImportSettings.loadMode`가 identity와 Resident/Stream 정책을 결정한다.
7. mix graph의 정본은 `Master -> BGM/SFX/Player/Monster/UI` bus와 명명된 effect bus다.
   FMOD의 reverb slot index `0..3`은 제품 계약이 아니며 stable `AudioBusId` 기반 send로 이관한다.
8. voice cap·stealing·virtualization은 엔진 정책이다. backend channel 열거와
   `getAudibility()`에 의존하지 않고 logical voice의 priority·effective gain·age·loop 상태로 판정한다.
9. 현재의 2D/3D equal-power spatial blend는 보존하되 **logical voice 하나**로 센다. 구현상 두 source를
   사용하더라도 voice limit·owner·pause·stop은 하나의 handle로 동작해야 한다.
10. PHASE 22 완료 전까지 FMOD와 miniaudio를 동시에 shipping하지 않는다. AU0/AU4/AU9의 제한된
    개발 A/B만 허용하고, AU8에서 프로젝트·stage·CI·PE import·third-party notice까지 한 번에 FMOD를 제거한다.
11. `efsw`는 Editor Asset DB의 파일 감시 구현으로 유지한다. AudioRuntime은 파일 감시기를 소유하거나
    디렉터리를 폴링하지 않고, EditorAssetDatabase가 게시한 asset-change와 cooked manifest만 소비한다.

---

## 1. 현재 소스 기준선 (2026-08-27 재확인)

### 1.1 실제 FMOD 사용 범위

현재 사용은 FMOD Studio authoring/event/bank 계층이 아니라 **FMOD Core 기본 재생 계층**이다.

- `Engine/SceneRuntime/SoundManager.cpp`
  - `FMOD::System` 초기화·update·shutdown
  - BGM/SFX/Player/Monster/UI `ChannelGroup`
  - sound load, BGM stream, loop, volume, pitch, 2D/3D attributes
  - voice cap, same-clip preemption, oldest/quietest/priority stealing
- `Engine/SceneRuntime/SoundManager.h`
  - 공개 `ChannelPair`와 listener/play API에 `FMOD::Channel*`, `FMOD_VECTOR` 노출
  - 내부 `FMOD::System`, `Sound`, `ChannelGroup`, `Channel` 소유
- `Engine/SceneRuntime/SoundComponent.h/.cpp`
  - 직렬화 값과 raw FMOD channel을 한 컴포넌트가 함께 소유
  - custom rolloff는 FMOD curve가 아니라 매 LateUpdate마다 엔진이 gain을 다시 쓰는 구조
- `Editor/EngineGUIWindow/InspectorWindow.cpp`
  - loop, volume, pitch, 3D mode/min/max distance, reverb send를 raw channel에 직접 적용
- `Editor/CreatorEditor.vcxproj`, `Player/Player.vcxproj`
  - FMOD include/lib 경로와 `fmod_vc.lib`·`fmodL_vc.lib` 링크
- `Tools/build.ps1`, `.github/workflows/build.yml`
  - `fmodL.dll` stage 및 clean CI 미완료의 원인으로 FMOD를 기록

직접 의존은 SceneRuntime의 SoundManager/SoundComponent, Editor Inspector, Editor/Player 프로젝트와
build/stage 경로에 집중되어 있다. `FMOD::Studio`, `.bank`, EventDescription/EventInstance 소비자는
발견되지 않았다. 따라서 middleware
authoring workflow를 재현하는 작업은 이 페이즈의 범위가 아니다.

### 1.2 포맷과 asset 경로의 현재 모순

- `EditorAssetDatabase.cpp`는 `.wav`, `.mp3`, `.ogg`를 등록 파일로 인정한다.
- `SoundManager::SoundLoaderThread/LoadSounds`도 같은 세 확장자를 1초마다 폴링한다.
- `.flac`은 miniaudio 기본 지원 포맷이지만 현재 CreatorEngine 등록 목록에는 없다.
- 저장소의 `Dynamic_CPP/Assets`와 제품 asset root에서 WAV/MP3/OGG/FLAC 실파일은 **0개**였다.
  기존 콘텐츠 parity를 주장할 자료가 없으므로 AU0가 재배포 가능한 합성 fixture를 먼저 만든다.
- clip identity가 파일명 stem이어서 다른 폴더의 동명 파일이 충돌하며, BGM 여부를 폴더 이름으로 추론한다.

### 1.3 수명·스레드 기준선

`SoundManager::initialize()`는 `SoundLoaderThread`를 detach한다. 해당 thread는 종료 조건 없는
`while (true)`로 폴더를 재귀 순회하고, `_isSoundLoaderThreadRunning`은 초기값 true에서 내려가지 않는다.
소멸자는 그 값이 false가 되기를 기다린다. 이 구조는 miniaudio로 복사하지 않는다.

새 계약은 다음과 같다.

- Editor Asset DB는 기존 `efsw` 감시를 유지하고 add/delete/move/modified/missed-action을 asset-change로 정규화한다.
- Asset 발견·변경은 Editor Asset DB 또는 cooked manifest가 생산한다.
- Runtime 오디오 서비스는 디렉터리를 폴링하지 않는다.
- game thread는 bounded command queue에 값 command를 제출한다.
- device callback은 파일 I/O, 로그, 엔진 object 접근, blocking lock, 동적 할당을 하지 않는다.
- shutdown은 `producer stop -> command drain/cancel -> voice -> clip/resource -> graph -> device` 순서다.

`efsw`의 callback thread에서는 import나 AudioRuntime API를 직접 호출하지 않는다. callback은 값 이벤트만
queue에 넣고, Editor main-thread dispatch가 meta/import/catalog 게시를 끝낸 뒤 AudioRuntime에 변경을 알린다.

### 1.4 기존 출력 중 parity 정본이 아닌 항목

- `setListenerAttributes()`는 정의돼 있지만 실제 listener 갱신 호출부가 확인되지 않았다. 현재 3D 출력은
  기본 원점 listener에 의존할 수 있으므로 새 구현의 의미 정본으로 삼지 않는다.
- `SoundComponent::Play()`는 앞서 계산한 equal-power 2D/3D gain을 두 채널의 full volume으로 다시 쓸 수 있다.
  spatial blend는 FMOD 출력 복제가 아니라 독립 golden fixture로 판정한다.
- Inspector가 reverb send를 쓰지만 대응하는 effect/reverb instance 구성 경로는 검증되지 않았다. legacy
  `reverbIndex`는 schema 입력으로만 다루고 실제 room reverb는 AU6 capture로 새로 증명한다.

### 1.5 현재 상태를 완료로 오해하지 않는 규칙

- miniaudio 문서가 기능을 지원한다는 사실은 CreatorEngine 구현 증거가 아니다.
- `miniaudio.c`가 프로젝트에 들어왔다는 것만으로 AU3을 완료로 세지 않는다.
- FMOD symbol 0만으로 AU8을 완료로 세지 않는다. package smoke·PE import·stage manifest도 통과해야 한다.
- 문서 작성 시점에는 소스 구현·빌드·재생·성능 검증을 수행하지 않았다.

---

## 2. 제품 포맷 계약

### 2.1 허용 포맷

| 확장자 | codec/container 정책 | 기본 load 정책 | 비고 |
|---|---|---|---|
| `.wav` | PCM/IEEE float WAV. 지원 bit depth는 importer fixture로 고정 | 짧은 SFX는 Resident 후보 | 손상 chunk와 과대 metadata를 검증한다 |
| `.mp3` | miniaudio 내장 MP3 decoder | 긴 music/ambience는 Stream 후보 | 원본을 재인코딩하지 않는다 |
| `.flac` | miniaudio 내장 FLAC decoder | 크기·길이에 따라 Auto | lossless source와 loop 정밀도를 검증한다 |

다음은 v1 제품 입력이 아니다.

- OGG/Vorbis, Opus, AAC/M4A, WMA, tracker module, MIDI
- 런타임 URL/HTTP stream, microphone capture, voice chat
- FMOD bank/event와 FMOD Studio project

지원하지 않는 파일은 `.meta`를 만들지 않고 `UnsupportedAudioFormat` diagnostic을 남긴다. 이미 meta가
있는 unsupported 파일은 DB에서 사라진 것처럼 처리하지 않고 `ImportFailed` 상태와 기존 last-known-good
cooked artifact를 구분한다. package는 unresolved/failed audio reference가 있으면 fail-closed한다.

### 2.2 channel·sample rate 규칙

- engine mix 기준은 AU0에서 장치 실측 후 고정하되 초기값은 **48 kHz · float32**다.
- source sample rate는 importer metadata에 보존하고 decode/resample은 Runtime backend가 담당한다.
- 3D point source는 mono를 정본으로 한다. stereo clip을 spatial로 지정하면 silent downmix하지 않고
  importer/Inspector가 경고하며 명시적 conversion 또는 non-spatial 사용을 요구한다.
- non-spatial music/UI의 stereo는 보존한다. surround source/output은 v1 완료 조건이 아니다.

### 2.3 `.meta`와 cooked clip

`AudioClipImportSettings`의 최소 정본은 다음과 같다.

```text
schemaVersion
assetId
loadMode: Auto | Resident | Stream
spatialKind: PointMono | NonSpatial
loopStartFrame / loopEndFrame (optional)
```

importer가 source를 읽어 생성하는 immutable metadata는 다음을 포함한다.

```text
codec: Wav | Mp3 | Flac
channels / sampleRate / frameCount / duration
sourceContentHash
payloadSize / cookedContentHash
```

`AudioClip` cooked payload는 원본 경로를 다시 열지 않는다. pak/VFS가 제공하는 bounded byte source에서
resident decode 또는 stream decode한다. 기본 정책은 원본 encoded payload를 보존하여 MP3 재인코딩과
generation loss를 피하고, header/index metadata만 cooked manifest에 고정한다.

---

## 3. 목표 계층과 소유권

```text
SoundComponent / managed binding / Editor Inspector
             |
             v
AudioClipId + AudioVoiceHandle + AudioPlayRequest
             |
             v
AudioRuntime (voice policy, buses, commands, metrics)
             |
             v
IAudioBackend
             |
             v
MiniaudioBackend (ma_engine/resource manager/node graph private)
             |
             v
Host-selected playback device / Null test device
```

### 3.1 공개 계약

```cpp
struct AudioVoiceHandle { uint32_t index; uint32_t generation; };
struct AudioClipId; // PHASE 17 D2의 canonical 128-bit asset identity를 감싼 strong type
struct AudioBusId;

struct AudioListenerState
{
    Vector3 position;
    Vector3 velocity;
    Vector3 forward;
    Vector3 up;
};

struct AudioPlayRequest
{
    AudioClipId clip;
    AudioBusId bus;
    float volume;
    float pitch;
    int priority;
    bool loop;
    float spatialBlend;
    Vector3 position;
    Vector3 velocity;
    float minDistance;
    float maxDistance;
    RolloffMode rolloff;
};
```

공개 header에는 `miniaudio.h`, `ma_*`, `FMOD_*`, vendor error code가 없어야 한다. backend error는
`AudioResult`와 구조화 diagnostic으로 번역한다. native pointer를 managed binding에 숫자로 노출하지 않는다.
`AudioClipId`를 위해 RenderEngine의 실험 `AssetId`를 SceneRuntime/AudioRuntime이 참조하지 않는다. D2가
확정한 중립 asset identity와 명시적 adapter를 사용하며 managed ABI는 고정 16-byte 값 또는 두 `uint64_t`로
버전 관리한다.

### 3.2 Runtime 소유권

- Host가 `AudioRuntime` 인스턴스를 만들고 device 설정·VFS·diagnostic sink를 주입한다.
- `AudioRuntime`이 clip cache, logical voice table, bus graph, command queue, metrics를 소유한다.
- `MiniaudioBackend`가 `ma_engine`, resource manager, sound group/node, backend source memory를 소유한다.
- Scene은 `AudioSourceSystem`과 `AudioListenerSystem`의 비소유 등록을 소유한다. process-global
  `SoundSystem`을 새 오디오 정본으로 승격하지 않는다.
- `AudioListenerComponent`는 active runtime Scene당 하나를 정본으로 하며 중복은 deterministic 선택과
  diagnostic을 남긴다. 이행 중에만 Scene `CameraSystem::GetPrimaryCamera()`를 fallback으로 허용한다.
- `SoundComponent`는 authoring 값과 handle만 들며, 파괴·scene transfer·DDOL 시 handle로 stop/rebind한다.
- Editor는 component setter 또는 `AudioRuntime` command만 호출한다. raw backend object를 직접 만지지 않는다.

### 3.3 command와 thread 경계

- 생성·정지·pause·seek·parameter 변경은 value command로 제출한다.
- transform/listener 갱신은 프레임마다 중복 command를 쌓지 않고 latest-state coalescing을 사용한다.
- queue가 가득 차면 정책별 counter를 올리고 중요 stop/shutdown command를 보존한다.
- device callback에서 engine allocator를 호출하거나 `Debug->Log*`를 호출하지 않는다.
- async decode job과 VFS read 수명은 clip resource가 소유하고 shutdown에서 명시적으로 cancel/drain한다.

---

## 4. 기능별 채택 결정

### 4.1 bus와 volume

기본 bus는 기존 직렬화 호환을 위해 BGM/SFX/Player/Monster/UI를 유지한다. enum ordinal을 wire나
cooked identity로 쓰지 않고 stable `AudioBusId`로 매핑한다. Master mute/volume과 bus volume은
logical dB 값을 정본으로 두고 backend에는 linear gain을 투영한다.

### 4.2 spatial blend와 rolloff

- `spatialBlend=0`은 2D, `1`은 3D, 중간값은 현재 equal-power `cos/sin` 규칙을 유지한다.
- 두 backend source를 쓰더라도 외부에는 voice handle 하나만 보인다.
- Linear/Inverse/Custom rolloff는 엔진이 하나의 curve sampler로 계산한다.
- min/max distance, listener/source velocity, 좌표 handedness와 forward/up normalization을 fixture로 고정한다.
- custom curve가 min/max 밖에서 보이는 값과 duplicate point 처리 규칙을 serializer test로 고정한다.

### 4.3 voice cap·stealing·virtualization

각 logical voice record는 다음 판정값을 보유한다.

```text
handle generation, clipId, busId, ownerId
createdFrame, playheadFrame, loop, priority
baseGain, busGain, attenuationGain, effectiveGain
state: Pending | Physical | Virtual | Paused | Stopped
```

- same-clip preemption은 `clipId`로 판정한다.
- Oldest는 wall clock이 아니라 engine frame/playhead 기준이다.
- Quietest는 backend `getAudibility()`가 아니라 engine effective gain으로 판정한다.
- LowestPriority의 동률은 effective gain, age, stable handle index 순으로 결정해 재현 가능하게 만든다.
- 짧은 one-shot은 cap에서 steal/drop할 수 있다.
- loop/persistent voice는 필요하면 Virtual로 전환해 playhead를 유지하고 다시 audible할 때 physical source를 얻는다.
- active/physical/virtual/stolen/dropped voice 수와 이유를 PHASE 14 profiler provider에 발행한다.

### 4.4 stream과 resident

폴더 이름으로 stream을 결정하지 않는다.

- `Resident`: 전체 decode가 예산 안에 들어오는 짧은 clip
- `Stream`: music/ambience와 큰 clip. bounded read-ahead와 seek/loop를 사용
- `Auto`: AU0의 duration/decoded-size threshold로 둘 중 하나를 결정하고 결과를 cooked metadata에 기록

stream underflow, decode error, corrupt payload는 silence로 영구 은폐하지 않고 rate-limited diagnostic과
counter를 남긴다. package smoke에는 최소 한 개의 MP3 stream과 FLAC loop를 포함한다.

### 4.5 reverb

현재 FMOD `reverbIndex 0..3` 직접 호출은 실제 effect graph의 안정된 identity가 아니다. 다음으로 바꾼다.

- `AudioBusId` 기반 named reverb bus
- source별 `reverbSendDb`
- effect node는 miniaudio node graph 뒤의 private 구현
- legacy `reverbIndex`는 schema migration에서 mapping table로 읽고 새 포맷에는 쓰지 않는다.

초기 제품에는 하나의 검증된 room reverb bus만 필수다. 여러 environment preset과 zone blending은 후속이다.
효과가 없는 UI만 남기지 않도록 impulse-response/wet-dry capture가 통과하기 전 Inspector control을 제품
기능으로 표시하지 않는다.

### 4.6 device와 장애 복구

- Windows v1은 WASAPI 우선, 실패 시 miniaudio의 지원 backend fallback을 진단에 기록한다.
- default device 변경·device invalidation·exclusive 충돌에서 Runtime state를 잃지 않고 재초기화한다.
- Editor는 audio device 없음으로 기동 전체가 실패하지 않는다. Null backend로 명시적으로 degraded된다.
- Player package smoke는 실제 device 경로와 Null deterministic 경로를 분리한다.

---

## 5. 실행 슬라이스

### AU0 — 기준선 유효성 분류·합성 fixture·실패 canary (P0, 3일)

- sine, impulse, silence, short loop를 WAV/MP3/FLAC으로 생성하는 재현 가능한 test fixture를 둔다.
- 기존 동작을 `유효(play/stop/loop/stream/bus)`, `고장(shutdown/lifetime)`, `미검증(listener/spatial/reverb)`으로
  분류하고 FMOD A/B는 유효 항목에만 사용한다.
- 2D/3D, spatial blend 0/0.5/1, rolloff는 FMOD 출력 복제가 아니라 offline/golden expected value를 기록한다.
- corrupt/truncated/oversized header와 OGG fixture가 실제 실패하는 canary를 만든다.
- detached loader 종료 hang, 명시적 Host shutdown 부재, callback 이후 접근을 실패 canary로 고정한다.
- callback/update CPU, peak memory, stream read, underrun, start latency를 동일 장치·buffer에서 기록한다.
- miniaudio stable tag/commit/hash와 license 선택을 확정한다.

**판정:** fixture 생성이 deterministic하고, failure canary가 거짓 양성 없이 실패하며, 절대 성능 예산과
유효 기능의 A/B threshold를 숫자로 기록한다. 실제 콘텐츠가 0개인 현재 상태나 고장난 FMOD 의미를 parity
근거로 사용하지 않는다.

### AU1 — backend-neutral 계약·모듈·listener·Host 수명 (P0, 5일)

- `AudioClipId`, `AudioVoiceHandle`, `AudioBusId`, listener/play/state command를 정의한다.
- generation stale-handle rejection과 logical voice table을 구현한다.
- Host 소유 initialize/update/shutdown과 Null backend를 먼저 연결한다.
- 중립 `AudioRuntime` 모듈과 Scene-owned source/listener system을 만들고 primary-camera fallback의 이행 종료점을 고정한다.
- 공개 header와 managed ABI에서 vendor type을 제거할 이행 façade를 만든다.

**판정:** Null backend로 create/play/update/stop/shutdown 100회, stale handle fixture, component 파괴·scene
transfer·DDOL 수명 test가 통과하고 process-global registry가 추가되지 않는다.

### AU2 — AudioClip importer·meta·catalog·cook·VFS 계약 (P0, 7일)

- Asset DB 허용 목록을 WAV/MP3/FLAC으로 고정하고 OGG를 명시 거부한다.
- `AudioClipImportSettings`와 immutable source metadata를 `.meta`/DB/cooked manifest에 연결한다.
- PHASE 17 D2의 canonical 128-bit identity를 `AudioClipId`로 감싸 filename `clipKey`를 이관하고, 동명 파일과
  native/managed ABI round-trip fixture를 추가한다.
- pak/VFS byte source와 resident/stream load 정책을 구현한다.
- 현재 전체 `Assets`를 복사하는 packer 위에 audio cooked metadata/catalog와 bounded byte range를 명시한다.

**판정:** 세 지원 포맷 import/cook/load, 동명 두 clip, source move/rename, corrupt input, OGG rejection,
missing/failed reference package fail-closed가 통과한다. Editor 미기동 cook에서도 결과 digest가 같다.

### AU3 — `MiniaudioBackend` 미배선 구현 (P0, 4일)

- exact pinned `miniaudio.c/.h`와 LICENSE/provenance를 벤더링한다.
- 한 implementation TU만 컴파일하고 public/header 전이를 막는다.
- `ma_engine`, resource manager, Null/실 device, error translation과 shutdown skeleton을 구현한다.
- 고정 job-thread 수와 efsw와 무관한 pak/VFS callback 수명·cancel/drain을 구현한다.
- 제품 Editor/Player 기본 backend에는 아직 배선하지 않는다.

**판정:** 독립 backend test target에서 WAV/MP3/FLAC resident/stream decode, device 없음, init 실패,
반복 init/uninit이 통과한다. FMOD 제품 경로의 동작과 build는 이 slice에서 바꾸지 않는다.

### AU4 — core playback·bus·stream parity (P0, 5일)

- play/stop/pause/resume/seek/loop/volume/pitch와 기본 bus graph를 구현한다.
- `AudioPlayRequest`와 clip VFS source를 miniaudio에 연결한다.
- MP3 stream과 FLAC loop의 seek/EOF/restart를 닫는다.
- 동일 fixture로 FMOD/miniaudio 개발 A/B를 실행한다.

**판정:** 기능 matrix 전부 통과, start/stop/loop frame 오차가 AU0 한계 안이고, 30분 stream underrun 0,
callback p99가 buffer duration의 50% 미만이다.

### AU5 — listener·3D·spatial blend·voice allocator·virtualization (P0, 6일)

- explicit `AudioListenerComponent` 선택과 source transform/velocity, Linear/Inverse/Custom attenuation을 구현한다.
- logical voice 하나 아래 2D/3D equal-power source pair를 숨긴다.
- bus별 cap, deterministic steal, persistent loop virtualization과 rehydrate를 구현한다.
- voice metrics를 backend-neutral counter로 발행한다.

**판정:** 0/0.5/1 blend와 distance/velocity fixture, 128 logical voice cap, 동률 steal replay,
virtual loop playhead 복구가 통과하고 raw source 수가 logical cap 판정을 왜곡하지 않는다.

### AU6 — named reverb bus·send·capture 검증 (P1, 3일)

- 한 개의 room reverb bus와 source별 send dB를 node graph에 연결한다.
- legacy `reverbIndex` migration과 새 `AudioBusId` 직렬화를 구현한다.
- impulse와 dry/wet capture로 실제 effect 소비를 검증한다.

**판정:** send off/dry/wet capture가 서로 구분되고 deterministic offline RMS/decay threshold를 통과한다.
backend node pointer가 Component/Inspector에 노출되지 않는다.

### AU7 — SoundComponent·Editor·managed 소비자 배선 (P0, 5일)

- `SoundComponent`, `SoundSystem`, `ClrHost`, Editor Inspector를 handle/value API로 옮긴다.
- raw channel getter와 Editor의 FMOD direct call을 제거한다.
- loader polling thread와 filename key/folder BGM 추론을 제거한다.
- `efsw` 기반 Editor Asset DB는 유지하고 import/catalog 완료 이벤트만 AudioRuntime command로 넘긴다.
- play mode 진입/이탈, scene transfer, component destroy, hot reload 순서를 고정한다.

**판정:** Editor Play 왕복 100회, scene 전환·DDOL·component destroy·managed 호출 회귀 뒤 voice/thread/
handle 증가 0이며 public/managed header의 FMOD/miniaudio token이 0이다.

### AU8 — FMOD 은퇴·build/package/CI 폐쇄 (P0, 3일)

- CreatorEditor/Editor/SceneRuntime/Player 프로젝트의 FMOD include/lib를 제거한다.
- `ThirdParty/Fmod`, `fmod_vc.lib`, `fmodL_vc.lib`, `fmod.dll`, `fmodL.dll` stage를 제거한다.
- `Tools/build.ps1`, CI, third-party notice, allowlist를 miniaudio source provenance에 맞춘다.
- PHASE 12.5 B5 clean-checkout Game leg를 다시 연다.

**판정:** source/project/stage/PE import에서 FMOD dependency 0, miniaudio runtime DLL 0, clean checkout
Editor+Player+Game package link/smoke와 license/SBOM scan이 통과한다.

### AU9 — 성능·device 장애·soak·최종 정산 (P0, 4일)

- 0/1/32/128 voice, resident/stream 혼합, reverb on/off workload를 절대 제품 예산으로 판정하고 AU0의 유효
  FMOD 항목만 참고 비교한다.
- default device 전환·device loss·Null fallback·재초기화와 shutdown race를 반복한다.
- profiler provider에 callback/update/decode/voice/underrun counter를 연결한다.
- A/B용 FMOD 개발 경로와 임시 adapter를 제거한다.

**판정:** 30분 workload underrun 0, callback p99 < buffer 50%, 절대 CPU/memory/startup 예산과 유효 A/B 회귀 통과,
device cycle 20회와 Editor/Player 종료 100회에서 thread/handle/voice/resource 잔류 0이다. 숫자가 기준을
넘으면 miniaudio를 억지 채택하지 않고 buffer·job·graph 원인을 기록한 뒤 재판정한다.

---

## 6. 의존 관계와 배정

```text
PHASE 17 D2/D5 -> AU2
PHASE 12.5 B2/B3 -> AU0 -> AU1 -> AU3
(AU2 + AU3) -> AU4 -> (AU5 + AU6) -> AU7 -> AU8 -> AU9

AU8/AU9 -> PHASE 12.5 B5 clean CI
AU8/AU9 -> PHASE 23 DL5/DL9/DL10 distribution/release gate
AU5/AU9 -> PHASE 14 Audio profiler provider
```

- AU0/AU1/AU3은 PHASE 22 정식 착수 전에 독립 slice로 진행할 수 있다.
- AU2는 PHASE 17의 `.meta`/cooked manifest 정본을 복제하지 않고 소비한다.
- AU3은 사용자가 앞서 정한 **기능 수집 후 코드 작성, 아직 제품 배선 없음**의 경계다.
- AU7 전에는 기본 Editor/Player backend를 바꾸지 않는다.
- `efsw`는 이 페이즈의 제거·교체 대상이 아니다. AudioRuntime에 watcher dependency를 추가하지 않는 것으로
  계층을 닫는다.
- PHASE 23 distribution stage에는 AU8이 끝난 제품만 들어간다. MSI에 FMOD와 miniaudio를 함께 넣는
  과도기 산출물은 만들지 않는다.

---

## 7. 검증 matrix

| 영역 | fixture/행동 | 완료 증거 |
|---|---|---|
| 포맷 | WAV/MP3/FLAC 정상·손상·truncated | 성공/실패 code와 DB 상태가 deterministic |
| 미지원 | OGG/Vorbis와 확장자 위장 | importer 명시 실패, package fail-closed |
| identity | 동명 clip·move·rename | `AssetId` 유지, filename key 충돌 0 |
| 기본 재생 | play/stop/pause/resume/seek/loop | Null/offline frame assertion + 실장치 smoke |
| stream | MP3 30분, FLAC loop/seek | underrun 0, EOF/loop frame 오차 기준 이내 |
| spatial | blend 0/0.5/1, min/max, velocity | captured gain/position 기준 통과 |
| voice | 1/32/128 cap, same clip, 동률 | deterministic steal/virtual/drop log |
| reverb | impulse dry/wet | RMS/decay threshold와 bus routing 일치 |
| 수명 | Play 100회, scene/DDOL/destroy, shutdown | voice/thread/handle/resource 증가 0 |
| device | 없음/loss/default switch 20회 | Null degrade 또는 복구, deadlock 0 |
| 성능 | resident/stream/reverb workload | callback p99·memory·latency·underrun gate |
| 패키지 | clean checkout Editor/Player/Game | FMOD import 0, miniaudio DLL 0, smoke 성공 |

---

## 8. 최종 완료 기준

다음을 모두 증명해야 PHASE 22를 완료로 표시한다.

1. 제품 Asset DB/importer가 WAV/MP3/FLAC만 인정하고 OGG/Vorbis를 명시적으로 거부한다.
2. `SoundComponent`, Editor, managed/public header에 vendor 타입과 raw backend pointer가 0이다.
3. `AudioClipId`/`AudioVoiceHandle`/`AudioBusId`가 identity·수명·오류 경계를 닫는다.
4. detached sound polling thread와 filename `clipKey` 정본이 제거된다.
5. 2D/3D blend, bus, loop, stream, rolloff, voice cap/steal/virtualization, reverb gate가 통과한다.
6. callback thread에서 파일 I/O·로그·blocking lock·engine object 접근·동적 할당이 0이다.
7. source, vcxproj, CI, stage, PE import, 설치 후보 manifest에서 FMOD dependency가 0이다.
8. miniaudio는 exact tag/hash로 source vendoring되고 license/provenance/SBOM 입력이 재현된다.
9. clean checkout의 CreatorEditor·Player·Game package build/smoke가 FMOD 공급 없이 통과한다.
10. AU9 성능·device·shutdown soak가 통과하고 측정 파일/명령이 보존된다.
11. Editor Asset DB의 `efsw` 감시는 유지되며 AudioRuntime/Player는 `efsw` API나 callback thread를 직접 알지 않는다.

---

## 9. 위험과 기각한 대안

| 후보 | 판정 | 이유 |
|---|---|---|
| `FMOD::Channel*`를 `ma_sound*`로 직접 치환 | 기각 | backend type 노출·수명·Editor 직접 호출이 그대로 남는다 |
| FMOD와 miniaudio를 shipping에서 장기 병존 | 기각 | 라이선스·stage·동작 정본이 둘로 갈라지고 PHASE 23 감사를 방해한다 |
| OGG/Vorbis custom decoder 추가 | 기각 | 제품 결정이 WAV/MP3/FLAC 세 포맷이며 decoder dependency만 다시 늘어난다 |
| 모든 source를 MP3로 재인코딩 | 기각 | loop 정밀도·generation loss·CPU 비용을 일괄 강제한다 |
| 폴더 `Sounds/BGM`으로 stream 판정 유지 | 기각 | move/rename이 runtime 정책을 바꾸고 multi-root project에서 취약하다 |
| backend channel count를 logical voice count로 사용 | 기각 | spatial blend 한 음원이 두 channel을 써 cap과 priority를 왜곡한다 |
| miniaudio shared DLL | 기각 | 공식 ABI 안정성 보장이 없고 MSI runtime dependency를 불필요하게 늘린다 |
| 오디오 device callback에서 엔진 event/log 호출 | 기각 | real-time thread에 allocation·lock·수명 역참조를 들여온다 |
| miniaudio보다 더 작은 자체 device/decoder 구현 | 기각 | WASAPI·decoder·resampler·device-loss 검증 범위가 외부 종속 절감 이익을 압도한다 |

---

## 10. 갱신 규칙

- 대시보드 AU0~AU9와 이 문서의 상태를 함께 바꾼다.
- `miniaudio.c` 추가, FMOD symbol 감소, 소리 출력 성공을 단독 완료로 세지 않는다. 각 slice의 **판정**을 통과해야 한다.
- 포맷 추가 요청은 custom decoder부터 붙이지 않는다. 제품 요구·cook 정책·라이선스·보안 fixture를 이 문서에서 먼저 재판정한다.
- 성능 수치는 backend 이름이 아니라 동일 device, sample rate, buffer, fixture, build configuration으로 비교한다.
- 파일 감시 변경은 PHASE 23의 efsw wrapper/root lifecycle 계약에서만 다루며 PHASE 22 완료 조건과 혼합하지 않는다.
- 실제 프로젝트 audio asset이 생기면 AU0 합성 fixture와 별도로 대표 content corpus를 고정하고 hash를 남긴다.
- miniaudio tag를 올릴 때 release note, source hash, license, offline/device/soak gate를 다시 실행한다.
- PHASE 23 MSI·Launcher는 AU8/AU9를 구현으로 추정하지 않고 package manifest와 PE import 결과를 직접 검증한다.
