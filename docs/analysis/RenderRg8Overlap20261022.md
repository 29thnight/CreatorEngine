# RG8 제품 장면 overlap·전체 프레임 비용 — 2026-10-22

## 판정

**TestShadow에서 compute 실행과 실제 overlap은 발생한다. 전체 프레임 비용은 OFF가 작으므로 기본 OFF를 유지한다.** RG8 상태는 progress, 제품 수용 기성은 0으로 유지한다.

`insufficient-gain`을 본 직전 viewer 검증 대상은 빈 SampleScene이었다. 이번 초기 진단 snapshot은 후보 23개, 측정 누락 0, state 거부 0이었다. 따라서 이 snapshot은 후보 시간 문턱이 아니라 최종 예상 이득 문턱에서 탈락했다. 제품 장면 로드·워밍업 후의 TestShadow에서는 두 수집 순서 모두 mode 2의 32/32 샘플이 compute를 제출했다.

## 현재 코드와 원인

- `EnhancedSceneRendererLiveDX12Adapter.cpp`는 mode 2의 후보 문턱으로 **1µs**를 전달한다.
- `BuildQueueSchedule`은 선언된 compute 호환성, 지원 state, 명시적 버전 graph, 측정 이력을 검사한다. `QueueCostModel` 기본값은 handoff **20µs**, 최소 예상 이득 **50µs**다.
- `insufficient-gain`은 후보가 없을 때와 탐색 결과의 예상 이득이 50µs보다 작을 때 사용된다. 이번 초기 snapshot의 후보 23개는 후자에 해당한다.
- 모델은 측정된 pass 시간과 고정 handoff 비용으로 두 큐를 시뮬레이션한다. CPU 기록·native 제출 비용과 batch/전이 비용을 별도 보정한 모델은 구현돼 있지 않다. `predictionCalibrated=false`를 유지한다.
- 수정 후 TestShadow의 mode 1은 중앙값 **3 batch / 0 wait / 547 barrier**, mode 2는 **48 batch / 23 wait / 약 1,968 barrier**였다. native 제출 CPU 중앙값은 mode 1의 **0.20~0.26ms**에서 mode 2의 **1.67~1.91ms**로 증가했다.
- mode 2의 예상 GPU 이득 중앙값은 **0.53~0.54ms**, 실제 overlap 중앙값은 **0.20~0.21ms**였다. GPU graph span에서 측정된 pass가 덮지 않은 구간은 약 **2.0ms**였고 mode 1은 약 **0.04ms**였다. 이 미사용 구간을 개별 queue wait 시간으로 합산하지 않았다.

## 적용한 수정

`BuildQueueDependencies`가 같은 compiled 구조·큐 배치의 CPU hazard topology를 재사용한다. owner-thread TLS 캐시는 최대 128개, pass 256개 이하·edge 4,096개 이하로 제한한다. 공유 compiled 구조의 소유권과 모든 queue assignment가 일치해야 사용하며, 큐 실행용 순서로 바뀐 graph는 제외한다. planning scratch와 adjacency 저장 공간도 재사용한다.

GPU 측정값과 placement 판단은 매번 다시 계산한다. native handle·callback·GPU owner는 캐시에 보관하지 않는다. 배리어 생성, COMMON release/acquire, reader join, 큐 제출·retirement는 기존 실행 경로에서 처리한다. 측정 비용을 0으로 바꿨다가 복원하면 같은 graph에서 fallback과 compute 채택이 다시 결정되는 검사를 추가했다.

기존 native 검사 개수 오류도 수정했다. `1162d4f9a`의 `ResetSoftwareSegment` 검사가 3프레임 루프에 추가돼 실제 437항목이 됐지만 기대값은 434였다. 수정 전 바이너리에서도 실패를 재현하고 실제 개수·변경 diff를 대조했다. native 기대값은 437, 새 planning 검사는 131로 맞췄으며 불일치 시 실제/기대 개수를 출력한다.

## 비교 조건

- RTX 4070 Ti, driver 610.88, VS18/v145 Release, DX12.
- 실제 `Dynamic_CPP/Assets/Scenes/TestShadow.creator`, window 1600×900, **Scene native render/display 972×394**.
- upscaler·FG·NIS·DeepDVC·aliasing·GPU validation OFF.
- 같은 프로세스에서 Forward **0→1→2**, 별도 프로세스에서 Reverse **2→1→0**. mode 0은 기존 실행기, mode 1은 graphics-only owned 실행기, mode 2는 overlap 탐색이다.
- 일반 프레임 warmup 32개 뒤 후보 64개를 수집하고, identity·완전한 이력·calibrated interval 검사를 통과한 첫 32개를 채택했다. 바이너리당 192개, 전후 합계 **384개**다.
- 일반 샘플 수집 후 같은 mode에서 별도 12초 `.ceprof`를 녹화했다. 표의 CPU는 완전한 **RenderThreadFrame wall time**, GPU는 녹화하지 않은 일반 샘플의 **calibrated query span**이다. primary/OFF는 timed-pass envelope이고 owned 경로는 QueuePrologue/QueueEpilogue scope도 포함한다. CPU whole-frame에는 녹화와 RG8 evidence 비용이 포함된다. FPS·물리 표시 주기를 계산한 값은 아니다.
- 첫 수집은 녹화의 nonzero `captureGeneration` 때문에 일반 프레임 게이트에서 0/32로 제외됐다. 수집 창을 분리해 재실행했으며 해당 첫 수집은 아래 수치에 포함하지 않았다.

## 측정 결과

단위 ms. CPU 칸은 p50 / p95, GPU는 p50이다.

| 순서·mode | 수정 전 CPU | 수정 후 CPU | 수정 전 GPU | 수정 후 GPU |
|---|---:|---:|---:|---:|
| Forward OFF(0) | 3.156 / 3.700 | 3.447 / 4.112 | 5.857 | 5.627 |
| Forward graphics(1) | 4.611 / 5.682 | 4.659 / 5.552 | 5.767 | 5.734 |
| Forward compute(2) | 11.947 / 15.087 | 10.891 / 13.565 | 7.210 | 7.375 |
| Reverse OFF(0) | 3.190 / 3.795 | 3.178 / 3.699 | 5.685 | 5.692 |
| Reverse graphics(1) | 4.674 / 5.822 | 4.680 / 5.722 | 5.699 | 5.712 |
| Reverse compute(2) | 11.444 / 13.215 | 10.072 / 11.911 | 6.953 | 7.483 |

mode 2 의존성 재계산 중앙값은 **2.126→0.562ms / 2.123→0.582ms**로 72.6~73.6% 감소했다. 전체 planning 중앙값은 **3.693→2.104ms / 3.693→2.501ms**였다. 전체 RenderThreadFrame p50은 이 수집에서 **8.8% / 12.0% 감소**했다. OFF 제어군 p50은 Forward +9.2%, Reverse -0.4%였으며, 기록된 전후 분포와 이 제어군을 함께 보존한다.

수정 후에도 mode 2 CPU p50은 OFF보다 **216% / 217% 증가**했고 GPU span도 증가했다. topology 재사용으로 CPU 비용은 줄었지만 현재 batch·전이·제출 비용을 실제 overlap이 회수하지 못한다. 기본 ON 채택 근거는 성립하지 않는다.

## 검증과 남은 작업

- Debug/Release 빌드 성공. 양 구성에서 GPU validation을 켜고 native **437항목**, planning **131항목**, 전체 RenderGraph 회귀 통과. RG5 final은 **129프레임·9 stage·최대 픽셀 오차 0**, validation error 0.
- 실제 live snapshot의 누락·잘못된 frame/view/해상도/temporal identity 변이 **52개 거부**.
- 실제 TestShadow의 일반 mode 2 프레임은 별도 DX12 **basic debug layer** 실행에서 compute 제출과 validation 문제 0·메시지 드롭 0을 확인했다. GBV shader instrumentation 실행과 구분한다.
- 일반 프레임은 양 바이너리·양 순서에서 모두 32/32 identity gate를 통과했고 raw calibrated interval을 다시 검증했다. `.ceprof`는 lossless native-real gate를 통과했다.
- 추가 제품 GBV readback 검증은 완료되지 않았다. CPU hot thread는 `D3D12SDKLayers::CreatePatchedPipelineState → NVIDIA shader creation`에 있었다. 스택 채집 중 HTTP polling 제한으로 실행이 중단됐으며 통과로 계상하지 않는다. native 전체 GPU-validation 회귀와 별도 결과로 보존한다.
- RG8의 다음 대상은 **많은 batch/COMMON 전이·native 제출 비용을 가격에 반영하는 모델과 탐색 budget**, RG7 조합 및 지원 행렬이다. cross-queue read/read 소유권이나 실제 의존 edge를 제거해 overlap을 만들지 않는다.

## 증거

실행 원장은 `Artifacts/rg8-overlap-20261022/`에 보존한다. `baseline-normal-final/`과 `after-normal/`에 normal cohort, raw interval, `.ceprof`, whole-frame 요약 및 binary identity가 있다. `native-final-Debug/`, `native-final-Release/`는 회귀 결과다. `harness-count-cause.json`은 기존 개수 오류의 재현·원인이고 `product-validation/capture-stack-cdb.log`는 추가 GBV 생성 단계의 스택이다.

[기계 판독 원장](RenderRg8Overlap20261022.json) · [RG8 소유 계획](../plans/RenderGraphDependencySchedulingPlan.md)
