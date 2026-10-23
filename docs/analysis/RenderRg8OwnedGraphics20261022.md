# RG8 owned graphics 추가 비용 제거 — 2026-10-22

mode 2에서 compute batch가 0이어도 별도 owned 기록·제출 경로를 사용했기 때문에 CPU 전체 프레임이 느려졌다. 이 경로 선택을 실제 compiled queue plan에 맞췄다. graphics-only 그래프는 mode 0과 같은 primary graphics 기록·RHI 비동기 제출을 사용하고, 실제 compute 배치가 있는 그래프만 owned 큐 경계를 연다.

## 원인과 로컬 UE 5.6.1 비교

수정 전 현재 바이너리의 TestShadow 정방향 프로파일에서 RenderThreadFrame 평균/engine frame은 OFF 3.044ms, mode 2 4.640ms였다. RenderCommandRecord가 0.947→2.319ms로 늘어 증가량의 대부분을 차지했다. 그 안팎의 RenderThread RHITicketWait는 0.0005→0.272ms다. 중첩된 scope이므로 이 수치들을 더하지 않는다.

현재 소스의 추가 직렬 구간:

1. `BeginFrame`이 요청 mode만 보고 deferred immediate 기록을 껐다.
2. 그래프가 async를 실제 사용하는지 결정하기 전에 `BeginQueueFrame`이 prefix를 제출하고 RHI CPU ticket을 기다렸다.
3. graphics-only도 owned prologue/graph/epilogue 세 배치를 별도로 기록·제출했다. `RHISoftwareCommandQueue::Submit`은 RHI thread에서 번역과 native 제출을 실행하는 `Execute` 작업이 끝날 때까지 호출자를 기다리게 한다.
4. 이 전체 SubmitOwnedGraph 호출이 RenderCommandRecord 안에 있었다. 프레임 끝에서도 owned 요청이라는 이유로 제출 CPU ticket을 기다렸다.
5. mode 2 요청만으로 compute queue/query heap과 비어 있는 compute profiling frame을 만들고, 자동 비용 모델을 제거한 뒤에도 모든 프레임 GPU timing을 강제했다.

이 대기들은 GPU 완료 대기가 아니라 RHI CPU 작업 완료 대기다. GPU 프레임 슬롯·자원 회수 fence와 구분한다.

로컬 `C:/Program Files/Epic Games/UE_5.6/Engine/Source`:

- `Runtime/RenderCore/Private/RenderGraphPass.cpp:435`: AsyncCompute 태그로 파이프라인 지정.
- `Runtime/RenderCore/Private/RenderGraphBuilder.cpp:2058`: 준비된 command list들을 `QueueAsyncCommandListSubmit`으로 넘긴다.
- 같은 파일의 `ExecuteSerialPass` 흐름은 기존 command list를 사용한다. `ImmediateFlush(DispatchToRHIThread)`는 `bDispatchAfterExecute` 등 명시적 경계에서 호출한다.
- `Runtime/RHI/Private/RHICommandList.cpp:1578`: `QueueAsyncCommandListSubmit`은 목록을 모아 `GRHICommandList.Submit(..., ERHISubmitFlags::None)`으로 전달한다.

언리얼도 작업 준비와 명시적 flush 경계에서 기다린다. graphics-only RDG를 이유로 우리처럼 별도 owned 세 배치의 동기 제출을 매번 구성하는 경로는 이 실행 흐름에 없다.

## 구현

- `SelectExecutionQueues`가 Compile 후 실제 queue plan을 선택한다. 요청 mode와 실제 경로는 따로 기록한다. mode 1과 compute 없는 mode 2의 effective mode는 0(primary graphics)이다.
- 준비는 공통 `PrepareParallel`로 수행한다. 실제 async에 한해서 이후 `BeginQueueFrame`을 연다. 이 과정은 기존 upload recording/descriptor 예약을 유지하며, SubmitQueues는 미소비·현재 예약만 받아 소비한다.
- graphics-only는 기존 RecordSoftware/RecordParallel→Enqueue→EndFrame을 사용한다. native RHI 큐·fence·completion retirement와 실제 async의 join은 유지한다.
- deferred immediate 기록을 요청 mode에 상관없이 적용한다. 실제 owned 프레임에서만 owned EndFrame 제출 ticket을 기다린다.
- compute queue/profiler는 실제 async를 사용할 때 초기화한다. compute profiling token도 해당 프레임에서만 만든다. timing 주기는 모든 요청 mode에 동일하게 적용한다.
- primary 경로의 증거는 `executionPath=primary-graphics`, `primaryEnqueued=true`다. native completion·owned batch 수를 만들어 내지 않는다. 일반 GPU interval의 frame/view/submission ID를 연결해 실제 완료된 표본을 감사한다.

## 검증 및 프레임 비교

VS 18/v145 최종 Debug·Release 빌드 모두 오류 0·경고 1이다. 두 구성에서 native 441검사, 계획 82검사와 전체 RenderGraph 회귀가 통과했다. native GPU validation 오류·readback 오차는 0이다. 실제 prepared async의 upload/descriptor/frame retirement를 검사했다.

실제 TestShadow OFF/ON 제어 캡처는 별도 실행했다. 8캡처의 입력을 대조하고 attachment·진단 단계 128개를 비교해 최대 오차 0·nonfinite 0을 확인했다. 이 캡처 시간은 성능 표에 포함하지 않는다. 성능 수집과 제품 캡처의 GPU validation은 OFF이며, GPU validation 통과는 앞의 native 회귀 결과다.

primary 접수 증거에 native 완료·추가 compute/wait·누락/중복 pass 등을 주입한 9개 변조 사례도 모두 거부했다.

### 전체 CPU 프레임

동일 Release TestShadow, 같은 RTX 4070 Ti / driver 610.88, window 1600×900 / Scene native render·display 972×394. 바이너리마다 동일 프로세스 0→1→2 및 별도 프로세스 2→1→0. 각 mode에서 일반 프레임 32 warmup, 64 후보 중 시간순 적격 32개를 선택했다. 수정 전후 총 384개 일반 GPU 표본이다. 이후 mode별 6초 lossless native-real 프로파일을 별도로 기록했다. upscaler·FG·aliasing·NIS·DeepDVC는 OFF다.

CPU는 완전한 RenderThreadFrame wall time이며 프로파일·evidence 비용을 포함한다. 단위 ms.

| 순서·mode | 수정 전 p50 / p95 | 수정 후 p50 / p95 | 수정 전 같은 순서 OFF 대비 p50 | 수정 후 같은 순서 OFF 대비 p50 |
|---|---:|---:|---:|---:|

| Forward-0 | 3.031 / 3.468 | 3.427 / 4.263 | +0.0% | +0.0% |
| Forward-1 | 4.559 / 5.286 | 3.375 / 3.968 | +50.4% | -1.5% |
| Forward-2 | 4.606 / 5.331 | 3.340 / 3.901 | +52.0% | -2.6% |
| Reverse-0 | 3.498 / 4.255 | 3.196 / 3.715 | +0.0% | +0.0% |
| Reverse-1 | 6.400 / 9.322 | 3.443 / 4.087 | +83.0% | +7.7% |
| Reverse-2 | 4.624 / 5.378 | 3.347 / 3.936 | +32.2% | +4.7% |

mode 2의 p50 자체는 양방향 -27.5% / -27.6%다. OFF 대조군도 정방향 +13.1%, 역방향 -8.6% 움직였으므로 이 원시 감소율 전체를 정확한 최적화 기여율로 쓰지 않는다. 같은 실행의 OFF 대비 추가 비용은 +52.0% / +32.2%에서 -2.6% / +4.7%가 됐다. 새 경로가 OFF보다 일관되게 빠르다는 판정도 하지 않는다.

같은 프로파일의 RenderCommandRecord 평균/engine frame은 mode 2에서 2.319 / 2.352ms → 0.998 / 0.999ms였다. RenderThread RHITicketWait는 0.272 / 0.278ms → 약 0.001ms다. 추가 기록·동기 제출 비용 제거는 코드 경로와 이 scope 변화로 확인된다.

### GPU 표본과 실제 경로

GPU 수치는 프로파일을 끈 일반 프레임의 calibrated query span이다. primary 경로는 timed-pass envelope이고, owned 경로는 QueuePrologue/QueueEpilogue 두 scope도 포함한다. 물리 표시 GPU 프레임 전체나 FPS로 바꾸지 않는다. 이전 RG8 문서의 모든 mode를 graph prologue~epilogue로 부른 문구도 이 구현에 맞춰 정정했다.

| 순서·mode | 수정 전 GPU span p50 ms | 수정 후 GPU span p50 ms |
|---|---:|---:|
| Forward-0 | 5.780 | 5.048 |
| Forward-1 | 5.641 | 5.631 |
| Forward-2 | 5.648 | 5.655 |
| Reverse-0 | 5.152 | 5.722 |
| Reverse-1 | 5.618 | 5.737 |
| Reverse-2 | 5.724 | 5.731 |

수정 후 mode 1·2의 일반 표본 128개 모두 primary-graphics로 접수됐고 owned batch·owned wait·compute batch는 0이었다. 같은 pass identity와 queue 0 GPU interval을 연결해 감사했다. OFF 표본 64개도 기존 primary 경로다. 그래프의 CS 작업은 DIRECT 큐에서 계속 실행되며 compute queue를 쓰지 않는다는 뜻이다.

GPU span은 정방향 OFF가 5.048ms, mode 2가 5.655ms였고 역방향은 5.722 / 5.731ms였다. OFF 대조군 자체도 순서·바이너리 사이에 변했다. GPU 향상이나 새로운 async 성능 수용을 판정하지 않는다.

기본 mode 0과 제품 async 미지정 상태는 유지한다. 제품 pass별 async opt-in·전체 프레임 순이득·RG7 조합은 남는다. [기계 판독 원장](RenderRg8OwnedGraphics20261022.json), 원시 근거는 `Artifacts/rg8-owned-graphics-20261022/`의 before-normal·after-normal·native-debug·native-release·capture-correctness에 있다.
