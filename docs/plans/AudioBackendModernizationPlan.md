# 오디오 백엔드 현대화 — FMOD Core 은퇴 · miniaudio 내재화 (PHASE 22)

## 2026-10-07 최근 병합 반영

PR #121(`c31ccb1c`)의 소비층·SoundGraph·FMOD 제거가 master에 적용됐다. §12와 cloud 검증 보고서의 Debug/Release/ASan+UBSan 각 3,825단정 및 retirement 1,732점검은 당시 검증 tree의 증거다. 후속 Windows 포인터 수정 `904ad55d`·Player 중복 종료 수정 `29024ddf`도 현재 HEAD에 포함되지만 이번 문서 감사에서 재빌드하지 않았다.

2026-10-08 후속 검증으로 **AU0~AU9 및 PHASE22를 완료했다.** 최종 clean engine과 실제 Dynamic_CPP의 선택 씬 package, 수정 후 실제 Editor 30분, 유효한 stream/ETW 계측 및 Player 프로세스 종료 100회가 모두 통과했다. 최종 판정은 §12.13과 Windows 보고서가 정본이다. 전체 원본 콘텐츠의 미해결 GUID 12개, 원격 CI 게임 레그와 PHASE23 MSI는 별도 범위다. Null/software 장치 검증을 실제 하드웨어 통과로 승격하지 않는다.

근거: [10월 4~7일 PR 적용 감사](../analysis/MergedPrReview20261007.md). 아래 과거 날짜의 검증 기록은 해당 시점의 증거이며 최신 HEAD의 통과를 뜻하지 않는다.

- 수립일: 2026-08-24
- 재검토일: 2026-08-27 — efsw 유지 결정과 소스 재감사 반영
- 재검토일: 2026-09-16 — **사전 정찰로 §1 기준선 14건 정정**. 정정 목록과 근거는
  [Phase22AudioPreflight.md](../analysis/Phase22AudioPreflight.md) §7. 이 문서의 §1·AU0·AU8 을 읽을 때는
  정찰 결과를 함께 본다. 특히 ① 현재 FMOD 경로는 `Sounds\` 디렉터리 부재로 클립을 적재하지 못해
  AU0 의 "유효 FMOD 항목 A/B" 가 성립하지 않고, ② AU8 이 지목한 `Tools/build.ps1` stage 는
  `Tools/runtime/deploy-runtime.ps1` 의 PE import 폐포로 이사했으며, ③ 배포본이 FMOD 로깅 빌드를
  라이선스 등재 없이 싣고 있다.
- 상태: **소스 구현·FMOD 철거 및 클라우드 회귀 통과 — Windows/실장치 acceptance PENDING** (2026-10-05). Debug·Release·ASan/UBSan 각각 3,825 단정 통과. 최신 소비 설계·검증 구분은 §12, [검증 보고서](../analysis/Phase22AudioCloudValidation.md)가 정본이다. 과거 §11의 측정은 당시 SHA의 기록이다.
- 배치: PHASE 17 직렬화·Asset/Cook 경계와 PHASE 12.5 package gate 뒤, PHASE 23 MSI·Launcher 제품화 앞
- 초기 추정: **45 인일**. AU0 기준선과 device/backend 스파이크 뒤 갱신
- 확정 포맷: **WAV · MP3 · FLAC만 지원**. OGG/Vorbis와 그 밖의 포맷은 importer에서 명시적으로 거부
- 백엔드 결정: **miniaudio를 소스 벤더링한 첫 `IAudioBackend` 구현체로 채택**

관련 정본:

- [RefactoringPlanDashboard.html](../RefactoringPlanDashboard.html) — PHASE 22 진행 상태
- [Phase22AudioPreflight.md](../analysis/Phase22AudioPreflight.md) — 2026-09-16 사전 정찰(기준선 재측정·정정 목록·착수 판정)
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
3. `SoundComponent`는 GUID clip/preset/graph 참조와 generation이 있는 논리 `PlaybackHandle`을 보유한다. 하나의 재생 인스턴스가 하나 이상의 내부 `VoiceHandle`을 소유한다.
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
FMOD Studio 호환 authoring workflow는 범위가 아니다. 2026-10-05 합의로 CreatorEngine 고유 Sound Cue 수준 SoundGraph 저작·재생은 §12 범위에 포함한다.

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

> **2026-09-24 부분 착지 — §11.** WAV와 결정적 무음 MP3/FLAC fixture 생성기·실패 canary·게이트가 섰다
> (`Tools/regression/verify-audio-voice-contract.ps1`). **이 슬라이스의 판정문에서
> "유효 FMOD 항목 A/B" 를 참고로 강등한다** — 비교 대상이 없다(§11.1). 남은 것은
> 압축 포맷의 비무음 파형·impulse, 뒤쪽 프레임 손상·실제 대용량 입력, stream/장시간
> workload의 절대 성능 예산이다. 단일 장치·32보이스 Release 기준선은 §11.10,
> 생성 fixture와 9개 손상 입력 matrix는 §11.11, 뒤쪽 절단·resident 경계는 §11.12에 기록했다.

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

> **2026-09-24 부분 착지 — §11.** `wave` 값 타입·계약·보이스 표·Null backend와
> Host 소유 `AudioHost`가 섰다. 장치 실패 후 Null 전환과 100회 시작/종료·장치 복구 뒤 낡은 핸들 거부를
> 로컬 게이트에서 확인했다. 남은 것은 Scene-owned source/listener 등록부, 실제 Editor/Player
> Host 소유·틱 배선, component/scene transfer/DDOL 수명과 옛 표면 이행이다.

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

> **2026-09-24 부분 착지 — §11.6.** 벤더링(0.11.25 · MIT-0 · PROVENANCE)과 단일 구현 TU 규약,
> 실 device open/close, error translation, 적재 시 디코드 검증이 섰다. 결정적 무음 MP3/FLAC의
> 적재·재생과 절단 파일 거부도 로컬 게이트에 추가됐다. **남은 것:** 다양한 실제 인코딩과 손상 입력,
> authored stream/resident 정책, pak/VFS byte source, job-thread 수명. §11.12에서 현재 경로를
> 임시 resident decode로 명시했으며 실제 stream은 아직 없다. 제품 배선은 여전히 없다.
> `FmodBackend` 는 출하 뒤로 미뤘다(§11.6).

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
- AU3의 과거 미배선 경계는 2026-10-05 구현 승인으로 해제했다. AU7 제품 이행과 AU8 FMOD 제거까지 진행한다.
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

---

## 11. 착수 기록 (2026-09-16)

사전 정찰([Phase22AudioPreflight.md](../analysis/Phase22AudioPreflight.md)) 뒤 AU0·AU1 을 착수했다.
정찰이 정정한 전제는 §7 에 있고, 여기에는 **정찰 이후에 정한 것과 실제로 착지한 것**만 적는다.

### 11.1 AU0 의 판정문을 바꾼 이유

계획서는 AU0·AU4·AU9 세 곳에서 "유효 FMOD 항목과의 A/B" 를 판정 근거로 썼다. 실행으로 확인한
현재 상태는 이렇다.

- 로더가 스캔하는 `Sounds` 디렉터리가 저장소에 없어 `LoadSounds()` 가 한 번도 실행되지 않는다.
  그래서 클립 표가 영구 공집합이고 모든 재생이 첫 줄에서 끊긴다 — **비교 대상이 없다.**
- 반대로 **폴더와 파일만 있으면 경로 자체는 동작한다.** 게이트가 fixture 를 넣자 적재·재생·정지·
  리스너 왕복이 전부 통과했다. 정찰의 "소리를 못 낸다" 는 코드 결함이 아니라 자산 부재였다.
- 종료는 클립이 하나라도 적재됐을 때만 끝난다. 클립이 없으면 `_isSoundLoaderThreadRunning` 이
  내려갈 경로가 없어 소멸자가 영원히 돈다.

따라서 A/B 는 **참고 수치**로 강등하고, 판정은 합성 fixture 의 golden 기대값·실패 canary·절대 예산으로
만 세운다. 한 가지를 기록해 둔다 — **fixture 를 넣으면 종료 결함의 조건이 지워진다.** canary 를 처음
짤 때 실제로 밟았고(거짓 초록), 빈 자산 루트로 도는 회차를 따로 두어 잡았다.

### 11.2 배선을 파사드가 아니라 신규 작성으로 간다

`SoundManager`·`SoundSystem` 을 감싸지 않고 새로 쓴다. 근거 셋.

1. 결함이 표면이 아니라 내부에 있다 — 보이스 표가 없고, 상태를 백엔드에 물어보고, steal 이
   `getPosition` 으로 나이를 재고, 풀에 소유자가 없다. 감싸면 전부 남고 타입만 가려진다.
2. 파사드는 `playFromSourceBlended(const SoundComponent&)` 를 계속 불러야 해서 **끊으려던 역방향
   간선조차 안 끊긴다**(하위 계층이 상위 타입의 열 필드를 직접 읽는다).
3. 갈아끼울 접점이 작다 — `Sound->` 외부 호출은 7종 12곳이고, 오디오 소스 총량은 1,160줄이다.
   그중 실제 FMOD 호출 순서 지식은 50줄 남짓이라 그것만 backend 구현으로 옮긴다.

부수로 확인된 것 하나. probe 가 `SoundManager` 하나를 쓰려고 `SceneRuntime.lib` 를 링크하자
**유니티 blob 이 Physics·PhysX·GameInput 까지 끌고 왔다.** 오디오만 떼어 검증할 수 없다는 뜻이고,
새 계층을 자기 단위로 두어야 할 이유가 하나 더 늘었다.

### 11.3 확정한 이름과 경계

- 네임스페이스는 **`wave`**. `VoiceHandle` · `PlayRequest` · `BusId` 는 §3.1 이 세운 이름을 그대로 쓰되,
  네임스페이스와 겹쳐 더듬지 않게 접두사 `Audio` 는 뗐다. 약칭은 쓰지 않는다.
- **클립 참조는 AU2 소관으로 남긴다.** 지금은 `wave::ClipKey` 가 파일명 문자열을 한 겹 안에 가둔다.
  `AssetId` 로 가려면 importer·`RuntimeAssetType`·`CookedAssetKind`·`.meta` 오디오 필드·pak 범위
  read·C# identity 여섯 축이 함께 와야 하고(정찰 §3.2), 그것이 AU2 다. 저작 참조가 0건이라 미루는
  비용도 0이다. 래퍼가 이음매라 전환 시 바꿀 곳은 서비스 경계 한 곳과 컴포넌트 필드 하나다.
- **등록부 소유권 이전은 별도 축.** 이번에는 배선만 긋고 전역 `SoundSystem` 은 그대로 둔다. 다만
  §3.2 의 조건은 지킨다 — 전역을 새 오디오 정본으로 **승격하지 않는다**.
- **보이스 풀을 폐기한다.** `playOneShotPooled`·`configureVoicePool`·`clearVoicePool`·`poolKey` 는
  엔진이 표를 안 들어서 필요했던 우회다. 표와 cap 정책이 있으면 one-shot 은 손잡이를 보관하지 않는
  재생일 뿐이다.
- **클립 적재는 명시 호출**(`LoadClip`). 폴링 스레드·공회전 예외·종료 정지가 원인째 사라진다.

### 11.4 착지한 것

| 산출물 | 내용 |
|---|---|
| `Engine/SceneRuntime/Audio/AudioValues.h` | `VoiceHandle`(index+generation) · `BusId` · `ClipKey` · `RolloffKind` · `VoiceState` · `BackendVoiceId` |
| `Engine/SceneRuntime/Audio/PlayRequest.h` | 재생 입력 한 덩어리(리버브·소유자 포함) |
| `Engine/SceneRuntime/Audio/ListenerState.h` | 듣는 자 자세 |
| `Engine/SceneRuntime/Audio/AudioService.h` | 계약 12 메서드. vendor 토큰 0 |
| `Engine/SceneRuntime/Audio/VoiceTable.h/.cpp` | 고정 용량 슬롯 표, 세대 발급·낡은 핸들 거부 |
| `Engine/SceneRuntime/Audio/AudioBackend.h` | 장치·클립·보이스·버스·리스너만 아는 계약. 정책 없음 |
| `Engine/SceneRuntime/Audio/NullAudioBackend.h/.cpp` | 장치 없이 도는 결정적 구현. degrade 경로 겸 판정 경로 |
| `Engine/SceneRuntime/Audio/MiniaudioBackend.h/.cpp` | miniaudio 를 무는 유일한 TU. pimpl |
| `Engine/SceneRuntime/Audio/AudioRuntime.h/.cpp` | `AudioService` 구현. 백엔드를 참조로만 받는다 |
| `Engine/SceneRuntime/Audio/AudioHost.h/.cpp` | 장치 실패 시 Null 전환과 시작·틱·종료를 소유. 제품 미배선 |
| `Engine/SceneRuntime/Audio/ClipDirectory.h/.cpp` | 폴더 훑기 **동기 호출 한 번**. 빠진 것을 세어 돌려준다 |
| `ThirdParty/miniaudio/` | 0.11.25 · MIT-0 · PROVENANCE.md |
| `Tools/regression/verify-audio-voice-contract.ps1` | 로컬 게이트. **run-all 미배선** |
| `Tools/regression/audio_voice_contract_probe.cpp` | probe. fixture 를 스스로 생성한다 |

게이트 현황(2026-09-16 실측): FMOD 기능 8 + 보이스 표 17 + wave·Null 20 + wave·miniaudio 13 +
클립 훑기 13 = **71 단정 초록**. 변이는 세 벌로 확인했다 — 보이스 표·fixture 6종, 런타임·백엔드
5종(가드 제거 · 감쇠 미적용 · 일시정지 회수 · 종료 미회수 · 손상 파일 수용), 훑기·종료 4종
(ogg 수용 · 충돌 미검사 · 적재 실패 무시 · **옛 결함 모양 재현**)이 전부 잡힌다.
`wave` 는 아직 `SceneRuntime.vcxproj` 에 넣지 않았다 — 제품 소비자가 생기는 다음 슬라이스에서
함께 넣는다.

> 앞서 이 자리에 적었던 **56** 은 항목을 더한 값이고 실제로 센 값이 아니었다. 실측은 보이스 표가
> 17(15 가 아니라)이라 58 이었다. 위 숫자는 게이트 출력을 구획별로 센 것이다.

★ 변이 한 종이 처음에 빠져나갔다. 런타임의 `HasClip` 가드를 걷어도 `NullAudioBackend` 가 자기
가드로 대신 막아 결과가 같았다 — 단정이 *런타임이 막는다* 가 아니라 *둘 중 하나가 막는다* 를 재고
있었다. `StartVoiceAttempts()` 로 **요청이 백엔드에 닿았는지**를 세는 관측점을 더해 자리를 못 박았다.

### 11.5 운영 제약

- **오디오 게이트를 `run-all.ps1` 에 배선하지 않는다.** 실제 출력 장치를 요구하므로 무인 회귀 세트의
  전제와 맞지 않는다. 로컬에서 손으로 돌린다.
- **음원을 저장소에 커밋하지 않는다.** 예외 없다. `Dynamic_CPP/Assets/Sound/` 와 `Sounds/` 를 `.meta`
  까지 무시로 막았다(두 철자를 다 막은 이유는 저작 폴더가 `Sound/` 인데 로더가 도는 경로가 `Sounds/`
  라서다). 게이트 fixture 는 probe 가 `Build/` 아래에 무음 WAV/MP3/FLAC 으로 생성한다.
- AU2 가 착지해 `.meta` 가 오디오 identity 의 정본이 되면 이 무시 규칙을 다시 판단해야 한다 —
  "음원은 빼고 identity 만 추적" 이 필요해질 수 있다.

### 11.6 `FmodBackend` 는 출하 뒤로 미룬다 (2026-09-16 결정)

당초 11.6 은 `FmodBackend` 를 먼저 세워 옛 동작과 A/B 하는 것이었다. 사용자 결정으로 **구현을
출하 뒤 엔진 개발자 몫으로 남기고, 이번에는 인터페이스화와 miniaudio 도입만 간다.**

근거는 §11.1 과 같은 줄기다 — A/B 의 기준이 될 *유효한 FMOD 항목* 이 없다. 비교 대상이 없는데
비교용 백엔드를 먼저 짓는 것은 공수를 판정 없는 곳에 넣는 일이다. 대신 같은 인터페이스 뒤에
자리만 비워 둔다.

| 구멍 | 누가 |
|---|---|
| `FmodBackend` (`fmod.hpp` 를 무는 유일한 TU) | 출하 뒤 엔진 개발자. 계약은 `AudioBackend.h` 가 고정 |
| `spatialBlend` 중간값 (0<b<1) | AU5. 지금은 0/1 만 |
| 보이스 상한·도둑질 | 런타임 정책. AU4 |
| 리버브 센드 | AU6 |

★ 계획서 §제약 10("FMOD와 miniaudio를 동시에 shipping하지 않는다")은 그대로 산다. miniaudio 가
들어왔지만 제품 배선은 아직 FMOD 하나다 — 동시 shipping 이 아니라 **미배선 공존**이다.

### 11.7 종료 canary 가 초록으로 바뀌었다 (2026-09-16)

옛 표면을 걷어도 되는 근거가 이것이다. **같은 물음을 두 배선에 던져 답이 갈린다.**

| 배선 | 클립 있음 | 클립 없음(제품의 실제 상태) |
|---|---|---|
| `SoundManager::Destroy()` | 끝난다 | **끝나지 않는다** |
| `wave::AudioRuntime::Shutdown()` | 끝난다 | **끝난다** |

옛 배선이 클립 있을 때만 끝나는 이유는 §11.1 에 적었다 — 종료가 적재 스레드의 플래그를 기다리는데,
그 플래그를 내리는 유일한 경로가 `LoadSounds()` 의 끝이다. `wave` 에는 기다릴 스레드가 없다.
폴더를 훑는 일이 **동기 호출 한 번**(`LoadClipsFromDirectory`)이 되면서 원인이 사라졌다.

★ 게이트는 이 붉음을 **기대 모양으로 적는다.** 단순 실패로 두면 게이트가 영원히 붉어 아무도
판정에 못 쓴다. 옛 배선의 빈 상태는 멈추는 것이 맞고, *멈추지 않으면 그것도 알아야 한다* —
누가 고쳤거나 canary 가 결함 조건을 잃었거나 둘 중 하나이기 때문이다. 옛 표면을 걷을 때 이
canary 도 함께 걷는다.

★★ 이 canary 는 `SoundManager` 를 **한 번도 만들지 않는 프로세스**에서 돈다. 싱글톤을 건드리면
FMOD 적재 스레드가 서서, 멈췄을 때 원인이 wave 종료인지 옛 스레드인지 가릴 수 없다.

변이로 이빨을 확인했다. `Shutdown()` 에 **옛 결함과 같은 모양**(클립이 비면 돌지 않는 루프)을
심자 canary 가 붉어진다 — 회귀가 오면 잡힌다는 뜻이다.

### 11.8 소비자 전환 경계 (AU7)

§6의 의존 순서대로 AU2·AU3, AU4~AU6 기능 판정을 닫은 뒤 접점 12곳과 Inspector 채널
직결 5블록을 `wave` 로 옮기고 `SoundManager`/`SoundSystem` 을 걷는다. `wave` 의
`SceneRuntime.vcxproj` 등록과 FMOD canary 철거도 그 전환 슬라이스에서 함께 한다.
Host 소유 수명과 클립 적재 시점은 AU1에서 먼저 확정하되, AU7 전에는 제품 기본 backend를
바꾸지 않는다. FMOD와 miniaudio를 동시에 shipping하지 않는다는 §0의 제약을 지킨다.

### 11.9 2026-09-24 AU0·AU1 보강

- 게이트가 외부 encoder 없이 무음 MP3(48 MPEG-1 Layer III frame)와 FLAC(12개의
  4096-sample constant frame)을 `Build/Validation/AudioVoiceContract/Formats` 아래 생성한다.
  FLAC 프레임은 RFC 9639의 STREAMINFO·frame CRC 규칙으로 만든다. miniaudio의 적재·재생
  단정 4개가 Debug/Release에서 통과했다. 절단 MP3/FLAC의 거부도 Debug에서 확인했다.
  WAV/MP3/FLAC 및 절단 fixture는 재생성 전후 SHA-256
  일치를 게이트에서 검사한다. 음원 파일은 저장소에 추가하지 않았다.
- `AudioHost`는 backend와 `AudioRuntime` 인스턴스를 소유하며 장치 시작 실패의 부분 자원을
  정리한 뒤 Null로 내려간다. 런타임 인스턴스를 Host 수명 동안 유지해 재시작 시 보이스
  generation이 초기값으로 되돌아가지 않게 했다. Null과 실제 장치의 generation 영역도 갈라
  장치 복구 뒤 옛 Null 핸들이 새 보이스를 가리키지 않게 했다. Null backend는 Stop에서
  보이스·클립·버스 저장소를 회수한다. 실패 장치 주입으로 100회 시작·재생·틱·종료,
  복구 뒤 이전 핸들 거부와 부분 초기화 정리를 확인했다.
- 절단 FLAC은 STREAMINFO만 유효해도 종전 `LoadClip`의 sound 초기화에 성공했다. 실제 첫
  PCM 프레임을 읽는 검사를 추가해 거부하도록 고쳤다. 뒤쪽 stream 손상은 AU3/AU9의
  decode-error·underrun 진단으로 별도 판정한다.
- 이 변경은 로컬 게이트에만 컴파일된다. `SceneRuntime.vcxproj`·Editor·Player의 기본 FMOD
  경로는 바꾸지 않았다. AU1의 Scene 소유 등록부·제품 Host 배선과 AU0의 성능 예산은 여전히
  판정 전이다.

### 11.10 2026-09-24 AU0 실장치 성능 기준선

> 이 표는 §11.12의 명시적 resident decode 변경 전 기준선이다. 변경 뒤의 현재 Release
> 반복 측정은 §11.12에 따로 기록한다.

`MiniaudioBackend(true)`가 로컬 측정 실행에서만 장치 callback을 감싼다. callback 안에서는
믹서 처리 전후 시간과 프레임 수를 고정 크기 원자 histogram에 기록한다. 할당·로그·잠금은 없다.
일반 `MiniaudioBackend()`의 callback 경로는 그대로다. `wave-performance` 모드는 FMOD
싱글톤을 만들지 않지만, 계약 probe 실행 파일에 FMOD DLL이 링크돼 있으므로 프로세스 메모리를
miniaudio 단독 점유량으로 해석하지 않는다.

`verify-audio-voice-contract.ps1 -Configuration Release -PerformanceRuns 5`의 동일 PC 반복값
(`Build/Validation/audio-voice-performance-release-final-20260924.log`, 로컬 git 무시 산출물):

| 조건/지표 | 관측 5회 |
|---|---:|
| WASAPI Realtek 스피커, 출력 | 48 kHz · 2채널 · callback 480 frames(10 ms) · device buffer 1056 frames |
| workload | 생성한 0.6초 무음 WAV 32개 동시 loop · 회당 약 3초 · Update 실측 32.7~33.0 Hz |
| callback p99 | 0.45~0.52 ms (10 µs histogram bin의 상한) |
| callback 최대 | 0.478~1.022 ms |
| callback 반 주기/전체 주기 초과 | 각각 0/0 (회당 305~307 callbacks, 10 ms callback 기준) |
| 장치 Start / 클립 Load | 68.4~72.9 ms / 0.367~0.452 ms |
| 프로세스 CPU, 1코어 대비 / peak working set | 0.52~2.59% / 13.31~13.35 MB |
| Update p99 | 27.0~66.4 µs (회당 99 calls) |

이 수치는 **resident 무음 WAV 한 조건의 로컬 기준선**이다. callback 시간은 miniaudio mixer
호출을 감싸며 장치 변환/driver 시간을 포함하지 않는다. CPU와 working set은 FMOD가 링크된
probe 프로세스 전체 값이다. `callback_over_full=0`은 실행 중 hardware underrun 0의 증거가
아니다. stream/decode/reverb·128 voice·실제 게임 틱·다른 장치·장시간 실행은 아직 측정하지
않았다. 따라서 AU0 절대 CPU/메모리/시작 예산과 AU4/AU9 underrun 판정은 아직 정하지 않는다.

### 11.11 2026-09-24 AU0 파형·루프·손상 입력 matrix

로컬 게이트가 git 무시 `Build/Validation/AudioVoiceContract/Formats` 아래 다음 바이트를
매번 생성하고 재생성 SHA-256 일치를 확인한다.

- WAV: 48 kHz mono 16-bit PCM의 1 kHz sine(0.5 amplitude), 첫 sample만 24576인
  impulse, 무음, 480 Hz 24주기/2400-frame(50 ms) 짧은 loop. offline miniaudio decode로
  길이·대표 sample·loop 접합부 기울기를 값으로 검사했다.
- MP3/FLAC: 기존 결정적 무음 파일과 별도로 4 MPEG frame(약 104 ms) 및 1 FLAC frame
  (4096 sample, 약 85 ms)의 짧은 loop를 생성했다. offline 첫 128 PCM sample이 0인지
  확인하고, 실장치에서 세 포맷 모두 250 ms 뒤에도 looping voice가 살아 있는지 검사했다.
- WAV/MP3/FLAC 각각 corrupt·truncated·oversized-header 세 종류, 총 9개를 만든다.
  oversized fixture는 실제 큰 파일이 아니라 **작은 파일이 큰 길이를 선언**한다. 각 파일의
  `LoadClip` 거부, clip 표 부재, 파일명이 들어간 오류 문장을 검사한다. 별도 프로세스에
  10초 제한을 걸어 디코더 정지도 실패로 만든다. OGG의 확장자 거부는 기존 폴더 스캔
  단정이 계속 맡는다.

Debug/Release 로컬 게이트가 통과했다(`audio-voice-matrix-debug-verified-20260924.log`,
`audio-voice-matrix-release-final-20260924.log`; git 무시 산출물). FMOD 장치가 열렸는데
miniaudio 시작만 실패한 실행도 이제 검사 생략이 아닌 실패다. 이 matrix는 첫 프레임 또는
헤더가 망가진 입력을 다룬다. **정상 prefix 뒤쪽 손상·실제 대용량 파일·stream 중단은 아직
거부/복구 계약이 없다.** MP3/FLAC의 비무음 sine·impulse를 재현 가능하게 만드는 encoder
경로도 남았다. 따라서 AU0는 여전히 부분 진행이다.

### 11.12 2026-09-24 뒤쪽 절단·임시 resident 경계

`tail.wav/mp3/flac`은 정상 파일의 첫 프레임을 보존하고 끝만 자른다. 생성 SHA-256은
로컬 게이트에 포함했다. 정상 원본과 48 kHz mono offline 디코드 결과를 비교한 현재 결과:

| 파일 | 원본 PCM frames | 절단본 PCM frames | 절단본 decoder 보고 길이 | 현재 `LoadClip`/`Play` |
|---|---:|---:|---:|---|
| WAV | 4,800 | 2,400 | 2,400 | 성공/성공 |
| MP3 | 60,187 | 58,933 | 58,932 | 성공/성공 |
| FLAC | 49,152 | 45,056 | 49,152 | 성공/성공 |

MP3의 1-frame 차이는 44.1→48 kHz 디코더 길이 계산과 실제 반환 프레임의 차이로 관측됐고,
이 fixture의 원본 대비 손실 판정에는 영향을 주지 않는다. **세 포맷 모두 실제 PCM이 사라졌는데
backend가 적재와 보이스 시작을 허용한다.** 이것은 §11.11의 첫 프레임/헤더 손상 9건 통과와
별개인 알려진 실패다. `LoadClip`이 첫 PCM만 확인하는 데다 miniaudio의 동기 predecode도
불완전한 끝을 오류로 거부하지 않았다. 따라서 stream 오류·underrun 0을 주장하지 않는다.

소스 점검에서 `StartVoice`에 `MA_SOUND_FLAG_STREAM`이 없음을 확인했다. 종전 기본 flag 0은
인코딩된 파일을 메모리에 두고 mixer callback에서 디코드하는 경로였다. AU2의 저작 metadata가
아직 없으므로 지금은 `MA_SOUND_FLAG_DECODE`로 **모든 보이스를 임시 resident**로 명시해
재생 시작 전에 디코드한다. 첫 재생은 동기 디코드 비용을 낸다. `LoadClip`의 probe는 해제되므로
이 변경을 클립 사전 적재나 실제 stream 지원으로 세지 않는다.

fixture의 offline 디코드는 `WAVE_AUDIO_PROBE`에서만 `MiniaudioBackend.cpp`가 구현하는
벤더 중립 검증 훅으로 옮겼다. 게이트는 `miniaudio.h` include 소유자가 이 구현 TU 하나인지
검사한다. 공개 헤더와 probe TU에는 벤더 헤더가 없다.

변경 뒤 동일 WASAPI 장치(48 kHz/2채널, 480-frame callback)의 Release 5회·32 looping WAV·
회당 3초 실측: callback p99 상한 **0.30~0.36 ms**, 반 주기 초과 **0회**, 장치 Start
**67.1~77.7 ms**, `LoadClip` **0.381~0.407 ms**, 첫 `Play` **0.144~0.162 ms**,
32개 `Play` 합계 **0.314~0.486 ms**. 이 짧은 resident WAV 표본만으로 종전 기준선 대비
개선률이나 제품 예산을 확정하지 않는다. 로그는
`Build/Validation/audio-tail-resident-release-final-20260924.log`와
`audio-tail-resident-debug-final-20260924.log`(git 무시 산출물)다.

뒤쪽 손상 fail-closed는 AU2의 포맷별 원본 검증·cooked metadata와 AU3의 stream decode
오류·job-thread 수명 계약에서 닫아야 한다. 실제 stream read/underrun 측정은 그 경로가 생긴
뒤에만 가능하다.

### 11.13 2026-09-24 AU2 자산 등록 포맷 경계

Editor Asset DB의 등록 확장자와 asset GUID 계약 게이트를 WAV/MP3/FLAC으로 맞췄다.
OGG는 신규 `.meta` 생성에서 거부하며, 기존 OGG `.meta`가 watcher 알림으로 들어와도
Editor DB가 catalog에 등록하지 않는다. 기존 FMOD `SoundManager`의 제품 적재 경로는
AU7 전환 전까지 그대로이며, 일반 `DataSystem` 부팅 catalog와 packer의 전체 Assets
복사는 아직 AU2의 오디오 정책을 적용하지 않는다. 따라서 이 단계는 **Editor DB의 진입
경계**만 닫았고, 패키지 OGG 거부나 AudioClip cook 완료로 세지 않는다.

Debug x64 `CreatorEditor.vcxproj` 전체 빌드는 통과했다. `verify-asset-guid-contract.ps1`은
170개 `.meta`를 파싱했으나 Strict 판정은 이 변경과 무관한 기존
`Dynamic_CPP/Assets/Prefabs/ImmProbe.prefab.meta`의 tracked/ignore 충돌 1건으로 실패했다.
비 Strict 실행은 종료 코드 0이지만 `identityReady=false`라 AU2 완료 근거로 쓰지 않는다.

### 11.14 2026-09-24 AU2 오디오 source stamp와 import 설정

Editor의 `.meta` 생성은 지원 오디오에 한해 `audioClip` schema 1 블록을 기록한다.
`loadMode=Auto`, `spatialKind=NonSpatial`이 신규 기본값이며, 재저작 때는 기존의
`Auto|Resident|Stream`, `PointMono|NonSpatial` 값을 보존하고 잘못된 값은 거부한다.
identity는 현재 일반 자산 계약의 canonical UUIDv4 `guid`를 사용한다. source에서
생성하는 `codec`, `payloadSize`, `sourceContentHash`(SHA-256)는 저작 설정과 분리해
매번 다시 계산한다. 파일 전체를 64 KiB 단위로 읽으며 크기·수정 시각이 읽는 동안
바뀌면 등록하지 않는다. 확장자별 최소 시그니처와 빈 파일도 거부한다.

이 source stamp는 **처음부터 완전한 encoded stream임을 증명하지 않는다**. 특히 정상
prefix 뒤쪽 절단은 원본과 다른 크기·해시로 관측되지만, 최초 import만으로 원래
바이트를 알 수 없으므로 여전히 `audioClip` meta가 만들어질 수 있다. 다음 cook 단계는
저장된 stamp와 source를 대조하고 전체 프레임·channels·sampleRate·frameCount를
검증해야 한다. 현재 `wave` 런타임은 아직 이 meta를 읽지 않는다.

생성 WAV/MP3/FLAC 세 포맷, 뒤쪽 절단의 stamp 변화, 손상 시그니처·OGG 거부를
기존 offline fixture probe에 추가했다. 첫 실행에서 8바이트 FLAC이 시그니처만으로
통과하는 것을 검출해 STREAMINFO 최소 길이·형태 검사를 보강했고, 재실행한 Debug
로컬 오디오 게이트 전체가 통과했다(`Build/Validation/audio-meta-debug-final-20260924.log`,
git 무시 산출물).

### 11.15 2026-09-24 AU2 cook 입력 봉쇄

`AssetCooker`의 source identity table 작성 단계에서 Assets 루트 전체의 오디오
입력을 확인한다. WAV/MP3/FLAC은 `.meta`가 반드시 있어야 하며, 저장된 schema 1의
`loadMode`·`spatialKind`·`codec`·`payloadSize`·`sourceContentHash`를 현재 원본과
대조한다. 같은 크기의 바이트 변경도 SHA-256 불일치로 실패한다. OGG 파일은 `.meta`
유무와 관계없이 거부한다. 이 검사는 staging 디렉터리를 만들기 전이므로 실패한
입력이 부분 cook 출력으로 게시되지 않는다.

`verify-audio-cook-stamp.ps1`은 git 무시 위치에 만든 작은 WAV와 기존 texture로
AssetCooker를 실제 실행했다. 정상 2회 manifest digest 일치, 같은 크기의 원본 변경,
잘못된 import 설정, OGG, 누락된 audio sidecar의 거부와 출력 부재가 통과했다.
Debug x64 AssetCooker·Editor 빌드도 통과했다. 기존 범용
`verify-experiment-asset-cooker.ps1`은 저장소의
`Library/ModelAssetGenerations/8a69bd42-f950-8265-9c0e-0ff597095141/8`
부재로 조기에 중단돼 이번 변경의 회귀 판정에는 사용하지 못했다.

이 시점에는 audio cooked artifact/manifest entry가 없었다. packer가 원본 Assets를
복사하므로 cook 이후 pack 전 바이트 변경까지 이 stamp가 보호하지 않는다.
처음부터 절단된 원본은 동일한 절단본으로 stamp를 만들 수 있으므로, 전체 encoded
frame 검증도 별도 작업으로 남았다. 당시 miniaudio 디코더는 SceneRuntime의
`MiniaudioBackend.cpp` 한 TU에 있고 AssetCooker는 RenderEngine·Utility만 링크했다.
이어 오프라인 decode 경계와 WAV/MP3/FLAC의 컨테이너 끝 조건을 검사한 뒤
오디오 artifact와 bounded payload range를 게시해야 한다.

### 11.16 2026-09-24 AU2 오프라인 encoded stream 검증

`AssetCooker`에 독립적인 miniaudio 구현 TU를 추가해 cook 입력 오디오를 끝까지
디코드한다. 장치·엔진·resource manager·threading 기능은 이 도구에서 끈다.
WAV는 RIFF 선언 크기와 실제 파일 길이를, FLAC은 STREAMINFO의 총 sample 수와
실제 디코드 PCM frame 수를 대조한다. MP3는 ID3v2/ID3v1 경계를 제외한 MPEG
Layer III frame chain이 파일 끝까지 정확히 이어지는지 확인한다. 디코더가 보고한
채널은 mono/stereo, sample rate는 양수여야 하며 `PointMono` 설정에는 mono만
허용한다. 디코드 도중 원본이 바뀌지 않았는지도 마지막 크기·SHA-256 재검사로
확인한다. 어느 검사든 실패하면 staging 생성 전에 cook을 중단한다.

전용 `verify-audio-cook-stamp.ps1`의 Debug·Release 실행에서 정상 WAV/MP3/FLAC,
각 포맷의 뒤쪽 절단본 3건(절단본에 맞춰 `.meta`를 재작성), 손상·절단·과장 길이
fixture 9건, stereo의 `NonSpatial` 허용과 `PointMono` 거부가 통과했다.
Debug·Release x64 `AssetCooker.vcxproj` 빌드도 통과했다. 이 검증은 MP3의
free-format 및 임의 확장 tag나 FLAC의 알 수 없는 총 sample 수 같은 입력을
보수적으로 거부할 수 있고, CRC가 없는 MP3 frame 내부의 모든 음질 손상을
증명하지 않는다.

이 시점에는 오디오 cooked artifact/manifest entry, bounded payload range,
AudioClipId·VFS 연결이 없었다. packer의 원본 Assets 복사 경로와 runtime stream
read 오류·underrun 대응도 별도 잔여 작업이다.

### 11.17 2026-09-24 AU2 오디오 artifact와 CEMF 게시

AssetCooker는 검증한 모든 WAV/MP3/FLAC을 GUID 경로
`Derived/Audio/<첫 두 자리>/<guid>.ceac`로 자동 게시한다. CEAC v1의 72바이트
헤더는 codec, Auto/Resident/Stream, PointMono/NonSpatial, 채널, sample rate,
PCM frame 수, encoded payload의 64비트 offset·길이와 payload SHA-256을 담는다.
payload는 원본 encoded 바이트 그대로이며, CEMF v2에는 `AudioClip` kind 7,
format version 1, 전체 artifact의 크기·SHA-256·경로를 기록한다. CEMF 버전은
바꾸지 않고 기존 entry 레이아웃을 사용한다. 명시적인 model/texture 인자가 없어도
오디오만 있는 Assets root를 cook할 수 있으며, 게시할 cooked asset이 전혀 없으면
실패한다.

검증 후 artifact 준비와 staging 기록 모두 64 KiB 단위로 source를 읽으면서 저장된
크기·해시를 다시 대조한다. staging 헤더를 재판독하고 최종 폐포 스윕에서 모든
artifact의 크기·해시를 stream 방식으로 확인한 뒤에만 출력 디렉터리를 원자적으로
게시한다. 전용 게이트는 정상 WAV/MP3/FLAC의 header range, payload byte equality,
CEMF kind/version/path/size/hash와 두 번 cook한 manifest digest 일치를 확인한다.
손상·절단·OGG·누락/불일치 sidecar는 출력이 없음을 확인했다.

이 시점에는 CEAC의 bounded range를 pak/VFS byte source로 열거나 backend에
전달하는 제품 소비자가 없었다. packer가 원본 Assets를 복사하는 기존 경로도 남아 있으므로
pack 이후 파일 변경 방지, AudioClipId 저작 참조와 package의 missing-reference
fail-closed는 이어서 연결해야 한다.

### 11.18 2026-09-24 AU2 cooked audio byte source

`Pak::Archive`에 virtual entry의 `sizeOf`와 `readRange`를 추가했다. 호출 범위를
entry의 uncompressed 길이에 대조하고 겹치는 chunk만 읽어 압축 해제한다. 읽기
chunk는 최대 4 MiB로 제한하며, 암호화된 pak에서도 앞선 chunk의 CTR 소비량을
반영한다. 기존 AES key handle이 초기화 직후 해제되는 작업 버퍼를 참조하던
수명 오류도 고쳤다. `contains`·`readAll`·range 조회는 path hash 뒤 실제
virtual path까지 대조한다.

`CookedAssetCatalog::OpenAudioClip`은 GUID로 CEMF AudioClip entry를 찾고,
loose cooked tree 또는 pak byte source에서 CEAC의 크기·헤더·전체 artifact
SHA-256·payload SHA-256을 64 KiB 단위로 확인한다. 성공한 source의
`ReadPayload`는 CEAC 헤더의 offset·길이를 벗어난 읽기를 거부한다. 소스가
reader를 공유 소유하므로 반환한 범위 핸들의 reader 수명이 보존된다.

`verify-audio-cooked-byte-source.ps1`은 실제 AssetPacker가 게시한 pak과
loose tree에서 4개 클립(WAV 2·MP3·FLAC)을 열어 source bytes와 대조했다.
97바이트 encrypted pak chunk 경계, 범위 밖 요청, 없는 GUID와 변조된 CEAC
거부를 확인했다. pak의 legacy `readAll`도 같은 암호화 fixture로 대조했다.

현재 pak reader는 range 호출마다 pak 파일을 다시 열고, clip open 시 전체
artifact를 해시한다. 이는 bounded 메모리와 무결성 경계이며 realtime stream
job/캐시의 성능 계약은 아니다. open 이후 파일이 바뀌지 않도록 mount를 고정하는
수명 계약도 아직 필요하다. 제품 `AudioBackend::LoadClip`은 아직 원본 파일
경로를 받으므로 이 source를 재생에 연결하는 일, AudioClipId 이관, stream
decode·cancel/drain과 packer의 원본 audio 제외는 남아 있다.

### 11.19 2026-09-24 AU2 cooked resident 재생 경로

`CookedAudioClipSource`가 검증한 manifest GUID를 보존한다. `AudioService`와
`AudioRuntime`은 이 source를 `ClipKey::FromGuid`로 등록하며, GUID 키와 동일한
철자의 legacy filename 키는 다른 값으로 취급한다. 저작 씬 필드의 GUID 이관은
아직 진행하지 않았다. `MiniaudioBackend`는 bounded CEAC payload를 메모리로
읽고 SHA-256을 다시 확인한 뒤, metadata와 동일한 채널·sample rate·frame 수의
PCM을 완전히 디코드해 적재한다. 보이스마다 독립된 `ma_audio_buffer`를 만들고
공유 PCM을 소유하여 재생 중 언로드해도 callback이 해제된 메모리를 읽지 않는다.
재생은 원본 파일 경로나 임시 추출 파일을 거치지 않는다.

이 단계에서 `Auto`와 `Resident`는 decoded PCM과 encoded payload 각각 64 MiB
이내일 때만 resident로 받는다. `Stream`은 작업자, 취소, seek/loop와 종료 시
drain 계약이 아직 없어서 명시적으로 거부한다. 따라서 큰 `Auto` clip의 stream
선택, source mount의 파일 identity 고정, 장시간 stream underflow 진단은 남아
있다. resident는 적재할 때 해시를 다시 확인하므로 open 뒤 파일이 바뀌면
거부하고, 적재 뒤에는 PCM snapshot을 재생한다. 기존 Editor/Player FMOD
제품 경로는 AU7 전까지 그대로다.

`verify-audio-cooked-byte-source.ps1`의 Debug·Release x64 실행에서 WAV 2개,
MP3, FLAC 네 클립 모두 pak byte source에서 device 재생·언로드를 통과했다.
loose/pak/encrypted range와 변조 거부 외에 open 뒤 payload 변경 거부,
`Stream`의 resident 오인 적재 거부도 확인했다. `RenderEngine`과 `AssetCooker`
Debug·Release 빌드를 다시 수행했다. Windows에서 staging 최종 rename이
간헐적으로 access denied를 반환해, 출력 경로가 여전히 없고 오류가
permission denied인 경우에만 제한된 재시도를 추가했다.

### 11.20 2026-09-24 AU2 pak mount 읽기 수명

stream worker가 CEAC를 읽기 전에 pak의 검증·읽기 대상이 같은 파일이어야 한다.
종전 `Pak::Archive`는 인덱스를 읽은 뒤 파일을 닫고 `readRange`와 `readAll`
호출마다 경로로 다시 열었다. 이제 Archive가 읽기 핸들을 수명 동안 보유하고
writer 공유를 거부한다. 인덱스와 모든 payload 읽기가 그 핸들을 사용하며,
동시 range 요청은 파일 커서 접근을 직렬화한다. `AssetPacker`는 후보 pak의
index 검증 핸들을 닫은 뒤 파일을 최종 이름으로 게시한다.

Debug·Release x64 AssetPacker 빌드와 `verify-audio-cooked-byte-source.ps1`
네 클립 게이트가 통과했다. 암호화 pak에서 겹치는 range를 세 스레드가
반복해도 동일한 바이트를 읽었고, mount 동안 파일 writer 열기는 거부됐다.
`verify-pak-source-exclusion.ps1`도 통과했다. 이 변경은 pak mount의 파일
수명을 고정한다. loose cooked tree reader는 여전히 경로 기반이며, 실제
stream decode 작업자·취소·drain과 `Auto`의 cooked resident/stream 결정은
후속 단계다.


## 12. 2026-10-05 소비 설계 확정·클라우드 분리 검증·제품 이행

### 12.1 승인된 범위와 두 소비 경로

간단한 음원도 그래프를 만들어야 하는 설계는 채택하지 않는다. 다음 둘은 같은 수명·믹스 정책을 공유한다.

1. **AudioClip / 선택적 SoundPreset 직접 재생**: GUID clip과 bus·gain·pitch·loop·spatial·attenuation·concurrency 기본값을 소비한다. Preset은 clip 또는 graph를 참조할 수 있다.
2. **첫 클래스 SoundGraph**: `.soundgraph` 저작 에셋과 GUID `.meta`, 전용 편집·검증·미리듣기, immutable compile 결과와 cooked artifact를 갖춘다. Clip·Random·Switch·Layer·Gain/Pitch·Parameter·Output과 typed initial parameters가 첫 범위다.

SoundGraph는 Sound Cue 수준의 재생 조합이다. oscillator, sample-DSP graph, sample-accurate trigger, MetaSound 호환, 여러 audible world의 독립 DSP mix와 split-listener는 이번 완료 주장에 포함하지 않는다. 기존 Lattice의 범용 canvas 기반만 재사용하며 Material IR이나 PR #119/#120 변경을 끌어오지 않는다.

### 12.2 소비 계약과 수명

- Scene 소유의 thin `SoundComponent`가 clip/preset/graph를 같은 `PlaybackRequest`로 낸다. component에는 backend 포인터가 없다.
- Host가 AudioHost·PlaybackService·AudioCatalog를 소유하고 Scene마다 SoundSystem 등록부와 World scope를 바인딩한다. 새 process-global singleton은 추가하지 않는다.
- `Play2D`, `PlayAt`, `PlayAttached`는 world/session/editor-preview scope를 명시한다. BGM의 scene 간 지속은 명시적 Session scope로만 요청한다.
- 공개 `PlaybackHandle(index,generation)`은 **Play 요청 한 번**을 가리킨다. `PlaybackInstance`가 자식 VoiceHandle들을 소유한다. emitter 기본 재생 handle과 fire-and-forget one-shot은 분리한다.
- graph 정의/프로그램은 공유 immutable data이고 random seed·typed parameter·선택된 branch·자식 voice는 인스턴스별 상태다. graph compile은 clip 누락, 순환, 출력, pin/parameter 타입, 최대 voice 수를 검증한다.
- owner 파괴 기본값은 전체 child stop. 명시적 DetachAndFinish는 현재 위치에서 loop를 끄고 scope 안에서 tail을 마친다. Scene 종료는 detached tail도 정리한다. managed GC는 voice 수명의 소유자가 아니다.
- scope 종료·slot 재사용은 generation을 바꾼다. scene/session이 끝난 핸들은 다음 세션의 재생을 건드리지 못한다. generation overflow slot은 재사용하지 않는다.
- play별 stop/pause/resume/gain/pitch/state/typed parameter를 제공한다. gain parameter 변경은 기존 playhead를 유지하고 Switch 변경은 바뀐 branch만 교체한다. random은 parameter 변경마다 재추첨하지 않는다.
- owner/game thread가 자산·그래프를 평가한다. worker/Inspector 입력은 bounded value mailbox로 전달하고 완료/queued-play 결과는 game thread에서 가져간다. callback은 Entity/C#/파일 경로/게임 객체를 참조하지 않는다.
- AudioListenerComponent는 Camera와 독립이다. v1은 하나의 audible world에서 explicit active listener 하나를 쓴다. legacy primary-camera fallback은 진단을 내며 이행 편의 경로임을 드러낸다.
- Inspector와 C#의 변경은 다음 owner tick에 동일한 full-settings 경로로 살아 있는 재생에 반영한다.

### 12.3 자산·마이그레이션·배포

- 기존 `clipKey` serialized field는 읽되 GUID가 정본이다. legacy basename은 catalog에서 유일한 경우만 GUID로 이관하며 동명 충돌·미해결 참조는 오류로 막는다. 첫 파일 선택이나 다른 폴더로의 묵시적 retarget은 하지 않는다.
- `sourceKind`, `soundPresetKey`, `soundGraphKey`로 소비 대상을 명시한다. graph/preset cook dependency는 scene/prefab 참조부터 추적한다. cooked runtime은 원본 authoring path 대신 CEMF와 bounded byte source를 소비한다.
- EditorAssetDatabase의 기존 파일 감시가 revision을 게시한 뒤 game thread에서 catalog를 갱신한다. 오디오 전용 폴링/detached loader thread는 없다.
- bus는 Master/BGM/SFX/Player/Monster/UI와 named Room send를 공유한다. bus cap과 concurrency group cap은 다른 정책이다. 우선순위·steal·virtualization·attenuation·stream cancellation/drain은 저수준 Runtime/Backend가 책임진다.
- `reverbIndex`는 legacy migration 입력이며 Room/Hall 같은 새 preset과 named send로 투영한다. 실제 wet/dry impulse gate 없이 청각적 품질을 검증했다고 표시하지 않는다.
- SoundManager와 raw SDK 소비를 철거하고 제품 vcxproj·link·runtime deployment·regression probe에서 FMOD를 제거한다. ThirdParty/Fmod 헤더도 제거한다. runtime/배포/package gate는 FMOD DLL 및 miniaudio runtime DLL 유입을 거부하고 miniaudio license/provenance를 묶는다.

### 12.4 분리 테스트의 경계

검증은 사용자의 PC나 진행 중인 다른 PR 작업 공간에서 수행하지 않는다. 최신 master `86f7efd30314de4dd19e3dbb30da9f820506e3dd`를 고정한 클라우드 Linux 복사본에서 기준선을 만들고, 새 구현 branch의 실제 source로 다시 실행한다. 기존 PR #119/#120 충돌 작업은 대기 상태를 유지한다.

기준선은 846개 distinct assertions(core 43, PCM decode 11, miniaudio Null-device 59, cook validation 20, CEAC/CEMF 670, cooked resident 43)를 통과했다. core/decode/Null 113개는 Debug·Release·ASan/UBSan으로 반복했다. 기준선의 Linux portability adaptation 사본 통과를 원본 또는 새 branch 통과로 바꿔 쓰지 않는다. LeakSanitizer는 executor ptrace 제약으로 실행 불가였고 성공한 ASan/UBSan 실행은 leak detection을 명시적으로 껐다.

새 branch의 반복 가능한 gate는 repository의 portable script와 Windows wrapper로 제공한다. 수정 뒤 같은 gate를 다시 실행하고 명령·compiler·source hash·assertion counts·실패 canary·관찰치를 보존한다. 핵심 항목은 다음과 같다.

- 실제 vendored decoder WAV/MP3/FLAC, 손상·tail truncation·unsupported format
- GUID/cooked bounds·digest·잘못된 참조, 동일 basename 충돌
- VoiceHandle/PlaybackHandle/scope generation, owner/detach/session/preview cleanup
- graph validate/typed params/random independence/layer/branch 변경, one-shot 분리
- bus/group caps·steal·virtualization·playhead, bounded queue/shutdown
- no-device PCM rendering, stream EOF/loop/seek/cancel/drain, reverb dry/wet impulse
- bounded repeated lifecycle/soak와 generation/missing-clip 방어를 깨는 negative canary

**장치가 없는 결과를 device pass로 세지 않는다.** 클라우드 `/dev/snd`가 없으므로 Null backend·miniaudio Null/no-device mixer·offline decoder만 실행 가능하다. Windows WASAPI 실제 출력, default-device/loss 20회, Editor Play 100회 실 UI, Windows MSVC Editor/Player 전체 build, 실제 pak/package smoke, PE import 폐쇄와 30분 hardware stream·callback p99/제품 CPU budget은 별도 **PENDING**이다. 짧은 Linux soak나 파일 decode 시간으로 이를 대체하지 않는다.

### 12.5 v1의 명시적 경계

- CEAC v1은 **클립 전체 loop**를 지원한다. 이 포맷에 저장되지 않는 `loopStartFrame` / `loopEndFrame`을 입력하면 Editor·catalog·cook 모두 오류를 낸다. 부분 loop 구간을 조용히 버리지 않는다.
- 기존 scene/prefab의 legacy basename은 cook에서 SoundComponent identity를 확인한 뒤 유일한 GUID로 이관한다. Player에서 스크립트가 새로 전달하는 동적 basename은 지원하지 않는다. 스크립트는 `AudioAssetId` 또는 GUID를 사용하며, 잘못된 값은 실패 진단을 확인한다.
- SoundGraph v1 편집기는 typed property/입력 연결 picker와 topology overview를 제공한다. 범용 Lattice/Material IR 변환 계층을 만들지 않았으며 드래그 기반의 완성형 DSP canvas·sample-accurate graph가 아니다. overview는 64 node로 그리기 비용을 제한하지만 모든 node는 속성 편집에서 접근할 수 있다.
- bus cap은 Physical 상태의 VoiceHandle 수, concurrency group cap은 해당 그룹의 전체 VoiceHandle 수를 센다. 하나의 layered PlaybackInstance가 여러 voice 예산을 쓸 수 있다. public PlaybackHandle과 backend source pair 수를 cap 단위로 혼동하지 않는다.
- callback histogram·runtime update·active/physical/virtual·stolen/dropped/rejected·stream read byte/failure 관찰과 실제 underrun/decode CPU 측정은 구별한다. 이번 구현에서 직접 수집하지 않는 수치를 0으로 보고하지 않는다.
- 장치 재개방 실패는 동일 graph/resource/voice를 유지한 degraded output에서 1/2/4/5초 retry로 처리한다. 초기 장치 개방만 실패한 경우도 같은 경로를 쓴다. graph/resource 초기화 실패는 true Null fallback이며, 이 경우도 알려진 길이의 one-shot은 host 시간으로 끝난다.

### 12.6 구현과 acceptance 표시

구현 착지와 제품 acceptance는 별개다. AU1~AU8 소스 구현/portable test가 진행되어도 Windows·hardware 조건이 남아 있으면 AU9와 PHASE 22 최종 완료를 표시하지 않는다. Draft PR에는 구현 목록, 실행한 테스트, 실패/미실행 항목을 분리해 적는다. 이 문서와 dashboard는 최신 branch 증거로 갱신하고 역사적 §11 측정은 보존한다.


### 12.7 이번 구현의 검증 정산

- 최종 클라우드 결과: **Debug / Release / ASan+UBSan 각각 3,825 / 3,825 단정 통과**. 구성 반복을 서로 다른 시나리오로 합산하지 않는다. 실제 파일·명령·수치·제외 항목은 [Phase22AudioCloudValidation](../analysis/Phase22AudioCloudValidation.md)에 기록했다.
- 처음에는 새 출력 디렉터리에서 object 재사용 없이 각각 3,817 단정을 통과했다. 마지막 UTF-8 별칭 수정은 compiler `-MM`으로 영향받는 TU 하나를 증명하고 세 구성 모두 재컴파일·재링크했다(해당 cook 단정 30→38). [최종 source ledger](../../Tools/regression/audio/evidence/source-sha256.json)와 [dependency proof](../../Tools/regression/audio/evidence/targeted-recheck.json)가 코드 identity를 고정한다.
- generation/missing-clip/graph-voice-bound negative canary 3개가 컴파일 후 단정 실패로 검출됐다. fixture 25개는 두 번 새로 생성한 결과가 바이트 단위로 같다. 실제 PCM에서 blend·distance·좌우 handedness·reverb dry/wet를 검사했다.
- 별도 consumer gate는 실제 SoundComponent/SoundSystem/Listener/Playback/Graph와 추출한 native binding을 컴파일하되 Scene/Entity 의존은 stub이다. API 33의 native/managed table 187개 field와 audio binding 31개를 대조했다. C# 실행이나 실제 Windows Scene 통합 통과로 세지 않는다.
- `verify-audio-retirement.py`는 source/project/deploy closure 1,732개 점검에서 실패 0. 새 Audio TU의 누락·중복 등록도 검사한다. `git diff --check`, 수정 project XML parsing, dashboard JavaScript syntax, miniaudio upstream blob 3종 대조도 통과했다.
- 실제 장치가 없는 클라우드에서 동작한 세 경로(logical Null / miniaudio software device / no-device PCM)와 native Windows·하드웨어·실제 Pak·패키지·C# 제품·LeakSanitizer 미실행을 구분한다. **AU9 및 PHASE 22 최종 acceptance는 완료 처리하지 않는다.**

### 12.8 Windows 제품 acceptance 후속 — 2026-10-07

VS 2026(v18) MSVC v145 x64로 Windows 독립 core/decoder/PCM/software-device
Debug·Release 각각 240/240, 실제 WASAPI 30분 resident 부하 307/307,
장치 재개방 20회 및 runtime 시작·종료 100회가 통과했다. 사용자 요청으로 기본
출력을 AirPods Max로 변경하고 별도 실제 WASAPI 실행도 307/307을 통과했다.
실제 WAV/MP3/FLAC loose/Pak/암호화 cross-chunk gate는 Debug에서 통과했고,
Windows loose mount의 파일 쓰기 차단과 수명 종료 후 쓰기 복구를 보강했다.

2026-10-08 추가 정산: 실제 Dynamic_CPP Editor의 기존 GUID-only WAV 메타데이터를
GUID 보존 방식으로 갱신했다. Windows 캐시 정리 함수의 매크로 재귀와 편집 모드에서
native OnBeginSimulation을 조기 소비하던 결함을 수정했다. VS18 Editor Debug/Release
build가 통과했고, Debug 실제 오디오 재생·정리 100회 및 Release 실제 smoke 1회,
사용자 AirPods Max 청각 확인, 동일 재생 핸들 유지한 기본 출력 변경 20회가 통과했다.
실제 Bluetooth 분리·재연결 1회는 사용자가 자동 출력 복구를 확인했다.

장치 재개방 20회, 기본 출력 변경 20회, 물리 재연결 1회는 서로 다른 증거다.
MP3 stream 30분 167/167, Debug/Release 실제 Pak gate, Release PE 67 images도
통과했다. 별도 Phase22Player 폴더는 현재 존재하지 않는다. 제품 성능 예산과 소비자
수명 검증은 아래 추가 결과로 정산했으나 package 전체 및 잔여 종료 조건은 닫지 않았다. 따라서 PHASE22 전체 완료로
표시하지 않는다. [현재 Windows 검증 보고서](../analysis/Phase22AudioWindowsAcceptance20261007.md)의
수치·미실행 경계를 완료 판정의 근거로 삼는다. 콜백 p99가 통과하더라도 관찰된 최대
지연과 한 주기 초과 횟수를 숨기지 않고 실제 hardware underrun으로 오인하지 않는다.

### 12.9 승인 예산 및 소비자 종료 정산 — 2026-10-08

사용자가 Release 128 voice 기준으로 update p99 <= 1ms, 부하로 증가한 private
memory <= 256MiB, 첫 재생 API 응답 <= 500ms를 승인했다. 실제 현재 프로젝트
Editor의 resident/stream 혼합 0/1/32/128 voice·reverb on/off를 30분 실행해
p99 상한 0.284ms는 전체 혼합 관찰치이고, 추가 메모리 73.46MiB 및 첫 재생 API 응답
135.15ms는 승인 기준을 통과했다. 별도 물리 128 voice 구간 212,798프레임 중
1ms 초과 33회(0.01551%)로 128 voice p99 <= 1ms도 확인했다.
첫 응답은 HTTP queue/polling을 포함하며 DAC 첫 샘플 지연은 아니다.
callback p99=3.60ms, 최대=13.764ms, 반 주기 초과=10회, read failures=0이다.
실제 driver underrun은 미확정이다. ETW event 41 두 건은 측정 시작 전 audiodg에서
발생했고 부하 구간에는 관찰되지 않았으나 전체 eventsLost=1,516이므로 0 판정으로 세지 않는다.

AU5는 deterministic PCM/voice 정책 회귀와 실제 128 physical voice 및 PointMono
호출, AU6는 deterministic dry/wet RMS·decay capture로 완료했다. AU7은 실제
Release Play/Stop 100회, C# playback/completion 100회 및 GC/scope 호출,
Scene/DDOL SoundComponent·component 제거·hot reload·SoundGraph presentation
owner 저장/재열기/preview와 warm 후 최종 thread/handle 증가 0으로 완료했다.
SoundGraph probe를 실제 마우스 클릭 검증으로 확대하지 않는다.

VS18 Debug/Release full solution, Release PE 67 images와 engine publication은
통과했다. Editor는 새 IntDir로 LNK4020=0을 확인했으며 MSB8028 경고가 남는다.
retirement gate는 현재 source/binary 2,385점검 실패 0이다.
현재 Dynamic_CPP package는 기존 OGG 자원 1개를 cook에서 명시 거부해 실패했고,
2026-10-08 사용자 지시로 원본·meta를 Library에 보관하고 GUID 유지 WAV로 변환했다. decoded PCM hash 동일, Assets 내 OGG 0개이며 clean engine 오디오 cook 29 clips PASS. 전체 package는 재질·씬의 미해결 dependency GUID 12개로 cook exit 4이다. 독립 clean snapshot
`b443aff5`의 VS18 Release full build·현재 프로젝트 Editor smoke·PE 53 images 및
engine publication도 통과했지만 clean Game package/Player smoke는 아직 미완료다.
실제 Release Editor 프로세스 시작·재생·정리·종료 100/100도 통과했다.
Player 종료 100회는 아직 실행하지 않았다. AU4/AU8/AU9의 이 잔여 조건을 닫기 전까지
전체 페이즈를 완료로 표시하지 않는다.

### 12.10 2026-10-08 선택 씬 package 수용

사용자 지시로 현재 프로젝트 Phase22ProductAcceptance.creator와 필요한 음원 4개·기본 pass shader만 선택한 package를 만들었다. BuildTool --asset-list는 Assets 상대경로의 명시적 목록과 기존 meta를 복사하며 dependency 실패는 계속 거부한다. 목록은 package manifest에 기록한다. Test1/Test2 및 기존 재질 원본을 변경하거나 별도 테스트 프로젝트를 만들지 않았다.

clean engine distribution ad40df63의 native runtime/cooker/packer + 수정한 로컬 BuildTool로 Release package 및 Player smoke PASS: GT 654 / promotions 120 / parser 0 / exit 0, C# audio cycles/completions 각 100. 수정 후 Player 종료 canary 1회도 통과했다. 전체 Project package, 수정된 도구까지 포함한 clean snapshot 재검증, Player 프로세스 종료 100회 완료로 확대하지 않는다. 실제 underrun 유효 계측도 남아 있다. 증거는 Phase22AudioWindowsAcceptance20261007.md의 선택 씬 패키징 절과 package-audio-scene-console.log이다.

### 12.11 최종 도구·스트림 계측 재검증 — 2026-10-08

오디오 관련 수정과 선택 목록 BuildTool을 포함한 clean snapshot
`3b64ea9621592a85e1e5c0cebf92a231846f0acb`의 VS18 Release 전체 solution build,
PE import closure 53 images, publication/license/provenance가 통과했다.
최종 engine build ID는 `aa012ebe-4428-467e-ab13-f49ee5b71999`이다.
이 배포본의 BuildTool로 현재 Dynamic_CPP 선택 씬을 패키징한
`FinalStage/Game-42be31c76b814dc9804220532d8c9d6e`도 verification=passed다.
로컬 변경 도구를 섞었던 앞선 package와 구별한다.

스트림 data source가 mixer에 반환하는 MA_BUSY/MA_NO_DATA_AVAILABLE을 직접
계수하고 정상 EOF는 제외했다. 실제 read 경로의 busy→회복→EOF canary와
VS18 Debug/Release 각각 240단정이 통과했다. 이 counter만으로 OS underrun을
판정하지 않는다. Windows Audio ETW event 24~49만 수집하는 필터는 canary에서
실제 audiodg event 33을 유실 없이 검출했다. 이 canary의 glitch는 그대로 기록하며
30분 workload underrun 0 통과로 세지 않는다.

배포본 Editor의 프로젝트 스크립트는 compile-game으로 프로젝트 Library에 빌드한
뒤 --managed-root로 명시했다. 배포본 파일을 수정하거나 별도 프로젝트를 만들지
않았다. 새 Editor의 30분 제품 부하 및 최종 native generation의 Player 종료
100회는 현재 실행 중이며, 결과가 확정될 때 최종 완료를 판정한다.

패키지의 압축 포맷 소비를 보완한 최종 선택 콘텐츠는 WAV 4개와 MP3 Stream,
FLAC Resident 각 1개다. 같은 clean engine의 최종 package
`FinalStage/Game-0c3e1057609742bca7b2f8df4fc57964`는 Player 624 GT frames,
120 promotions, parser calls=0, exit 0이며 C# playback/completion 100회 중
resident/stream 각 50회, MP3/FLAC 각 25회를 통과했다.

최초 신규 soak는 Intro.wav의 Auto가 Stream을 선택해 resident/stream 혼합
표시가 잘못된 것을 계측으로 발견하고 중단했다. `budget-061591...`는 수용에서
제외했다. 명시적 Resident인 Phase22Spatial을 사용한 실제 Editor canary에서
1 voice의 stream reads=0, 혼합 32 voices의 stream reads>0을 확인한 뒤
새 프로세스에서 재실행했다. 과거 혼합이라고 기록한 Intro 기반 측정도 같은
resident/stream 구성 증거로 재사용하지 않는다. CPU 관찰치 자체와 구성 주장은
구별하며, 최종 AU4/AU9 판정은 수정 후 실행만 사용한다.

### 12.12 수정 후 실제 Editor 30분 수용 — PASS

`budget-b69f9f06dd2143cd8f20e84f7224925f`에서 새 최종 Editor를
1800.827초 실행했다. 명시적 Resident/Stream 혼합 0/1/32/128 physical voices,
reverb on/off, 실제 장치 출력과 각 단계의 voice/instance 수가 일치했다.
스트림 PCM read 12,099,284회 중 MA_BUSY/MA_NO_DATA_AVAILABLE 0,
I/O read failures 0, backend failures 0이다. EOF는 starvation에서 제외했다.

128 voice만의 943,138프레임 중 1ms 초과 93회(약 0.00986%)로 p99 <= 1ms.
무부하 최소 private memory부터 전체 peak까지의 보수적 증가 106.1758MiB,
첫 재생 API 응답 127.9356ms로 승인 예산을 통과했다. 최초 순간 baseline의
증가 1.875MiB 대신 무부하 최솟값 기준을 최종 memory 수용에 사용한다.
callback 180,694회, p99 3.80ms, 최대 7.3796ms, 반 주기 초과 19회도
보존한다. nearest-rank p99 < buffer duration 50%를 통과했다.

`final-underrun-0ff2f487400f40758f1bfc361c194955`의 독립 Windows Audio ETW
필터 캡처에서 event 26~49 glitch 관찰 0, capture/read/stop exit 0,
eventsLost/logBuffersLost/realTimeBuffersLost 모두 0이다. 선택 Audio event 자체는
0개이며, 같은 provider/filter가 실제 event 33을 검출한 앞선 canary와 함께
계측 유효성을 판정했다. 관찰 범위 밖 오류가 존재하지 않는다는 보장은 아니다.
Stop 후 voice/instance=0, Editor exit 0 및 PID 종료를 확인했다.
AU4는 완료이며, AU9의 최종 Player 프로세스 종료 100회가 진행 중이다.

### 12.13 PHASE22 최종 종료 — 2026-10-08

최종 선택 씬 package `Game-0c3e1057609742bca7b2f8df4fc57964`에서 실제 Player
프로세스 시작·검증·정상 종료 **100/100 PASS**. 매 회차 최소 600 GT frames,
120 presentation promotions, cooked text parser calls=0, C# audio playback/completion
100회(resident/stream 각 50, MP3/FLAC 각 25), exit code=0과 PID 종료를 확인했다.
100개 ledger의 순서·개수·engine build ID를 재대조했고 최종 pak hash도 일치한다.
이전 generation의 26회와 managed playback 횟수를 프로세스 종료 횟수에 합산하지 않았다.

증거는 `Build/Validation/Phase22Closure/final-phase22-acceptance.json`,
`final-editor-acceptance.json`, `player-exits-af8a3b956d3b4185955046accf67999f/summary.jsonl`.
§12.12의 수정 후 30분 성능·stream/ETW 수용, 앞선 실제 Editor 종료 100회·warm
thread/handle 증가 0, default-device 20회·물리 재연결 1회 및 clean build/package
폐쇄와 함께 AU9를 완료했다. **AU0~AU9 done, PHASE22 완료**다.

수용 범위는 사용자가 선택한 실제 프로젝트의 별도 오디오 씬과 최종 native generation이다.
Test1/Test2·기존 재질의 12개 미해결 콘텐츠 참조는 별도 정리 항목으로 남는다.
로컬 clean acceptance를 원격 CI 실행 완료 또는 PHASE23 설치 제품 수용으로
확대하지 않는다. B5의 오디오 선행 차단은 해제하며 CI 게임 레그 자체는 후속이다.
