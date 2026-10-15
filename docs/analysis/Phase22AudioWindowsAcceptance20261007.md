# Windows 오디오 제품 수용과 증거 해석

기준: master `493536e300e274cbf59989cdd62b20310c8ae64f`의 소스와 호출부. 이 문서는 현재 구현을 설명하며, 이번 문서 갱신에서 빌드·테스트·제품 실행을 수행하지 않았다.

## 날짜가 붙은 경로의 의미

이 파일명은 2026-10-07부터 작성된 acceptance 기록의 링크를 유지한다. 현재 checkout의 기능은 [오디오 운영 계약](../plans/AudioBackendModernizationPlan.md)을 따른다. 과거 날짜의 PASS·30분 측정·종료 반복 수를 이번 문서 갱신의 실행 결과로 재발급하지 않는다. 새 수용은 동일 commit에서 만든 바이너리와 새 산출물로 판정해야 한다.

## 제품 배선에서 확인할 것

Editor와 Player 모두 AudioHost→PlaybackService→AudioCatalog를 생성하고 SceneManager의 SoundSystem에 서비스를 연결한다. Editor는 authoring catalog와 preview, Player는 cooked catalog와 byte source를 사용한다. 두 Host의 종료는 playback shutdown→catalog clear→audio host shutdown 순이다. 이 순서와 실제 drain 완료는 leak/callback-after-free 검사에서 핵심이다.

## 수용 행렬

1. 실장치: `verify-audio-windows-hardware.ps1`에서 실제 output mode, sample/period/buffer, callback 통계를 기록한다. Null/Offline 결과를 Device로 분류하지 않는다
2. stream: `verify-audio-windows-stream.ps1`에서 cooked source, loop/seek, 읽기 실패·starvation을 확인한다. resident 파일만으로 streaming을 판정하지 않는다
3. device transition: `verify-audio-default-device-switch.ps1`에서 outputInterrupted, reroute/restart 횟수와 복구 후 voice 상태를 확인한다
4. Editor: `verify-audio-live-editor.ps1`, `verify-audio-editor-lifecycle.ps1`에서 컴포넌트 저작·listener·scope·Play/Stop/reload를 확인한다
5. 종료: `verify-audio-editor-exits.ps1`, `verify-audio-player-exits.ps1`로 실제 process exit와 자원 drain을 확인한다. 창이 닫혔다는 것만으로 process 종료를 추정하지 않는다
6. 배포: `verify-audio-player-package.ps1`과 `verify-audio-pe-closure.ps1`로 cooked 자산과 DLL 폐포를 각각 확인한다

## 성능 수치의 단위

runtime Update의 wall time, device callback time, decoder worker CPU, 전체 process CPU는 다른 값이다. logical voice 수와 physical source branch 수도 다르다. spatialBlend 중간값은 두 source를 사용해도 하나의 logical voice로 집계한다. profileCallbacks opt-in 여부, voice 구성, stream/resident 비율, 장치 period, background 부하를 반드시 기록한다.

`AudioCallbackMetrics.p99UpperNanoseconds`는 해당 계측의 상한 관측치다. callbackCount가 충분하지 않거나 profiler가 꺼진 상태의 0을 성능 수용으로 해석하지 않는다. streamStarvationReads와 streamReadFailures도 서로 다른 원인이다. ETW를 사용하면 선택한 provider/filter와 callback 계측 시간의 대응을 남긴다.

## 보완 없이 주장할 수 없는 항목

이 소스 개정은 hardware 출력·청감·latency·장시간 안정성·모든 driver 복구를 확인하지 않았다. 옛 문서의 수용 예산은 제품·작업 부하가 같다는 증거 없이 새 플랫폼에 적용할 수 없다. 결과와 provenance가 없는 과거 서술은 참고 이력이며 현재 release gate가 아니다.

## 구현 근거

- [Editor/EngineEntry/EditorMain.cpp](../../Editor/EngineEntry/EditorMain.cpp)
- [Player/PlayerMain.cpp](../../Player/PlayerMain.cpp)
- [Engine/SceneRuntime/Audio/AudioHost.cpp](../../Engine/SceneRuntime/Audio/AudioHost.cpp)
- [Engine/SceneRuntime/Audio/MiniaudioBackend.h](../../Engine/SceneRuntime/Audio/MiniaudioBackend.h)
- [Tools/regression/verify-audio-windows-hardware.ps1](../../Tools/regression/verify-audio-windows-hardware.ps1)
- [Tools/regression/verify-audio-windows-stream.ps1](../../Tools/regression/verify-audio-windows-stream.ps1)
- [Tools/regression/verify-audio-default-device-switch.ps1](../../Tools/regression/verify-audio-default-device-switch.ps1)
- [Tools/regression/verify-audio-editor-exits.ps1](../../Tools/regression/verify-audio-editor-exits.ps1)
- [Tools/regression/verify-audio-player-exits.ps1](../../Tools/regression/verify-audio-player-exits.ps1)
