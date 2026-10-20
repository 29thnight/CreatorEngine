# 2026-10-20 워커 분배 기준선과 병목 분석

## 결론과 범위

최신 저장 계측은 **워커 풀 전체의 장기 편중보다 렌더 소유자 스레드의 직렬 작업 집중과 명령 기록 대상별 작업량 차이**를 보여 준다. ETW ON 구간에서 공용 워커 8개의 평균 Running 비율은 각각 7.15–8.02%이고 최대/최소 비율은 1.121배다. 같은 구간의 RenderThread는 81.28% Running이다. `RenderGraphDeclare`는 평균 5.097 ms 중 5.035 ms가 Running이므로, 이 단계의 대부분을 워커 대기 시간으로 해석할 수 없다.

명령 기록에는 별도의 개선 후보가 있다. 현재 캡처는 8개 공용 워커를 사용하지만 네이티브 command target은 4개다. `RenderCommandRecord.Worker1`이라는 **기록 대상 1**은 그래프당 두 작업의 합이 평균 1.723 ms이며, 그중 OS Running이 1.671 ms다. 이것은 `[Worker 1]`이라는 물리 작업 스레드 번호가 아니다. 다른 기록 대상과 비용 차이가 있지만, 의존 관계와 같은 대상의 순서를 유지한 채 분배해야 한다.

이 문서는 **변경 전 저장 계측을 재분석한 기준선**이다. 변경 후 Windows 호스트 실행, 실제 GPU 출력 검증 및 엔진 프레임 성능 재측정은 이 분석 환경에서 수행하지 않았다. 작업 분배 변경이 실제 프레임 시간을 줄였다는 주장은 하지 않는다. 정본 수치는 [WorkerBalance20261020.json](WorkerBalance20261020.json)에 있으며, 대용량 원본을 복제하지 않고 기존 계측 파일을 참조한다.

참고 방법론은 사용자가 제시한 [cpu-performance-engineering](https://github.com/usamahz/cpu-performance-engineering)의 공식 README다. 전용 MCP는 이번 세션의 호출 가능한 도구로 노출되지 않아 README의 측정·동시성·allocator 관점을 적용했다. 이 작업에서는 저장 실측과 코드 정적 근거를 구분하고, 누적 작업 합과 의존 관계의 critical path를 따로 평가하며, 분배 장치에는 고정 크기 또는 소유자가 재사용하는 저장소를 사용한다. 외부 코드를 복제한 구현이 아니며, 실제 변경 후 측정 전에는 speedup을 주장하지 않는다.

## 1. 캡처와 소스 신원

| 항목 | 확인한 값 |
|---|---|
| 계측 파일 게시 커밋 | `7ebcd2b1a2a181e88e4c2fe1c8952242242ab536` |
| 캡처가 기록한 코드 HEAD | `4eeba98f2c9ad5907debfa4eaa10b2f004afb149` — owned declaration packet 병렬화 이후 |
| 구성·장면 | Debug, CreatorRobot, Play mode, CreatorEditor, 엔티티 62개 |
| 렌더 뷰 | Scene `viewId=1`, Game `viewId=2`, 각각 1186×514 |
| 실행 자원 | 공용 워커 8개, 논리 CPU 8개, 프로세스 affinity `0xff` |
| OS가 보고한 코어 ID | `0, 2, 4, 6` — 논리 CPU 두 개씩 연결 |
| 백엔드 | 네이티브 DX12 앞의 software command recording/translation wrapper 사용 |
| GPU validation | off |
| 구간 | baseline 및 ETW, 각각 엔진 계측 ON/OFF 약 15초 |
| ETW ON 범위 | 1364.921–16377.786 ms, 15,012.865 ms |
| ETW OFF 범위 | 26462.896–41476.539 ms, 15,013.643 ms |

`softwareRhi=true`는 CPU에서 GPU를 에뮬레이션했다는 뜻이 아니다. 엔진의 소프트웨어 명령 스트림을 CPU에서 기록하고 네이티브 명령으로 번역하는 경로다. `RHISoftwareTranslate`를 GPU 패스 시간과 혼합하지 않는다.

근거: [measurement.json](../../Artifacts/current-state-20261020/measurement.json), [identity.json](../../Artifacts/current-state-20261020/json/conditions/identity.json), [worker-count.json](../../Artifacts/current-state-20261020/json/conditions/worker-count.json), [worker-start-scheduler.json](../../Artifacts/current-state-20261020/json/conditions/worker-start-scheduler.json), [baseline-play-before.json](../../Artifacts/current-state-20261020/json/conditions/baseline-play-before.json), [ranges.json](../../Artifacts/current-state-20261020/json/conditions/ranges.json). `viewId`의 의미는 [EnhancedSceneRenderer.h](../../Engine/RenderEngine/Render/Scene/EnhancedSceneRenderer.h)의 안정된 Editor/Game view ID 계약을 따른다.

### 소스 신원의 한계

[source-identity.json](../../Artifacts/current-state-20261020/json/source-identity.json)은 캡처 전부터 존재한 Editor 프로젝트·GUI·문서 변경 등 dirty 목록을 함께 기록한다. [source-verification.json](../../Artifacts/current-state-20261020/json/source-verification.json)의 `sourceUnchanged=true`는 **캡처 도중 소스가 변하지 않았다는 뜻**이며, 전체 작업 트리가 깨끗한 HEAD였다는 보증이 아니다.

기록된 582개 파일 SHA256을 변경 중인 작업 트리가 아닌 고정된 `4eeba98` Git blob과 대조했다. 98개는 원문 바이트가 일치하고 445개는 LF를 CRLF로 변환했을 때 일치한다. 나머지 39개는 일률적인 줄바꿈·BOM 변환으로 일치하지 않아 동등성을 확정하지 않았다. 혼합 줄바꿈일 수도 있고 다른 바이트 차이일 수도 있으며, SHA만으로 둘을 구분하지 않는다. 목록과 핵심 경로의 hash는 정본 JSON의 `integrity.source_identity`에 보존했다. `EnhancedRenderGraph.cpp`, `EnhancedSceneRenderer.cpp`, `MaterialGraphSceneHost.cpp`, `GpuGeometryVisibility.cpp`, `RHICommandTranslator.cpp`, `RHISubmissionThread.cpp` 등 분석 경로는 원문 또는 CRLF 변환 hash로 연결된다.

저장소 [verification.json](../../Artifacts/current-state-20261020/verification.json)은 원본 381개 파일의 hash 일치와 캡처 round trip 일치를 보고한다. 이번 독립 분석에서는 필요한 43개 파일을 내려받아 manifest의 byte 수와 SHA256을 다시 확인했으며 불일치는 0개였다. 이번 분석이 381개 전체를 다시 검증했다고 주장하지 않는다.

## 2. 시간 축과 표본 선택

- **baseline 전체 summary**는 ETW를 켜지 않은 엔진 계측 캡처다. 프레임 경계 402개, 완전한 `RenderGraphDeclare`/`MaterialGraphScenePrepare`/`RenderCommandRecord` scope는 각각 794개다. CPU 렌더 제출 신원이 완전한 표본은 뷰별 396개다.
- **ETW 전체 summary**는 프레임 경계 376개이고, CPU 렌더 제출 표본은 뷰별 349개다. 아래 OS 상태 분석은 전체 summary와 구분하여 **ON 구간에 완전히 포함된 scope**만 사용한다. 경계 partial flags의 하위 두 비트가 설정된 scope는 제외한다.
- ETW ON의 완전한 `RenderCommandRecord`는 678개이며, Scene/Game 각각 339개로 실제 렌더 측정 구간에 일대일 대응한다. 작업 scope 4407개 모두 부모 기록 구간에 속한다. `MaterialGraphScenePrepare`는 경계 위치 차이로 679개다.
- p50은 median, p95/p99는 정렬한 실제 표본의 `min(n-1, floor(n*p))` 인덱스다. 서로 다른 자식 scope의 p95를 더하지 않는다.
- ETW의 Running은 OS가 CPU에 올린 시간으로 유용한 작업량과 동일하지 않다. Ready는 실행 가능하지만 스케줄되지 않은 시간이고, Waiting 자체는 특정 mutex·fence의 대기 원인을 알려 주지 않는다.
- 여러 워커의 시간 합은 **core-ms** 또는 겹치는 scope의 합이다. 이를 wall time, 프레임 시간, 화면에 표시된 FPS로 바꾸어 쓰지 않는다.

[baseline summary](../../Artifacts/current-state-20261020/json/baseline/summary.json), [ETW summary](../../Artifacts/current-state-20261020/json/etw/summary.json), [ETW timeline metadata](../../Artifacts/current-state-20261020/json/etw/timeline-metadata.json), [scope-metrics.json](../../Artifacts/current-state-20261020/json/scope-metrics.json)이 근거다. 원본 scope와 OS 상태를 다시 교차하여 주요 네 단계의 count·mean·Running/Ready/Waiting 평균이 저장된 scope-metrics와 반올림 오차 범위에서 같음을 확인했다. ETW lost buffers/events는 모두 0이고, 캡처의 partial scope는 14개다.

## 3. 공용 워커와 소유자 스레드

ETW ON 구간의 전체 OS 상태다. 이 표는 scope의 중첩 합이 아니다.

| 스레드 | TID | Running ms | Running % | Ready % | Waiting % |
|---|---:|---:|---:|---:|---:|
| Worker 1 | 41212 | 1157.188 | 7.708 | 1.976 | 90.316 |
| Worker 2 | 34792 | 1106.196 | 7.368 | 2.304 | 90.327 |
| Worker 3 | 11764 | 1079.478 | 7.190 | 2.300 | 90.510 |
| Worker 4 | 41704 | 1203.606 | 8.017 | 1.982 | 90.001 |
| Worker 5 | 41928 | 1184.620 | 7.891 | 1.965 | 90.144 |
| Worker 6 | 25748 | 1083.018 | 7.214 | 2.321 | 90.465 |
| Worker 7 | 16056 | 1073.741 | 7.152 | 2.282 | 90.566 |
| Worker 8 | 32968 | 1200.635 | 7.997 | 1.998 | 90.005 |
| RenderThread | 28068 | 12202.699 | 81.282 | 0.922 | 17.796 |
| RHIThread | 29888 | 800.315 | 5.331 | 0.592 | 94.077 |
| PresentationThread | 37368 | 4083.657 | 27.201 | 0.369 | 72.429 |
| GameThread | 35984 | 1319.856 | 8.791 | 0.357 | 90.851 |

공용 워커의 Running 합은 9088.482 core-ms다. 이를 15,012.865 ms로 나누면 **평균 0.605개 워커가 동시에 CPU에서 실행**된 셈이다. 8개 풀의 평균 사용률은 7.567%이며, 60.5%의 전체 머신 사용률이라는 뜻이 아니다. 워커 Running의 변동계수는 4.65%다.

엔진 계측 OFF·ETW ON 구간에서도 공용 워커 Running은 7.85–8.58%, 최대/최소 1.093배이고 RenderThread Running은 74.02%다. 계측 ON 한 구간만의 현상으로 단정하지 않으면서도, 실제 작업 비용의 정량 비교는 동일한 측정 모드에서 해야 한다.

원본은 [Worker 1 thread](../../Artifacts/current-state-20261020/json/etw/thread-41212.json)를 포함한 `json/etw/thread-<TID>.json`의 `states`다. 스레드 이름과 TID 대응은 [ETW metadata](../../Artifacts/current-state-20261020/json/etw/metadata.json)에 있고, 전체 숫자는 정본 JSON의 `etw_thread_windows`에 있다.

### 렌더 단계가 진행되는 동안의 동시 실행

| 소유자 단계 | 완전한 scope 수 | 해당 구간에서 CPU에 올라간 공용 워커 수의 평균 | 8개 풀 기준 Running % |
|---|---:|---:|---:|
| ScenePrepare | 679 | 1.376 | 17.20 |
| Declare | 678 | 0.225 | 2.81 |
| Compile | 678 | 0.002 | 0.03 |
| Record | 678 | 2.201 | 27.51 |

이 표는 **시간상 동시성**이다. 같은 구간에서 실행되는 이전 제출의 RHI 번역 등도 포함될 수 있으므로, 해당 단계가 그 워커 작업을 직접 소유한다고 해석하지 않는다. 기록 job scope 내부의 Running만 합한 값은 그래프당 평균 3.932 core-ms이며, 아래 Record wall 2.238 ms와 별개의 값이다.

## 4. CPU 단계별 병목

### ETW 없는 baseline 전체 scope

| 단계 | scope 수 | 평균 ms | p95 ms |
|---|---:|---:|---:|
| RenderThreadFrame | 401 | 28.9159 | 32.1104 |
| RenderView | 794 | 12.7257 | 14.7164 |
| RenderGraphDeclare | 794 | 4.4058 | 5.1819 |
| MaterialGraphScenePrepare | 794 | 2.7748 | 3.3690 |
| RenderCommandRecord | 794 | 1.8888 | 2.2795 |
| RHISoftwareTranslate | 794 | 1.0639 | 1.2274 |
| RenderGraphCompile | 794 | 0.7963 | 1.1069 |
| RenderGpuCollect | 1599 | 1.3648 | 3.3272 |

부모·자식 또는 다른 스레드에서 겹치는 단계가 섞여 있으므로 열을 합쳐 프레임 시간을 만들지 않는다. `RenderGpuCollect`는 뷰마다 정확히 한 번 호출되는 scope도 아니다. 별도 CPU 제출 신원 표본의 평균은 Game 12.6548 ms, Scene 12.7123 ms이고 각각 396개다.

### ETW ON에서 확인한 소유자 실행·대기

| 단계 | wall 평균 ms | Running ms | Ready ms | Waiting ms |
|---|---:|---:|---:|---:|
| Declare | 5.0969 | 5.0355 | 0.0229 | 0.0386 |
| ScenePrepare | 3.3817 | 3.0607 | 0.1024 | 0.2184 |
| Record | 2.2384 | 0.3012 | 0.0137 | 1.9236 |
| RHI Translate | 1.2165 | 0.2943 | 0.0168 | 0.9055 |
| Compile | 0.8705 | 0.8694 | 0.0008 | 0.0002 |
| GPU Collect | 1.5092 | 1.4977 | 0.0064 | 0.0050 |

Declare의 wall 중 Running은 98.79%다. 반대로 Record와 RHI Translate 소유자는 대부분 워커 완료를 기다리는 구조이며, 작업 단위 재분배 후보가 된다. GPU Collect는 이름과 달리 이 표본에서 대부분 CPU Running이므로, 비싼 시간을 모두 GPU fence 대기로 설명하면 잘못이다. 구체적인 collect 내부 원인은 별도 조사 대상이다.

### Declare의 비싼 노드

`RenderDeclare.<node>`라는 실제 이름별 scope가 있어 순서로 노드 번호를 추측할 필요가 없다. 아래는 baseline 794회씩의 평균이다.

| 노드 | 평균 ms |
|---|---:|
| METEOR.Scene.Cull.First | 0.5866 |
| METEOR.Shadow.Cull.Final | 0.4196 |
| METEOR.Shadow.Occluder | 0.3601 |
| SSGI | 0.3283 |
| METEOR.Shadow.Raster | 0.3179 |
| METEOR.Shadow.History | 0.2855 |
| METEOR.Scene.Visibility | 0.2418 |
| METEOR.Scene.Occluder | 0.2360 |
| METEOR.Scene.Geometry | 0.2041 |
| METEOR.Scene.Cull.Final | 0.1817 |

`RenderGraphDeclareNodeBody`가 이 범위 안에 중첩되므로 두 종류의 scope를 더하지 않는다. 기존 병렬 packet build는 공용 워커 8개에서 총 약 108 ms/794 Declare, 즉 뷰당 약 0.136 ms의 **작업 합**에 불과하며, 호출당 평균 6.3–7.0 µs다. Packet dispatch는 뷰당 평균 2.5회×0.0276 ms, join은 0.0654 ms다. 이 작은 경로를 모든 Declare의 대리 지표로 사용하지 않는다.

### ScenePrepare의 남은 비용

baseline의 `MaterialSceneIndirectCommandsPrepare`는 평균 0.8248 ms이고 내부 `MaterialSceneIndirectPrepare`는 0.8103 ms다. `MaterialScenePacketJoin`은 0.3988 ms, `SurfaceStagesPrepare` 0.2547 ms, `TilePacketPrepare` 0.1682 ms다. `MaterialMeshGpuPrepare`는 뷰당 8회이며 합계 평균 약 0.5033 ms다. 값은 중첩 관계가 있어 단순 합산 대상이 아니다.

PacketTask는 뷰당 4개, 합계 평균 1.2923 ms의 병렬 작업량이며, 내부 PacketPrepare는 뷰당 8회다. PlanUpload와 OutputBindings는 각각 뷰당 9회이고 두 종류의 scope 합은 약 0.1267 ms다. 따라서 이 두 작은 마커만 없애서 PacketPrepare의 전체 작업량이 사라진다고 볼 수 없다. ETW PacketJoin 평균 0.4995 ms 중 Running 0.3150 ms는 owner assist가 포함되는 구현과 일치하며, join wall 전체를 수동적인 대기로 계산하지 않는다.

## 5. 기록 대상별 분배

시간 구간 포함 관계로 ETW ON의 678개 Record를 렌더 측정과 연결했다. Scene 339개는 모두 6개의 job scope, Game 339개는 모두 7개의 job scope였다. 같은 target이 의존 단계마다 다시 실행되므로 target 수와 job 수는 다르다.

| command target | Scene job 수/graph | Scene job wall 합 평균 ms | Game job 수/graph | Game job wall 합 평균 ms |
|---|---:|---:|---:|---:|
| 0 | 1 | 0.9300 | 1 | 1.0022 |
| 1 | 2 | 1.6760 | 2 | 1.7692 |
| 2 | 1 | 0.3876 | 2 | 1.0446 |
| 3 | 2 | 1.3374 | 2 | 1.2100 |

Scene은 target 합 최대/최소가 약 4.32배, Game은 약 1.77배다. 이 비율은 서로 다른 의존 job 수를 가진 target의 합을 비교한 것이며, 그 비율만큼 빨라질 수 있다는 예측이 아니다. 두 뷰 전체에서 target 1의 wall 합 1.7226 ms 중 실제 Running은 1.6710 ms다. 해당 편중은 OS 대기만으로 생긴 것이 아니다.

| 기록 단계 관측 | 평균 ms/graph | 해석 |
|---|---:|---|
| owner Record wall | 2.2384 | 실제 기록 API 구간 |
| 모든 record job wall 합 | 4.6784 | 병렬 구간이 겹치는 합; wall 아님 |
| record job 시간 구간의 합집합 | 1.7914 | 하나 이상 job scope가 열려 있는 시간 |
| Record 시작→첫 job 시작 | 0.2557 | 사전 준비와 dispatch 포함 |
| 마지막 job 종료→Record 끝 | 0.1339 | join 후 처리·finalization 포함 |
| 첫 job→마지막 job 사이 모두 비어 있는 간격 | 0.0574 | 의존 전환 등의 구간; 원인은 별도 확인 |

관측한 모든 그래프의 마지막 job은 target 3의 두 번째 짧은 job이었다. 따라서 target 1이 가장 비싸다는 사실만으로 전체 DAG의 마지막 노드나 임계 경로를 확정할 수 없다. 캡처에는 개별 record unit의 CPU callback timing과 실제 진단 topology payload가 없고, [recording-task-graph.json](../../Artifacts/current-state-20261020/json/conditions/recording-task-graph.json)은 기본 모드만 기록한다. GPU pass 비용을 CPU 기록 비용으로 대신하여 가상의 최적 분배 성능을 계산하지 않았다.

## 6. GPU와 계측 영향

baseline은 각 뷰 396개 렌더의 GPU pass 표본을 갖는다. 같은 뷰에서 pass별 시간을 합하고 렌더 수로 나눈 평균은 Scene 5.9646 ms, Game 6.1244 ms다. 이 값은 **기록된 GPU pass 시간 합**이며, 보정된 queue busy 합집합·GPU 전체 프레임 span 또는 p95가 아니다.

`Shared.First.Cull`, `Shared.ShadowRecovery.Cull`, `CameraMeshlet.Recovery.Cull` 세 종류의 합은 Scene 평균 2.5240 ms, Game 2.5395 ms로, 각 뷰의 측정 pass 합 중 42.32%, 41.46%다. CPU command target 분배는 이 GPU shader 자체의 수행량을 줄이는 변경과 구분한다.

완료한 real view render 수를 창 길이로 나눈 관측률은 다음과 같다. **Scene/Game 합계 뷰 처리율이며 표시 FPS가 아니다.**

| 모드 | 완료 뷰 수 | 초 | 뷰/초 |
|---|---:|---:|---:|
| baseline, 엔진 계측 ON | 778 | 15.1597 | 51.3201 |
| baseline, 엔진 계측 OFF | 904 | 15.1354 | 59.7277 |
| ETW, 엔진 계측 ON | 686 | 15.1889 | 45.1646 |
| ETW, 엔진 계측 OFF | 828 | 15.1671 | 54.5918 |

서로 다른 창의 계측 부담과 실행 조건이 섞여 있으므로, 이 차이를 단일 요인의 인과 효과나 변경 후 개선율로 제시하지 않는다. [measurement-windows.json](../../Artifacts/current-state-20261020/json/measurement-windows.json)과 [environment](../../Artifacts/current-state-20261020/json/conditions/baseline-environment.json)의 관측 한계도 유지한다.

## 7. 변경과 수용 조건

### 변경의 적용 범위

이 기준선은 다음 작업의 근거다. 명령 target 상한을 실제 가용 워커 범위에 맞추되 기존 최대 8개의 계약을 지키고, contiguous partition의 최대 추정 비용을 줄이는 후보를 기존 CPU dependency·wave 수용 조건으로 평가한다. RHI 번역에서는 긴 target을 먼저 배정하고, 먼저 끝난 작업자가 아직 시작하지 않은 target을 맡을 수 있게 한다. ScenePrepare에는 독립 packet 작업의 분리와 재사용 scratch를 구현했다. **반복 상수 업로드 배칭은 이번 PR에서 수행하지 않았다.**

### command target 용량과 소유권의 정적 검토

[WorkPartition.h](../../Engine/Utility_Framework/WorkPartition.h)의 새 분할 함수는 caller가 제공한 span과 scalar만 사용한다. 비용 합의 정수 overflow를 결과 기록 전에 거부하며, 기존 명령 순서를 보존하는 연속·비어 있지 않은 구간을 만든다. [EnhancedRenderGraph.cpp](../../Engine/RenderEngine/Render/Graph/EnhancedRenderGraph.cpp)의 `consider`가 기존 CPU dependency와 wave 수, 추정 critical work 조건으로 후보를 평가한다. 최대 target 추정 비용의 최소화만으로 후보를 무조건 채택하지 않는다.

command target 수와 raster slice 수는 다른 값이다. [MaterialGraphSceneHost.cpp](../../Engine/RenderEngine/MaterialGraphSceneHost.cpp)의 `RasterRecordingSlices`는 최대 4개를 반환하며, [MaterialGraphSceneShadowStage.inl](../../Engine/RenderEngine/MaterialGraphSceneShadowStage.inl)을 포함한 두 raster split caller가 이 값을 전달한다. 그래프는 `min(pass.maxSlices, poolWorkers)`를 적용하므로 command target을 8개로 늘려도 `recordedGeometryRoutes[4]`와 `shadowRecordingStats[6][4]`의 slice 인덱스는 0–3이다. DX12·Vulkan command pool의 slot/encoder는 초기화된 worker 수만큼 준비되고, DX12 제출 배열과 공통 target 검증의 최대 용량은 기존 `kMaxWorkers=8`이다.

정상 Editor·Player 부팅에서는 [EngineBootstrap.h](../../Engine/RuntimeHost/EngineBootstrap.h)의 `InitializeRuntime`이 scheduler를 시작한 뒤 `Run`이 app을 생성·초기화한다. 이후 렌더 스레드의 첫 pipeline build에서 live command pool이 초기화된다. 따라서 정상 부팅에서 scheduler가 아직 시작되지 않아 command pool이 1개 fallback으로 고정되는 순서는 확인되지 않았다.

[RHICommandTranslator.cpp](../../Engine/RenderEngine/RHI/RHICommandTranslator.cpp)는 고정 용량을 넘는 target count를 결과 배열 접근 전에 거부한다. 고정 크기 claim queue는 각 target을 한 작업자에게만 배정하고, 그 작업자가 해당 target의 모든 segment를 번역한다. 모든 작업을 join한 뒤 실패 선택과 최종 제출은 원래 target 순서를 사용한다. 이 정적 검토는 native driver의 병렬 실행 검증을 대신하지 않는다.

### 변경 후 실행에서 확인할 조건

**추정 비용의 균형은 실제 비용의 균형을 보장하지 않는다.** 변경 후에는 같은 코드 구성·장면·뷰·worker 수의 Windows 실행에서 다음을 확인해야 한다.

1. 원래 GPU command 제출 순서, CPU dependency, target의 단독 기록 소유권과 실패 시 전체 drain을 유지한다.
2. 1·4·8 worker와 빈 target·부분 사용 target·큰 비용·정수 overflow 경계를 검증한다. command target 최대 8개와 raster slice 최대 4개의 독립된 용량 계약을 유지한다.
3. 같은 방식으로 Record/Translate/ScenePrepare의 wall·p95, target별 실제 CPU 비용과 생성된 DAG의 critical path를 측정한다. 현재 없는 per-unit timing을 사용할 경우 그 진단 비용을 별도 구분한다.
4. 엔진 계측 OFF의 정상 실행 관측과 GPU validation을 켠 정확성 실행을 분리한다. CPU-only 회귀 통과를 실제 DX12/Vulkan 실행 통과로 표기하지 않는다.

이 문서의 렌더링 시간·사용률은 전부 변경 전 저장 계측이다. 다음 절의 CPU 회귀 검증은 코드 계약을 확인하며 Windows native 실행 완료나 성능 개선을 뜻하지 않는다.

## 8. 구현과 검증 요약

### 구현

| 변경 | 적용 내용과 유지하는 계약 |
|---|---|
| live command pool 용량 | 기존 고정 4개 대신 초기화 시 가용 공용 워커 수를 1–8로 제한한다. Record는 초기화된 pool의 worker 수를 사용하고 RHI translation job 상한도 최대 8개까지 허용한다. raster split 최대 4개의 계약은 별도로 유지한다. |
| contiguous partition | 새 minimax 함수가 연속 구간의 최대 추정 비용을 최소화한 후보를 만든다. 기존 DAG critical-work·wave 검사가 수락 여부를 결정한다. 함수 자체에는 heap 할당이 없다. |
| declared-writer 조회 | `HasDeclaredWriter`의 `const auto usages = pass.usages` 값 복사를 제거하고 기존 usages를 span으로 읽는다. 매 pass의 usage container 복사·할당을 줄이며 조회 조건은 유지한다. |
| RHI translation 분배 | active segment의 command count를 추정 비용으로 사용하여 긴 target부터 안정적으로 정렬한다. 고정 크기 atomic claim queue에서 다음 target을 가져오되 target 내부 segment의 순서와 단독 소유, 원래 순서의 오류·제출 처리를 유지한다. |
| ScenePrepare packet | camera 최대 2개와 shadow 최대 9개의 독립 descriptor를 두 phase의 indexed batch로 처리한다. 각 batch의 작업자가 다음 descriptor를 claim하며, owner가 보유한 고정 11개 `render_vector<PacketDraw>`의 용량을 다시 사용한다. vector 슬롯은 worker 번호 대신 논리 packet ID에 연결한다. 기존 retained arena의 immutable group span과 작업별 결과·오류 슬롯은 전체 drain까지 살아 있다. |

작업 분배용 지역 저장소는 bounded array·span·scalar다. RHI translation의 새 queue는 이 검증 환경에서 128 bytes이고 cost array는 64 bytes다. 이것은 해당 구조의 크기이며 함수 전체 stack 크기나 native RHI 전체의 무할당을 보장하는 값은 아니다. ScenePrepare의 packet record vector는 owner 상태에 보관하고, 참조를 빌려 `resize`·덮어쓰기를 수행한다. packet마다 지역 vector와 그 Debug proxy를 다시 만드는 방식은 피한다. record는 `UploadPackets`에서 복사되므로 재사용 vector의 주소를 완료된 frame에 게시하지 않는다.

### CPU 회귀 검증과 한계

세부 실행 결과와 실제 검증 입력의 SHA256은 [WorkerBalance20261020Validation.json](WorkerBalance20261020Validation.json)에 모은다. normal·sanitizer·실제 scheduler 검증을 구분하며, 아래의 native scheduler는 엔진 `JobScheduler`·`ThreadPool`·enkiTS CPU 경로를 뜻한다.

| 검증 | 확인한 범위 |
|---|---|
| partition oracle | 유효 입력 669,936개 case를 normal 및 ASan/UBSan에서 통과했다. 이 중 669,928개는 가능한 cut을 모두 비교하고, 4096개 원소를 가진 8개 case는 분배·단조성을 검증한다. 별도로 잘못된 입력 4개를 거부하며 결과를 보존하는지 확인한다. helper의 추가 동적 할당은 0이다. 0비용, 0·1·최대 target, 잘못된 크기와 합 overflow를 포함한다. |
| recording DAG | 실제 scheduler에서 물리 worker 1·4·8, command target 8, legacy/native 정책의 24,485개 검증을 normal 및 ASan/UBSan에서 통과했다. unrelated previous-wave 완료를 강제로 기다리게 하는 mutant는 거부됐다. |
| translation | exact production `Translate` body를 CPU RHI fixture 경계에서 실행했다. job 1·2·4·8, 실제 scheduler worker 1·4·8, target·segment 단독 소유, active-only·포화 비용, 원래 오류/제출 순서, 지연 참여자와 실패 drain을 검증했다. queue 10,000회 반복에서 일반·aligned allocation counter가 모두 0이었다. normal 및 실제 scheduler ASan/UBSan 실행을 통과했다. |
| ScenePrepare packet | exact Host packet admission/build/join과 `UploadPackets`, RenderArena 및 실제 scheduler를 실행한 308,808개 검증을 normal·ASan/UBSan에서 각각 통과했다. worker 1·2·4·8, 기록 bytes·출력 소유권·오류 순서·drain을 확인했다. 전역 pool을 시작하지 않고 주입한 scheduler만 4·8 worker로 실행한 경우도 검증한다. 작업 수 계산은 해당 scheduler가 소유한 pool의 worker 수를 조회한다. |

ScenePrepare의 warm allocation 비교는 **해당 packet CPU fixture에서 관측한 할당 요청**이다. worker 4·8, draw 97·4096, static cache hit/miss의 8개 조건에서 각각 16회 warmup 후 17개 표본을 수집했다. cache hit의 median은 preparation당 133→64회, cache miss는 172→85회였다. 변경 후 모든 표본은 각각 64·85회다. 집계는 일반·aligned `new`와 arena upstream service의 할당 요청을 포함하며, RHI upload와 기존 GPU packet plan은 fixture이고 mimalloc은 malloc shim으로 대체했다. 또한 작은 admission-count adapter가 실제 preparation helper로 전달한다. 이 결과를 전체 `MaterialGraphScenePrepare`의 무할당, Windows 할당 횟수 또는 프레임 시간 개선으로 해석하지 않는다.

ASan/UBSan 실행에서는 sandbox의 `/proc` 접근 제약으로 LeakSanitizer를 비활성화했다(`detect_leaks=0`). 따라서 이 결과를 leak 검사 통과라고 쓰지 않는다. 강제 지연·admission 실패 fixture 일부는 join되는 `std::thread`를 사용하며, 전체 결과를 실제 native RHI·driver 검증으로 확장하지 않는다.

이 Linux 환경의 include-boundary gate는 **FAIL이며 기존 실패가 그대로 남아 있다.** 변경 전 HEAD `7ebcd2b`의 Git blob을 별도 임시 트리에 복원하고 현재와 같은 sparse-checkout footprint에서 같은 검사기·allowlist로 실행한 결과, 양쪽 모두 상향 간선 27개·물리 경계 69개로 stdout까지 동일했다. 이번 수정으로 추가된 위반은 0개다. 전체 HEAD scanner 입력에는 현재 sparse checkout에 없는 `ThirdParty/DxTimingCaptureLibrary/lib/DxTimingCaptureLibrary.lib.vcxproj`가 하나 더 있어 물리 경계 70개가 된다. 이 footprint 차이를 수정에 의한 위반 감소로 보고하지 않는다. 검사기나 allowlist를 변경해 통과시키지 않았다.

Linux CPU 검증으로 확인한 범위는 분배·순서·수명·실패 처리와 제한된 할당 계약이다. **Windows 빌드, DX12/Vulkan native GPU 실행, GPU 출력 동일성, 변경 후 CPU/GPU 시간 및 표시 FPS는 아직 검증하지 않았다.** 최종 수용에는 7절의 동일 조건 재측정이 필요하다.
