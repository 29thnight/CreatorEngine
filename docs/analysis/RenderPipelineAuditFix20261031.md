# 렌더 파이프라인 감사 수정 — 2026-10-31

기준은 master `f4264beb85a4183ab4373313a2492fe5c427c739`다. 이번 변경은 재현한 RG9 정확성 결함과 CPU 기록·입력 준비의 반복 비용을 수정한다. Windows native 빌드·GPU 출력·일반 프레임 성능은 이 환경에서 실행하지 않았다. 따라서 제품 성능 수용이나 PHASE 완료율을 올리지 않는다.

## 수정한 문제와 결과

| 문제 | 변경 후 동작 | 보존한 경계 |
|---|---|---|
| transient mip 수만 달라져도 retained program을 재사용 | `MatchesStructuralSchedule()`에서 `mipLevels`를 비교하고 불일치하면 다시 compile | 신규 compile과 동일한 미초기화 subresource 거부, 마지막 정상 program의 Reset 복구 |
| 기록 target 하나를 worker에 제출한 직후 owner가 대기 | target 하나는 owner에서 즉시 실행; 여러 target은 기존 scheduler 사용 | worker/inline callback 재진입·중지 scheduler 거부, 예외 전파·callback 수명, 진단 요청의 기존 제출 경로 |
| inline 기록도 dispatch·worker job·owner join으로 집계 | 실제 다중 target 제출에서만 카운터 증가 | GPU wait/retirement는 별도이며 변경하지 않음 |
| 이미 계산한 모델 pin index가 `PooledDraw`에만 남음 | `EnhancedDrawItem` 끝에 선택적 hint로 전달하고 Seal에서 검증 후 사용 | 전체 handle·실제 descriptor owner·payload provenance 확인, 잘못된 hint는 기존 검색 fallback |
| 재질 identity 검색과 borrow에 각각 lock | `BorrowExact`의 한 lock에서 hint·검색·정확한 owner 확인 | 동일 값의 다른 instance와 pin되지 않은 generation 거부 |
| immutable draw의 material borrow를 반복 조회 | 자기 input의 실제 draw 주소와 source index가 일치하면 저장된 borrow 재사용 | 외부/복사 record는 table lookup, append-only pin table의 strong owner 유지 |
| 동일 geometry identity 목록을 매번 정렬 검사 | 이전 sealed input과 순서·key·전체 handle이 같으면 O(N) 비교로 collision proof 재사용 | 변경된 입력은 원래 validator, 현재 owner/payload 검증은 계속 수행 |
| 뷰별 generation 진단 scratch를 매번 새로 할당 | LiveState의 값 전용 배열 capacity 재사용 | 에셋 owner를 추가로 보관하지 않음, 기존 진단 값과 정렬 방식 유지 |

이는 제거한 검색·정렬·잠금·제출 작업에 대한 설명이다. 실제 프레임 시간 감소나 GPU overlap 증가를 측정한 결과는 아니다. GPU queue 정책, 제품 async 태그, aliasing 기본값은 이번 변경의 대상이 아니다.

교차 리뷰에서 inline callback의 재진입이 기존 worker 검사만으로는 막히지 않는 점을 확인했다. recording-plan TLS cache의 참조와 target 소유권을 보호하도록 callback 동안 RAII scope를 유지하고, PrepareParallel·RecordParallel·RecordSoftware·SubmitQueues 진입에서 중첩 기록을 거부한다. 예외 뒤 scope 복구도 검사한다. scheduler shutdown 자체를 inline callback 완료의 근거로 사용하지 않으며, 기존 owner/RenderThread join 순서가 native 해제보다 앞서야 한다.

## RG9 결함과 회귀

1 mip에서 mip 0만 쓰고 전체 texture를 읽는 선언은 유효하다. 접근 범위를 그대로 둔 채 2 mip로 바꾸면 mip 1이 초기화되지 않으므로 compile이 실패해야 한다. 원래 retained 경로는 기존 program을 재사용해 성공했고, 같은 선언의 fresh graph만 실패했다.

기존 `CheckTransientMips()`는 mip 수와 사용 범위를 동시에 바꿔 범위 차이만으로도 재컴파일됐다. 새 검사는 접근 범위를 고정한 `1 → 2 → 1 → 1` 전환, 게시 program의 직접 binding schema, cached/fresh 오류 동치와 실패 뒤 Reset 복구를 확인한다. fixture의 현 API 불일치를 먼저 복구한 뒤 원래 matcher에서 신규 검사 두 개가 실제 assertion으로 실패하는 RED를 확인했고, mip 비교를 추가한 뒤 기존 전체 suite를 그대로 통과했다.

공식 portable graph runner는 실제 graph translation unit을 빌드한다. native service와 platform allocation은 fixture이며, 사용하지 않는 worker 제출 경계는 여전히 명시적으로 거부한다. 미사용 WinRT 의존을 `RHIResourceTypes.h`에서 제거했고 SDK 대역 헤더를 production에 추가하지 않았다.

## 실행한 검증

환경: Linux x86-64, GCC 13.3, C++23 standalone/shipping 계약. enkiTS는 v1.12 commit `0289cf6ffce86697e1f46cb4eeca1c946bffec71`을 사용했다.

| 검사 | 결과 | 실제 범위 |
|---|---|---|
| 신규 RG9 회귀, 원래 matcher | 2개 assertion 실패 | mip-only 무효화와 cached/fresh 초기화 오류 동치의 RED |
| `verify-render-graph-program.py` 전체 | 통과 | 실제 CPU graph compiler/binding, 기존 범위·queue·alias·복구와 inline entry 검사; native device는 fixture |
| `verify-recording-task-graph.py --recording-helper-only` | 761검사 통과, wave-join mutation 검출 | 실제 helper·JobScheduler·ThreadPool, 물리 worker 1/4/8, 단일 owner·다중 worker·실패·수명·진단·재진입 |
| recording-helper ASan + UBSan | 761검사 통과, mutation 검출, leak detection OFF | 위 실제 CPU 실행 helper 범위만 해당 |
| `verify-scene-input-reuse.py` | 2,124검사 통과 | 실제 ownership/containers/mesh handle 및 추출한 production 함수 본문; 전체 Seal TU는 아님 |
| scene-input ASan + UBSan | 통과, leak detection OFF | 위 portable 계약 범위만 해당 |

scene-input 검사는 전체 mesh identity 19필드 변경, stale/범위 밖 hint, 같은 값의 다른 owner, generation 변경, 외부 draw 조작, 순서 변경·새 key collision, 1,021개 pin의 동시 append를 포함한다. 실제 `SceneViewInput::Seal`과 재질 경로를 쓰는 기존 native probe에도 회귀를 추가했지만, 작성된 native 검사를 실행 통과로 계산하지 않는다.

graph entry 회귀는 실제 RAII scope를 직접 활성화해 `PrepareParallel`과 `SubmitQueues`가 native 준비·plan·출력·queue 동작을 변경하기 전에 거부되고 scope 종료 후 정상 동작하는지 검사한다. 실제 helper callback 안의 중첩 호출과 예외 복구는 recording-helper suite가 검사한다. `RecordParallelInternal`·`RecordSoftware`의 조기 guard 위치는 정적 검토 범위이며 이 fixture에서 두 경로의 전체 native 기록을 실행하지 않는다.

### 재현 명령

저장소 루트에서 실행한다. recording runner는 위 commit의 로컬 enkiTS checkout 경로가 필요하며 의존성을 다운로드하지 않는다.

```bash
python Tools/regression/verify-render-graph-program.py
python Tools/regression/verify-recording-task-graph.py --enkits /path/to/enkiTS --recording-helper-only
ASAN_OPTIONS=detect_leaks=0:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 python Tools/regression/verify-recording-task-graph.py --enkits /path/to/enkiTS --recording-helper-only --sanitize
python Tools/regression/verify-scene-input-reuse.py
ASAN_OPTIONS=detect_leaks=0 python Tools/regression/verify-scene-input-reuse.py --sanitize
git diff --check
```

### 통과로 세지 않은 항목

- recording runner의 기본 전체 gate는 helper 변경 전부터 planning/storage pointer 재사용 assertion에서 실패한다. 선택 실행은 이 gate를 통과 처리하지 않으며, 기존 기본 경로와 assertion을 유지한다.
- `verify-translation-work-queue.py`는 최신 RHI profiling/backend scope/provenance를 반영하지 않은 fixture 때문에 컴파일되지 않았다. 해당 consumer의 통합 검증 통과로 계산하지 않는다.
- include-boundary gate는 sparse checkout에서 상향 include 7개·물리 경계 85건으로 실패했다. 지적한 source/project 경로 중 이번 PR에서 변경한 경로는 없고, project-parse 5건은 checkout에서 제외한 프로젝트다. 전체 master checkout의 결과나 나머지 실패 원인까지 이 관찰로 입증하지 않는다. allowlist는 변경하지 않았다.
- scene-input 기본 LeakSanitizer 실행은 컨테이너의 `/proc` task/ptrace 제약으로 실패했다. 이후 leak detection을 끈 ASan+UBSan 결과를 누수 검사 통과로 확대하지 않는다.
- GCC의 기존 reflection/noexcept 관련 경고가 출력된다. warning-free 빌드로 표시하지 않는다.
- Windows/MSVC Debug·Release 및 non-unity 빌드, DX12/Vulkan native 기록·RG9 GPU·재질 수명 회귀, 픽셀·validation, 일반 프레임 CPU/GPU p50/p95는 미실행이다. 단일 target의 owner 실행에 맞춘 native self-test, inline/worker 혼합 기록의 실패 복구·진단·종료 순서도 재실행이 필요하다.

## 보고서 정정과 잔여 범위

[RG9 구현 기록](RenderRg9Implementation20261023.md)의 mip program 검증 문구에 mip-only 사각지대를 명시하고, [정본 계획](../plans/RenderGraphDependencySchedulingPlan.md)과 [진척 원장](Phase4Progress20261022.md)에 현재 수정본의 native 재검증이 열려 있음을 덧붙였다. 과거 실행의 source/binary hash와 인일 원장은 보존하며, 그 기록을 이번 코드의 완료 근거로 재사용하지 않는다.

[기술설명서](../TechnicalGuide.md)의 자동 측정 기반 큐 배치 설명도 현재의 authored async 정책과 graphics-only primary 경로에 맞췄다. CPU worker 절감과 GPU async compute는 서로 다른 작업이다.

실제 제품 pass의 async opt-in과 전체 프레임 개선, RG7 조합, Declare 비용, 뷰 간 독립 실행, pending geometry/preview 정책, shadow cache 예산 재설계는 이 PR에서 완료한 것으로 주장하지 않는다. 이들 변경은 대표 장면의 trace와 native 수명·출력 검증을 함께 확보해야 한다.
