# enkiTS 1.12 — CreatorEngine의 제한된 worker spin 확장

이 포트는 [vcpkg 기준 커밋](https://github.com/microsoft/vcpkg/tree/9e593bb18ea69cc5095e012465dcd675a822ed0d/ports/enkits)의
`enkits` 1.12 포트를 복사한 오버레이입니다. 원본 enkiTS의 저작권과 Zlib 라이선스를 유지하며,
CreatorEngine 수정본을 원본 배포판과 구분합니다. 버전은 1.12, 이 포트의 리비전은 1입니다.

- 원본: [dougbinks/enkiTS, v1.12](https://github.com/dougbinks/enkiTS/tree/v1.12).
- 원본 다운로드의 SHA512와 `fix_shared_install.patch`, C 인터페이스, 정적/동적 링크 선택,
  CMake 설치 및 저작권 파일 설치는 기준 포트 그대로입니다.
- 추가 패치: `creator-scoped-idle-spin.patch`.
- 실제 소비자: `Engine/Utility_Framework/ThreadPoolSpin.h` 및 `ThreadPool.cpp`.
- 기존 `vcpkg-configuration.json`의 `./ports` 검색 경로로 선택됩니다.

## 추가된 C++ 계약

`TaskSchedulerConfig::idleSpinCallbacks`는 엔진이 소유한 `userData`, `begin`, `poll`, `end`를 받습니다.
callback과 `userData`의 주소는 Initialize 전에 고정하며 scheduler의 모든 thread가 종료될 때까지 유효해야
합니다. callback은 예외, 할당, 블로킹, scheduler 재진입 없이 반환합니다. 원본의 C++11 빌드 조건을 유지하기
위해 함수 포인터의 형식에 C++17 `noexcept`를 요구하지 않고, 호출 helper 자체를 `noexcept`로 고정합니다.

1. worker가 실제 작업을 실행한 뒤 큐가 비면 원본의 기본 spin을 먼저 수행합니다.
2. 기본 spin을 소진하면 그 유휴 구간에서 `begin`을 한 번 시도합니다. 엔진은 유휴 deadline,
   프레임 전체 예산, 동시 worker 허가를 확보했을 때만 true를 반환합니다.
3. 추가 반복은 실제 `TryRunTask`를 실행하며, 매 반복 전에 `poll`로 시간과 범위 종료를 검사합니다.
4. 작업을 찾으면 분할/재게시/사용자 callback 실행 **전에** `end(TaskFound)`로 spin 계측과 허가를 끝냅니다.
5. 예산 소진, 범위 종료, scheduler 종료에서는 각각의 결과로 끝낸 뒤 원본 대기 경로를 사용합니다.
   sleep의 즉시 반환이나 허위 깨움만으로 같은 유휴 구간의 예산을 다시 받지 않습니다.

`end`는 성공한 `begin`마다 한 번 호출합니다. `BudgetExhausted`와 `ScopeEnded`는 대기 진입 시도이며,
실제 OS sleep 횟수가 아닙니다. 원본의 마지막 큐 재검사가 작업을 발견하거나 semaphore 토큰이 이미 있으면
실제 sleep 없이 돌아올 수 있습니다. spin의 wall time은 OS 선점을 포함할 수 있으며 CPU 작업 시간과 다릅니다.

대기자 계수, thread 상태 게시, 마지막 큐 재검사, semaphore wait/signal 순서는 변경하지 않습니다.
`profilerCallbacks.waitForNewTaskSuspendStart`에는 spin을 넣지 않습니다. worker 전용이며 dispatcher나
owner의 대기 정책을 바꾸지 않습니다. 모든 callback이 null이면 추가 spin은 비활성입니다.

## 빌드 경계

헤더는 `ENKITS_CREATOR_IDLE_SPIN_API`를 1로 정의합니다. `TaskSchedulerConfig`에 필드가 추가되므로 C++ ABI가
원본과 다릅니다. 오버레이의 헤더와 라이브러리/DLL을 함께 설치하고 모든 소비자를 다시 빌드해야 합니다.
헤더만 복사해 기존 DLL과 혼용하면 안 됩니다. 포트 파일과 패치 변경은 vcpkg ABI 입력에 포함됩니다.
controller 생성자는 새 export `enki::GetCreatorIdleSpinApiVersion()`을 확인합니다. 원본 라이브러리에는 이
symbol이 없으므로 수정 헤더와 원본 라이브러리를 혼용하면 링크/로드 단계에서 실패합니다.

이번 변경 작성 환경에서는 빌드, 실행 테스트, GPU 검사, 벤치마크를 수행하지 않았습니다.
원본 v1.12에 대한 패치 적용 가능 여부와 대기 경로의 보존은 정적으로 확인했습니다.
