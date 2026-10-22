# RenderGraph program, frame bindings와 실행 수명

이 문서는 `EnhancedRenderGraph`에 추가된 재사용·subresource·queue 실행 계약을 설명한다.
구현 정본은 [EnhancedRenderGraph.h](../../Engine/RenderEngine/Render/Graph/EnhancedRenderGraph.h),
[컴파일러](../../Engine/RenderEngine/Render/Graph/EnhancedRenderGraph.cpp),
[subresource 컴파일러](../../Engine/RenderEngine/Render/Graph/EnhancedRenderGraphSubresources.cpp),
[queue 실행기](../../Engine/RenderEngine/Render/Graph/EnhancedRenderGraphQueues.cpp)다.
기존 versioned graph의 검증, culling, RAW/WAR/WAW, 수명 분석과 배리어 계획을 유지한다.

## 세 가지 소유 범위

| 범위 | 보관하는 것 | 보관하지 않는 것 |
|---|---|---|
| `GraphProgram` | 불변 논리 resource/pass 슬롯, 버전과 접근 계약, resource schema, 검증된 구조·순서·수명 정보 | native handle, 현재 epoch, 콜백, frame/view owner |
| `FrameBindings` | 현재 import handle·초기 상태·상태 writeback, 새 pass 콜백, CPU 기록 비용, 외부 저장소 owner | 과거 프레임 콜백의 자동 재사용 |
| frame 실행 (`EnhancedRenderGraph`, `QueueExecution`) | 현재 epoch와 resource, 특수화한 배리어, queue 배치·wait, recording lease, 완료 frontier | 다른 in-flight view의 가변 pass 상태 |

`SnapshotProgram()`은 마지막 성공한 compile에서 게시한 불변 program을 반환한다.
`InheritProgram()`은 비어 있고 queue가 보유하지 않는 Reset graph에 같은 program을 공유한다.
새 view와 새 frame의 handle epoch는 각각 독립이다. 공유 program에서 얻는 `ResourceHandle()`은
epoch가 0인 논리 주소이며, 현재 실행에서 `RebindProgramHandle()`로만 변환한다.

### 점진적 이주 경로

제품 호출 순서는 다음과 같다.

1. 완료된 frame 실행을 Reset하거나 새 실행을 만들고, 필요하면 `InheritProgram()`을 호출한다.
2. `BeginProgramBinding()` 뒤 현재 view의 준비된 입력으로 기존 `DeclareAll()`을 실행한다.
3. `FinishProgramBinding()`으로 현재 선언을 검증한다. 같은 구조라면 retained usage span을 재사용한다.
4. `Compile()`이 현재 native resource와 상태를 특수화하고, 실행기가 기록·제출한다.

이 경로에서는 **Declare 함수가 여전히 실행된다**. 새 callback capture, import와 output handle을
만들어야 하기 때문이다. 같은 main usage 목록의 arena 복사와 구조/DAG 재컴파일을 줄이는 경로다.
`programBindingReused`, `programUsageCopiesAvoided`가 실제 성공 여부와 피한 usage 복사를 나타낸다.
반복 phase 목록은 현재 실행 저장소에 복사한다. 모든 선언과 할당을 제거했다는 뜻은 아니다.

program binding이 열려 있을 때 `LivePipelineDesc`는 declaration packet 수집을 시작하지 않는다.
이 경로는 한 소유자가 논리 슬롯을 순서대로 게시하고 retained span을 연결한다. 다른 선언 경로의
packet materialization과 동시에 실행하면 동일한 usage 저장소를 덮을 수 있으므로 함께 쓸 수 없다.

구조가 달라지면 이미 만들어진 현재 선언으로 같은 frame에서 정상 compile을 수행한다. topology,
feature 구성, resource schema, 반복 구조와 접근 계약 변화는 구조 변경이다. native handle 번호,
초기 native 상태, callback capture와 `recordCost` 변화만으로 program identity를 바꾸지 않는다.
`recordCost`는 CPU recording plan의 입력이며 GPU duration을 대신하지 않는다.

### 직접 binding 경로

준비된 불변 packet을 갖춘 feature는 `FrameBindings`의 resource/pass 배열을 논리 슬롯 수만큼 채우고
`BindProgram(program, bindings, error)`를 호출할 수 있다. 이 경로는 Declare를 실행하지 않는다.
모든 import, callback 형태와 외부 owner가 필요하고 현재 native schema가 program과 다르면 실패한다.
호출자는 적합한 program variant를 선택하거나 정상 선언 경로로 돌아간다.

외부 owner는 import 저장소와 callback이 참조하는 CPU 데이터를 모두 보장해야 한다. graph는
resource와 callback을 먼저 해제하고 마지막에 이 owner를 해제한다. program만 유지해도 프레임
저장소가 유지되는 구조가 아니며, 이전 callback capture를 복구하는 fallback도 없다.

## Subresource의 데이터 의존성과 native 상태

`RGPassUsage`의 `textureRange`는 mip/layer/aspect, `bufferRange`는 byte offset/size다.
기존 기본값은 전체 resource다. 부분 범위 선언은 `ExplicitVersioned`에서 검증한다.
범위의 끝과 overflow, 0 길이, format aspect, 3D texture의 단일 array layer를 확인한다.
잘못된 범위를 전체 resource로 바꾸어 받아들이지 않는다.

부분 `Write` 버전에서 변경하지 않은 구간은 부모 버전의 데이터를 이어받는다. 읽는 cell에 실제로
도달하는 writer로 RAW edge를 만들고, live 접근끼리 겹치는 cell에만 WAR/WAW를 만든다.
초기화하지 않은 transient cell을 Read/Modify하면 compile이 실패한다. imported resource의 최종
출력은 각 cell의 마지막 writer를 보존한다. 관계없는 부분 write는 culling할 수 있다.

native 상태의 단위는 device capability에 따라 더 넓을 수 있다. DX12 legacy buffer state는 전체
buffer 단위다. depth/stencil layout을 나눌 수 없는 장치는 두 aspect를 함께 추적한다. 한 callback에서
넓어진 동일 상태 단위에 서로 다른 상태가 필요하면 compile이 거부한다. 바깥으로 상태를 반환할 때는
마지막 사용에서 전체 cell을 명시된 imported final state 또는 Common으로 모으므로 단일 writeback과
pool 상태가 실제 native 상태를 나타낸다.

컴파일러의 Cartesian mip/layer/aspect partition은 resource마다 최대 65,536 cell이다. 이를 넘는
선언은 큰 배열을 만들기 전에 진단과 함께 거부한다. 범위 기반 graph는 현재 placed alias allocation을
사용하지 않고 committed 경로를 유지한다. queue 간 상태 이전도 보수적인 resource 단위 ordering을
유지한다. 범위별 native transition이 지원된다는 사실이 모든 범위별 queue overlap을 뜻하지 않는다.

## 여러 queue에서의 alias 안전성

`ResourcesHappenBefore(A, B)`는 A의 **모든 live 사용**에서 B의 **모든 live 사용**으로 dependency DAG의
경로가 있는지를 검사한다. heap class와 선형 lifetime 구간이 맞아도 이 증명이 없으면 둘은 같은 heap
offset을 쓰지 않는다. 그룹의 연속 occupant마다 같은 조건을 확인하므로 앞선 모든 occupant에도
추이적으로 적용된다. 이 정책은 선형 순서만 본 allocator보다 메모리를 더 사용할 수 있다.

queue planner는 이 DAG edge를 보존하고 다른 queue의 edge에는 GPU wait를 만든다. 따라서 queue
placement가 바뀌어도 검증한 occupant가 겹치지 않는다. CPU callback이 기록되는 순서는 이 증명의
재료가 아니다. GPU 명령 실행의 happens-before가 필요하다.

alias activation은 native object의 resource state를 초기화하지 않는다. 각 placed object의 이전
native 상태를 별도로 유지한다. 비활성 object에는 queue prologue/epilogue transition을 만들지 않고,
활성 occupant만 batch boundary에서 전이한다. RT/DS 첫 사용의 full clear와 첫 write 제한을 유지한다.
compute queue에서 다룰 수 없는 초기 상태를 가진 placed resource는 graphics 경로에 남긴다.
현재 native placed allocator는 DX12에서 지원하며 Vulkan은 capability에 따라 committed 경로를 쓴다.

## 불변 texture의 graphics/compute 읽기 공유

지원 대상은 backend가 `SupportsConcurrentTextureShaderReads()`를 제공하고 명시적 상태가 유지되는
imported texture다. graph 전체의 live 사용이 ShaderResource/PixelShaderResource의 Read여야 한다.
buffer, 반복 pass, 부분 범위 graph 또는 한 번이라도 쓰는 resource에는 이 최적화를 적용하지 않는다.

graphics prologue가 `SharedShaderResource`를 확립한다. compute는 prologue의 timeline을 기다린 뒤
그 상태를 읽고, compute command list에서 graphics shader state를 전이하지 않는다. graphics
epilogue는 마지막 compute batch를 기다린 뒤 원래 계획된 최종 상태로 되돌린다. 이 명시적 fanout/fanin
계약이 있을 때만 graphics/compute read-read dependency를 생략한다. capability가 없는 backend는
기존 ownership ordering을 유지한다. 현재 DX12가 이 계약을 지원하며 Vulkan은 지원하지 않는다.

## 완료와 실패 후 수명

각 성공한 native batch 제출의 timeline point를 `QueueExecution::retirement`에 추가한다.
같은 native fence에서는 가장 큰 발급 값을 유지하고 서로 다른 queue fence는 따로 유지한다.
비어 있거나 유효하지 않은 집합은 완료 증명이 아니다. graphics scalar `completion`은 호환 경로의
최종 join을 나타내며, 여러 queue의 resource retirement 조건은 집합으로 확인한다.

recorded batch의 lease는 graph와 외부 owner를 함께 보유한다. accepted batch를 각 queue에서 완료
수집하기 전에는 Reset/변경이 거부된다. 도중 제출 실패에서는 accepted point만 retirement에 남고
`completed`는 false, `recoveryRequired`는 true다. 이 집합이 완료되어도 전체 frame이 성공했다는 뜻은
아니다. final state writeback을 게시하지 않으며, 부분 실행 resource를 reusable pool로 돌려보내지 않는다.
native backend가 제출했으나 signal하지 못한 작업은 backend의 recovery 보유 계약도 지켜야 한다.

## 검증 범위와 재현

```bash
python Tools/regression/verify-render-graph-program.py
python Tools/regression/verify-render-graph-program.py --sanitize
```

이 fixture는 실제 graph translation unit과 arena, ownership 구현을 compile/link한다. native device,
queue와 encoder는 descriptor, barrier, wait와 수명을 관찰하는 mock이다. worker scheduler는 실행하지
않으며 예상하지 않은 worker dispatch를 거부한다. 새 callback, view별 epoch, schema miss, 부분 범위
hazard/culling, shader read fanout/fanin, HB alias queue 제출과 실패 후 retirement를 실행해 확인한다.

`/proc` task 접근이 차단된 ptrace sandbox에서는 LeakSanitizer가 시작하지 못할 수 있다. 그 환경에서는
`ASAN_OPTIONS=detect_leaks=0`으로 address/undefined sanitizer를 실행하고 leak 검사는 미실행으로 기록한다.
이 CPU 검사는 native DX12/Vulkan 빌드, debug layer, 실제 GPU 출력·완료·메모리 또는 성능 검증을
대신하지 않는다. queue 비용 모델의 handoff 값도 아직 측정으로 보정한 값이 아니며, 재사용과 scratch
capacity 유지의 구현만으로 frame 시간 개선을 주장하지 않는다.
