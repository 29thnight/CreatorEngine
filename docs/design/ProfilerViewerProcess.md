# 별도 ProfilerViewer 프로세스와 IPC 소유권

> 기준: master `493536e300e274cbf59989cdd62b20310c8ae64f`의 소스 정적 조사. 이 문서의 재작성에서 빌드·실행·성능 측정은 하지 않았다.

## 생성과 종료

Editor의 `viewer_process().open_or_focus`는 Editor 기준의 고정 Tools/ProfilerViewer/ProfilerViewer.exe를 시작하거나 기존 owned viewer에 focus를 요청한다. 상승 권한으로 실행하지 않으며 open/focus가 viewer 종료를 기다리지 않는다. process는 생성 시 Windows Job에 원자적으로 배정되고 kill-on-job-close를 사용한다. transport가 끊겨도 Editor의 child 소유권은 남는다.

shutdown은 graceful close 후 bounded job cleanup을 수행한다. 독립 실행한 Viewer는 이 Editor가 종료시키는 대상이 아니다. 프로세스 생성/pipe 실패는 launch_status의 error/message로 남기며 unowned launch로 fallback하지 않는다.

## engine owner와 transport worker

`engine_process::pump`는 UI owner의 scene-lifetime 보호 아래 bounded command를 실행하고 immutable DTO를 넘긴다. IPC, encoding, 파일 scan/index는 worker가 맡는다. diagnostic serializer는 scene 포인터를 capture하지 않는다. 반대 방향 command는 schema/object epoch를 검증한 후 owner 경계에서 적용한다.

프로토콜 v3는 nonce, sequence, target PID/creation time/Windows session과 exact payload 길이를 검증한다. packet 최대 64 KiB, capture 128 MiB, diagnostics 2 MiB, command 4096 bytes, pending command 16개라는 상한을 갖는다. local named pipe는 remote client를 거부하고 logon SID에 묶인다.

record/stop/clear/counter mask는 현재 capture generation과 일치해야 한다. DX start/stop은 별도의 exact capture-service session을 사용하므로 일반 Clear가 오래된 DX Stop을 새 세션으로 돌려서는 안 된다. counter mask 등 value 범위도 validation을 통과해야 한다.

## 파일·분석·표시 책임

Viewer가 파일 열기, index, timeline window 준비와 export를 소유한다. engine은 immutable capture/artifact 위치와 live diagnostics를 제공한다. 작은 메모리 snapshot을 요청하는 것과 engine 객체 전체를 viewer가 원격 포인터로 탐색하는 것은 다르다. Memory 수집도 engine GameThread 경계에서 DTO로 만들어 전송한다.

## 수용 범위

missing viewer, malformed packet, stale generation, broken pipe, slow consumer, 큰 파일 취소, Editor crash/종료, 독립 viewer 보존과 ordinary-user token을 실제 Windows에서 확인해야 한다. 프로토콜과 cleanup 코드 확인은 해당 failure fixture 통과가 아니다. [ProfilerContinuousRecording](ProfilerContinuousRecording.md)과 [Dx12DeepCapture](Dx12DeepCapture.md)가 파일 및 별도 ETW 경계를 설명한다.

## 구현 근거

- [Engine/EngineDiagnostics/ProfilerViewerProcess.h](../../Engine/EngineDiagnostics/ProfilerViewerProcess.h)
- [Engine/EngineDiagnostics/ProfilerViewerProcess.cpp](../../Engine/EngineDiagnostics/ProfilerViewerProcess.cpp)
- [Engine/EngineDiagnostics/ProfilerViewerProtocol.h](../../Engine/EngineDiagnostics/ProfilerViewerProtocol.h)
- [Engine/EngineDiagnostics/ProfilerViewerTransport.h](../../Engine/EngineDiagnostics/ProfilerViewerTransport.h)
- [Tools/ProfilerViewer/ProfilerViewer.cpp](../../Tools/ProfilerViewer/ProfilerViewer.cpp)
- [Tools/ProfilerViewer/PreparationWorker.h](../../Tools/ProfilerViewer/PreparationWorker.h)
- [Editor/EngineGUIWindow/ProfilerLiveDiagnosticsBridge.cpp](../../Editor/EngineGUIWindow/ProfilerLiveDiagnosticsBridge.cpp)
