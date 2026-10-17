# RG8 실제 큐 기록·제출 연결 — 2026-10-09

**후속 구조 감사 우선:** [RG7·RG8 상세 감사](RenderRg7Rg8StructuralAudit20261009.md)에 따라 RG8 제품 완료 판정을 철회하고 progress·기성 0으로 되돌렸다. 아래 실행/정확성 증거는 보존하되 다음의 최종 종료 문구는 현재 상태가 아니다.

**같은 날 최종 종료:** [RG8 종료·기본 OFF 판정](RenderRg8Closure20261009.md)의 Release 1496×692 336출력·일반 GPU 192프레임·연속 메모리 비교와 최신 Release 전체 회귀로 기존 채택 판정을 마쳤다. RG8 done·기성 16이며 성능 개선 미입증으로 기본 OFF를 유지한다. 아래 진행 문구는 최종 측정 전 이력이다.

RG8 고정 순서의 두 번째 단계다. 첫 단계의 compiled queue plan을 Q0 native queue 서비스에 연결했다. RG7·Q0는 종료 상태를 유지한다. RG8 전체 수용과 제품 기본 활성화는 아직 아니다.

## 구현 범위

- `IRHIQueueRecorder`와 DX12 구현은 큐 종류에 맞는 새 allocator/list와 RHIEncoder를 만들고 닫힌 기록을 Q0 batch로 밀봉한다. allocator와 외부 저장소 보관 토큰은 해당 큐의 GPU 완료까지 batch가 소유한다.
- `EnhancedRenderGraph::SubmitQueues`는 owned ExplicitVersioned 그래프의 모든 패스를 먼저 기록한다. 기록 중 예외, 인코더가 보고한 dropped command와 큐 상태 불일치는 제출 전 실패한다.
- graphics prologue가 초기 상태를 COMMON으로 맞추고, 각 패스는 COMMON → 요구 상태 → COMMON으로 기록한다. 반복 패스는 내부 phase 상태/UAV 순서를 별도로 기록한다. split callback은 한 slice로 실행한다.
- compiled queue plan의 큐 간 간선을 Q0 GPU Wait/Submit으로 연결한다. compute의 첫 작업은 prologue를 기다린다. 최종 graphics epilogue는 compute 마지막 완료점까지 합류한 뒤 모든 사용 자원의 계획 최종 상태를 복구한다.
- 최종 상태 writeback은 epilogue 제출 수락 뒤 반영한다. 반환된 completion은 두 큐 전체의 완료점이다. graphics/compute의 `CollectCompleted`로 보관된 batch를 회수한다.
- batch가 shared graph와 외부 owner 토큰을 보관하므로 호출자가 graph 참조를 놓아도 자원은 남는다. 보관 중 Reset/선언 변경/다시 실행은 거부한다. 한 그래프의 큐 제출은 Reset 전 한 번만 허용한다.
- 부분 제출 실패는 `recoveryRequired`로 반환하고 재사용 pool 반환을 차단한다. Q0는 실행 후 fence 없는 실패도 격리한다. 이 상태에서는 fallback 재실행을 하지 않고 큐 drain/장치 손실 처리를 거친다.
- compute encoder에서 ShaderResource는 NON_PIXEL_SHADER_RESOURCE로 변환한다. graphics 전용 상태의 compute 전이는 기록 시 거부한다.

COMMON 경유는 초기 구현의 보수적인 계약이며 모든 direct/compute 전환에 필수인 규칙으로 설명하지 않는다. 불필요한 COMMON 전이를 피하라는 [Microsoft 자원 배리어 문서](https://learn.microsoft.com/en-us/windows/win32/direct3d12/using-resource-barriers-to-synchronize-resource-states-in-direct3d-12)와 [compute 큐 상태 제약](https://microsoft.github.io/DirectX-Specs/d3d/D3D12EnhancedBarriers.html)을 확인했다. 패스별 새 allocator와 전이의 비용은 제품 수용 측정 대상이다.

## 현재 호출 계약과 다음 소비 범위

Imported DEFAULT 자원은 진입 전에 이 큐에서 사용 가능해야 한다. owner는 imported storage·pipeline·descriptor·transient pool 등 callback이 참조하는 저장소를 GPU 완료까지 보관해야 한다. device services는 큐 회수보다 오래 살아 있어야 한다. 프레임 upload/descriptor ring을 회수 전에 reset하면 안 된다.

초기 native 단계는 RG7 aliasing과 기존 단일 큐 profiler가 없는 owned graph를 지원했다. 아래 후속 단계에서 프레임 회수와 실제 live graphics graph의 재질 캐시/profiler/owner를 연결했다. 후속 SSAO compute 소비까지 아래에 기록했으며 기본 OFF를 유지한다. 제품 연결 후 기존 기준인 픽셀/validation/GPU critical path/peak memory 비교로 채택을 결정한다. 이는 새로운 종료 조건 추가가 아니라 기존 2·3단계의 남은 소비 및 수용 범위다.

## 검증 구성

기존 Editor 명령 `dx12.rendergraph queue-execution`:

- 실제 Produce/Transform compute 셰이더로 64개 정수 값을 만들고 단일/다중 큐 readback과 수식 결과 비교.
- graphics → compute → graphics 의존성, 독립 imported RT의 최종 ShaderResource 복구 및 후속 native 상태 검사.
- GPU gate 지연 중 graph 참조 해제, Reset/중복 제출 거부, 무관한 높은 fence 값으로 조기 회수되지 않음, 합류 후 graph 회수.
- 기록 callback 예외의 제출 전 거부와 실행 후 실패 격리/안전한 drain 후 해제.
- `--smoke-offscreen`, GPU validation, 별도 테스트 프로젝트 없음.

실제 빌드·검사 결과는 완료 산출물 확인 후 아래에 기록한다.

첫 Debug 실행에서 테스트의 UAV를 읽기 전용 SetRootBuffer에 연결한 오류가 validation으로 검출됐다. 계산 결과와 수명 검사는 통과했어도 이 실행은 수용 실패다. 기존 UAV table/CreateBindings/SetBindings 경로로 fixture를 수정했다. 실패 산출물은 RG8QueueExecution/Debug에 보존하고, 최종 수용은 수정 뒤 DebugFinal·ReleaseFinal만 사용한다.

## 최종 수용 결과

- VS 2026(v18) Debug·Release 최종 빌드 통과.
- 두 구성 각각 native 실행 65검사, 기존 큐 배치 24검사, Editor 전체 RenderGraph 회귀 통과.
- 실제 Produce/Transform 셰이더의 단일/다중 큐 정수 결과 오차 0, GPU validation 오류 0, 정상 종료 코드 0.
- 지연 GPU의 graph 보관, 무관한 높은 fence와 회수 분리, 완료 후 보관 해제, imported texture 최종 상태, 기록 예외 및 실행 후 격리/회수 확인.
- Debug native 검사 1631.9ms / 기존 전체 회귀 83080.3ms, Release 790.288ms / 14513ms. 셰이더 준비·gate·검증·대기를 포함하는 검사 소요 시간이며 제품 GPU 성능 수치가 아니다.
- `Build/Verification/Phase43/RG8QueueExecution/{DebugFinal,ReleaseFinal}`: 명령별 결과, 실행 로그, 빌드 대상/runtime/source 해시. 집계는 `execution-acceptance.json`.
- 최종 engine graph/recorder/provider/encoder/test/shader와 실행 바이너리 해시 일치, 변경 공백 및 PowerShell 문법 검사 통과.

이번 연결은 owned graph의 native 실행 기반으로 수용한다. RG8 전체 상태는 진행 중·기성 0이다. 프레임 회수 기반의 후속 결과는 아래에 기록한다. 이후 기존 제품 픽셀/GPU critical path/peak memory 기준으로 채택 여부를 판정한다. RG7 기본 OFF와 RG7/Q0 종료 상태를 유지한다.

## 2026-10-09: DX12 프레임 회수 연결

`DX12DeviceResources::BeginQueueFrame`은 즉시 리스트의 upload prefix를 제출하고 CPU 제출 ticket의 성공을 확인한 뒤, 같은 Q0 서비스의 timeline으로 graphics 큐에 GPU Wait를 건다. CPU ticket 확인은 native 큐 호출 수락 확인이며 GPU drain이 아니다.

prefix를 제출해도 현재 upload recording과 descriptor version은 봉인하지 않는다. prefix 전에 준비한 상수·descriptor를 그래프가 나중에 읽을 수 있기 때문이다. `JoinQueueFrame`은 owned graph의 graphics/compute 합류 완료점을 primary DIRECT 큐에 연결한다. 이후 기존 `EndFrame`의 fence가 모든 큐의 사용을 포함하여 upload·descriptor·프레임 allocator 슬롯을 회수한다. 완료점이 연결되지 않은 EndFrame, BeginFrame, 중간 flush, 기존 parallel submission은 거부한다.

취소 또는 EndFrame admission 거부는 먼저 Q0 큐와 primary prefix를 drain한 뒤 할당을 반환한다. 완료/장치 손실을 증명하지 못하면 메모리를 반환하지 않는다. 취소한 Q0 서비스는 admission을 철회하며, 큐 실행 재개에는 backend 재초기화가 필요하다. 기존 single-queue 프레임은 재개할 수 있다. primary Wait의 fence는 중복 없이 보관하고 서비스 teardown에서 primary 큐까지 완료를 확인하여 해제한다.

기존 Editor queue-execution 검사에 실제 primary prefix 셰이더 → COMPUTE의 상수·descriptor 읽기 → DIRECT readback → primary frame fence 연결을 추가했다. gate로 3프레임을 지연시켜 pending upload/descriptor 보관과 4번째 슬롯의 대기, 64개 정수 결과, 완료 후 graph 해제, 다른 서비스 completion 거부, 미합류 취소의 drain/revocation과 single-queue 복구를 확인한다. 별도 테스트 프로젝트와 화면 차단 실행은 사용하지 않는다.

이 연결은 제품 DX12 frame lifecycle을 사용하는 회수 기반이다. **이 프레임 회수 검사 당시에는 실제 live scene 그래프의 queue cutover를 하지 않았다.** live 준비/재질 캐시의 제출 transaction·profiler·pipeline owner 연결 결과는 아래 후속 절을 따른다. 그 뒤 기존 3단계의 제품 픽셀/validation/GPU critical path/peak memory 수용을 수행한다. RG8 진행 중·기성 0, RG7/Q0 완료 및 RG7 기본 OFF는 유지한다.

### 프레임 회수 최종 검증

- VS 2026(v18/v145) Debug·Release 최종 빌드 통과. 최종 로그는 `rg8-frame-debug-final-build.log`, `rg8-frame-release-final-build.log`.
- 각 구성 owned/native+frame 122검사(기존 65 + 프레임 57), 큐 계획 24검사, 기존 Editor 전체 RenderGraph 회귀 통과.
- 실제 primary prefix → compute → readback 3프레임의 64개 정수 값 오차 0. GPU validation 오류 0, 종료 코드 0.
- Debug native/frame 1176.65ms·전체 RG 53735.7ms, Release 989.775ms·13028.8ms. gate·shader 준비를 포함한 검사 시간이며 제품 GPU 성능으로 해석하지 않는다.
- `Build/Verification/Phase43/RG8FrameRetirement/{DebugFinal,ReleaseFinal}`에 명령 결과·로그·소스/runtime 해시 보관. `frame-retirement-acceptance.json`은 두 구성의 결과와 현재 소스/바이너리 해시 일치를 확인한다. 이전 `Debug`는 중간 구현 검사이며 최종 수용은 Final 두 구성이다.
- Release 빌드는 `Utility_Framework.pdb`의 LNK4020 형식 레코드 경고가 있다. 일부 디버거 기호/형식 정보가 제한될 수 있으며, 빌드 성공·GPU validation 0과 구분한다.
- 장치 손실 분기를 기존 recovery 경로로 전달하도록 보완했으나 새 queue-frame 자체의 실제 RemoveDevice 실행을 이번 결과로 주장하지 않는다. 전체 RG 회귀에 포함된 기존 장치 손실 검사와 구분한다.
- PowerShell 구문·변경 공백·dashboard 스크립트 구문 검사 통과. RG8 기성/기본 설정과 RG7/Q0 종료 상태 변경 없음.

## 2026-10-09: 실제 live scene 소비 연결

`CREATOR_RENDERGRAPH_QUEUE_EXECUTION=1`로 실제 DX12 씬 그래프를 Q0 owned graphics 큐에서 실행한다. 기본 OFF이며, 이번 소비 연결은 compute 분산을 하지 않는다. 기존 프로파일러는 단일 큐 시계 계약이므로 해당 graphics 큐의 timestamp frequency/clock calibration으로 초기화하고 pass timestamp를 같은 큐에 기록한다. primary 큐의 Resolve는 graph join 뒤 실행한다. compute endpoint와 단일 큐 profiler를 함께 사용하는 SubmitQueues는 계속 거부한다.

재질 residency·palette·visibility·material packet 준비가 성공한 뒤 prefix를 제출한다. 비동기 재질 준비에서 반환되는 정상 deferred 상태는 큐 서비스를 철회하지 않는다. prefix의 upload/descriptor recording을 그대로 유지하고, EndFrame의 CPU 제출 ticket 성공과 primary 합류 fence를 확인한 뒤 같은 recording의 재질 캐시를 게시한다. 이는 CPU native 제출 수락 확인이며 GPU drain이 아니다.

LivePipeline을 shared owner로 보관하여 Q0 batch가 pipeline·pass·transient pool 저장소와 graph를 유지한다. 프레임 시작과 정상 lifecycle drain에서 완료 batch를 수집하고, backend 해체/장치 손실 처리에서 Q0 서비스까지 정리한 뒤 pass/cache를 파괴한다. 큐가 철회된 실행 실패는 기존 renderer 실패 경로로 전달한다. GPU에서 일부 실행한 그래프를 fallback으로 다시 제출하지 않는다.

SubmitQueues의 기록 시간은 native queue admission과 분리하여 CPU 기록 계측에 전달한다. 기존 compiled graph 진단과 제품 capture 경로는 유지한다. aliasing 동시 선택은 거부한다. RG7 기본 OFF와 종료 판정은 바꾸지 않는다.

제품 검증은 기존 Editor의 OFF/ON 제어 캡처·scene/game/preview·씬 교체·리사이즈·재질 소스 복구/편집/게시와 GPU validation을 사용한다. `verify-rg8-live-queues.ps1`는 현재 바이너리의 native/full 회귀를 재사용하며 모든 제품 실행은 `--smoke-offscreen`이다. 실제 실행 결과는 아래와 같다.

### live graphics 최종 검증

- VS 2026(v18/v145) Debug·Release 빌드, 각 native/frame 122검사·큐 계획 24검사·Editor 전체 RenderGraph 회귀 통과.
- 각 구성 OFF/ON 독립 Editor 실행으로 scene/game/preview, 재질 소스 복구·편집·저장/재로드·게시, 씬 교체·리사이즈 검증. 모든 실행은 비표시 모드이며 별도 C++ 테스트 프로젝트를 만들지 않았다.
- Debug: ON 실제 graph 제출 298회. OFF/ON 각각 GPU profiler 65구간 measured, query overflow/drop 0. 332×182 제어 캡처 7개 첨부의 최대 절대 오차 0. GPU validation 0·drop 0·정상 exit 0.
- Release: ON 실제 graph 제출 324회. OFF/ON 각각 GPU profiler 65구간 measured, query overflow/drop 0. 332×182 제어 캡처 7개 첨부의 최대 절대 오차 0. GPU validation 0·drop 0·정상 exit 0.
- `Build/Verification/Phase43/RG8LiveQueues/{NativeDebug,NativeRelease,Debug,Release}`에 명령 결과·로그·출력 비교·실행 바이너리/소스 해시 보관. `live-acceptance.json`은 현재 소스 및 바이너리 일치를 재확인한 집계다. 빌드 로그는 `rg8-live-debug-build-fixed.log`, `rg8-live-release-build.log`.
- Release의 기존 `Utility_Framework.pdb` LNK4020 경고는 남아 있다. 일부 디버거 형식 정보 제한이며 GPU 검증 결과와 구분한다.
- 이 결과는 live graphics 소비 수용이다. 332×182 출력 동일성과 GPU timestamp 수집을 성능 개선이나 전체 제품 해상도 수용으로 확대하지 않는다.

이 graphics-only 수용 시점의 후속 순서는 compute 분산과 큐별 profiler 연결 → 기존 제품 픽셀/validation/GPU critical path/peak memory 비교에 따른 채택 판정이었다. 새 종료 조건을 추가하지 않는다. RG8 진행·기성 0·기본 OFF와 RG7/Q0 완료 상태를 유지한다.

## 2026-10-09: SSAO compute 분산과 큐별 profiler

`CREATOR_RENDERGRAPH_QUEUE_EXECUTION=2`는 기존 owned graphics 실행에 compute 큐를 더하는 명시적 진단 선택이다. 기본 OFF와 `1`의 graphics-only 경로는 유지한다. SSAO.Compute/SSAO.Filter 작성자가 compute 호환성을 선언하고, 같은 view의 완료된 GPU 측정값이 1μs 이상인 패스만 배치한다. 측정값 없는 첫 프레임은 graphics에서 실행한다. 1μs는 이 파일럿의 배치 하한이며 성능 채택 기준이나 최적 임계값으로 주장하지 않는다.

두 큐는 별도 DX12GpuProfiler의 query heap/readback/주파수/clock calibration을 사용한다. RenderGraph endpoint에 연결한 profiler로 각각 기록하고, 합류 뒤 primary 리스트에서 두 질의를 resolve한다. 프레임 fence가 두 큐와 resolve를 모두 포함하므로 기존 슬롯 회수 계약을 유지한다. 같은 profiler를 두 큐에 붙이는 요청은 제출 전에 거부한다.

각 큐의 raw timestamp는 해당 큐의 calibration으로 QPC에 변환한다. 통합 span과 busy는 변환된 구간의 범위와 합집합이며, 큐별 raw tick을 직접 빼거나 겹치는 구간을 두 번 합산하지 않는다. 패스 시간 합계는 별도 값이다. compute 구간은 queueId=1로 기존 EngineDiagnostics sink에 전달한다. 어느 큐의 calibration도 유효하지 않으면 통합 시간을 정상 측정으로 게시하지 않는다. 이 원칙은 [Microsoft의 큐별 timestamp/clock calibration 계약](https://learn.microsoft.com/en-us/windows/win32/direct3d12/timing)을 따른다.

기존 Editor native 검사에 작성자 선언·동일 profiler 거부·graphics/compute 구간 수와 귀속·각 시계의 calibration 확인을 추가했다. 제품 OFF/ON 검사는 실제 compute 제출과 SSAO 두 구간, 통합 busy≤span, 기존 출력/재질/세 뷰/수명 검사를 함께 확인한다. 최종 실행 결과는 아래와 같다.

### SSAO compute 최종 검증

- VS 2026(v18/v145) Debug·Release 최종 빌드 통과. 빌드 로그는 `RG8Compute/debug-final-build.log`, `RG8Compute/release-build.log`.
- 구성별 native/frame/profiler 135검사·계획 24검사·Editor 전체 RenderGraph 회귀 통과. 신규 13검사는 큐별 profiler 초기화, 작성자 선언, 공유 profiler의 제출 전 거부, query resolve/collect, 큐별 구간 수·귀속/calibration을 확인한다.
- Debug OFF/ON: ON graph 제출 286회·compute batch 560회. 캡처 65구간 중 SSAO compute 2구간, query overflow/drop 0·busy≤span. 332×182 제어 출력 7개 최대 절대 오차 0. GPU validation 0·exit 0.
- Release OFF/ON: ON graph 제출 310회·compute batch 608회. 캡처 65구간 중 SSAO compute 2구간, query overflow/drop 0·busy≤span. 332×182 제어 출력 7개 최대 절대 오차 0. GPU validation 0·exit 0.
- 최종 수용한 제품 실행에서 scene/game/preview·재질 소스 복구/편집/게시·저장/재로드·씬 교체/리사이즈 통과. 기존 Editor를 비표시 모드로 사용했다.
- `Build/Verification/Phase43/RG8Compute/{NativeDebug,NativeRelease,Debug,Release,ReleaseRetry}`에 원본 결과·로그·비교 보관. `compute-acceptance.json`은 결과와 현재 소스/바이너리 해시 일치를 확인한 집계다.
- 최초 Release/On은 재질 편집 Apply의 생성 쌍 디렉터리 rename에서 `Access is denied`로 실패했다. GPU 검증 통과로 세지 않으며 결과를 보존했다. 생성 캐시 영역의 후속 create/write/read/rename/delete probe는 통과했지만 최초 원인은 미확정이다. 엔진 바이너리 변경 없이 실패한 ON만 ReleaseRetry/On에서 다시 실행했고, 동일 exe/runtime 해시를 확인한 Release/Off를 재사용했다. 이 결과로 파일 게시 실패가 수정됐다고 주장하지 않는다.
- Release의 기존 `Utility_Framework.pdb` LNK4020 형식 정보 경고는 남아 있다. 이번 결과는 Vulkan runtime이나 프로파일러 뷰어의 별도 compute 레인 UI 수용을 주장하지 않는다. 기존 sink에 queueId를 전달하며 이 검증의 계측 범위는 backend/capture다.

이번 제품 OFF/ON은 기존 기본 경로(0)와 compute 파일럿(2)의 정확성 비교다. owned graphics(1)도 유지하므로 후속 성능 분석에서 1↔2의 큐 분산 효과와 0 대비 실행 구조 전환 비용을 구분한다.

SSAO 파일럿의 제품 compute 소비와 큐별 계측 연결을 수용한다. 332×182 제어 출력 검사를 전체 해상도·GPU critical path 개선·peak memory 채택 판정으로 확대하지 않는다. 남은 작업은 기존 3단계 제품 비교와 채택 판정이며 RG8 진행·기성 0·기본 OFF, RG7/Q0 완료를 유지한다.
