# 오디오 클라우드 증거의 범위와 재현 안내

기준: master `493536e300e274cbf59989cdd62b20310c8ae64f`의 소스와 호출부. 이 문서는 현재 구현을 설명하며, 이번 문서 갱신에서 빌드·테스트·제품 실행을 수행하지 않았다.

## 판정 범위

이 경로는 과거 격리 오디오 검증의 증거를 찾는 곳이다. 현재 구현의 정본은 [오디오 운영 계약](../plans/AudioBackendModernizationPlan.md)이다. 이번 개정에서 기존 결과를 재실행하지 않았으며 예전 결과의 compile provenance와 source hash가 현재 checkout과 일치한다고 주장하지 않는다.

## 보존한 증거

- [검증 harness 안내](../../Tools/regression/audio/README.md)
- [기존 결과](../../Tools/regression/audio/evidence/results.json)
- [컴파일 provenance](../../Tools/regression/audio/evidence/compile-provenance.json)
- [source SHA-256](../../Tools/regression/audio/evidence/source-sha256.json)
- [실패 canary](../../Tools/regression/audio/evidence/failure-canaries.json)
- [fixture 결정성](../../Tools/regression/audio/evidence/fixture-determinism.json)
- [대상 재점검](../../Tools/regression/audio/evidence/targeted-recheck.json)

위 JSON은 원자료이며 이 문서화에서 변경하지 않았다. 결과를 사용할 때 source hash·컴파일 옵션·실행 모드·fixture와 현재 수정 범위를 함께 대조한다.

## 격리 검증으로 확인할 계약

AudioRuntime은 장치 정책과 독립된 voice 표를 사용한다. PlaybackService는 scope 종료, generation 핸들, graph parameter와 completion을 게임 스레드에서 처리한다. Null backend는 수명·상태 테스트에 사용할 수 있고 MiniaudioBackend의 noDevice Render는 실제 mixer PCM을 읽는 별도 경로다.

따라서 state-only 결과와 PCM 결과를 분리해야 한다. voice 수가 맞다는 사실은 파형·loop 경계·stereo/mono 규약을 증명하지 않는다. 반대로 synthetic PCM이 맞아도 실제 Editor의 Scene listener, Inspector 저작, Play/Stop snapshot, cooked Player mount가 모두 연결됐다는 뜻은 아니다.

## 실패 canary를 함께 읽는 이유

손상·잘림·잘못된 metadata, capacity 초과, stale handle, scope 종료 후 재사용처럼 거부되어야 할 입력이 성공하면 정상 fixture의 초록도 신뢰하기 어렵다. 새 변경에서는 성공 case만 복사하지 말고 그 변경의 실패 canary가 같은 코드 경계를 통과하는지 확인한다.

## 명시적 비포함

클라우드 offline 실행은 Windows 실제 default device, WASAPI buffer 상태, 청감, 장치 재선택, OS mixer, ETW CPU 배치, shutdown 100회 또는 30분 soak를 대체하지 않는다. 해당 수용은 [Windows 검증 경계](Phase22AudioWindowsAcceptance20261007.md)에서 별도로 관리한다.

## 구현 근거

- [Tools/regression/audio/README.md](../../Tools/regression/audio/README.md)
- [Engine/SceneRuntime/Audio/NullAudioBackend.cpp](../../Engine/SceneRuntime/Audio/NullAudioBackend.cpp)
- [Engine/SceneRuntime/Audio/MiniaudioBackend.h](../../Engine/SceneRuntime/Audio/MiniaudioBackend.h)
- [Engine/SceneRuntime/Audio/PlaybackService.cpp](../../Engine/SceneRuntime/Audio/PlaybackService.cpp)
