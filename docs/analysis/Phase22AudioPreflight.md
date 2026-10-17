# 오디오 변경 전 점검 경계

기준: master `493536e300e274cbf59989cdd62b20310c8ae64f`의 소스와 호출부. 이 문서는 현재 구현을 설명하며, 이번 문서 갱신에서 빌드·테스트·제품 실행을 수행하지 않았다.

## 현재 기준선

이 파일은 초기 FMOD 정찰 링크의 호환 진입점이다. 현재의 배선은 [오디오 운영 계약](../plans/AudioBackendModernizationPlan.md)에 정리했다. 당시의 “listener 없음”, “Play 중 update 없음”, “cooked audio 경로 없음”을 지금의 결함으로 승계하지 않는다. EditorMain/PlayerMain은 AudioHost·PlaybackService·AudioCatalog를 만들고 SceneManager에 바인딩하며 매 프레임 오디오 update를 수행한다.

## 변경 전 확인할 흐름

먼저 소리의 source가 Clip/Preset/Graph 중 무엇인지와 asset ID를 확인한다. Editor preview의 resident source load와 Player의 cooked catalog load는 다른 경로다. 같은 이름의 source 파일이 존재해도 package에 올바른 cooked artifact가 없으면 Player 재생을 보증하지 못한다.

다음으로 AudioHost::Mode를 확인한다. Device, DegradedDevice, Null, Offline, Stopped가 별도 상태다. Null 성공을 device 성공으로 보고하거나 offline Render를 물리 출력으로 기록하지 않는다. listener는 SoundSystem/AudioListenerComponent가 반영하며 spatial clip은 mono 요구를 만족해야 한다.

마지막으로 재생의 scope와 owner 정책을 확인한다. World 종료, Session 종료, preview 종료, owner 파괴는 서로 다른 중단 원인이다. 전역 stop으로 특정 scene의 문제를 해결하면 살아남아야 할 음악이나 preview까지 끊을 수 있다.

## 인터페이스·배포 점검

vendor 타입은 MiniaudioBackend 구현 안에 머물고 소비자는 PlayRequest/AudioService/PlaybackService 값을 사용한다. 확장자는 importer/cooker/catalog/backend에서 모두 지원되는지 확인한다. 파일 확장자만 허용하고 실제 decoder 검증을 생략하지 않는다.

배포 확인에는 PE import closure와 package 자산 closure가 각각 필요하다. source token 감사만으로 배포 DLL 제거·license 포함·stream mount 수명을 증명할 수 없다. `verify-audio-retirement.py`, `verify-audio-pe-closure.ps1`, `verify-audio-player-package.ps1`의 입력과 산출물을 별도로 확인한다.

## 실행하지 않은 검사

이번 개정은 실행 전 점검 방법을 설명한다. 현재 OS의 default device, 실제 callback underrun, 장치 전환, 청감, 30분 예산은 확인하지 않았다. [클라우드 자료](Phase22AudioCloudValidation.md)와 [Windows 자료](Phase22AudioWindowsAcceptance20261007.md)를 혼합해 새로운 PASS를 만들지 않는다.

## 구현 근거

- [Editor/EngineEntry/EditorMain.cpp](../../Editor/EngineEntry/EditorMain.cpp)
- [Player/PlayerMain.cpp](../../Player/PlayerMain.cpp)
- [Engine/SceneRuntime/Audio/AudioHost.h](../../Engine/SceneRuntime/Audio/AudioHost.h)
- [Engine/SceneRuntime/Audio/AudioCatalog.cpp](../../Engine/SceneRuntime/Audio/AudioCatalog.cpp)
- [Engine/SceneRuntime/SoundSystem.cpp](../../Engine/SceneRuntime/SoundSystem.cpp)
- [Tools/regression/verify-audio-retirement.py](../../Tools/regression/verify-audio-retirement.py)
- [Tools/regression/verify-audio-pe-closure.ps1](../../Tools/regression/verify-audio-pe-closure.ps1)
- [Tools/regression/verify-audio-player-package.ps1](../../Tools/regression/verify-audio-player-package.ps1)
