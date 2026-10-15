# 오디오 런타임·재생 서비스 운영 계약

기준: master `493536e300e274cbf59989cdd62b20310c8ae64f`의 소스와 호출부. 이 문서는 현재 구현을 설명하며, 이번 문서 갱신에서 빌드·테스트·제품 실행을 수행하지 않았다.

## 현재 소유 구조

EditorMain과 PlayerMain은 `MiniaudioBackend`를 넣은 `AudioHost`를 만들고 128 logical voice 용량으로 시작한다. 이어 `PlaybackService`, `AudioCatalog`를 만들고 SceneManager에 바인딩한다. 오디오 소비자는 backend의 vendor 객체 대신 엔진 값을 사용한다.

- AudioHost: backend 선택·시작·종료·출력 상태
- AudioRuntime/AudioService: logical voice generation, cap/steal/virtualization, bus, listener, clip lease
- PlaybackService: Clip/Preset/Graph를 한 logical playback으로 묶고 scope와 owner 정책 관리
- SoundSystem: Scene의 SoundComponent/AudioListenerComponent와 서비스 사이의 연결
- AudioCatalog: Editor 자산과 cooked Player 자산의 해석·등록
- MiniaudioBackend: 장치, 디코더 작업자, PCM/stream source, 실제 source branch, reverb node

이 문서는 AU0~AU9 일지의 대체 정본이다. 과거 실측은 [클라우드 증거](../analysis/Phase22AudioCloudValidation.md)와 [Windows 수용 안내](../analysis/Phase22AudioWindowsAcceptance20261007.md)에서 해당 provenance를 확인한다.

## 시작·장애·종료

`AudioHost::Start`는 device runtime을 먼저 시작한다. 실패한 backend의 일부 자원을 Stop으로 정리한 뒤 Null runtime을 시작한다. 둘 다 실패하면 false다. `noDevice`를 명시한 성공은 Offline이며 물리 장치 출력과 다르다. 시작 후 장치가 끊긴 경우 `Mode`는 DegradedDevice로 보일 수 있다. 초기 Null fallback과 재생 중 장치 복구를 같은 사건으로 처리하지 않는다.

Host는 restart 사이에도 runtime 객체를 유지해 voice generation이 1로 되돌아가지 않게 한다. Null voice에는 별도 high-bit generation namespace가 있어 복구 후 device handle과 충돌하지 않는다. MiniaudioBackend의 restart 요청은 main thread에서 처리하고 graph/voice ID를 유지한다.

종료에서는 SceneManager 바인딩을 끊고 PlaybackService를 shutdown한 뒤 catalog를 clear하고 AudioHost를 shutdown한다. runtime은 producer를 닫고 voice·clip lease를 퇴역시킨 뒤 backend를 정지한다. backend는 decode worker를 join하고 source mount를 놓는다. 임의의 callback에서 Scene이나 CLR를 해제하지 않는다.

## 값과 수명

`VoiceHandle`은 저수준 논리 voice이며 `PlaybackHandle`은 그래프의 여러 child voice를 묶을 수 있는 별도 세대 핸들이다. 핸들 복사나 C# GC가 소리를 중단하지 않는다. `PlaybackScope`는 World, Session, EditorPreview 종류를 가진다. Scene 전환에 살아남는 음악은 Session을 명시적으로 선택하고 Play mode 종료에는 session도 끝난다.

attached playback은 owner ID와 위치를 받는다. owner 파괴 정책은 Stop 또는 DetachAndFinish다. `EndScope`는 scope에 속한 재생을 끝내며 completion reason은 Finished, Stopped, ScopeEnded, OwnerDestroyed, Replaced다. completion은 게임 스레드에서 poll하는 값이며 장치 callback이 사용자 함수를 실행하지 않는다.

## 스레드와 프레임 연결

PlaybackService의 일반 API는 host/game thread 전용이다. 예외는 worker-to-game `Enqueue`로, mutex로 보호한 bounded command mailbox(1024)를 사용한다. device callback은 이 서비스를 직접 실행하지 않는다. Update에서 요청·완료를 회수하며 handle이 유효한지와 `LastError`를 확인한다.

Host 프레임 말미는 AudioHost::Update→PlaybackService::Update→PublishAudioProfile 순이다. Scene의 SoundSystem은 Update/LateUpdate에서 컴포넌트 재생 상태와 위치·리스너를 반영한다. Editor는 preview tick과 자산 갱신 경로가 추가된다. pause delta와 실제 장치 clock을 혼동하지 않는다.

## 재생 입력과 정책

`PlayRequest`는 clip, bus, concurrency group, persistent/virtualization, volume/pitch/priority/loop, spatialBlend, position/velocity, distance/rolloff, reverb send, owner ID를 가진 값이다. spatialBlend 0은 2D, 1은 3D이고 중간값은 equal-power 분기 혼합이다. 실제 backend source가 둘이어도 logical voice는 하나다.

AudioRuntime이 bus/group/physical limit과 Reject/Oldest/Quietest/LowestPriority 정책을 정한다. backend는 그 정책을 다시 판단하지 않는다. virtual voice는 clip generation lease를 유지한다. clip replacement나 unload가 기존 virtual voice를 최신 clip으로 바꿔서는 안 된다.

## Clip·Preset·Graph

SoundGraph의 현재 node 종류는 Clip, Random, Switch, Layer, GainPitch, Parameter, Output이다. compile 단계가 정의를 검증하고 immutable program을 게시한다. 각 playback은 parameter map과 random state를 별도로 가진다. Parameter node는 PCM을 출력하지 않는다. graph의 maximumVoices 기본값은 32다.

Preset이 기본 설정을 소유한다. 명시적으로 저작된 component 설정은 `overridePresetSettings`로 구분한다. 초기 graph parameter는 Play request에 함께 담아 시작 순간의 값을 확정한다. 값 변경은 typed SetParameter 결과를 확인한다. 일반 DAW graph나 임의 DSP plugin host로 해석하지 않는다.

## 자산과 스트리밍

Editor catalog는 authoring source를 읽고 Player catalog는 cooked catalog와 byte source를 읽는다. `MiniaudioBackend::LoadClip`은 preview PCM을 완전히 검증해 보관하며 64MiB resident 한도를 적용한다. 큰 파일을 source path로 직접 streaming하는 제품 계약이 아니다.

Cooked load mode가 Stream이거나 Auto에서 길이/용량 조건을 넘으면 streaming 경로를 사용한다. Auto의 시간 기준은 10초이며 oversized도 stream을 선택한다. backend는 stream마다 두 250ms page와 두 소유 decode worker를 사용한다. 읽기 실패·starvation 계수는 정상 EOF와 구분한다. resident/cooked metadata의 channels는 1~2이며 공간화 요청은 mono clip을 요구한다.

장치 요청 기본값은 48kHz stereo다. 현재 backend는 2채널, 8kHz~192kHz 요청 범위를 검증한다. 요청값과 ActualSettings가 같다고 가정하지 않는다. `Render` offline capture는 Start에서 noDevice를 명시한 경우에만 유효하다.

## 관리 API

`AudioAssetId`는 canonical 16-byte 자산 ID이며 파일 경로가 아니다. C# `Audio.Play2D`, `PlayAt`, `PlayAttached`는 World/Session scope를 명시한다. nullable settings를 생략하는 경우 preset 기본값과 구분된다. `PlaybackHandle.State`, Stop/Pause/Resume, SetGainPitch/Transform/Parameter를 사용하고 `Audio.TryDequeueCompletion`으로 완료를 읽는다.

managed cap은 0~1024, group ID는 1~65535다. 0 cap은 해당 bus/group을 끈다. 모든 엔진 오디오 호출은 게임 스레드 규약을 따른다. LastError와 무효 handle을 확인하지 않으면 자산 실패와 정상 무음을 구분할 수 없다.

## 검증과 남은 요구

source 계약, synthetic PCM, cooked byte source, package closure, 실제 output device, 장치 전환, 장시간 soak는 서로 다른 검증이다. callback p99는 계측이 켜진 sample의 상한 관측이고 wall-clock runtime update와 별개다. 현재 코드만으로 hardware underrun·청감·latency·모든 driver 복구를 보증하지 않는다.

확장 시에도 scene/preview/session 수명 분리, callback 무할당·무로그 경계, generation pin, bounded mailbox, mono spatial 규칙과 실제 package byte source를 유지한다. 기존 계획의 FMOD 대체 backend는 현재 기본 배선이 아니며 추가 backend를 완료 기능으로 소개하지 않는다.

## 구현 근거

- [Engine/SceneRuntime/Audio/AudioHost.cpp](../../Engine/SceneRuntime/Audio/AudioHost.cpp)
- [Engine/SceneRuntime/Audio/AudioRuntime.h](../../Engine/SceneRuntime/Audio/AudioRuntime.h)
- [Engine/SceneRuntime/Audio/PlaybackService.h](../../Engine/SceneRuntime/Audio/PlaybackService.h)
- [Engine/SceneRuntime/Audio/MiniaudioBackend.cpp](../../Engine/SceneRuntime/Audio/MiniaudioBackend.cpp)
- [Engine/SceneRuntime/Audio/SoundGraph.h](../../Engine/SceneRuntime/Audio/SoundGraph.h)
- [Engine/SceneRuntime/Audio/AudioCatalog.cpp](../../Engine/SceneRuntime/Audio/AudioCatalog.cpp)
- [Engine/SceneRuntime/SoundSystem.cpp](../../Engine/SceneRuntime/SoundSystem.cpp)
- [Editor/EngineEntry/EditorMain.cpp](../../Editor/EngineEntry/EditorMain.cpp)
- [Player/PlayerMain.cpp](../../Player/PlayerMain.cpp)
- [ScriptCore/Audio.cs](../../ScriptCore/Audio.cs)
