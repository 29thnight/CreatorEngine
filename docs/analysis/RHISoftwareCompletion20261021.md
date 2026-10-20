# 소프트웨어 RHI 명령 목록: 기존 제품 경로 연결과 할당 최적화

기준일: 2026-10-21 KST / 2026-10-20 UTC. 기준 master는 `088ea7b70c1a610e93fd2e0f6a2f35a2b9a5e175`다. 요청한 [09df9a16의 구현 스펙](https://github.com/29thnight/CreatorEngine/blob/09df9a16b775029ae55c7a2553e4ca8e4bcd87a4/docs/design/RHISoftwareCommandListSpecification.md)은 이 master의 스펙과 내용이 같다.

## 1. 판정과 범위

DX12의 기존 parallel-pool 소프트웨어 기록 경로에 더해 **DX12 owned/multiqueue 소비 어댑터와 Vulkan의 기존 graphics parallel-pool 제품 연결**을 구현했다. 명령 수락·번역·native 제출·GPU 완료를 구분하고, CPU 저장소를 GPU 완료까지 불필요하게 붙잡지 않도록 했다. target 저장소와 native encoder의 재사용, bounded admission, bool 실패 후 fallback을 함께 보완했다.

기능 구현, CPU 계약 검사, 실제 GPU 제품 수용은 서로 다른 판정이다. 이 변경은 `CREATOR_RHI_SOFTWARE_COMMANDS=1`의 명시적 선택을 유지한다. Windows Editor와 실제 DX12/Vulkan GPU에서의 픽셀·validation·lifecycle 검사 및 CreatorRobot 전후 계측이 끝나기 전에는 기본 경로 전환이나 120 FPS 달성을 승인하지 않는다. CPU fixture의 native 경계는 GPU/driver 동작을 증명하지 않는다.

| 경로 | 기준 master | 이번 변경 | 수용 범위 |
|---|---|---|---|
| DX12 graphics parallel pool | 소프트웨어 기록, 고정 자원, 직렬/병렬 번역, 기존 FIFO 연결 | target 수 축소/재확장 재사용, 바이트 admission, bool 계약, 계측 보강 | 실제 공용 코드 CPU 검사; 새 제품 GPU 실행 필요 |
| DX12 owned graphics/compute | native recorder만 제품에 연결 | 소프트웨어 recorder/queue/factory, 병렬 번역과 실제 queue transaction 연결 | CPU queue 계약 검사; native 두 프레임 readback 및 timeline 회귀 추가 |
| Vulkan graphics parallel pool | typed translator가 있으나 제품 software 경로와 고정 자원 수명 연결 부족 | validator, immutable recording snapshot, private descriptor pool, 기존 FIFO 연결 | 공식 Vulkan 헤더를 사용한 CPU 계약 검사; 실제 Vulkan GPU 실행 필요 |
| Vulkan owned/multiqueue native backend | native factory/recorder 자체가 없음 | 이번 변경의 기존 제품 소비 경로에 포함되지 않음 | generic adapter 구현을 Vulkan owned 지원으로 확대 해석하지 않음 |
| native encoder 자체의 미지원 기능 | 백엔드별 차이 존재 | bool 미지원은 기록 전에 false, void 미지원은 번역 실패로 보존 | 미지원 native 기능을 성공으로 위장하지 않음 |

이번 fork/join의 대상은 **선언이 끝난 그래프의 명령 기록과 독립 native target의 번역**이다. `RenderGraphDeclare`의 모든 노드 callback을 병렬화한 변경이 아니며, `MaterialGraphScenePrepare`의 준비 작업을 직접 단축한 변경도 아니다.

## 2. 최신 master 계측으로 정한 기준선

[기준선 추출 JSON](RHISoftwareCompletion20261021-baseline.json)은 `Artifacts/remeasure-12-20261020`의 manifest, 조건, summary, scope 지표 및 소스 identity를 추적한다. 자료를 커밋한 HEAD는 `088ea7b…`, 자료가 보고한 실행 코드 HEAD는 `c12fe3f…`다. Debug CreatorRobot Play, 8 workers, software RHI 활성, Scene/Game 1186×514, window당 약 15초 조건이다.

### 전체 baseline summary

같은 전체 summary의 호출당 inclusive wall time이다. 아래 부모/자식 구간이나 다른 스레드의 구간을 더해 전체 프레임 시간을 계산하면 안 된다.

| 구간 | 표본 수 | 평균 ms | p50 ms | p95 ms |
|---|---:|---:|---:|---:|
| RenderGraphDeclare | 818 | 4.089827 | 3.964750 | 4.843200 |
| MaterialGraphScenePrepare | 818 | 2.820819 | 2.773850 | 3.448600 |
| RenderCommandRecord | 818 | 1.703046 | 1.707100 | 1.921500 |
| RHISoftwareTranslate / RHIThread | 818 | 0.851698 | 0.823100 | 1.035800 |
| RenderGraphCompile | 818 | 0.768262 | 0.735950 | 0.981900 |
| RenderView | 818 | 12.312635 | 12.295350 | 14.370300 |

별도 ETW ON containment-selected 689개 구간에서 `RenderGraphDeclare`는 평균 4.688408ms 중 Running 4.682907ms다. `RenderCommandRecord`는 wall 2.091382ms, owner Waiting 1.743231ms이고, `RHISoftwareTranslate`는 wall 1.031247ms, owner Waiting 0.613263ms다. 이 대기는 join/작업 의존성의 관측값이며, 그대로 제거할 수 있는 낭비 시간이라는 뜻은 아니다. ETW ON과 위 baseline은 계측 조건·표본 집합이 달라 코드 변경 전후로 비교하지 않는다.

전체 baseline의 logical target별 기록 작업 합계/record는 target 0~7 순서로 `0.233694, 0.814389, 0.877799, 0.746818, 0.908396, 0.357570, 0.647552, 1.191871ms`다. 이는 물리 Worker 0~7의 실행 시간이 아니다. target 하나에 여러 segment가 속할 수 있고, 구간 합계는 critical path와 다르다. 큰 target을 먼저 맡기고 빈 worker가 다음 target을 가져가는 정책을 유지한 이유이며, 그래프 의존성이나 stateful target을 무시하고 8등분할 근거는 아니다.

기록 ON/OFF의 완료 real view 처리량은 각각 52.92056/60.92271뷰/초다. 한 publication에 여러 뷰가 있으므로 displayed FPS로 바꾸지 않는다. 이 캡처는 direct-native/software A/B가 아니다. 기록 중 파일이 바뀌지 않았다는 `sourceUnchanged=true`는 pristine HEAD 증명이 아니다. 선택한 RHI 입력 32개 중 31개는 exact 또는 균일 LF→CRLF 비교로 대응하며 `RHIResourceState.h` 1개는 미해결이다. 전체 715개 입력의 완전 재현을 주장하지 않는다. pixel acceptance/native quality gate도 이 자료만으로 통과하지 않는다.

## 3. owned queue의 기록·번역·제출 계약

[RHISoftwareQueueRecorder](../../Engine/RenderEngine/RHI/RHISoftwareQueueRecorder.h), [구현](../../Engine/RenderEngine/RHI/RHISoftwareQueueRecorder.cpp)은 `IRHIQueueRecorder`의 기존 호출면을 유지한다. callback은 software encoder에 한 번 기록되고, `Finish`는 native list를 열지 않고 소프트웨어 batch를 밀봉한다. wrapper는 native queue와 동일한 `RHIQueueIdentity`와 timeline을 사용하며 새 GPU queue를 만들지 않는다.

`Submit`은 backend의 `ExecuteSubmission`에 전체 transaction을 한 번 전달한다. owner 내부에서 독립 target들을 번역하고 합류한 뒤 native `Submit`을 수행하며, 실제 signal이 발행된 경우에만 timeline point를 반환한다. CPU 번역 완료만으로 issued point를 미리 만들지 않는다. `Wait`, `Signal`, `Submit`은 같은 transaction 직렬화 경계를 사용한다. 호출 스레드가 mutex를 가진 채 다른 owner의 응답을 기다리지 않도록 잠금은 owner callback 안에서 획득한다.

밀봉 batch의 제출 권한은 CAS로 한 번만 소비한다. 같은 `shared_ptr`의 여러 alias가 동시에 제출해도 replay는 한 번이며, 실패 후 재시도로 이미 기록한 draw prefix를 다시 실행하지 않는다. CPU 번역 오류와 native 실행 후 fence 불명확 실패의 소유권은 별도다. 후자는 기존 backend quarantine 경로에 남는다. worker 0개를 보고하는 legacy recorder는 한 target의 한 segment만 허용한다. pass 상태 초기화 계약이 없는 native callback에 여러 segment를 이어 붙이지 않는다.

[DX12 제품 어댑터](../../Engine/RenderEngine/RHI/DX12/EnhancedSceneRendererLiveDX12Adapter.cpp)는 recorder를 프레임마다 만들지 않고 보존하며, 기존 기본값 4가 아닌 실제 command worker 수를 전달한다. graphics/compute endpoint의 native identity를 유지하고 제출 함수에만 software wrapper를 전달한다. transaction 도중 앞 batch가 제출된 뒤 뒤 batch가 실패해도 이미 사용한 `nextSignal`을 되돌리지 않는다. owned 경로의 자원 버전 고정은 기존 동기 `ExecuteSubmission` rendezvous와 `BeginQueueFrame` 이후 descriptor/upload 준비 계약에 의존한다.

## 4. 분배와 재사용 최적화

번역은 기존 `job_scheduler`/enkiTS와 `RHITranslationWorkQueue`를 사용한다. target별 명령 수를 비용 추정치로 삼아 큰 target부터 배치하고, 각 job이 다음 target을 동적으로 가져간다. target 안의 segment/opcode 순서는 유지하며 최종 native 제출 순서는 compiled `targetOrder`로 복원한다. 오류도 완료가 빠른 worker 순서 대신 원래 target 순서로 확정한다. 새로운 스레드 풀이나 target 수만큼의 프레임 지역 동적 컨테이너를 만들지 않는다.

작은 batch는 owner에서 직렬로 처리한다. 제품 연결의 `minCommandsPerJob=256`은 master가 이미 사용한 보수적 정책을 이어 받은 값이다. CPU synthetic 결과만으로 실제 장면의 최적 임계값이라고 확정하지 않는다. 명령 종류별 native 비용이 다르므로 opcode 수는 비용의 근사이며, 큰 stateful target 하나의 내부를 임의로 잘라 완전 균등 분배를 보장하지 않는다.

| 저장소 | 변경 | 수명/할당 경계 |
|---|---|---|
| software targets/encoders/budgets | 별도 `activeTargetCount`, 보관 배열은 축소하지 않음 | 8→1→8일 때 비활성 객체를 재사용; 활성 prefix만 검사·통계·밀봉 |
| software owned wrapper | domain당 최대 64개 객체 보관 | 마지막 alias에서 CPU 입력/owner 해제; shared control block 할당까지 0이라는 주장은 하지 않음 |
| DX12 owned target | allocator/list와 encoder를 같은 pool storage에 보관 | 미제출 확정 취소 또는 native GPU retirement 뒤 재사용 |
| DX12 target 목록 | 최대 8개의 고정 배열, 일반 외부 API overflow에는 기존 확장 저장소 | 일상적인 1~8 target 경로의 지역 vector 제거 |
| DX12 native submit 포인터 목록 | queue 멤버 scratch의 capacity 보존 | 기존 admission 직렬화 아래 사용; 모든 반환 경로에서 borrowed pointer 제거 |
| DX12 완료 batch 수집 | 최대 8개의 고정 retirement 배열로 나누어 수집 | 한 번 읽은 fence 완료값을 기준으로 선별하고 잠금 밖에서 해제; 지역 vector 재할당 제거 |
| Vulkan metadata | frozen pool 멤버 vector와 내부 capacity 보존 | 완료 전 외부 owner가 있으면 재사용 금지 |
| Vulkan native 제출 목록 | 최대 8개의 command buffer / submit-info 고정 배열 | 프레임 지역 vector 제거 |

`IRHIQueueRecording::AcquireTranslationEncoder`는 논리 pass 상태 초기화와 native cache 초기화를 구분한다. DX12는 segment마다 pipeline 유효성을 비우면서 같은 native target의 PSO/root-signature/heap cache를 보존한다. 새 native list recording에서는 전체 상태를 reset한다. 캐시된 encoder가 다른 `DX12DeviceResources` facade에 속하면 재생성한다.

## 5. 바이트 admission과 CPU/GPU 수명

[RHISubmissionThread](../../Engine/RenderEngine/RHI/RHISubmissionThread.cpp)는 기존 용량 3의 FIFO에 기본 256MiB의 queued+running software arena 수용 제한을 추가한다. 단위는 `GetRecordingStats().allocatedBytes`로, arena chunk의 예약 용량이다. native 완료점 예약/upload 진행 전에 단일 batch 초과를 거절하고, 합계 초과 시 기존 FIFO의 condition variable을 사용한다. 대기 중 owner 취소·세대 변경·fault도 재검사한다.

번역/취소 work capture의 소멸이 끝난 다음 CPU byte credit을 반환한다. 취소된 토큰의 소멸자가 진행 중인 상황에서도 메모리가 먼저 재사용된 것으로 계산하지 않는다. CPU credits가 반환돼도 native batch, upload, descriptor, GPU resource owner는 기존 completion/timeline retirement가 유지한다.

owned queue는 `Finish`에서 domain 전체 sealed/running arena를 기본 256MiB까지 접수한다. RDG가 여러 callback을 먼저 기록하고 이후 제출하기 때문에 한도 초과 시 blocking wait를 하지 않고 terminal failure를 반환한다. 번역 합류 후 또는 마지막 미제출 alias 폐기 시 credit을 정확히 한 번 돌려준다. **이 제한은 아직 Finish하지 않은 callback들의 순간 메모리, 프로세스 전체 RSS, GPU 자원 메모리의 hard cap이 아니다.** 기존 GPU frame slot/retirement 제한과 역할을 구분한다.

기존 제품의 DX12/Vulkan 렌더러는 각 view의 `BeginFrame`부터 prefix/body 제출을 거쳐 `EndFrame`까지 같은 backend frame slot을 사용한다. 두 backend의 3개 frame slot은 재사용 전에 이전 ticket 및 GPU fence를 기다리며, tail fence를 저장한 다음에만 다음 slot으로 이동한다. 따라서 software용으로 별도 GPU batch 3개 제한을 덧붙이지 않았다. prefix/body와 여러 큐가 한 frame credit을 공유하는 기존 계약을 사용한다. 독립적인 generic queue API에 프로세스 전체 GPU batch 개수 제한이 생겼다는 의미는 아니다.

upload prefix는 기존 FIFO의 준비 작업에서 본문보다 먼저 처리하며 frozen version이 stale/duplicate 사용을 거부한다. prefix가 이미 실행된 뒤 본문 번역이 실패한 경우 앞의 GPU 작업을 취소된 것으로 바꾸지 않는다. abort는 현재 미제출 기록에만 적용하고 이전 prefix의 fence를 지우지 않는다. 실행 불명확 실패는 숫자 fence만으로 해제하지 않고 기존 device-loss/drain 증명 또는 quarantine을 따른다.

## 6. bool 실패와 상태 보존

스펙 §6의 `false`는 fallback을 계속 기록할 수 있어야 한다. 기존 `CopySpan` 후 `Append` 방식은 간접 batch의 payload만 소비한 뒤 명령 예산에 걸리거나 sticky error를 남길 수 있었다.

[RHISoftwareCommandList](../../Engine/RenderEngine/RHI/RHISoftwareCommandList.h)는 고정 payload의 `TryAppend`와 opcode+span을 함께 예약하는 `TryAppendWithSpan`을 사용한다. list/공유 target 예산과 overflow를 먼저 검사하고 한 arena allocation이 성공한 후 header, span, 통계를 함께 게시한다. 거절 시 opcode, payload 예산, logical encoder 상태, sticky error를 변경하지 않는다. void API의 실패는 기존처럼 sticky recording failure이며 부분 정상 명령만 골라 제출하지 않는다. GPU begin/end query의 불완전 기록도 session 실패로 유지한다.

Vulkan validator는 accepted command 이후에만 rendering-open 상태를 갱신한다. 그래픽/컴퓨트 pipeline을 별도 고정 2-entry 상태로 보존해 native Vulkan의 interleaved binding 의미를 맞췄다. DX12의 기존 마지막 pipeline/bind point 의미는 유지한다.

`RHICommandFailure`는 recording ID, backend generation, 원래 target/opcode 위치와 함께 recording/admission/translation/submission/retirement 단계를 전달한다. DX12 `ExecuteCommandLists` 및 Vulkan `vkQueueSubmit2` 호출 직전에 `executionUncertain`을 설정하고 완료 signal이 수락된 경우에만 해제한다. 실행 전 거부와 실제 실행 시도 뒤 실패를 구분하며, 재시도로 draw를 중복 실행하지 않는다.

범위가 제한된 TLS 진단 sink는 동기 owner handoff에서 값만 전달한다. worker는 공유 sink에 쓰지 않고 join 후 원래 target 순서로 선택한 결과를 전달한다. `SubmitWithDiagnostics`, 그래프의 `QueueExecution.failure`, 제출 티켓의 `GetCommandFailure`, structured `ConsumeFailure`까지 같은 상태를 보존한다. 티켓은 완료를 acquire로 확인한 뒤 결과를 읽는다. 기존 문자열 오류 API는 유지하며, 이 변경이 오류 객체의 별도 capture JSON 형식을 추가하는 것은 아니다.

## 7. Vulkan 고정 recording과 native 수명

[VulkanRecordingResources](../../Engine/RenderEngine/RHI/Vulkan/VulkanRecordingResources.cpp)는 binding/sampler 요청, render target, pipeline/layout metadata, resource handle generation과 native object lease를 기록 시점에 고정한다. 번역 worker는 mutable current frame registry를 다시 해석하지 않는다. 필요한 cube view는 producer가 snapshot 전에 준비한다. 기존 cube view와 호환되지 않는 요청을 기존 view 파괴·대체로 처리하지 않는다.

image/buffer/pipeline을 live registry에서 해제하거나 invalidation해도 고정 recording이 남아 있으면 실제 native object 파괴를 늦춘다. upload recording은 pin 뒤 추가 할당을 밀봉하며, CPU snapshot이 남은 segment는 GPU 완료처럼 보이더라도 recycle/trim하지 않는다. frame-slot별 frozen pool은 외부 shared owner가 없을 때만 재사용한다. native 장치 파괴 전에는 기존 drain/retirement 계약을 완료해야 한다.

각 frozen pool의 전용 descriptor recycler는 producer의 mutable active pool과 분리하지만 device 전체 `maxUpdateAfterBindDescriptorsInAllPools` 예산은 공유 atomic credit으로 제한한다. native pool 생성 실패 시 예약 credit을 반환한다. 별도 pool을 만들 때마다 device 제한이 새로 생긴 것으로 계산하지 않는다.

실제 primary graphics queue identity는 device 초기화마다 공유 incarnation 발급기로 만든다. owner lifecycle generation은 별도다. frozen translator는 캡처한 identity와 현재 device identity가 모두 일치해야 하며, 다른 device나 재초기화 이전 batch를 같은 queue로 취급하지 않는다.

Vulkan의 `AcceptParallelSubmission`과 그래프 자원 게시에서는 software ticket의 native batch pointer가 없다는 계약을 반영해 `GetIdentity().recordingId/completion`을 사용한다. 마지막 segment의 미지원 명령도 join 후 진단에 포함한다. 기존 Vulkan native가 지원하지 않는 `CopyTexture`, `CopyPartialToReadback`, `ClearRenderTargetRect`를 no-op 성공으로 인정하지 않는다.

## 8. RHI identity 계측과 소비

`RHISoftwareRecord`, `RHISoftwareSeal`, `RHISoftwareAdmissionWait`, `RHICommandTranslate`, `RHICommandTranslateJoin`, `RHINativeSubmit`, `RHIBatchRetire` scope를 추가했다. 기존 `RHISoftwareTranslate`는 기준선 연결을 위해 유지한다. `RHICommandTranslateJoin`은 동기 `RunParallel` 호출 구간이므로 worker 실행 시간을 포함하며 pure waiting으로 해석하지 않는다.

성공한 `BeginRecording`에서 process-unique recording ID를 발급하고 같은 값을 record/Seal/translate/submit/retire 및 native GPU query까지 전달한다. 실패한 Begin이나 취소한 recording은 다음 세션의 ID로 재사용되지 않는다. aggregate 이벤트의 target은 sentinel이며, target 작업은 원래 software target index를 남긴다. source engine frame의 availability, 64-bit frame/view/device/backend identity를 값으로 전달하고 CPU task나 native submission 번호로 대체하지 않는다.

`profile_rhi_scope`는 해당 이벤트와 identity를 기존 bounded page의 sidecar로 함께 기록한다. 일반 CPU 이벤트 구조는 64B를 유지하며 모든 opcode에 스코프를 만들지 않는다. sidecar의 wire record는 100B다. snapshot format 8, streaming format 9 및 measurement chunk version 3에서 새 RHI axis를 읽고 쓴다. 기존 형식은 계속 읽으며, 예전 version으로 위장한 새 axis는 거부한다. 64-bit identity를 float나 32-bit GPU submission 번호로 줄이지 않는다.

계측에는 고정 메모리 비용이 있다. 같은 GCC/비 Shipping ABI에서 기준 master와 새 헤더의 실제 `sizeof`를 비교하면 다음과 같다. Windows/MSVC ABI 크기를 대신하는 값은 아니다.

| profiler 저장소 | master | 이번 구현 | 의미 |
|---|---:|---:|---|
| `profile_event` | 64B | 64B | 일반 이벤트 크기 유지 |
| `profile_render_measurement` | 200B | 280B | identity sidecar의 메모리 표현 80B 증가; wire는 100B 유지 |
| `event_chunk` | 22,840B | 25,400B | page당 2,560B 증가, 약 11.2% |
| `thread_stream` | 8,432B | 17,648B | 열린 scope 최대 64개용 context·atomic publication 저장소 9,216B 증가 |

page의 sidecar 상한은 32개다. 초과 시 기존 pool에서 다음 page를 얻고, pool/ring 예산이 부족하면 기존 drop 정책을 따른다. 256-page 초기 pool의 증가분은 0.625MiB이며, 기존 최대 8,192-page cap에 도달하면 구조체 총량은 178.4375→198.4375MiB다. capture ring 예산과 live snapshot 투영은 커진 실제 구조체 크기를 계산하고, 별도 producer pool은 기존 page 개수 cap을 유지한다. 예산과 무관하게 지역 컨테이너를 늘리거나 이벤트마다 heap 객체를 만들지 않는다. 계측 OFF 및 warm producer의 무할당 검사는 이 저장소 경계만 검증하며 cold pool 확장이나 전체 renderer의 무할당을 뜻하지 않는다.

연결 키는 이벤트의 frame/thread/depth/flags/queue/submission/view/ticks/marker 전체다. cross-frame 및 Stop에서 잘린 스코프도 같은 키를 유지한다. 중복 매칭이나 없는 정보는 unknown으로 표시하며 가장 가까운 시간이나 마지막 제출에서 추측하지 않는다. ProfilerViewer 타임라인과 Python 분석 도구는 RHI identity를 표시하고, 이 sidecar를 real-view/GPU 처리량·FG·FPS의 분모에 섞지 않는다. Python의 `--include-rhi-identities`는 원본 identity와 exact/ambiguous/unmatched 판정을 내보낸다.

owned 경로는 caller의 CPU context를 RHI owner/worker에 전달하고 batch의 frozen capture context를 native submit까지 보존한다. DX12 `submission_scope`는 실제 `ExecuteCommandLists`만 감싸며 뒤의 Signal 시간이나 GPU 실행 시간과 혼합하지 않는다. native DX12 query와 Vulkan offline capture query는 recording/target/pass identity를 보존하고 capture manifest의 query interval로 소비한다. Vulkan의 raw queue timestamp는 clock domain을 명시하며, 기존에 없는 Vulkan live calibrated GPU timing producer를 구현했다고 판정하지 않는다. Present와의 연관은 실제로 보존된 publication/view provenance가 있는 경우에만 사용할 수 있다.

## 9. 성능 공학 근거와 검증 원칙

참고 저장소 [cpu-performance-engineering](https://github.com/usamahz/cpu-performance-engineering)의 측정 우선·원인 분리·재현 가능한 benchmark 원칙을 적용했다. 현재 연결된 도구에는 해당 MCP가 없다. 저장소의 [기존 MCP/CLI 확인 기록](reac-20261019/shaders/cpu-perf-evidence.json)은 CLI selftest 성공과 MCP stdio initialize timeout을 구분한다. CLI 가용성을 MCP 접속 성공으로 보고하지 않는다.

이번 증거는 실제 소스 입력 bytes와 SHA-256, compiler/flags, pinned enkiTS/mimalloc, 실행 명령을 보존한다. synthetic CPU 경계와 실제 backend를 구분하고 correctness를 먼저 검사한다. 바뀌기 전 mirror의 기능 PASS를 최종 수정 소스의 PASS로 바꾸지 않는다. allocator 호출 계수와 elapsed time을 구분하며 하드웨어 counter/실제 GPU 검증이 없는 범위를 명시한다.

## 10. 재현 및 제품 수용

CPU 회귀 runner는 [verify-rhi-software-portable.py](../../Tools/regression/verify-rhi-software-portable.py), [verify-rhi-software-queue.py](../../Tools/regression/verify-rhi-software-queue.py), [verify-vulkan-software-commands.py](../../Tools/regression/verify-vulkan-software-commands.py)다. 입력 의존성을 고정 mirror로 복사하고 최종 live hash와 비교한다. runner별 결과의 native 경계 설명을 함께 읽어야 한다.

비 Shipping profiler의 실제 collector/producer/ring/codec은 [verify-profile-rhi-context.py](../../Tools/regression/verify-profile-rhi-context.py)의 `--compat` 및 `--sanitize`로 검사한다. Windows QPC/thread ID/process counter만 명시적인 portable boundary로 대체한다. Python 소비자 호환은 [verify-material-profile-format.py](../../Tools/regression/verify-material-profile-format.py)로 검사한다. ProfilerViewer의 실제 UI 실행을 이 CPU 검사로 대체하지 않는다.

[정적 검사 근거](RHISoftwareCompletion20261021-static.json)는 신규 native 소스 10개의 project/filter 단일 등록과 XML 파싱을 확인한다. `git diff --check`는 통과했다. `scripts/check_include_boundary.py`는 이 Linux checkout에서 **실패**했으며, 같은 파일 집합의 master를 별도 디렉터리로 재구성해 비교한 결과 stdout/stderr가 완전히 같았다. 두 실행 모두 상향 include 27개·물리 경계 부채 69개를 보고했다. 이 변경에 의해 늘어난 항목은 없었지만 전역 gate를 PASS로 바꾸지 않았고 allowlist도 수정하지 않았다.

Windows에서는 먼저 실제 Editor/RenderEngine Debug 빌드를 완료한 뒤 다음 명령을 새 출력 디렉터리로 실행한다.

```powershell
pwsh -File Tools/regression/verify-rhi-software-commands.ps1 -Configuration Debug
pwsh -File Tools/regression/verify-rhi-software-commands.ps1 -Configuration Release
pwsh -File Tools/regression/verify-rhi-software-native.ps1 -OutputDirectory Artifacts/rhi-software-completion-native
```

DX12 native 회귀에는 owned graphics의 두 프레임 clear/barrier/readback, duplicate submit 거부, 실제 graphics↔compute timeline wait/signal, GPU 완료 후 owner 반환과 native target pool 재사용 검사를 추가했다. compute 테스트는 timeline 검증이며 compute workload 성능 측정은 아니다. 이 호스트에는 MSVC/Windows SDK, 실행 가능한 CreatorEditor, GPU/Vulkan ICD가 없어 이 native 회귀를 실행하지 않았다.

최종 제품 수용은 같은 CreatorRobot 장면·해상도·worker 수·전경·워밍업에서 direct/serial/parallel을 순서를 바꾸어 반복하고 기록 ON/OFF와 ETW를 분리해야 한다. 색상·깊이·readback, query identity, 플레이 시작/종료, reload/resize, 정상 shutdown, device-loss quarantine을 백엔드별로 확인한다. 평균/p50/p95/p99, 전체 critical path, CPU Running/Ready/Waiting, allocator, GPU in-flight, publication→presentation을 함께 비교해야 한다. 기존 허용 오차를 완화하거나 software 실패 시 자동 native fallback으로 오류를 숨기지 않는다.

## 11. 최종 CPU 검증과 측정 결과

### 기능 및 메모리 계약

검증 대상은 표에 적은 실제 엔진 소스다. native device/queue/encoder 또는 WSI 경계에는 CPU fixture를 사용했다. 정상 실행과 ASan/UBSan 실행은 같은 최종 입력 SHA-256을 사용했고 실행 후 소스 불일치는 없었다. 공용 RHI·queue·Vulkan 검사는 `CE_SHIPPING=1` 조건으로 실행했다. 별도 profiler 검사는 `CE_SHIPPING=0`의 실제 EngineDiagnostics를 실행했으며, 두 종류 모두 실제 native driver나 전체 Editor 실행을 포함하지 않는다. LeakSanitizer는 이 호스트의 프로세스 검사 제약 때문에 비활성화했다.

| 근거 | 최종 일반 실행 | 최종 ASan/UBSan | 검사 범위 |
|---|---:|---:|---|
| [공용 명령·번역·FIFO](RHISoftwareCompletion20261021-validation.json) | 33,883 checks / 1,755 scenarios PASS | 33,883 checks / 1,755 scenarios PASS | 실제 recorder/list/translator/submission thread/arena/mimalloc/scheduler; 입력 137개 |
| [Owned queue 어댑터](RHISoftwareCompletion20261021-queue.json) | 4,845 checks PASS | 4,845 checks PASS | 실제 queue wrapper와 scheduler; 입력 100개 |
| [Vulkan 계약](RHISoftwareCompletion20261021-vulkan.json) | 393 checks PASS | 393 checks PASS | 실제 Vulkan translation units 및 명시된 device 함수 본문; 입력 122개, 공식 Vulkan 1.3.290 헤더, native/WSI fixture |
| [비 Shipping profiler](RHISoftwareCompletion20261021-telemetry.json) | identity 517 + 기존 core 1,396 checks PASS; 기존 render measurement exit 0 | 동일 결과 PASS | 실제 EngineDiagnostics 8개 TU와 codec; 입력 30개, QPC/process-counter 및 test-allocator boundary |
| [Python capture 소비자](RHISoftwareCompletion20261021-validation.json) | 135 checks PASS | 해당 없음 | snapshot/streaming 1~9 호환, 64-bit identity, FPS 분모 보존 |

공용 회귀는 worker 1/2/4/8, target 1/2/4/8, target당 segment 1/3/17, 번역 job 상한 1/2/4/8, 직렬/병렬·역순·불균등 입력을 검사한다. 명령 digest와 개수, 원래 제출 순서, producer payload 파괴, bool fallback, deterministic failure, peer worker 합류, 중복 제출, CPU/GPU 소유권 경계를 확인한다. CPU byte admission 검사는 단일 batch 초과, 합계 제한 대기, owner 취소 및 quarantine뿐 아니라 **취소된 자원의 소멸자가 아직 실행 중이면 credit을 반환하지 않는지**까지 검사한다. 기존 39개 encoder 메서드 coverage 검사도 통과했다.

기준 master의 공용 검사는 같은 외부 번역 fixture로 33,810 checks / 1,748 scenarios를 통과했다. baseline/current의 차이는 새 byte admission 5개 및 structured failure 2개 시나리오 등이며, 검사 개수 증가를 성능 개선으로 해석하지 않는다. Vulkan 증거는 전체 Windows Editor 빌드가 아니다. `VulkanDeviceResources.cpp`의 플랫폼 의존 구간은 증거 파일에 명시한 실제 함수 본문을 추출해 검사했다.

### 실제 allocator 호출 비교

[CPU 측정 JSON](RHISoftwareCompletion20261021-cpu.json)의 비교는 `BeginRecording → AcquireEncoder → FinishRecording` 구간이다. 실제 mimalloc allocator 요청을 관찰했으며, 16회 워밍업 후 17개 표본의 중앙값이다. 소멸자는 측정 구간 밖이다.

| 기록 형태 | master `088ea7b` mimalloc 호출 | 이번 구현 mimalloc 호출 | 변화 |
|---|---:|---:|---|
| 8 target 유지 | 9 | 9 | 동일 |
| 8→1→8로 축소 후 재확장 | 65 | 9 | 56회 감소, **86.15% 감소** |

두 경우 모두 이 구간의 `std::operator new` 호출은 0이었다. 이는 renderer의 실제 allocator 요청 9회가 남는다는 사실과 함께 읽어야 한다. 전체 프레임 무할당, RSS 감소, 프레임 시간 감소를 뜻하지 않는다. 별도 owned wrapper의 128회 반복 8/1/8 warm cycle은 shared control block을 포함해 각각 10/3/10회, 요청 바이트 400/120/400으로 일정했다. Vulkan의 warm snapshot/binding fixture는 128회 동안 `std::operator new` 0회를 확인했으며 native driver 내부 할당은 측정하지 않았다.

### 현재 구현의 직렬/병렬 CPU 대조 실험

아래는 변경 전후 속도 비교가 아니라 **현재 실제 translator의 Serial/Parallel 비교**다. GPU 대신 명령 digest를 계산하는 native encoder 경계를 사용하고, 기록은 측정 구간 밖에서 수행했다. 조건마다 16쌍 워밍업 후 직렬/병렬 순서를 교대해 65쌍을 측정했다. compiler는 GCC 13.3, 기존 정책은 `minCommandsPerJob=256`, target 8개/각 segment 4개다. 컴파일과 다른 회귀 실행을 멈춘 약 5초의 실험이지만 전용 물리 머신이나 Windows 환경은 아니다.

| 물리 worker | 입력 형태 | 명령 수 | Serial p50/p95 µs | Parallel p50/p95 µs |
|---:|---|---:|---:|---:|
| 1 | 작은 batch | 224 | 2.203 / 2.233 | 2.204 / 2.244 |
| 1 | 균등 | 131,264 | 1,034.289 / 1,247.510 | 1,089.372 / 1,337.886 |
| 1 | 단일 target 집중 | 69,824 | 501.375 / 755.108 | 610.069 / 843.791 |
| 1 | 균등 + 명시적 CPU 작업 | 32,960 | 1,596.146 / 2,151.222 | 1,668.355 / 2,228.679 |
| 4 | 작은 batch | 224 | 2.804 / 3.034 | 2.794 / 2.935 |
| 4 | 균등 | 131,264 | 963.532 / 1,220.990 | 654.617 / 945.114 |
| 4 | 단일 target 집중 | 69,824 | 505.021 / 702.098 | 602.698 / 818.828 |
| 4 | 균등 + 명시적 CPU 작업 | 32,960 | 1,542.044 / 2,031.171 | 673.595 / 871.243 |
| 8 | 작은 batch | 224 | 2.885 / 6.840 | 2.925 / 5.789 |
| 8 | 균등 | 131,264 | 936.571 / 1,305.257 | 587.025 / 893.696 |
| 8 | 단일 target 집중 | 69,824 | 491.570 / 733.025 | 595.057 / 895.235 |
| 8 | 균등 + 명시적 CPU 작업 | 32,960 | 1,530.837 / 1,934.836 | 554.936 / 951.223 |

작은 batch는 병렬 옵션에서도 job 1개로 owner에서 처리한다. 균등 + CPU 작업은 명령마다 32회 정수 연산을 추가한 fixture이며 native driver의 비용 모델이 아니다. 8-worker 균등 입력의 p50은 약 37.3% 짧았지만 단일 target 집중 입력은 약 21.1% 길었다. 명령이 큰 target 하나에 몰리면 동적 target 배분만으로 그 target의 critical path를 줄일 수 없다.

측정 뒤 별도 1회 실행의 물리 worker별 명령 수는 8-worker 균등 + CPU 작업에서 각 4,120개였다. 균등 입력은 6개 worker가 참여했고, 집중 입력은 4개 worker가 참여했으며 한 target의 66,072개 명령이 전체 69,824개 중 대부분을 차지했다. 이 단일 관측은 매 실행의 균등 참여나 CPU 이용률 증명이 아니다. 물리 worker hook ID 1~8을 배열 0~7로 정규화했고 owner는 별도 8번 칸이다.

이 결과만으로 명령 종류별 비용, 실제 driver, Debug 계측 및 GPU overlap을 포함하는 제품 임계값을 다시 정하지 않았다. 검증된 개선은 저장소 재확장의 allocator 요청 감소이며, 전체 프레임 개선량은 §10의 native 전후 계측이 필요하다.
