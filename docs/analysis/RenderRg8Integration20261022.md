# RG8 원격 구조와 로컬 정책 통합 — 2026-10-22

master를 ea87639ec에서 24db52e89로 fast-forward하고, 겹친 7개 파일의 로컬 RG8 변경을 통합했다. 원격 GraphProgram/FrameBindings·뷰별 입력/실행 상태·subresource/공유 읽기·DAG aliasing·제출 전 전체 batch 번역·큐별 retirement·GPU 표시 계약을 유지했다. async 배치는 DeclareAsyncCompute로만 요청하며, 그래프가 compute 큐를 실제 사용하지 않으면 primary graphics 비동기 제출 경로를 쓴다.

## 통합한 실행 경로

- 요청 mode와 실제 실행 경로를 구분한다. mode 1과 compute 없는 mode 2는 effective mode 0이다.
- 준비는 공통 PrepareParallel을 사용한다. 미소비·현재 upload/descriptor 예약만 owned 제출에 넘긴다.
- 실제 async일 때만 owned 큐와 compute profiler를 초기화한다. 모든 mode의 background GPU timing 주기는 8로 맞췄다.
- 원격 EndFrame의 CPU 대기 제거를 유지한다. 제출 ticket과 GPU fence가 슬롯 재사용·표시·수명을 관리한다.
- 원격의 재사용 queue-planning scratch와 mutex를 유지하고 자동 비용 모델·배치 시뮬레이션을 제거했다. 공유 읽기 검사는 실제 async 계획에서 수행한다.
- GraphProgram 직접 바인딩에서도 asyncCompute 플래그를 복원한다. 태그 변경은 구조 재사용을 거부한다.
- DAG가 수명 순서를 증명한 aliasing 그래프는 authored async를 허용한다. 이에 맞춰 기존 aliasing 일괄 거부 회귀를 수정했다.

## 기존 변경 보존

통합 밖의 기존 수정 파일 50개는 작업 전 SHA-256과 일치하고, 기존 삭제 2,862개도 보존했다. 작업 전 수정 파일 사본·patch·삭제 목록은 Artifacts/rg8-integration-20261022에 있다. 겹친 7개 파일의 원본 stash도 유지했다. 새 커밋이나 push는 하지 않았다. 최종 index에 staged 파일과 미해결 충돌은 없다.

## 빌드와 회귀

VS 18/v145로 Debug·Release 전체 빌드를 수행했다. Debug 첫 빌드는 경고 23·오류 0, 최종 증분 빌드는 경고 1·오류 0이다. Release는 경고 23·오류 0이다. SceneRuntime 생성 파일 중복과 C# 분석/trim 경고가 남아 있다.

두 구성 모두 native RG8 실행 441검사·계획 82검사와 전체 RenderGraph 회귀를 통과했다. GPU validation 오류와 native readback 오차는 0이다. Debug software RHI native 회귀도 통과했다: 비동기/병렬 번역, upload rollover, arena/queue retirement, owned 큐와 전체 그래프를 검사했다.

새 Compile의 native import 검사가 기존 compile-only fixture의 미등록 handle을 거부했다. R6b 대역이 소유한 texture/buffer descriptor와 해제 상태를 추적하게 수정하고, readback fixture 자원도 명시적으로 등록했다. 중립 buffer 검사는 대역이 소유한 유효한 buffer를 사용한다. 생산 Compile의 유효성 검사는 유지했다. 실패 기록은 native-debug, native-debug-final, native-debug-complete에 보존했고, 최종 통과 기록은 native-debug-passed와 native-release에 있다.

실제 production graph TU의 CPU 회귀는 프로그램 바인딩·소유권·부분 범위 hazard/barrier·공유 읽기·happens-before async aliasing을 통과했다. 직접 바인딩의 async 보존과 compute-compatible만으로는 async가 선택되지 않는 검사도 추가했다. 기능 합성 81검사, LiveViewScheduling과 PreparedViewInputPool 회귀도 통과했다. 이 CPU 회귀는 native services를 대역으로 제공하며, DX12 native 검사는 위의 별도 에디터 실행 기록이다.

## 같은 조건의 OFF/ON 전체 프레임 비교

같은 Release 바이너리, TestShadow, 창 1600×900, 실제 Scene render/display 972×394, native parallel CPU executor, upscaler/FG/NIS/DeepDVC/aliasing OFF. 각 프로세스 안에서 0→1→2와 2→1→0을 실행했다. Scene 기본 fixed-60Hz 정책, 같은 workspace와 동일한 pacing 상태(display, background 10)를 사용했다. 정책 설정과 실제 렌더 횟수는 구분한다. 모든 기록은 한 활성 프레임에 Scene 뷰 하나를 기록했다.

일반 표본은 모드별 32 warmup·64 후보에서 시간순 적격 32개를 고른 총 192개다. 별도의 10초 lossless CEPROF를 모드별로 기록했다. native-real provenance와 GPU 이벤트 신원이 통과했다. 아래 값은 CPU wall time(ms)이며 표시 FPS가 아니다.

첫 cohort는 RG8 증거 출력을 켰다. mode 1/2의 128 표본은 전부 primary-graphics 접수이고, compute batch·owned 제출 batch는 0이었다. 요청 mode의 native 완료를 만들어 넣지 않았다. 이 cohort의 GPU query span은 primary의 timed-pass envelope다.

### RG8 증거 출력 ON

| 순서·mode | 모든 RT 프레임 p50 / p95 | 렌더 포함 전체 RT 프레임 p50 / p95 | 렌더 포함 프레임 수 | 일반 GPU query span p50 |
|---|---:|---:|---:|---:|
| Forward-0 | 0.300 / 6.032 | 3.793 / 6.580 | 309 | 7.075 |
| Forward-1 | 0.213 / 5.951 | 4.289 / 6.800 | 310 | 8.253 |
| Forward-2 | 0.254 / 5.402 | 4.217 / 6.579 | 310 | 7.929 |
| Reverse-0 | 0.278 / 4.584 | 3.821 / 6.125 | 306 | 6.896 |
| Reverse-1 | 0.170 / 6.021 | 3.803 / 6.330 | 305 | 7.701 |
| Reverse-2 | 0.265 / 6.820 | 5.179 / 8.058 | 298 | 8.584 |

전체 RT 프레임 p50에는 렌더 작업이 없는 프레임이 들어가 낮게 나온다. 활성 프레임 값도 RenderCommandRecord 구간만 자른 값이 아니라, 그 작업을 포함하는 전체 RenderThreadFrame의 처음부터 끝까지다. 이 cohort에서 mode 2의 활성 프레임 p50은 OFF 대비 정방향 +11.2%, 역방향 +35.5%였다. 프로파일에는 정방향 RenderSubmit 비용 증가와 역방향 애니메이션/재질 준비/기록 join 증가가 함께 관찰되었다.

### RG8 증거 출력 OFF 추가 비교

같은 바이너리·모드 순서·워밍업·후보 수·10초 기록·뷰 조건에서 CREATOR_RG8_EVIDENCE만 0으로 바꿨다. 이 cohort는 전체 프레임 프로파일의 보충 비교이며 per-submission queue 접수 audit을 수행하지 않는다. 큐 접수 증거는 앞 cohort에 별도로 남아 있다. 후보 표본을 완전한 queue admission 표본으로 둔갑시키지 않았으며, 프로파일 자체의 native-real 신원·완전성은 검사했다.

| 순서·mode | 모든 RT 프레임 p50 / p95 | 렌더 포함 전체 RT 프레임 p50 / p95 | 렌더 포함 프레임 수 |
|---|---:|---:|---:|
| Forward-0 | 0.409 / 6.370 | 4.145 / 7.118 | 300 |
| Forward-1 | 0.585 / 6.961 | 4.442 / 7.744 | 304 |
| Forward-2 | 0.438 / 7.678 | 5.152 / 8.384 | 302 |
| Reverse-0 | 0.360 / 8.126 | 5.289 / 9.295 | 281 |
| Reverse-1 | 0.300 / 6.162 | 3.797 / 6.842 | 307 |
| Reverse-2 | 0.328 / 6.436 | 4.133 / 7.198 | 307 |

- Forward mode 2 / OFF: 활성 전체 프레임 p50 +24.3%, p95 +17.8%.
- Reverse mode 2 / OFF: 활성 전체 프레임 p50 -21.9%, p95 -22.6%.

설정은 같지만 실제 렌더 수와 공통 작업 시간은 실행 구간마다 변동했다. mode 2가 양방향에서 일관되게 개선되거나 악화되는 결과는 재현되지 않았다. 따라서 현재 장면의 성능 이득이나 async 제품 수용을 선언하지 않는다. 제품의 async 태그는 0이고 기본 mode 0을 유지한다. 예전 구조의 전체 프레임 수치는 뷰 스케줄링 조건이 달라 직접 전후 성능으로 비교하지 않는다.

## 제품 출력 비교

Release 실제 장면의 OFF/ON 제어 캡처 8개에서 입력을 대조하고 attachment/진단 단계 128개를 비교했다. 최대 절대 오차 0, nonfinite 0이다. 캡처 비용은 성능 표본에 포함하지 않았다. 제품 성능 수집과 이 제어 캡처의 GPU validation은 OFF이고, GPU validation 통과는 별도의 native 회귀 기록이다.

## 증거

- Artifacts/rg8-integration-20261022/result.json: 소스/바이너리 해시와 전체 판정
- native-debug-passed / native-release / software-debug: 최종 에디터 회귀
- graph-probe-result.log / feature-probe-result.log / view-result.log / input-result.log: CPU 회귀
- normal: 신원 연결된 일반 표본과 증거 출력 ON 프로파일
- quiet: 증거 출력 OFF 보충 프로파일
- capture/pixel-comparison.json: 128개 출력 비교
- binary-debug / binary-integrated: 검증한 바이너리 사본
