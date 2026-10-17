# 프로파일 수집의 현재 구성과 검증 계획

> 기준: master `493536e300e274cbf59989cdd62b20310c8ae64f`의 소스 정적 조사. 이 문서의 재작성에서 빌드·실행·성능 측정은 하지 않았다.

## 현재 연결된 경로

EngineBootstrap이 profiler_service를 worker 생성 전에 초기화하고 thread_pool 및 RenderThread hook을 설치한다. scope/event는 producer-owned stream, frame boundary는 collector, 연속 파일은 writer가 처리한다. Renderer hook은 backend에서 CPU QPC 축으로 변환된 GPU span과 submission generation을 전달한다.

일반 녹화와 standalone Viewer, deep DX capture를 별도 책임으로 유지한다. 현재 snapshot 파일 버전은 6, continuous stream은 7이며 Viewer IPC는 3이다. 서로 다른 version space를 하나의 “프로파일러 버전”으로 합치지 않는다.

## 완료 기준을 기능별로 분리

- 수집 무결성: foreign stream touch, abandoned stream, unacked tail, dropped/late 자료와 GPU failure를 명시하고 incomplete capture를 정상 결과로 숨기지 않음
- 의미 정합성: engine frame, scene/view, CPU ownership, native/generated provenance와 서로 다른 측정 clock을 유지
- 상호작용: record/stop/clear는 비동기 상태 전이, 오래된 generation command 거부, UI thread에서 I/O/join 없음
- 파일: version/CRC/크기 제한, 중단 복구, 큰 녹화 window/index와 cancellation
- 비용: stopped/recording/live viewing/continuous write/ETW 모드별 같은 workload에서 owner·collector·writer 비용을 구분

## 남은 실증

헤더나 regression harness가 존재한다고 위 gate가 통과한 것은 아니다. 긴 frame scope, idle worker tail, GPU 지연, queue 포화, disk full, Viewer disconnect, shutdown을 실행해야 한다. 녹화 overhead 및 p95/p99는 이번에 측정하지 않았다.

API의 정본은 [ProfilerContinuousRecording](../design/ProfilerContinuousRecording.md), process 책임은 [ProfilerViewerProcess](../design/ProfilerViewerProcess.md), 선택적 ETW는 [Dx12DeepCapture](../design/Dx12DeepCapture.md)에 둔다. 이전 단계별 완료 수치와 임시 local artifact 경로는 현재 master의 실행 증거로 재사용하지 않는다.

## 구현 근거

- [Engine/RuntimeHost/EngineBootstrap.h](../../Engine/RuntimeHost/EngineBootstrap.h)
- [Engine/EngineDiagnostics/ProfileService.h](../../Engine/EngineDiagnostics/ProfileService.h)
- [Engine/EngineDiagnostics/ProfileCaptureFile.h](../../Engine/EngineDiagnostics/ProfileCaptureFile.h)
- [Engine/EngineDiagnostics/ProfilerViewerProtocol.h](../../Engine/EngineDiagnostics/ProfilerViewerProtocol.h)
- [Engine/EngineDiagnostics/DxCaptureService.h](../../Engine/EngineDiagnostics/DxCaptureService.h)
