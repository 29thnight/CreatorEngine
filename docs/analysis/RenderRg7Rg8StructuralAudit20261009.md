# RG7·RG8 구조 및 배선 재감사 — 2026-10-09

후속: [구조 수정 1 — 배치·기록 저장소 재사용](RenderRg8Batching20261009.md)과 [구조 수정 2 — 공통 배리어 계획](RenderRg8Barriers20261009.md)을 적용했다. 아래 내용은 수정 전 소스의 감사 기록이다. 다중 큐의 보수적 경계 전이·overlap·RG7 조합은 남아 있으며 RG8 progress·기성 0·기본 OFF를 유지한다.

## 결론

사용자의 문제 제기는 타당하다. 특히 RG8은 단순히 이득을 확인하지 못한 정도가 아니다.
실제 제품 배치가 compute와 graphics를 겹치지 못하게 구성되어 있고,
그 상태에서 기존 풀·기록·제출 경로보다 비싼 실행기를 전체 그래프에 적용했다.
RG8의 제품 구현 완료 판정을 철회하고 progress로 되돌린다. 기존 정확성 검사 결과는 보존한다.

RG7에는 기존 경로와 다른 실제 힙 공유가 있다. 다만 그래프 한 벌의 할당량 감소와
엔진이 계속 보유하는 전체 메모리 감소를 연결하는 정책이 부족하다.
RG7의 제한된 aliasing 구현/실험 종료와 기본 OFF는 유지하되 제품 메모리 최적화 완료로 해석하지 않는다.
두 항목을 같은 표현인 “개선 미입증”으로 묶어 설명한 것은 구현의 차이와 결함을 가렸다.

이번 작업은 현재 소스와 보관된 실행 산출물을 대조한 상세 분석이다.
새 엔진 빌드/GPU 실행/성능 측정은 하지 않았다. 측정값은 이전 실행의 결과이고,
726 transition 요청 및 overlap 가능성은 현재 실행 알고리즘에 보관된 그래프를 대입한 정적 재구성이다.
검토한 소스와 산출물 해시 및 계산 결과는
`Build/Verification/Phase43/Rg7Rg8StructuralAudit20261009/audit.json`에 보관했다.

## RG8 — 제품 배선과 실행기 문제

### 1. P1: 선택한 배치에는 graphics/compute overlap이 없다

`BuildQueueSchedule`은 기존 `m_executeOrder`를 그대로 돌면서 큐 종류만 바꾼다.
독립 graphics 작업을 compute 구간에 채우거나 critical path 감소를 평가하지 않는다.
live 후보는 SSAO 두 패스뿐이고, 이전 GPU 시간이 1000ns 이상이면 옮긴다.

TestShadow 제품 캡처의 실제 순서는 다음과 같다.

```
graphics: ... → LX.Scene.GBuffer(18) → [대기] → Deferred(21) → ...
compute :             [대기] → SSAO.Compute(19) → SSAO.Filter(20)
```

Compute는 GBuffer의 depth/normal을 기다리고 Deferred는 SSAO.Filtered를 기다린다.
GBuffer와 Deferred 사이에 다른 graphics 작업을 배치하지 않았다.
두 큐의 순서 edge와 실제 planner의 wait edge를 재구성했을 때,
compute/graphics 패스 쌍 중 **어느 쪽에도 선후 의존 경로가 없는 쌍은 0개**였다.
즉 이 그래프 배치에는 서로 겹칠 수 있는 패스가 없다. 드라이버 타임라인을 새로 측정한 주장은 아니다.

근거: `EnhancedRenderGraph.cpp:2146,2173`, live adapter `:357–367`,
`EnhancedSSAOPass.cpp:184` 이후 및 RG8 Forward-2/sample-0/manifest.json.
compute queue에서 Dispatch가 실행됐다는 사실만으로 async overlap을 수용한 검사가 잘못됐다.

### 2. P1: 매 패스마다 allocator/list를 생성한다

`SubmitQueues`는 prologue, 각 패스, epilogue를 각각 `recorder.Record`로 기록한다.
`DX12QueueRecorder::Record`는 매 호출마다 `CreateCommandAllocator`와
`CreateCommandList`를 호출한다. 완료된 allocator/list를 재활용하는 풀이 없다.

기존 제품은 `RecordParallel`과 `DX12CommandListPool`을 사용하며
frame/worker 슬롯의 allocator/list를 reset해 재사용한다.
RG8은 compute 두 패스를 옮기면서 graphics 전체까지 새 one-shot 기록 경로로 바꿨다.
패스 분할/병렬 기록도 해당 경로에서 사용하지 않는다.

캡처 그래프 72패스에는 74쌍의 allocator/list가 필요하다.
진단 PBR 패스 16개를 뺀 일반 그래프는 56패스이므로 실행기 기준 58쌍이다.
별도 정상 실행 로그도 graph 제출 224회에 batch 13,022회, 약 58.1개/graph를 기록한다.
7개 `LX.MeshWorldReuse`처럼 본문이 CPU 상태만 검사하는 패스도 독립 기록/제출 단위가 된다.

근거: `EnhancedRenderGraphQueues.cpp:125,139,198`, `DX12QueueService.cpp:633–686`,
`EnhancedSceneRenderer.cpp:5933–5941`, `DX12CommandListPool.cpp:152–159`,
`MaterialGraphMeshSurface.cpp:1232`.

### 3. P1: 패스 단위 제출·Signal과 CPU 동기 왕복

각 기록에 대해 `Submit`을 따로 호출하고, native `ExecuteCommandLists` 바로 뒤에
`Signal`한다. `QueueOperation`은 RHI 제출 스레드 밖에서 호출될 때
`ExecuteAndWait`로 CPU 완료를 기다린다. live의 그래프 기록 단계가 이 제출들을 직접 수행한다.
따라서 큐 간 교차점이 아닌 패스 수에 비례해 제출/Signal/CPU 왕복이 늘어난다.

또 wait 중복 제거 키가 `(producer, consumer, resource)`이므로
같은 producer fence를 같은 consumer가 여러 자원 때문에 반복 기다린다.
검토한 캡처는 wait 8개지만 producer/consumer 쌍은 6개다.

`BeginQueueFrame`의 prefix flush 및 `EndFrame`의 ticket wait도 추가된다.
이 ticket wait는 **CPU 제출 완료 대기**다. 이를 매 패스 GPU 완료 대기라고 부르면 틀리다.
각 비용의 실제 비율은 따로 계측하지 않았으므로 느려진 시간을 한 원인에 전부 귀속하지 않는다.

근거: `EnhancedRenderGraphQueues.cpp:228–278`, `DX12QueueService.cpp:44–62,179–235`,
`DX12DeviceResources.cpp:1020–1053,1097–1106`, live adapter `:328–337`.

### 4. P1: 컴파일한 barrier 계획을 버리고 매 패스를 COMMON 왕복시킨다

기존 `Compile/PlanBarriers`는 이전 사용 상태를 이어 받아 필요한 전이를 계획한다.
그런데 RG8 실행기는 매 패스에 전체 자원 수만큼 state vector를 새로 만들고
모두 COMMON으로 시작한다. 패스 전에 요구 상태로, 패스 후 COMMON으로 되돌린다.
graphics→graphics의 연속 읽기도 같은 규칙을 따른다. 전이를 자원별로 따로 호출한다.

72패스 캡처의 compiled snapshot에는 barrier 114개가 기록돼 있지만,
RG8 실행 알고리즘으로 재구성한 transition 요청은 prologue/epilogue 포함 **726개**다.
이는 native GPU 캡처로 직접 센 값이 아닌 코드 경로의 요청 수다.
중요한 점은 비용 증가뿐 아니라 **Inspector/캡처의 compiled barrier 정보가 실제 RG8 실행 계획을
대표하지 못한다**는 것이다. 동일 그래프에 두 종류의 상태 계획을 유지하는 구조 문제다.

근거: `EnhancedRenderGraph.cpp:2102`, `EnhancedRenderGraphQueues.cpp:125–209`.

### 5. P1: 비용/의존 기반 스케줄러가 아니라 임계값에 의한 큐 분류다

1μs threshold는 candidate 자체의 측정 시간만 본다. 겹칠 graphics 작업,
교차 대기/전이/제출 비용, critical path 변화와 추가 보유 메모리를 계산하지 않는다.
모든 physical resource 사용을 직렬화하여 read/read도 보수적으로 wait를 추가한다.
이 보수성 자체는 현재 상태 전이 모델에서 안전을 위한 선택이지만,
그 모델로 성능용 multi-queue scheduler를 완성했다고 볼 수 없다.

기존 plan 검사는 read/read wait가 있고 compiled 순서를 그대로 유지하는 것을 정답으로 삼는다.
135 native 검사와 픽셀 일치는 제출/수명/정확성의 증거지만 생산적인 overlap의 증거가 아니다.

근거: `EnhancedRenderGraph.cpp:2170–2230`,
`RenderQueueScheduleRg8Tests.h:39–59,88–93`.

### 6. P1: RG7+RG8 조합을 구현하지 않고 거부한다

제품 배선은 aliasing과 owned queue execution을 함께 켜면 오류를 반환한다.
`SubmitQueues`도 `m_aliasing`을 거부하고 planner는 aliasing이면 compute를 배제한다.
이 제한은 현재 단일 순서 lifetime에서 필요한 안전장치이므로 단순히 제거하면 안 된다.
그러나 큐 이동에 따른 lifetime 확장과 aliasing 이득의 상쇄를 함께 평가한다는
원래 RG8 계획은 이 상태에서 검증되지 않았다.

근거: `EnhancedSceneRenderer.cpp:5845–5849`, `EnhancedRenderGraphQueues.cpp:22`,
`EnhancedRenderGraph.cpp:2171`.

## RG7 — 실제 공유는 있으나 전체 보유량 최적화가 빠져 있다

### 7. 실제 차이: 서로 다른 자원이 같은 heap offset 0을 공유한다

단순히 기존 texture pool에 이름만 붙인 코드는 아니다.
수명이 겹치지 않는 자원을 heap class별로 묶고 실제 `CreateHeap` /
`CreatePlacedResource(heap, 0, ...)`를 사용한다. alias barrier와 필요한 RT/DS clear,
완료 이후 그룹 반납 및 캐시가 있다. 이 기반과 기존 실패/수명 검사는 가치가 있다.

보관된 제품 Scene 그래프에서 6개 그룹/15개 자원은 SSAO, SSGI, Bloom을 공유한다.
예: `SSAO.Raw → SSGI.HiZ.0 → PostChain.Bloom`.

| 항목 | 값 |
|---|---:|
| 15개 자원을 개별 할당했을 때 allocation bytes 합 | 11.625MiB |
| 6개 공유 heap bytes 합 | 8MiB |
| 한 그래프에서 줄인 allocation bytes | 3.625MiB |
| 모든 transient allocation bytes 대비 감소율 | 2.73% |
| 동일 snapshot의 공유 heap 보유/대여/캐시 | 16/8/8MiB |

즉 “전혀 차이가 없다”는 판단은 RG7에는 맞지 않는다.
다만 위 3.625MiB는 단일 그래프의 allocation 비교이고, 전체 엔진의 residency 절감이 아니다.
8MiB 캐시가 관측되었다고 OFF 대비 정확히 그만큼 악화됐다고 계산할 수도 없다.
OFF의 전체 pool 보유 원장과 동일 수명 시점 비교가 없기 때문이다.

근거: `EnhancedRenderGraph.cpp:943–1158`, `DX12DeviceResources.cpp:2640–2710`,
`RG7Closure-20261008/OnFirst/Release-On/graph-scene.json`.

### 8. P2: 그룹 전체가 같아야 재사용하며 용량 기반 재사용/퇴출 정책이 없다

캐시는 heap bytes/alignment/class뿐 아니라 **그룹의 모든 resource description과 순서가
정확히 같은 경우에만** 재사용한다. heap 공간과 개별 placed resource 생성을 묶었기 때문이다.
그룹 구성 한 항목만 바뀌어도 호환 heap 전체를 재사용하지 못한다.

반납은 32개/128MiB 제한 안이면 보관하고, 제한을 넘으면 새로 반납된 그룹을 버린다.
안 쓰는 이전 그룹을 우선 퇴출하거나 전체 committed pool과 함께 예산을 관리하지 않는다.
새 workload와 호환되지 않는 그룹이 상한을 채운 경우, 새 그룹이 반복 생성·파괴되는 구조가 가능하다.
이 thrash는 코드에서 도출한 조건부 문제이며 이번 제품 실행에서 발생했다고 주장하지 않는다.
제품 resize는 cache를 비우므로 모든 resize에서 누적된다고 말하는 것도 부정확하다.

또 매 Compile마다 후보 수집/정렬/그룹화/description 구성/선형 cache 탐색을 다시 한다.
기존 기록의 준비 CPU 중앙값은 약 0.006→0.024ms다. 약 18μs의 추가 비용이며
RG8의 수 ms 악화와 같은 크기로 과장하면 안 된다.

근거: `EnhancedRenderGraph.cpp:982–1107,1384–1399`, `EnhancedRenderGraph.h`의 cache 상한,
`EnhancedSceneRenderer.cpp:2885–2897`.

## 이전 측정/종료 판단에서 잘못한 부분

1. 정확성·수명 테스트 통과를 구조의 적정성과 혼동했다. 실제 API를 썼다는 것과 효율적으로 배선했다는 것은 별개다.
2. RG8은 graph 내 overlap 가능성부터 확인했어야 했다. compute 구간 2개 검출만으로 제품 배선 수용을 선언했다.
3. RG8의 0↔1/2 비교에서 전체 기록·제출 구조가 바뀌는 것을 알고도,
   allocator/list 생성과 패스별 제출 구조를 먼저 감사하지 않고 미채택으로 종결했다.
4. RG7은 단일 그래프 절감과 캐시를 포함한 보유량을 연결하지 못했다.
   단순 DXGI 최대값 증가를 aliasing 기법 자체의 한계로 일반화할 수 없다.
5. CPU record 계측은 RG8에서 `recordingMilliseconds`까지이며 이후 Submit/Wait 비용은 빠진다.
   기본 경로와 전체 CPU 부담을 비교하려면 record와 submit을 함께 제시해야 한다.
6. 따라서 RG8 “완료” 판정은 성급했다. 기존 결과는 정확성/수명 회귀 자료로 유지하고,
   제품 실행기의 구조 수정이 필요한 progress로 되돌린다. RG9를 곧바로 다음 작업으로 제시한 판단도 정정한다.

## 수정 우선순위와 유지할 코드

먼저 RG8의 전체 그래프 one-shot 실행기를 제품 최적화 경로로 취급하는 것을 중단한다.
기본 OFF 상태에서 아래 순서로 구조를 정리해야 한다. 이번 감사에서는 엔진 코드를 변경하지 않았다.

1. 기존 frame/worker pool과 제출 경로를 활용하도록 queue별 recording batch를 구성한다.
   allocator/list는 fence 완료 이후 재사용하고, 같은 큐의 연속 패스는 묶는다.
   Signal/Wait는 실제 교차 의존과 최종 완료점에만 둔다.
2. compiled 계획에 queue별 state/ownership 경계를 포함시켜 실행기와 Inspector가 같은 계획을 소비한다.
   매 패스 COMMON 왕복과 별도 state vector를 없앤다.
3. 독립 작업과 교차 비용을 고려해 overlap 가능성이 있는 경우만 compute로 이동한다.
   TestShadow SSAO 배치에 겹칠 작업이 없으면 기존 graphics 실행 경로를 선택해야 한다.
4. RG7과 함께 사용할 경우의 큐별 lifetime/완료점 계획을 연결하거나,
   기존 계획의 미구현 항목으로 명시한다. 현재 거부 조건을 검증 없이 풀지 않는다.
5. RG7은 실제 줄어드는 바이트에 비례해 cache 보유 예산을 정하고,
   사용하지 않는 그룹 퇴출 및 가능한 범위의 heap 재사용을 개선한다.
   heap class/first-use 조건과 전체 memory inventory를 보고 확장 대상을 결정한다.

Q0의 queue identity/timeline, frame reservation, 실패 후 격리·회수,
RG7의 placed resource/alias barrier/poison·수명 검사, 큐별 GPU clock calibration은 유지할 기반이다.
삭제/교체 우선 대상은 이 기반 자체보다 RG8의 패스별 one-shot 제품 실행과 중복 상태 계획이다.

수정 확인은 반복 캡처 횟수를 늘리는 식으로 진행하지 않는다.
warm frame의 allocator/list 신규 생성 수, batch/Signal/Wait 수, 실제 barrier 수,
CPU record+submit, overlap 가능한 패스 쌍/실제 queue 시간, 캐시 포함 보유 바이트를 확인해야 한다.
픽셀/실패/수명 검사는 기존 회귀를 재사용한다. 이들은 새로운 기능 추가가 아니라 이번에 빠뜨린
원래 queue pool·효율적인 실행·수명/메모리 채택 판단의 직접적인 검증 항목이다.
