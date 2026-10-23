# RG8 명시적 async compute 전환 — 2026-10-22

TestShadow에서 자동 배치 탐색 ON은 OFF보다 전체 CPU/GPU 프레임을 느리게 만들었다. 해당 탐색 실험을 중단하고 패스 작성자가 실행 큐를 명시하는 방식으로 전환했다. 이전 성능 원장은 [RenderRg8Overlap20261022.md](RenderRg8Overlap20261022.md)에 보존한다.

## 로컬 언리얼 코드와 적용

설치된 UE 5.6.1, changelist 44394996:

- `RenderCore/Private/RenderGraphPass.cpp:435`: `AsyncCompute` 플래그로 파이프라인 결정.
- `RenderCore/Private/RenderGraphPrivate.cpp:213`: 기본 정책은 async 태그 패스만 실행, 별도 강제 모드는 모든 compute 대상.
- `RenderCore/Private/RenderGraphBuilder.cpp:1556`: cross-pipeline producer/consumer로 fork/join과 자원 수명 구성.
- `Renderer/Private/Lumen/Lumen.cpp:72`: 하드웨어 지원과 기능 설정으로 사용 여부 결정.

CreatorEngine에는 `DeclareAsyncCompute(pass)`를 추가했다. 이 호출은 compute-compatible 콜백 계약을 포함하며, 실제 async 실행을 요청한다. `DeclareComputeCompatible(pass)`만 호출하면 graphics 큐에 남는다. 지원하지 않는 큐·timeline, 그래프 모드, aliasing, graphics-only state에서는 graphics로 대체한다. 기존 큐·펜스·completion retirement와 실패 격리는 유지한다.

컴파일된 패스 순서를 바꾸지 않는다. 지정된 큐 배치에 대해 의존성을 한 번 구성하고, RAW/WAR/WAW와 큐 간 리소스 상태 소유권에 필요한 wait를 생성한다. 후보 그룹/개별 toggle, GPU 비용 시뮬레이터, minimum-gain 판정, 탐색 전용 128-entry topology cache, 실행 순서 override와 비용 모델 API는 제거했다. 기존 prediction/trial/simulation 진단 필드는 저장된 schema 소비자를 위해 0/false로 남고, 실행 정책을 `placement=authored`로 명시한다.

제품 패스의 async 선언은 현재 0개다. 기존 호환 선언을 실행 요청으로 자동 변환하지 않았다. mode 2는 `no-async-passes`로 graphics 실행하며 기본 mode 0을 유지한다. 향후 패스 묶음별 전체 프레임 개선이 확인되면 그 패스에만 실행 요청을 추가한다.

Matrix/Queue/DAG는 기존 snapshot과 실행 결과를 사용한다. 선택 패스 Inspector에 compute compatibility와 async request를 각각 표시한다. Freeze/Refresh의 snapshot 소유 구조는 유지한다.

## 검증

- VS 18 / v145 Debug·Release 빌드 통과. 최종 재컴파일 로그의 경고는 Debug 3개, Release 1개이며 오류는 0개다.
- 두 구성 모두 native 실행 437검사, authored 계획 76검사, 전체 RenderGraph 회귀 통과. native readback 오차 0, GPU validation 오류 0.
- 계획 회귀는 무측정 async 실행, compatibility-only graphics 유지, 타이밍/hint 독립성, 지원/상태/aliasing fallback, 큐 간 RAW·물리 소유권 wait, 구조 캐시·슬롯 재사용, viewer 요청 snapshot, 측정 identity/domain/history를 확인한다. native 회귀는 실제 dispatch/readback, authored 순서, 기록/제출 실패, 지연 회수·격리, frame retirement·병렬 기록을 확인한다.
- 첫 Debug 전체 회귀는 RG-V `StorageBytes()`에서 실패했다. 덤프에서 실제 `DiagnosticPass` 크기는 176바이트인데 선택된 inline COMDAT가 과거 168바이트 stride로 순회하는 것을 확인했다. snapshot 소비 번역 단위들을 재컴파일한 뒤 Debug·Release 전체 회귀가 통과했다. 실패 실행은 `Artifacts/rg8-explicit-20261022/native-debug`, 분석은 `crash-analysis.log`와 `crash-layout.log`, 통과 실행은 `native-debug-final`·`native-release-final`에 보존했다.

제품 async 순성능 수용과 RG7 조합은 완료로 변경하지 않는다. [기계 판독 원장](RenderRg8ExplicitAsync20261022.json).

## 실제 TestShadow 양방향 비교

Release, 동일 RTX 4070 Ti, window 1600×900 / Scene native render·display 972×394. 동일 프로세스 0→1→2 및 별도 프로세스 2→1→0. 모드마다 일반 프레임 32 warmup, 64 후보 중 시간순 적격 32개를 선택했다. 총 192개 일반 GPU 프레임이다. 이후 모드별 6초 lossless native-real 프로파일을 별도로 기록했다. GPU validation·upscaler·FG·aliasing·NIS·DeepDVC는 성능 수집에서 OFF다.

CPU는 프로파일의 전체 `RenderThreadFrame`이며 recording/evidence 비용을 포함한다. GPU는 프로파일을 끈 일반 프레임의 calibrated query span이다. primary/OFF는 timed-pass envelope이며 owned 경로만 QueuePrologue/QueueEpilogue scope를 포함한다. 표시 FPS로 환산하지 않는다.

| 순서·모드 | CPU p50 ms | CPU p95 ms | GPU graph span p50 ms |
|---|---:|---:|---:|
| Forward-0 | 3.281 | 3.844 | 5.460 |
| Forward-1 | 4.547 | 5.360 | 5.181 |
| Forward-2 | 4.571 | 5.167 | 5.650 |
| Reverse-0 | 3.192 | 4.065 | 5.565 |
| Reverse-1 | 4.496 | 5.155 | 5.683 |
| Reverse-2 | 4.625 | 5.512 | 5.505 |

mode 2의 양방향 64개 일반 표본은 전부 `placement=authored`, `no-async-passes`, compute batch 0, 탐색 0, simulation 0ms, 배치 3, 큐 간 wait 0이다. 계획 CPU p50은 정방향 0.01945ms / 역방향 0.01985ms다. 전체 mode 2 CPU p50은 같은 순서의 OFF 대비 +39.3% / +44.9%로, owned graphics 경로의 비용은 남아 있다. 새 정책을 제품 성능 채택으로 판정하지 않는다. 기본 mode 0을 유지한다.

실제 explicit async 실행은 native 테스트의 authored 패스로 확인했다. 제품 장면의 기본 graphics 실행과 synthetic native async 정확성 증거를 각각 기록했다. 후속 제품 opt-in에는 해당 패스 묶음의 전체 프레임 개선이 필요하다.
