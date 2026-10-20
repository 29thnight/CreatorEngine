# RenderGraphDeclare: 선언 CPU 비용과 병렬 실행 단위

<!-- master-doc-review:start -->
## 2026-10-20 master 구현 대조

기준: `27de3f5a02aadde7456aab680db3184c36ee735e`. 대시보드는 변경하지 않는다. 아래 현행 대조와 소스 선언이 이전 시점의 구조·미구현 표기보다 우선한다.

[전체 대조·링크 정리 원장](MasterDocumentationAudit20261020.html)

### 현행 계약과 이 문서의 적용 범위

- 제품 live graph는 명시적 버전과 dependency order를 사용한다. 선언 metadata 재사용과 dependency recording은 현재 구현 기반이다.
- 소프트웨어 RHI의 frozen recording은 독립 target의 병렬 native 번역과 조기 sealed-batch handoff를 지원한다. 직렬 전용 번역·별도 C# SRP 저작 전환을 후속 필수 작업으로 두지 않는다.
- queue/alias/hazard·reader join·retirement를 보존하고 실제 overlap·fallback·whole-frame 비용으로 RG8 채택을 판정한다.

### 해당 구현에서 확인한 선언·소유 경계

- [Engine/Utility_Framework/ThreadPool.cpp:385](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/Utility_Framework/ThreadPool.cpp#L385) — `for (auto& task : std::span(nodes_.data(), nodes_.size()))`
- [Engine/Utility_Framework/ThreadPool.cpp:654](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/Utility_Framework/ThreadPool.cpp#L654) — `done_.wait_for(lock, std::chrono::microseconds(100), ready);`
- [Engine/Utility_Framework/ThreadPool.cpp:753](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/Utility_Framework/ThreadPool.cpp#L753) — `void thread_pool::dispatch_graph(std::span<const job_graph_node> nodes, std::function<void(std::size_t)> execute,`
- [Tools/regression/verify-render-base0.ps1:95](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Tools/regression/verify-render-base0.ps1#L95) — `estimatedArtifactGiB=$estimatedGiB; startFreeGiB=[IO.DriveInfo]::new([IO.Path]::GetPathRoot($output)).AvailableFreeSpace/1GB`
- [Tools/regression/verify-render-base0.ps1:189](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Tools/regression/verify-render-base0.ps1#L189) — `$proc=[Diagnostics.Process]::Start($start)`
- [Tools/regression/verify-render-base0.ps1:471](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Tools/regression/verify-render-base0.ps1#L471) — `[IO.File]::WriteAllText("$case/editor.stdout.log",$stdout.GetAwaiter().GetResult())`
- [Engine/Utility_Framework/JobScheduler.cpp:207](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/Utility_Framework/JobScheduler.cpp#L207) — `void job_handle::wait_assisting() const`
- [Engine/Utility_Framework/JobScheduler.cpp:318](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/Utility_Framework/JobScheduler.cpp#L318) — `job_handle job_scheduler::submit_indexed(std::size_t count, std::function<void(std::size_t)> task)`
- [Engine/Utility_Framework/JobScheduler.cpp:344](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/Utility_Framework/JobScheduler.cpp#L344) — `job_handle job_scheduler::submit_graph(std::span<const job_graph_node> nodes, std::function<void(std::size_t)> task,`
- [Engine/RenderEngine/GpuGeometryVisibility.cpp:1427](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/RenderEngine/GpuGeometryVisibility.cpp#L1427) — `void GpuGeometryVisibility::Frame::DeclareMeshletBatch(EnhancedRenderGraph& graph,`
- [Engine/RenderEngine/GpuGeometryVisibility.cpp:1477](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/RenderEngine/GpuGeometryVisibility.cpp#L1477) — `DeclareMeshletBatch(graph, frames.subspan(first, index - first), prefix.c_str());`
- [Engine/RenderEngine/GpuGeometryVisibility.cpp:1484](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/RenderEngine/GpuGeometryVisibility.cpp#L1484) — `DeclareMeshletBatch(graph, frames.subspan(first), prefix.c_str());`
- [Engine/RenderEngine/MaterialGraphMeshSurface.cpp:1864](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/RenderEngine/MaterialGraphMeshSurface.cpp#L1864) — `bool MeshSurfaceBatch::PrepareDispatch(std::span<const std::shared_ptr<const MeshSurfaceBatch>> batches,`
- [Engine/RenderEngine/MaterialGraphMeshSurface.cpp:1975](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/RenderEngine/MaterialGraphMeshSurface.cpp#L1975) — `bool MeshSurfaceBatch::PrepareDeclarationDispatch(std::span<const std::shared_ptr<const MeshSurfaceBatch>> batches,`
- [Engine/RenderEngine/MaterialGraphMeshSurface.cpp:2165](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/RenderEngine/MaterialGraphMeshSurface.cpp#L2165) — `bool MeshSurfaceBatch::DeclareBatch(EnhancedRenderGraph& graph, std::span<const MeshSurfaceBatch* const> batches,`
- [Tools/regression/verify-material-mesh-surface.ps1:8](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Tools/regression/verify-material-mesh-surface.ps1#L8) — `$repo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))`
- [Tools/regression/verify-material-mesh-surface.ps1:9](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Tools/regression/verify-material-mesh-surface.ps1#L9) — `if ([string]::IsNullOrWhiteSpace($VisualStudioInstallation)) {`
- [Tools/regression/verify-material-mesh-surface.ps1:10](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Tools/regression/verify-material-mesh-surface.ps1#L10) — `$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'`
- [Tools/regression/verify-meshlet-declaration-cpu.py:97](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Tools/regression/verify-meshlet-declaration-cpu.py#L97) — `('struct GpuGeometryVisibility::BufferPool', True),`
- [Tools/regression/verify-meshlet-declaration-cpu.py:102](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Tools/regression/verify-meshlet-declaration-cpu.py#L102) — `access_start = access.index('    inline void NormalizeSceneReads(')`
- [Tools/regression/verify-meshlet-declaration-cpu.py:109](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Tools/regression/verify-meshlet-declaration-cpu.py#L109) — `'void GpuGeometryVisibility::Frame::DeclareMeshletBatch(',`
- [Tools/regression/verify-mesh-dispatch-declaration.py:63](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Tools/regression/verify-mesh-dispatch-declaration.py#L63) — `'struct MeshSurfaceBufferPool'):`
- [Tools/regression/verify-mesh-dispatch-declaration.py:66](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Tools/regression/verify-mesh-dispatch-declaration.py#L66) — `for anchor in ('bool MeshSurfaceBatch::IsPreparedForGraph(', 'bool MeshSurfaceBatch::PrepareDispatch(',`
- [Tools/regression/verify-mesh-dispatch-declaration.py:67](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Tools/regression/verify-mesh-dispatch-declaration.py#L67) — `'bool MeshSurfaceBatch::PrepareDeclarationDispatch('):`

### 이전 근거의 유효 범위

기존 세부 계약은 위 현행 구현과 모순되지 않는 범위에서 유지한다. 과거 수치·실행 통과·해시·장치 조건은 당시 기록이다. 로컬 산출물이 사라진 항목은 링크 대신 경로와 미보존 상태를 표시하며, 현재 master의 재실행 통과로 승격하지 않는다.

<!-- master-doc-review:end -->

<details data-master-history><summary>세부 계약·원래 분석과 당시 측정 기록</summary>

기준 커밋: `4d5cbfa2f1389dee1c77a5bf53575b969f566467` (`master`, 2026-10-19).

## 범위와 판정

개선 범위는 `LivePipelineDesc::DeclareAll`의 **RenderGraphDeclare 내부**다.
`MeshSurfaceBatch::DeclareBatch`에서 만드는 변환 dispatch와
`GpuGeometryVisibility::Frame::DeclareMeshletBatch`의 선언 자료를 변경한다.
장면 준비, 공용 JobScheduler/ThreadPool, 그래프 Compile, 명령 기록, 셰이더는 변경하지 않는다.

`EnhancedSceneRenderer.cpp`의 두 production 호출은 `DeclareAll`을 직접 실행한 다음
`Compile`로 진행한다. `PrepareSceneRecording`과 `SceneHost::Prepare`는 이보다 앞이다.
따라서 최근 장면 준비의 worker 중첩을 이번 선언 병렬화의 근거로 사용하지 않았다.
그래프의 자원 등록, 버전 진행, pass 추가, blackboard 발행은 기존 소유자에게 남긴다.
병렬화 대상은 확정된 입력에서 최종 업로드의 겹치지 않는 구간을 채우는 CPU 작업이다.

메시릿 업로드를 별도 작업 하나로 보내는 안도 실제 엔진 스케줄러로 비교했다.
작은 작업의 제출 비용과 4/8-worker 결과가 이를 지지하지 않아 최종 코드에서 제거했다.
메시릿 경로에는 저장 공간 재사용, 읽기 정규화, 중복 선언의 조기 반환을 적용한다.
큰 메시 변환에서는 입력 복사·그룹 목록·job 기록을 여러 구간으로 나눠 실행한다.

호출 경계는 [LivePipelineDesc](../../Engine/RenderEngine/Render/Core/EnhancedLivePipelineDesc.cpp)와
[EnhancedSceneRenderer](../../Engine/RenderEngine/Render/Scene/EnhancedSceneRenderer.cpp),
실제 변경은 [메시 선언 준비](../../Engine/RenderEngine/MaterialGraphMeshSurface.cpp)와
[메시릿 선언](../../Engine/RenderEngine/GpuGeometryVisibility.cpp)에서 확인할 수 있다.

## 저장소에 남은 계측의 해석

출처는 기준 커밋의 [현재 렌더링 배선 보고서](LiveRenderWiring20261012.html)다.
파일명은 10월 12일이지만 10월 19일까지의 결과가 들어 있다.

| 기록 | 저장된 수치 | 이번 작업에서의 의미 |
|---|---|---|
| 10월 18일 최종 Normal | Declare p50 **3.724 ms**, RenderThread sampled inclusive **22.63%** | 선언 CPU 비용을 조사할 근거다. exclusive 시간이나 병렬 가능한 비율은 아니다. |
| 10월 19일 장면 준비 묶음 병렬화 | Declare 평균 **5.805 → 5.749 / 5.782 ms** | 앞 단계의 준비 개선만으로 선언 시간이 충분히 줄었다고 볼 수 없다. |
| 10월 19일 재측정 8 | Declare 평균 **4.303 → 5.805 / 5.973 ms** | 추가 세부 계측을 포함하므로 증가분 전체를 연산 퇴행으로 해석하지 않는다. |
| 최신 Draw/메시 병렬 준비 | Game CPU p50 **12.759 → 12.041 / 11.974 ms**, Running 중첩 **0.291 ms/준비** | RenderGraphDeclare 앞 단계의 기존 결과다. 이번 PR의 개선율로 사용하지 않는다. |

보고서가 연결하는 `Artifacts/...` 원본 ETW·native 캡처는 기준 Git 트리에 없다.
따라서 저장된 요약을 확인했으며, 원본 capture에서 새 통계를 계산했다고 주장하지 않는다.
아래 CPU 검증은 이번 변경의 production 본문을 실행한 별도 실험이다.
현재 Linux 환경에서 CreatorRobot의 Windows/DX12 프레임 시간을 새로 측정하지 않았다.

## CPU 성능 MCP와 적용한 원칙

[cpu-performance-engineering](https://github.com/usamahz/cpu-performance-engineering/tree/deb5a0bac46760503b6f4a2608bdfed470c8532e)
커밋 `deb5a0bac46760503b6f4a2608bdfed470c8532e`를 확인하고,
공식 `cpu-perf==0.1.1`의 MCP client/server로 다음 조회를 수행했다.

- `ask(question="work stealing critical path thread pools", section=9)`
- `get_benchmark(slug="9", parts=["method", "reproduce"], max_chars=5500)`

두 MCP 호출 모두 오류 없이 완료했다. 이는 설계·벤치마크 방법 조회이며
CreatorEngine이나 외부 저장소의 하드웨어 벤치마크를 실행한 결과가 아니다.
외부 원문의 하드웨어별 배속을 이 엔진의 예측치로 옮기지 않았다.

작업 총량과 합류까지의 가장 긴 경로를 함께 줄이고, 제출 비용보다 작은 일을
항목별 task로 만들지 않는 원칙을 적용했다. 먼저 중간 할당·초기화·복사와
중복 tree lookup을 제거한 다음 구간 크기를 비교했다.
[oneTBB의 구간 크기 설명](https://uxlfoundation.github.io/oneTBB/main/tbb_userguide/Controlling_Chunking_os.html)과
[Cilk work-stealing 분석](https://publications.csail.mit.edu/lcs/pubs/pdf/MIT-LCS-TM-548.pdf)도 확인했다.
Cilk의 이론적 경계를 CreatorEngine 스케줄러의 성능 보장으로 해석하지 않는다.

## 구현

### 메시 변환: 계획을 고정하고 최종 업로드에 직접 쓴다

`PrepareDeclarationDispatch`는 `DeclareBatch` 전용이다.
즉시 기록 경로가 사용하는 기존 `PrepareDispatch`와 네 번의 업로드 실패 주입 계약은 유지한다.

`MeshSurfaceBufferPool`은 `MeshTransformDeclarationScratch`를 처음 필요할 때 한 번 만든다.
새 계획은 최대 4,096개 job, 12,288개 입력 identity, 32,768개 hash slot을 고정 배열로 보유한다.
identity는 **원본 주소와 바이트 길이**이며, 처음 등장한 순서의 offset과 16-byte 정렬을 유지한다.
정렬 사이의 바이트는 0으로 채운다. 내용이 같아도 주소나 길이가 다르면 합치지 않는다.
매 선언의 reset은 이전에 점유한 slot만 지우므로 작은 입력 때문에 전체 hash 배열을 초기화하지 않는다.

상수·입력·job·group의 크기를 확정한 뒤 `ReserveUploadBatch` 한 번으로 네 upload slice를 확보한다.
지역 `vector<byte>`에 입력을 모아 다시 업로드로 복사하거나 지역 job/group vector를 확장하지 않는다.
입력 바이트, group, job의 구간을 각각 나누므로 큰 메시 하나도 여러 worker가 처리할 수 있다.
각 worker는 자신에게 정해진 최종 메모리 구간만 쓴다. 결과의 순서는 실행 순서에 의존하지 않는다.

병렬 진입 조건은 **상수를 제외한 input + job + group 업로드가 8 MiB 이상이고,
기존 풀의 worker가 4개 이상인 소유자 호출**이다. 조건을 만족하면 고정 8구간을 한 번 제출한다.
작은 입력, worker가 부족하거나 정지된 스케줄러, 이미 worker인 호출은 같은 구현을 동기 실행한다.
이미 worker인 호출에서도 같은 scratch와 직접 쓰기 경로를 사용한다.
이는 아래 단일 CPU 환경에서 확인한 보수적 진입 조건이다. 모든 CPU·장면에서 최적이라는 보장은 아니다.

fill을 제출한 소유자는 출력 descriptor를 만든다. 명시적 합류를 끝낸 다음
descriptor와 recording 유효성을 확인하고 완성된 dispatch만 결과로 발행한다.
callback이 보유할 dispatch는 GPU 실행까지 필요한 결과 객체이므로 scratch와 구분한다.
최종 hot path에 빈번히 생성되는 지역 동적 컨테이너를 추가하지 않았다.
고정 상한의 계획과 descriptor 배열은 풀의 수명 동안 보유하므로 최초 사용의 메모리 비용이 있다.
아래 할당 수와 시간은 이 scratch를 준비한 뒤의 steady-state 결과다.

### 메시릿: 고정 owner 배열과 풀 소유 scratch

공유 view의 상한 8개는 `array<shared_ptr<const Frame>, 8>`로 보유한다.
분할은 입력 `span`의 부분 구간을 사용한다. 동일 Frame이 반복되면 최초 owner만 포함한다.
모든 view가 이미 같은 그래프에 선언된 경우 유효성 검사 뒤 반환하여
사용 목록과 descriptor scratch를 건드리지 않는다.

reset/cull/count/finalize/scatter 사용 목록과 정규화 scratch,
vertices/topologies descriptor 목록의 용량을 `BufferPool`에 남긴다.
최대 8개인 ids/arguments/first descriptor 목록은 고정 배열로 만든다.
매번 tree node를 할당하던 geometry/topology 읽기 집합은 `NormalizeSceneReads`로 대체한다.

정규화는 **동일한 전체 handle과 version의 호환되는 읽기만** 결합하고 최초 사용 위치를 보존한다.
잘못된 중복 write는 여전히 거부한다. 그래프의 버전 진행 순서와 GPU pass 순서는 유지한다.
`AddPass(const vector&)`는 사용 목록을 그래프의 저장 공간에 복사하며,
callback은 고정 owner 배열을 값으로 보유한다. scratch 참조가 기록 단계로 빠져나가지 않는다.
기본 정규화 호출도 큰 임시 인덱스 용량을 thread-local에 유지한다.
그 저장소는 자원 소유권이나 frame 포인터를 보유하지 않는다.

### 합류와 저장소 수명

`render_graph_declaration_task`는 기존 `submit_indexed`를 한 번 호출한다.
각 index마다 `std::function`이나 작업 vector를 만들지 않고 수명이 보장된 callable을 빌린다.
기존 profile context를 이동 불가능한 guard 멤버에 두고 worker에 전달한다.
callback은 guard와 callable의 두 포인터만 빌린다. 정상 합류는 `wait_assisting`을 사용한다.
정상 경로는 명시적으로 합류해 worker 오류를 전달하고,
소유자 예외 경로의 소멸자는 수락된 작업을 회수한 뒤 원래 예외를 보존한다.
callable과 그 입력·출력은 task guard보다 먼저 선언한다.
`join`의 계측 생성 자체가 실패해도 일반 `wait`로 작업을 회수한 뒤 해당 오류를 재전파한다.
소멸자는 계측 등록이나 지원 실행을 다시 시작하지 않고 일반 `wait`만 수행한다.

합류 지원은 소유자 스레드에서도 임의의 pool callback을 실행할 수 있다.
이미 보유한 `std::mutex`에 같은 스레드가 `try_lock()`을 하는 것은 허용되는 재진입 검사가 아니다.
`render_graph_declaration_lock`은 stack-local guard를 연결한 thread-local 포인터 목록으로
같은 mutex가 현재 선언 스택 어디에 있는지 **잠금 시도 전에** 확인한다.
동일 scratch 재진입과 다른 worker의 경합은 블로킹 없이 거부한다.
독립 scratch의 중첩 실행과 정상 owner의 잠금은 허용한다. 별도 컨테이너 할당은 없다.

### RHI 경계

DX12 `CreateBindings`의 descriptor page 부족은 invalid table을 반환하며,
upload prefix 제출이나 recording 교체를 호출하지 않는다.
Vulkan `CreateBindings`도 독립적인 descriptor Request를 복사할 뿐이며
실제 descriptor-set 생성은 encoder binding 단계다.
두 upload segment allocator는 Active recording의 안정적인 mapped 주소를 보유하고,
완료된 Pending/Available 세그먼트만 회수·trim한다.
헤더의 recording 계약도 `BeginRecording` / `OnSubmitted` / `AbortRecording`을 worker 합류 후로 제한한다.

따라서 현재 backend 구현에서 fill 중 descriptor 준비가 upload 주소를 무효화하지 않는다.
descriptor 실패·예외가 발생하면 합류가 먼저 끝나고 상위 abort로 진행한다.
합류 후 token 확인은 stale 결과 거부 검사이며, 그 확인만으로 메모리 수명을 보장한다고 해석하지 않는다.

## 이번 CPU 검증

### 실행 경계와 방법

CPU 계측은 Linux 6.18.55, glibc 2.39, G++ 13.3.0, C++23 `-O2`에서 실행했다.
메시릿 일반·sanitizer 회귀 러너는 `-O1`을 사용한다.
CPU는 AMD EPYC 9V74이며 affinity는 0..8, cgroup `cpu.max`는 `800000 100000`이다.
실제 엔진의 `JobScheduler.cpp`, `ThreadPool.cpp`와 저장소 기준 enkiTS v1.12
(`0289cf6ffce86697e1f46cb4eeca1c946bffec71`)를 사용했다.
`CE_CONTRACT_STANDALONE=1`, `CE_SHIPPING=1`이므로 제품 CPU profiler는 제거된 빌드다.
소스 hash는 러너 출력에 남긴다. 외부 의존성은 로컬 `--enkits` / `--enki-ts-root`
경로로 받으며 러너가 다운로드하지 않는다.

메시 변환 검사는 현재 `PrepareDispatch`, `PrepareDeclarationDispatch`, `IsPreparedForGraph`,
dispatch/pool 구조체의 실제 본문을 임시 번역 단위로 추출한다.
계획과 합류 helper는 제품 헤더를 직접 포함한다. 그래프나 native 장치 전체를 구동한 검사는 아니다.
CPU RHI 모형은 업로드·descriptor 저장소를 미리 보유해 모형의 반복 할당이 결과를 지배하지 않게 한다.

메시 변환 계측은 같은 활성 풀에서 다음 세 구현을 iteration마다 순환 순서로 실행한다.

1. 기준 커밋과 본문이 동일한 기존 `PrepareDispatch`.
2. 실제 새 `PrepareDeclarationDispatch`.
3. 새 본문의 `start` 병렬 인자만 `false`로 바꾼 명시적 직렬 제어군.

제어군은 배포되는 별도 제품 경로가 아니다. 같은 직접 쓰기 알고리즘에서
제출·합류와 CPU 병렬 실행의 추가 기여를 분리하기 위한 비교다.
조건당 6회 warm-up 뒤 크기에 따라 60~2,000개 표본을 수집한다.
입력 구성, 스케줄러 시작·종료와 RHI 모형 reset은 계측 밖이다.

메시릿 검사는 실제 `DeclareMeshletBatch`, `BuildShared`, `UploadMeshletInputs`, pool과
소멸자, `NormalizeSceneReads`를 실행하고 native 장치와 그래프는 CPU 모형을 사용한다.
두 구현 모두 소유자에서 동기 실행한다. 조건당 120개 표본을 4블록으로 수집하고,
블록마다 12회 warm-up 뒤 스케줄러 정지/실행 순서를 교대했다.
기준/변경 바이너리는 같은 최종 probe를 사용한 별도 실행이다.
동일 process 안에서 두 바이너리를 교대로 측정한 통계 실험으로 해석하지 않는다.

### 메시 변환 결과

최종 소스의 8-worker 실행 p50이다. 시간은 `PrepareDeclarationDispatch`의
계획·예약·fill·바인딩·합류를 포함하는 CPU 준비 범위다.

| 입력 | 기존 준비 | 새 직렬 제어군 | 최종 선언 준비 | 기존 대비 감소 |
|---|---:|---:|---:|---:|
| 32 points, 1 job | 0.381 µs | 0.181 µs | 0.181 µs | 52.5% |
| 입력을 공유하는 64 jobs | 26.170 µs | 9.935 µs | 12.168 µs | 53.5% |
| 약 1 MiB, 1 job | 81.363 µs | 21.963 µs | 22.473 µs | 72.4% |
| 약 4 MiB, 1 job | 364.210 µs | 98.729 µs | 102.365 µs | 71.9% |
| 약 8 MiB, 1 job | 1,401.634 µs | 467.956 µs | 352.542 µs | 74.8% |
| 약 13 MiB, 1 job | 3,557.287 µs | 1,031.807 µs | 617.201 µs | 82.6% |

앞의 네 행은 실제 최종 코드도 inline이다. 별도 함수로 컴파일한 제어군과 실제 함수의
차이를 작은 입력에서 병렬 실행한 효과로 해석하지 않는다.
8 MiB/13 MiB 입력에서는 새 직렬 제어군 대비 추가 p50 감소가 각각 **24.7%/40.2%**였다.
이것은 최종 한 번의 표본 집합이며, 앞선 동일 계열의 4/8-worker 구간 비교에서도
큰 입력의 개선 방향을 확인했다. 수치를 CPU나 장면 전반의 보장으로 사용하지 않는다.

꼬리 지연은 같은 비율로 줄지 않았다. 약 13 MiB의 p95는
직렬 제어군 1,298.599 µs, 최종 1,294.002 µs였다.
약 26 MiB의 결과는 원자료에 포함하지만 기존 구현이 allocator history에 따라
초기 비교의 5~6 ms에서 후속 비교의 20~24 ms까지 변해 대표 개선율에서 제외한다.
이 조건의 최종 p50은 1,694.267 µs, 직렬 제어군은 2,123.374 µs였으며
p95는 각각 2,781.256/2,776.879 µs였다.

진입 조건을 결정할 때 1-worker의 8 MiB 병렬 준비는 486.654 µs로
그때의 직렬 제어군 368.415 µs보다 느렸다. 13 MiB도 814.139/759.747 µs였다.
따라서 최종 코드에서는 worker가 4개 미만이면 inline을 사용한다.
최종 경계 검사에서 1-worker의 8 MiB 입력도 scratch를 재사용하며 추가 제출을 피함을 확인했다.

warm 준비의 C++ heap 할당은 직렬 **1회**이며 GPU 실행까지 필요한 dispatch 결과 객체다.
8-worker 병렬 준비는 기존 executor의 job 상태를 포함해 **5회**다.
이는 메시 항목마다 생성하는 컨테이너가 아니며 job 개수에 따라 늘지 않는다.
같은 장치 모형에서 기존 준비는 대표 입력에 따라 9~35회였다.
네 upload slice의 예약 호출은 **4회에서 1회**로 줄었다.

11개 메시 입력의 p50/p95·할당 수, 메시릿 21조건의 결과와 source/dependency hash는
[CPU 계측 요약 JSON](RenderGraphDeclareCpu20261019.json)에 보관했다.
visibility 계측 뒤 helper의 task 정리와 probe 포맷이 바뀌었으나,
계측한 visibility 함수·정규화 본문과 사용하는 lock 동작은 그대로다.
최종 포맷 상태에서도 12,970개 일반 회귀를 다시 통과했다.

### 메시릿 선언 결과

8-worker 풀이 실행 중인 조건의 p50이다. 두 버전 모두 visibility 작업을 제출하지 않는다.

| live bins | 기준 | 변경 | 감소율 |
|---:|---:|---:|---:|
| 32 | 6.970 µs | 2.574 µs | 63.1% |
| 64 | 13.240 µs | 4.256 µs | 67.9% |
| 128 | 27.541 µs | 7.972 µs | 71.1% |
| 512 | 108.844 µs | 27.652 µs | 74.6% |
| 2,048 | 494.316 µs | 108.353 µs | 78.1% |
| 4,096 | 1,013.870 µs | 218.861 µs | 78.4% |
| 4, 고정 4096 descriptor | 17.647 µs | 16.715 µs | 5.3% |

1/4/8-worker와 7입력 크기의 21개 조건 모두 work-count checksum이 일치했다.
이 checksum은 전체 업로드 byte parity를 대신하지 않는다. 공유 업로드 핵심 필드의
독립 oracle과 현재 구현의 스케줄러 상태별 전체 upload byte/metadata digest 검사는
별도 회귀 검사에서 수행했다.

제거한 비동기 visibility 실험에서는 64 bins의 직접 실행 약 4.2 µs가
1/4/8-worker에서 약 41.5/46.2/48.6 µs로 늘었다.
빈 단일 작업의 실제 제출·합류 p50도 29.895/43.115/59.650 µs였다.
큰 입력에서 1-worker 이득은 있었지만 4/8-worker에서 추가 이득이 일관되지 않았다.
따라서 최종 visibility 개선은 작업량·할당·정규화 비용 감소에 집중했다.

### 정확성·실패 경로 검사

| 검사 | 최종 결과 | 실제 확인한 경계 |
|---|---|---|
| 순수 메시 계획 | 일반 및 ASan+UBSan 통과, warm heap allocation **0** | 주소+크기 identity, 순서·정렬·padding, 8구간의 완전하고 겹치지 않는 쓰기, 상한·오류·reset |
| 실제 메시 준비 + 결정적 executor | 일반 및 ASan+UBSan 각각 **201개 검사 통과** | 기존 serializer와 전체 upload byte parity, descriptor 순서, 예약·바인딩·epoch·제출·worker 실패, 예외 시 합류와 마지막 결과 보존 |
| 실제 메시 준비 + 실제 스케줄러 | 일반 **190개**, 최종 8-worker 계측 포함 **38,995개 검사 통과** | 1/4-worker, 정확한 8 MiB−16/8 MiB 경계, 큰 단일 메시·공유 입력, inline의 warm allocation 1회 |
| 실제 메시릿 선언 | 일반 및 ASan+UBSan 각각 **12,970개 검사 통과** | single/shared/ordered/history, 공유 업로드 핵심 필드 oracle·스케줄러 상태별 전체 upload byte/metadata digest, pass·handle version 순서, 8뷰·4096 bins 분할, null/중복/no-op, scratch 재사용·재진입, graph/native 실패, callback 수명 |
| 실제 합류 helper + 실제 스케줄러 | 1/4-worker 회귀 통과 | index별 한 번 실행, 오류·owner unwind·중복 제출·상한 거부, 중첩 inline, owner assistance, 같은/다른 scratch 경합, 잠금 상태 복원 |
| 형식·프로젝트 | 통과 | clang-format 19.1.5로 새 파일 전체·기존 변경 범위 포맷, `git diff --check`, MSBuild XML 파싱·새 헤더 편입 |
| include 경계 | 기준·변경 모두 exit 1, 출력 동일 | 기존 상향 include 27개·물리 경계 69개. 이번 변경에 따른 추가 항목 없음 |

ASan·UBSan 검사는 실제로 실행했다. LeakSanitizer는 이 환경에서 `/proc/<pid>/task`
검사가 거부되므로 `ASAN_OPTIONS=detect_leaks=0`으로 누수 탐지만 끈다.
nonshipping 프로파일 서비스의 컨텍스트 전파·계측 생성 실패는 정적 검토 범위다.
Windows profile service를 대체한 테스트 구현으로 실행 결과를 만들지 않았다.
기존 즉시 `PrepareDispatch`와 기록 단계 `SharedBatch::Dispatch` 본문이 기준과 동일함도 확인했다.

### 재현 명령

다음은 저장소 루트에서 실행한다. `/path/to/enkiTS`는 위 커밋의 로컬 checkout이다.

```bash
python3 -B Tools/regression/verify-mesh-dispatch-declaration.py
ASAN_OPTIONS=detect_leaks=0 python3 -B Tools/regression/verify-mesh-dispatch-declaration.py --sanitize address
python3 -B Tools/regression/verify-mesh-dispatch-declaration.py --enkits /path/to/enkiTS
python3 -B Tools/regression/verify-mesh-dispatch-declaration.py --enkits /path/to/enkiTS --workers 8 --benchmark

g++ -std=c++23 -O2 -pthread -DCE_CONTRACT_STANDALONE -Wall -Wextra -Werror \
    -IEngine/Utility_Framework -IEngine/RenderEngine \
    Tools/regression/mesh_dispatch_declaration_plan_probe.cpp -o /tmp/mesh-declaration-plan
/tmp/mesh-declaration-plan

python3 -B Tools/regression/verify-meshlet-declaration-cpu.py
ASAN_OPTIONS=detect_leaks=0 python3 -B Tools/regression/verify-meshlet-declaration-cpu.py --sanitize address
python3 -B Tools/regression/verify-meshlet-declaration-cpu.py --benchmark \
    --enki-ts-root /path/to/enkiTS --workers 1 4 8 --retain-native-storage
python3 -B Tools/regression/verify-meshlet-declaration-cpu.py --benchmark \
    --enki-ts-root /path/to/enkiTS --workers 1 4 8 --retain-native-storage \
    --source-root /path/to/CreatorEngine-4d5cbfa

python3 -B Tools/regression/verify-render-graph-declaration-task.py --enkits /path/to/enkiTS --timeout 20
```

단위는 CPU probe의 µs다. 위 표의 감소율을 전체 `RenderGraphDeclare`나 프레임의
예상 개선율로 곱하지 않는다. 실제 frame의 입력 분포·allocator·계측·동시 worker 부하가 다르다.

## Windows 수용 경계

이 환경에서는 v145/MSVC `import std` 전체 엔진 빌드와 DX12/Vulkan GPU 실행을 하지 않았다.
portable probe의 CPU byte parity는 shader 결과, GPU validation, ETW Running 중첩 또는
실제 `RenderGraphDeclare` p50/p95의 개선을 대신하지 않는다.
CPU 모형의 일반 RAM과 실제 mapped upload의 cache 속성도 같다고 가정하지 않는다.

기존 `Tools/regression/verify-material-mesh-surface.ps1`는 Debug/Release의 DX12 GPU validation 검사다.
그래프 self-test는 `dx12.rendergraph`이며,
`Tools/regression/verify-render-base0.ps1 -Configuration Release -Backend dx12 -FixtureProject ... -OutputDirectory ...`
로 실행할 수 있다.

`Tools/regression/measure-meteor-transform-dispatch.ps1 -FixtureProject ... -OutputDirectory ...`는
Release CreatorEditor를 offscreen/DX12로 실행하고 validation을 끄는 성능 수집 도구다.
동일 fixture·카메라·pose·worker 수·계측 설정의 기준/변경 바이너리 교대는 별도로 수행한다.
공유 Suzanne의 16/64/256/512개 복제만으로는 새 병렬 진입 조건에 도달함이 보장되지 않는다.
병렬 경로 수용에는 실제 합산 input/job/group 업로드가 **8 MiB 이상**인 fixture와
활성 worker **4개 이상**을 사용하고 `RenderGraphDeclareMeshFill`의 worker 실행 및
`RenderGraphDeclareJoin`을 확인한다.

정확성에서는 GPU validation을 켜고, 성능에서는 동일 설정으로 warm-up 뒤
`RenderGraphDeclare`, `MaterialMeshDispatchPrepare`, `RenderGraphDeclareMeshPlan`,
`RenderGraphDeclareMeshFill`, `RenderGraphDeclareJoin`을 확인한다.
전체 프레임과 독립 leaf scope, 같은 조건의 p50/p95 및 실패·손실 여부를 함께 남긴다.
새 세부 scope의 켜짐/꺼짐이 다른 수집을 순수 최적화 효과로 비교하지 않는다.

</details>
