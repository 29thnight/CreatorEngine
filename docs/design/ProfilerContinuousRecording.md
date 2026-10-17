# 연속 프로파일 기록과 완전성 계약

> 기준: master `493536e300e274cbf59989cdd62b20310c8ae64f`의 소스 정적 조사. 이 문서의 재작성에서 빌드·실행·성능 측정은 하지 않았다.

## producer, collector, writer

`profiler_service`는 thread stream을 소유하고 producer가 자기 stream에 scope를 기록한다. `publish_frame(engine_frame)`은 경계와 봉인 요청을 collector에 전달한다. collector가 rolling capture와 finalized frame을 구성하고 continuous writer가 디스크 I/O를 맡는다. UI가 기록 buffer를 직접 순회하거나 writer 완료를 기다리지 않는다.

recorder 상태는 stopped/starting/recording/pausing/frozen이다. `record`, `pause`, `clear`는 비동기 제어이고 clear는 적용 ticket을 반환한다. pause 직후 frozen이 보장되지 않는다. live summary는 전체 capture 복사 없이 상태를 읽으며 live snapshot 간격은 기본 100ms다.

## 유한 자원과 손실

service는 chunk/queued-frame/memory 상한을 갖고 writer queue 기본값은 64 MiB/64 batch다. 일반 append는 디스크나 queue 빈자리를 기다리지 않고 거절한 자료를 loss counter에 남긴다. stop tail의 append_final과 wait는 collector/background 전용이다.

Stop 시 producer acknowledgement, open scope와 GPU submission tail을 collector가 drain한다. 기본 drain timeout은 250ms이고 0..5000ms로 제한한다. unacked stream, pending/failed GPU submission, dropped event/counter/frame, late drop를 함께 봐야 한다. 비어 있는 트랙과 받지 못한 트랙을 같은 것으로 취급하지 않는다.

## 파일 형식과 복구

현재 snapshot writer는 CEPROF v6, continuous writer는 v7이다. 이전 snapshot v1/v2/v4 및 stream v3/v5를 구분해 읽는 경계가 있으며 버전 번호를 섞지 않는다. 최신 포맷은 render measurement와 CPU presenter-return evidence를 포함할 수 있다. 오래된 파일의 없는 축은 unknown으로 남긴다.

파일 codec는 구조체 padding을 그대로 쓰지 않고 필드 단위의 versioned encoding을 사용한다. open/write 실패, unsupported version, truncated, checksum mismatch, resource limit, malformed, canceled를 구별한다. written/flushed byte는 OS 전달·C++ stream flush 관측이며 물리 디스크 내구성 보장이 아니다. crash 시 아직 ring에 있던 tail은 사라질 수 있다.

## viewer와 완료 판단

recording path/status/writer identity는 함께 게시된다. ring Clear가 writer identity를 바꾸는 것과 같지 않다. Viewer는 indexed overview와 선택 window를 백그라운드에서 준비하고 recording을 계속할 수 있다. timeline/export가 열린다는 사실만으로 capture_complete가 true가 되지 않는다.

실제 장시간 녹화, disk full, 중간 종료 복구, Stop 중 GPU tail, Clear/재녹화, 손상·큰 파일 cancellation을 검증해야 한다. 이 문서는 소스 계약이며 새 녹화나 측정 결과를 포함하지 않는다.

## 구현 근거

- [Engine/EngineDiagnostics/ProfileService.h](../../Engine/EngineDiagnostics/ProfileService.h)
- [Engine/EngineDiagnostics/ProfileService.cpp](../../Engine/EngineDiagnostics/ProfileService.cpp)
- [Engine/EngineDiagnostics/ProfileRecording.cpp](../../Engine/EngineDiagnostics/ProfileRecording.cpp)
- [Engine/EngineDiagnostics/ProfileCaptureFile.h](../../Engine/EngineDiagnostics/ProfileCaptureFile.h)
- [Engine/EngineDiagnostics/ProfileCaptureFile.cpp](../../Engine/EngineDiagnostics/ProfileCaptureFile.cpp)
- [Tools/ProfilerViewer/PreparationWorker.h](../../Tools/ProfilerViewer/PreparationWorker.h)
