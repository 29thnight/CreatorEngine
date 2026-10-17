# DX12 deep capture의 별도 ETW 경계

> 기준: master `493536e300e274cbf59989cdd62b20310c8ae64f`의 소스 정적 조사. 이 문서의 재작성에서 빌드·실행·성능 측정은 하지 않았다.

## 활성화 조건과 책임

deep capture는 일반 CEPROF recorder와 다른 capture_service·helper·파일을 사용한다. `Directory.Build.targets`는 EngineDxDeepCapture=true, non-Shipping, x64 조건에서만 CE_DX_TIMING_CAPTURE를 켠다. 옵션이 꺼진 빌드에서 UI/CLI 이름만으로 ETW 수집 능력이 있다고 판단하면 안 된다.

`start_capture`와 `stop_capture(session_id)`는 CLI/GT 및 Viewer/PT가 공유하는 admission 경계다. success는 queued 요청 수락이지 ETW가 정상 기록 중이라는 보증이 아니다. 상태는 accepted/in_progress/stopping/finalized 외에 failed/permission_denied/unavailable을 구분한다. 동시 pending operation은 bounded이며 파일 I/O와 분석은 persistent worker가 맡는다.

## helper protocol

CEDX v1은 숫자 identity와 fixed record를 교환한다. pointer, 문자열 path, 임의 provider 또는 명령 실행 요청을 helper로 전달하지 않는다. nonce/sequence, record kind, flags, time ordering과 exact layout을 검증한다. queue 상한은 8192 records이며 parent lease와 connection/I/O/shutdown timeout이 있다.

record는 QPC, 원본 ns, process creation/session, API/hardware queue, execution/submission/view/frame identity 및 ETW/transport loss를 따로 보존한다. API marker ID를 PIX marker 이름·Begin/End 의미로 추정하지 않는다. correlation이 없거나 ambiguous하면 그 상태를 드러내야 한다.

## 권한과 측정 의미

CLI나 일반 Viewer command는 호출자가 helper 경로·provider·출력 경로·elevation을 고르지 못한다. 서비스 내부의 별도 start API에 elevation 인자가 있다고 모든 실행이 자동 승격되는 것은 아니다. 실제 권한 거부는 permission_denied로 다뤄야 한다.

GPU execution, CPU ExecuteCommandLists 범위, presenter return, 화면 표시 시점은 서로 다른 축이다. ETW 일부가 잡혔다고 frame latency·광자 표시 시간 또는 전체 GPU 업무를 정확히 재구성했다고 주장하지 않는다.

## 미검증 수용 조건

ordinary-user 거부, 허용된 elevated helper의 최소 권한, 잘못된 nonce/PID 재사용, parent death, loss/slow reader, cancellation, 출력 파일 복구를 실제 Windows에서 검증해야 한다. 이 문서는 옵션과 protocol 코드를 확인했으며 ETW collector를 실행하지 않았다.

## 구현 근거

- [Directory.Build.targets](../../Directory.Build.targets)
- [Engine/EngineDiagnostics/DxCaptureService.h](../../Engine/EngineDiagnostics/DxCaptureService.h)
- [Engine/EngineDiagnostics/DxCaptureService.cpp](../../Engine/EngineDiagnostics/DxCaptureService.cpp)
- [Engine/EngineDiagnostics/DxCaptureProtocol.h](../../Engine/EngineDiagnostics/DxCaptureProtocol.h)
- [Engine/EngineDiagnostics/DxCaptureProcess.cpp](../../Engine/EngineDiagnostics/DxCaptureProcess.cpp)
- [Engine/EngineDiagnostics/DxCaptureSecurity.h](../../Engine/EngineDiagnostics/DxCaptureSecurity.h)
- [Tools/DxCaptureHelper/DxCaptureHelper.cpp](../../Tools/DxCaptureHelper/DxCaptureHelper.cpp)
