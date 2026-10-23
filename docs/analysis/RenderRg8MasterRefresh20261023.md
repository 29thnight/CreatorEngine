# master 재동기화와 전체 프레임 재측정 — 2026-10-23

> 2026-10-23 제품 결정 갱신: 사용자 요청으로 현행 RG8 구현을 제품 수용(done·16인일)한다. 기본 OFF를 유지하고 추가 성능 개선·제품 pass opt-in·RG7 조합은 [RG8-F 미래 계획](../plans/RenderGraphDependencySchedulingPlan.md#rg8-f)으로 이관한다. 아래 측정값과 당시 순성능 판정은 보존한다.

master를 24db52e89에서 0509753a3으로 fast-forward했다. 새 커밋은 d2a705ff3(렌더링 반복 할당 감소), 0509753a3(공용 CPU allocator Utility 분리)이다. 작업 트리에 있던 authored async 정책과 graphics-only primary 실행 경로를 보존해 통합했다. 이 측정은 최신 master와 해당 로컬 변경을 결합한 바이너리다.

## 통합과 보존

원격 변경 80개 파일 중 기존 로컬 수정과 5개가 겹쳤다. 새 queue plan·submission·barrier scratch·diagnostic 용량 재사용을 유지했다. 자동 비용 모델/시뮬레이션/실행 순서 변경은 되살리지 않았다. 로컬 SelectExecutionQueues도 새 ResetQueueDiagnostics를 사용해 계획 저장소 용량을 재사용한다. compute가 없는 mode 2는 계속 primary graphics 경로다.

통합 밖의 기존 수정 파일 56개는 시작 시 SHA-256과 일치하고, 기존 삭제 2,862개도 유지했다. 기존 변경 사본·patch·목록은 Artifacts/rg8-master-refresh-20261022에 보관했다. 겹친 5개 파일의 원본 stash도 유지했다. 최종 index는 비어 있고 미해결 충돌은 없다. 새 커밋과 push는 하지 않았다.

새 할당 회귀 fixture가 예전 QueueCostModel API를 사용하므로 현재 authored 요청 방식에 맞췄다. 현재 코드는 timing hint 유무 양쪽에서 명시적 async를 검사한다. 구형 baseline의 자동 배치 API는 dependent template 분기로 보존하고, JSON 측정 이름은 현재 정책을 명확히 표시한다. 생산 코드에 자동 배치 모델을 다시 추가하지 않았다.

## 빌드·회귀

VS 18/v145 Debug·Release 전체 빌드 모두 오류 0이다. 경고는 Debug 23개, Release 31개다. Release 경고 중 8개는 Utility_Framework.pdb 타입 기록 손상의 LNK4020이며 일부 디버거 타입 정보 접근에 영향을 준다. 그 밖에 SceneRuntime 생성 파일 중복, C# 분석/trim 경고가 있다.

Debug·Release 각각 RG8 native 실행 441검사, 계획 82검사와 전체 RenderGraph 회귀를 통과했다. GPU validation 오류와 native readback 오차는 0이다. Debug software RHI native 회귀도 비동기/병렬 번역, upload rollover, pool/arena retirement와 owned queue를 포함해 통과했다.

새 Utility Arena와 기존 RenderArena pool을 함께 연결한 production CPU graph 회귀도 통과했다. 기능 합성/광원 선택 회귀는 1,027검사를 통과했고, 워밍업 후 128프레임의 feature dispatch·observer dispatch·256-light selection 각각 standard new와 mimalloc 요청이 0이었다. CPU 대역 allocator는 실제 요청을 계수하며 Windows CRT로 저장소를 제공한다.

현재 정책으로 갱신한 graph allocation fixture는 8회 워밍업 후 각 512회 반복하여 10개 예산을 통과했다. 선언, 일반/subresource/alias compile, authored queue plan(hint 있음/없음)은 추가 요청 0이다. queue 제출은 native mock의 할당을 제외하고 완료 lease용 standard new 1회/56바이트를 유지하며 mimalloc 요청은 0이다. 이 lease는 GPU 완료까지 그래프를 보호하므로 제거하지 않는다. CPU fixture의 할당 결과와 에디터 전체 프레임 측정은 별도 증거다.

## 동일한 뷰 조건의 OFF/ON

RTX 4070 Ti, Windows 드라이버 32.0.16.1088. 같은 Release exe/runtime, TestShadow, 창 1600×900, Scene render/display 972×394, native parallel CPU executor. upscaler·FG·NIS·DeepDVC·aliasing은 OFF다. Scene 기본 fixed-60Hz 정책, 같은 workspace, 동일한 pacing(display / backgroundFps 10)을 유지했다. 실제 렌더 수는 표에 별도로 표시한다.

프로세스별 0→1→2, 2→1→0 순서를 사용했다. 일반 GPU 표본은 mode마다 32 warmup·64 후보에서 시간순 적격 32개를 골라 총 192개다. mode 1/2의 접수 증거 128개 모두 primary-graphics이고 compute/owned 제출 batch는 0이었다.

모든 프레임 표는 CPU wall time(ms)이다. 렌더 포함 전체 프레임은 RenderCommandRecord를 포함하는 RenderThreadFrame의 시작부터 끝까지이며, 특정 내부 scope만 잘라낸 값이 아니다. 모든 RT 프레임 분포에는 작업이 없는 프레임도 들어간다. 표시 FPS와 구분한다.

### 진단 출력 OFF — 모드별 20초 기록

별도 보충 cohort에서 CREATOR_RG8_EVIDENCE=0으로 기록했다. per-submission queue 접수 audit은 앞의 일반 cohort에 보존했으며, 이 cohort는 프로파일의 native-real 신원과 완전성을 검사한다. 실제 렌더를 포함한 프레임당 Scene 뷰는 모두 하나였다.

| 순서·mode | 모든 RT 프레임 p50 / p95 | 렌더 포함 전체 RT 프레임 p50 / p95 | 렌더 포함 프레임 수 |
|---|---:|---:|---:|
| Forward-0 | 0.181 / 6.259 | 3.638 / 6.977 | 608 |
| Forward-1 | 0.191 / 5.998 | 3.639 / 6.748 | 603 |
| Forward-2 | 0.172 / 5.947 | 3.584 / 6.466 | 603 |
| Reverse-0 | 0.173 / 6.180 | 3.650 / 7.069 | 606 |
| Reverse-1 | 0.182 / 6.006 | 3.600 / 6.605 | 607 |
| Reverse-2 | 0.170 / 6.021 | 3.546 / 7.187 | 609 |

- Forward mode 2 / OFF: 렌더 포함 전체 프레임 p50 -1.49%, p95 -7.32%.
- Reverse mode 2 / OFF: 렌더 포함 전체 프레임 p50 -2.84%, p95 +1.67%.

mode 2의 p50은 약 1.5~2.8% 낮았으나 p95는 양방향에서 일관되게 개선되지 않았다. 이 장면에는 authored async 태그가 없고 실제 compute 큐 batch도 없다. async overlap 이득이나 제품 수용을 선언하지 않으며 기본 mode 0을 유지한다. 이전 10초 실행의 변동이 큰 수치와 단순 차감해 이번 커밋의 정확한 기여율로 사용하지 않는다.

### 진단 출력 ON — 모드별 10초 기록

| 순서·mode | 모든 RT 프레임 p50 / p95 | 렌더 포함 전체 RT 프레임 p50 / p95 | 렌더 포함 프레임 수 | 일반 GPU query span p50 |
|---|---:|---:|---:|---:|
| Forward-0 | 0.168 / 6.462 | 3.716 / 7.117 | 305 | 7.959 |
| Forward-1 | 0.157 / 6.180 | 3.791 / 6.481 | 305 | 8.141 |
| Forward-2 | 0.153 / 6.215 | 3.963 / 6.694 | 307 | 7.939 |
| Reverse-0 | 0.163 / 6.729 | 4.203 / 7.210 | 306 | 7.455 |
| Reverse-1 | 0.165 / 7.211 | 4.893 / 7.754 | 307 | 7.821 |
| Reverse-2 | 0.165 / 7.372 | 4.438 / 7.883 | 306 | 8.924 |

GPU query span은 primary timed-pass envelope이며 물리적인 표시 GPU 프레임 시간과 구분한다. 진단 출력 ON과 OFF cohort는 별도로 보관했다.

## 출력과 증거

같은 Release의 실제 제품 장면 OFF/ON 제어 캡처 8개에서 입력을 대조했다. attachment/진단 단계 128개 비교 모두 최대 절대 오차 0, nonfinite 0이다. 캡처 시간은 성능 표본에 넣지 않았다. 제품 성능/제어 캡처의 GPU validation은 OFF이고, GPU validation 통과는 별도의 native 회귀 실행이다.

Artifacts/rg8-master-refresh-20261022/result.json에 소스·바이너리 해시와 전체 판정을 저장했다. native-debug / native-release / software-debug는 에디터 회귀, normal은 일반 표본·10초 프로파일, quiet는 20초 프로파일, capture/pixel-comparison.json은 출력 비교다. graph-probe-result.log / feature-probe-result.log / allocation-budget.json은 CPU 회귀다. binary-debug / binary-integrated에 실제 검증·측정한 바이너리를 보관했다.
