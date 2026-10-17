# GCCE: 2026-10-09 검증 기록과 현재 재검증 계약

> 기준: `493536e300e274cbf59989cdd62b20310c8ae64f`의 코드·호출 연결 정적 검토. 이 문서 갱신에서는 빌드·GPU 실행·성능 측정을 수행하지 않았다.

## 결과를 읽는 범위

2026-10-09 기록은 당시 revision의 Debug/Release `scene.gc.selftest` 반복 실행을 남긴 역사적 증거다. 각 구성 20회 통과라는 기록을 현 checkout의 새 실행 결과나 배포 승인으로 읽지 않는다. 당시 로컬 `Artifacts/gc-validation-20261009` 로그는 저장소에 항상 포함되는 산출물이 아니며, 원본 로그가 없으면 문서의 요약만으로 재현성을 입증할 수 없다.

현재 소유권과 종료 순서의 정본은 [GCCE 통합](../design/GCCEIntegration.md)이다. 이 페이지는 그 계약을 어떤 게이트로 확인할지 설명한다.

## 실행 진입점과 판정

`verify-scene-gc.ps1`은 Debug/Release 구성을 선택하고 기본 20회(허용 1..1000) selftest를 실행한다. 기본 timeout은 180초다. 기존 Editor가 실행 중이면 거부하며, 선택한 실행 파일과 runtime DLL이 관련 Scene·Component·selftest 소스보다 오래되면 먼저 갱신하도록 실패한다. 따라서 오래된 바이너리의 통과를 소스 검증으로 잘못 세지 않는다.

도구는 격리 workspace와 commandlet을 만들고 `scene.gc.selftest`를 반복한다. 각 성공 marker의 cycle/lifecycle/staleHandles/selection 값은 2, DDOL/workerPin 값은 1이어야 하고 incrementalSteps는 양수여야 한다. exit code 0, 정확한 성공 횟수, 실패 marker 부재를 함께 요구한다. finally에서 환경 변수를 복원하고 남은 process를 정리한다.

실제 테스트는 `SceneGCSelfTest.cpp`, 명령 연결은 `RenderTestCommands.cpp`다. 테스트 이름이 있거나 handler가 등록됐다는 사실은 실행 통과와 다르다. marker의 숫자는 테스트 사례 수이며 collector latency 상한이나 모든 Scene 경로의 coverage가 아니다.

## 이 게이트가 필요한 이유

수집 가능한 cycle과 결정적 Destroy를 구분하고, 파괴 뒤 generation handle과 selection이 stale가 되는지 확인해야 한다. DDOL의 Scene 간 이동은 완전 파괴와 다르며 worker pin은 메모리 수명만 유지한다. pin이 논리 Destroy를 되돌리거나 concurrent mutation을 안전하게 만들지는 않는다.

## 아직 별도 증거가 필요한 범위

- shipping/runtime package에서 단일 GCCE DLL·CRT·debug-check 구성이 일치하는지
- NativeAOT/CLR 경계, sanitizer, 긴 반복·메모리 압력·큰 tracer의 실제 pause 분포
- 실제 prefab/Inspector/Scene 전환 UI, callback 재진입과 예외, pending 작업이 있는 shutdown
- worker join, root 해제, 서비스 종료 뒤 domain 마지막 파괴의 전체 제품 경로

이 문서 갱신에서는 도구를 실행하지 않았다. 해당 스크립트는 실행·환경 변경을 수반하므로 현재의 정적 문서 검사와 구분한다.

## 구현 근거

- [`Tools/regression/verify-scene-gc.ps1`](../../Tools/regression/verify-scene-gc.ps1)
- [`Editor/RenderTests/Tasks/SceneGCSelfTest.cpp`](../../Editor/RenderTests/Tasks/SceneGCSelfTest.cpp)
- [`Editor/EngineEntry/Commands/RenderTestCommands.cpp`](../../Editor/EngineEntry/Commands/RenderTestCommands.cpp)
- [`Engine/SceneRuntime/SceneManager.cpp`](../../Engine/SceneRuntime/SceneManager.cpp)
- [`Engine/SceneRuntime/Component.cpp`](../../Engine/SceneRuntime/Component.cpp)
