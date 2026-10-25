# 컨테이너·자료구조 최적화 분석 (2026-10-25)

기준: `master` `d86fe25e0` + 작업 트리(미커밋 변경 포함). 줄 번호는 작업 트리 기준이므로, 커밋된 리비전과 어긋날 수 있다.

이 문서는 소스 정적 분석에 근거한 **추정 연구 문서**다. 비용은 호출 빈도, 원소 규모(N), 할당 횟수, 복잡도, 메모리 접근 모델로 추정했고, 장면 규모 가정은 항목마다 적었다.

재검토: 2026-10-25, `master` `54e937658` + 작업 트리에서 주요 항목을 대조했다. 최초 분석 전체를 다시 검증한 것은 아니며, 아래 정정과 안전 조건을 반영했다.

**검증 범위:** 이번 재검토는 문서·소스 확인만 수행했다. 엔진 실행, 프로파일링, 전후 벤치마크는 수행하지 않았다. 본문의 시간(ns/µs/ms), 배속, 메모리 절감률은 해당 경로의 실측 결과가 아니라 가정에 따른 추정이다. 기존 mem.bench 기록도 개별 경로의 프레임 비용을 입증하지 않는다. 호출처 수는 프레임당 실행 횟수를 뜻하지 않는다.

**우선순위 해석:** 모든 순위는 복잡도·예상 규모·정확성 위험에 따른 잠정 조사 순서이며, 측정된 병목 순위나 확정 구현 순서가 아니다. 실제 장면의 활성 경로, 호출 빈도, 원소 수·축출 수, 캐시 상태와 전후 CPU/GPU 시간을 계측한 뒤 다시 정해야 한다.

### 비용 모델의 공통 전제

- **할당기**: mimalloc은 전역 할당기가 아니다. 다음 경로에서만 쓰인다.
  - `Engine/Utility_Framework/Arena.cpp:65`
  - `Engine/Utility_Framework/HeapAllocator.h`
  - `Engine/RenderEngine/RenderAllocator.h`

  `std::map`/`unordered_map` 노드와 `std::function` 힙 캡처는 CRT(Windows heap)로 간다. `Editor/EngineEntry/Commands/DiagnosticsCommands.cpp:176`의 mem.bench 기록에는 다음이 있다.
  - 프레임당 약 272건
  - mimalloc 기준 256B 이하 22~75ns
- **연산 단가**:

  | 연산 | 가정 비용 |
  |---|---|
  | CRT 소형 노드 할당 + 해제 | 50~150ns |
  | MSVC `unordered_map` 조회 (FNV-1a + 버킷 리스트 노드 미스) | 30~80ns |
  | 차가운 캐시 라인 원자 RMW | 40~100ns |
  | 연속 배열 원소 | 수 ns |

## 0. 분석 범위

### 방법
- `Engine/` 전체의 연관 컨테이너·`deque`·`priority_queue` 선언 약 470곳을 전수 분류했다.
- 다음 비효율 패턴은 grep 휴리스틱으로 전체를 훑었고, 밀집 파일은 직접 읽었다.
  - `vector` 중간 `erase` / 앞쪽 `insert`
  - `std::find`·`any_of` 류 선형 탐색
  - `vector<bool>`
  - `std::function` 멤버·컨테이너
  - 스냅샷 반환 함수
  - "가장 오래된 것 전수 탐색" 축출 패턴

### 직접 읽은 영역

| 영역 | 범위 |
|---|---|
| RenderEngine | 씬 렌더러, MaterialGraph(SceneHost·SceneLookup·MeshSurface·RenderBindings·InstancePins·Runtime), 렌더 그래프, RenderScene, 프록시 큐, 클러스터 스트리밍, 폰트, 광원 선별, 스프라이트·UI·반투명 정렬, DataSystem 캐시, AssetDepot(Texture·Model·Material 런타임), LX 머티리얼 런타임 캐시, ShaderMetadataRuntime, 애니메이션 샘플러·트랙 배치 |
| RHI | 영속 힙, 디스크립터, PSO, 루트 시그니처, 텍스처·메시 캐시, 축출 정책, 퇴역 큐, 제출 스레드, sparse 페이지 풀, 입력 레이아웃 캐시, 배치 시그니처, DLSS 어댑터, Vulkan 리소스 테이블·렌더 서비스·파이프라인 캐시, 예산 코디네이터 |
| 셰이더 | `Dynamic_CPP/Assets/Shaders/DefaultPassShader/ForwardCull.slang` (광원 타일 컬링) |
| SceneRuntime | Scene(선택·물리·Canvas), 물리 바인딩, Tag, BlackBoard, 폴리지, 트랜스폼 계층, 오디오 런타임·PlaybackService, Entity 컴포넌트 조회, SystemSchedule, SceneManager 일부, 입력 액션, BT 평탄화, 충돌 지오메트리 라이브러리 |
| Physics / Effect / EngineDiagnostics | `PhysicsScene` 디스패처·슬롯·센서 페어 / `EffectState` / 마커·스레드 스트림·집계·작업 큐 |
| Utility_Framework | JobScheduler, ThreadPool, Arena, LogSystem·LogStore, Delegate, TypeTrait, 자체 컨테이너(RingBuffer·ConcurrentQueue·FixedPool·DynamicBitset·HashingString·MemoryPool) |
| ThirdParty | GCCE `block_allocator`·`domain`, `ownership_cpp` `ownership.hpp` |
| 관리 코드 | ScriptRuntime(F#) Registry·Contacts·Trees, ScriptCore(C#) Entity·ScriptRegistry·BlackBoard·BehaviorTree |
| Lattice | `Core/LXGraph`, `ImGui/LXCanvas`·`LXNodeItems`·`LXStyle` 선언, `Material` 선언 |
| Editor | 자산 DB, 썸네일 캐시, 콘솔 명령 큐, HierarchyFlatten, 인스펙터 등록 표 |

### 제외하거나 얕게 본 영역

| 영역 | 이유 / 상태 |
|---|---|
| `BuildTool`(C#) | 오프라인 패키징 도구 |
| `GameScripts` | 수용 프로브. 할당 패턴만 확인 |
| `Engine/Network` | 계약 인터페이스만 있고 구현 없음 |
| `Engine/Contracts`, `Engine/RuntimeHost`, `Player`, `HeadlessHost`, `SingletonManager` | 플랫폼·명령 파싱·부트스트랩. 프레임 경로가 아님. 선언만 확인 |
| `Tools/`, GCCE·ownership_cpp 외 `ThirdParty` | 범위 밖 |
| `vector` 약 2,910곳 | 개별 열람하지 않음. 위 휴리스틱에 걸린 지점만 읽음 |
| Lattice `Material` 컴파일러, `LXDocument` | 선언만 확인. 컴파일 시점 맵 |
| Editor 인스펙터·컨텐츠 브라우저·노드 에디터 창 본문, `EditorRender` 패스 본문 | 선언만 확인 |
| `AssetMetaRegistry`, `ArtifactByteSource`, `PrefabUtility` 본문 | 선언만 확인 |
| 셰이더 | `ForwardCull.slang`만 읽음. 나머지 GPU 자료구조(파티클 정렬, Effect GPU 시뮬레이션)는 보지 않음 |
| Effect 정의·IR 검증, cooked manifest, 임포터 대부분 | 빌드·로드 시점. 결정성 요구 때문에 대상에서 뺌(§21) |

## 1. 결론 요약

- 비용 증가가 예상되는 후보는 주로 **접근 패턴과 맞지 않는 자료구조 계열**이다. 실제 비용 비중은 미확인이다.
  - 매 프레임 전수 순회·정렬
  - 중첩 선형 탐색
  - 노드 기반 조회
  - 원자 refcount 복사
  - 예산 축출의 전수 최소값 탐색
- 런타임 프레임 비용의 상위 조사 후보는 다음 셋이다. 아래 수치는 미실측 가설이며, 실제 상위 병목인지는 미확인이다.
  - 폴리지 CPU 컬링: 0.4~4ms
  - 프록시 스냅샷 refcount: 0.5~3ms
  - GPU 자산 축출 전체 검사 + 조건을 통과한 후보 정렬: 0.2~1ms
- 순간 정지(hitch) 우선 조사 후보는 **퇴역 큐 `vector` 루프 `erase`** 다. 대량 언로드 때 50~200ms라는 미실측 추정은 있으나 실제 발생 시간이나 위험 1위는 입증하지 않았다.
- 에디터 우선 조사 후보는 **Lattice 캔버스의 O(L²·(P+N)) 헤더 링크 처리**다. 링크 200·핀 1천에서 프레임당 수십 ms는 미실측 추정이며, 헤더 전용 노드 스타일 등 해당 경로의 활성 조건에 좌우된다.
- 정확성 문제를 4건 발견했다.
  - `TagManager`의 `string_view.data()` 조회: 범위 밖 읽기 가능성
  - 폰트 아틀라스 고갈: 한글 대량 텍스트
  - 헤더 `static std::set g_guids`: TU마다 사본이 생겨 중복 검사가 무력화됨
  - Forward+ 타일당 32개 고정 목록: 넘치면 광원이 조용히 잘림
- 엔진 안에 이미 쓸 수 있는 도구가 있다. 새 구조를 만들기보다 이것들을 넓혀 쓰는 편이 낫다(§20).
  - `FixedPool`, `DynamicBitset`, `render_pointer_set`
  - `ownership_cpp`의 `borrowed` / `local_owner` / 지연 퇴역 훅
- B-tree가 맞는 곳은 로드 시점 카탈로그뿐이고 효과도 작다(§22).

## 2. 우선순위 표

표의 순위는 **잠정 조사 우선순위**, 비용은 **미실측 추정**이다. 정확성 항목도 실제 재현 여부와 잠재 위험을 구분해 검증해야 한다.

### 2.1 런타임 (게임·플레이어 프레임)

| 순위 | 지점 | 제안 계열 | 추정 비용(가정) | 절 |
|---|---|---|---|---|
| 1 | 퇴역 큐 `RHICompletionRetireQueue::Collect` | FIFO 링 / `erase_if` 한 패스 | 대량 언로드 시 50~200ms 1회(1만 엔트리) | §9 |
| 2 | 폴리지 컬링 | 셀 격자/BVH + SoA + 캐시된 월드 AABB | 0.4~4ms/프레임(인스턴스 1만~10만) | §4, §5 |
| 3 | 프록시 스냅샷 | `borrowed` 배열 + 지연 퇴역(에폭) / 청크 COW | 0.5~3ms/프레임(프록시 1만 × 프레임당 4회라는 미검증 가정) | §12 |
| 4 | GPU 자산 축출 | 연령 버킷(timer wheel) | 0.2~1ms/프레임(상주 2천~1만) | §8 |
| 5 | Forward+ 광원 목록 | 2.5D 깊이 마스크 / 클러스터 / z-binning + 가변 길이 목록 | 정확성(타일당 32개 초과분 잘림) + 깊이 불연속 타일 과다 셰이딩 | §14 |
| 6 | 예산 LRU 전수 탐색 10곳 + MeshSurface touch | Intrusive LRU / linked hash map | 축출 1회 O(n), 트림 O(n·k), touch O(n) | §8 |
| 7 | 프레임 임시 set/map | 재사용 정렬 vector / open addressing / 고정 배열 | 0.05~0.5ms/프레임 | §6 |
| 8 | 반투명·스프라이트·UI 정렬 | 패킹 키 radix sort | 0.05~0.3ms/프레임(수천 항목) | §15 |
| 9 | 애니메이션 클립 데이터 | 클립 단일 블롭 + SoA 시간 배열 / 균일 샘플 | 뼈 수 × 트랙 3개의 포인터 추적, 캐시 미스 | §16 |
| 10 | 폰트 아틀라스 | Skyline 패킹 + 페이지 LRU | 정확성(아틀라스 고갈) | §17 |
| 11 | TagManager / 헤더 `g_guids` | Interning + 투명 조회 / 단일 정의 | 정확성 | §7, §19 |
| 12 | 영속 힙 free-list | TLSF | 할당마다 1~3µs(mutex 안) | §10 |
| 13 | recording ID 키 `std::map` 7종 | Sequence buffer | 0.01~0.05ms/프레임 | §11 |
| 14 | 물리 바인딩 조회 | Slot map | 0.05~0.15ms/프레임(바디 1~2천) | §3 |
| 15 | MaterialGraph InstancePins / 입력 레이아웃·LX 셰이더 정적 캐시 | Open addressing / 슬롯 인덱스 배열 / 읽기 위주 COW 스냅샷 | draw·인스턴스마다 전역 mutex + 노드 조회 | §13 |
| 16 | C# BlackBoard / `GetComponents*` / BT 인스턴스 | Tagged union + key ID / 호출자 버퍼 / 공유 평탄 BT + 에이전트별 상태 | GC 압력(Set마다 박싱, 조회마다 `List`) | §18 |
| 17 | 미리 해시된 `uint64` 키 맵 | Identity hasher + open addressing | 조회마다 FNV 재해시 | §13 |
| 18 | 스레드 간 큐, MSVC deque, PPL `concurrent_queue` | 상한 SPSC/MPSC 링 / 프레임 이중 버퍼 | 원소마다 노드 할당 + 동기화 | §10 |
| 19 | JobScheduler 태스크, 페이지 리더 | 태스크 객체 풀 + inplace function / 호출자 버퍼 | 제출·페이지 읽기마다 할당 | §10 |
| 20 | 클러스터 스트리밍 LRU, sparse 페이지 풀 | Intrusive LRU / 평탄 페이지 테이블 | 재구성마다 O(n log n), 타일마다 노드 | §8, §14 |
| 21 | 오디오 보이스 제한 | 증분 카운터 + 인덱스 힙 | 제한 강제 시 O(V²) | §17 |
| 22 | Effect 슬롯, SystemSchedule origin 집합, F# 레지스트리 | Free-list 스택 / 플래그 + 카운터 / mark-and-compact | 소규모 O(n) | §3, §17, §18 |

### 2.2 에디터·도구

| 순위 | 지점 | 제안 계열 | 추정 비용(가정) | 절 |
|---|---|---|---|---|
| 1 | Lattice 캔버스 링크 그리기 | 프레임(또는 그래프 리비전) 단위 인덱스: 핀→노드, 노드→레이아웃·크기, 노드별 측면 포트 순서 | O(L²·(P+N))/프레임, 링크 200·핀 1천이면 수십 ms | §19 |
| 2 | Lattice 그래프 검증·undo | ID 인덱스 + 구조 공유 undo | 편집마다 1~3ms, undo 메모리 O(편집 × 그래프) | §19 |
| 3 | Scene 선택 목록 | 순서 vector + 엔티티 인덱스 소속 비트셋 | 전체 선택(N=1만) 시 O(N²) ≈ 5천만 비교 | §19 |
| 4 | 자산 DB 경로 맵·debounce | 경로 interning + linked hash map / radix tree | 대량 변경 시 틱마다 O(n log n · 경로 비교) | §19 |
| 5 | 임포트·쿠킹 해시 | 양자화 공간 해시 / 64비트 간선 키 | 임포트 시간 | §19 |

## 3. Slot map / generational index / free-list

ID → 객체 조회를 해시 대신 배열 인덱스 접근 한 번으로 바꾼다. 엔티티(`Engine/SceneRuntime/EntityHandle.h`)와 Vulkan 리소스 테이블(`VulkanResourceTable.h:318`, 슬롯 + free 목록)은 이미 이 패턴을 쓴다.

| 지점 | 현재 | 관찰 |
|---|---|---|
| `Engine/SceneRuntime/Scene.cpp:2280` | `m_physicsBodies.find(pose.binding)` | 렌더 포즈마다 해시 조회 |
| `Engine/SceneRuntime/Scene.cpp:3328` | `m_physicsContactOwners.find` × 2 | 접촉마다 조회 2회 |
| `Engine/SceneRuntime/ScenePhysicsSimulation.h:173` | 같은 ID로 해시맵 4개 | `binding_id`가 `++m_next`로 계속 증가(`ScenePhysicsSimulation.cpp:60`) |
| `AnimationScheduler.h:72`, `AIManager.h:114`, `AnimatorSystem.h:205` | `unordered_map<id, …>` | ID → 슬롯 매핑만 해시로 남은 경우 포함 |
| `Engine/Effect/EffectState.cpp:64` | 빈 슬롯을 선형 탐색 | generation은 이미 있다. free-list 스택만 붙이면 O(1) |

- 제안: `binding_id = (slot << 32) | generation`. 저장소로는 `Engine/Utility_Framework/FixedPool.h`(free list + live 비트맵, O(1), 할당 없음)를 재사용할 수 있다. 현재 사용처는 1곳뿐이다.

## 4. 공간 분할 (셀 격자 / BVH / 클러스터 계층)

- 이 분석에서 확인한 폴리지 컬링 경로에는 CPU 셀/BVH 기반 일괄 기각이 없다. 엔진 전체의 CPU 공간 구조 부재를 입증한 것은 아니다. 메시에는 GPU 컬링(HZB) 경로가 있다.
- 폴리지는 `Engine/SceneRuntime/FoliageComponent.cpp:591`에서 인스턴스마다 `RebuildWorldMatrix()` → AABB 변환 → 프러스텀 판정을 매 프레임 반복한다. 인스턴스당 약 40ns로 보면 1만~10만 개에서 0.4~4ms다.
- 제안:
  - 정적 폴리지를 셀 격자나 셀 BVH로 묶는다.
    - 셀이 프러스텀 밖이면 셀 전체를 건너뛴다.
    - 셀이 완전히 안쪽이면 개별 판정 없이 수용한다.
  - 월드 행렬과 월드 AABB는 배치·편집 시점에 캐시한다.
  - 같은 구조를 에디터 피킹, 근접 질의, 데칼·라이트 영향 범위 질의에 재사용한다.

## 5. SoA와 CSR

- 폴리지 SoA: 컬링은 위치·바운드·플래그만 읽는다. 분리하면 SIMD로 4~8개씩 판정할 수 있다. §4와 함께 적용하면 2~6배를 추정한다.
- CSR / counting sort:
  - `Engine/RenderEngine/PrimitiveRenderProxy.cpp:27` `instanceMap`: 키가 밀집된 타입 인덱스다. `offsets[]` + 평탄 배열로 바꾼다.
  - `Engine/RenderEngine/Render/Graph/EnhancedRenderGraph.cpp:2046`, `:2071`: 간선마다 `none_of`로 중복 검사(O(E·deg))를 한다. 비트 인접 행렬 + CSR로 바꾼다.
  - 같은 파일 `:2080` `priority_queue<uint16_t>`: 비트셋 + find-first-set.
  - 같은 파일 `:1731`, `:1980` `seen` 선형 중복 검사: 패스 수 상한이 작으므로 `DynamicBitset`.

## 6. 프레임마다 다시 만드는 임시 컨테이너

| 지점 | 현재 | 제안 |
|---|---|---|
| `Engine/RenderEngine/Render/Scene/EnhancedSceneRenderer.cpp:5550` `activeMeshes` | `std::set` 삽입 | 재사용 vector + sort/unique |
| `EnhancedSceneRenderer.cpp:5645` `modelPinIndices` / `meshPinIndices` | 빌드마다 `std::map` 2개 | 재사용 open addressing |
| `EnhancedSceneRenderer.cpp:6040`, `:6131` `currentViews` | `std::set`, 상한 `kMaxLiveCameraViews` | 고정 배열에서 선형 탐색 |
| `Engine/RenderEngine/Render/Scene/EnhancedTemporalViewState.h:21`, `:28` | 프레임마다 `clear` + 원소마다 노드 | 정렬 vector 2개 swap + 이진 탐색 |
| `Engine/RenderEngine/MaterialGraphSceneHost.cpp:1856` `sources` | draw마다 `std::map` | 재사용 해시 |
| `MaterialGraphSceneHost.cpp:710`, `Engine/RenderEngine/MaterialGraphSceneGpuData.h:53` | 포인터 키 `std::map` | 원래 주소 순서라 해시로 바꿔도 잃는 것 없음 |

- 이미 있는 도구: `Engine/RenderEngine/RenderArena.h:33` `render_pointer_set`.
- `arena_map`(`Arena.h:373`)은 할당 비용만 줄인다. RB-tree 포인터 추적은 남는다.

## 7. String interning / 투명 조회 / perfect hash

| 지점 | 문제 | 제안 |
|---|---|---|
| `Engine/SceneRuntime/TagManager.cpp:162`, `:176`, `:191`, `:202`, `:223` | `find(tag.data())`: 조회마다 `std::string` 할당. **`string_view.data()`는 널 종료가 보장되지 않아 범위 밖을 읽을 수 있다** | 투명 해시로 `string_view` 직접 조회 |
| `TagManager::m_taggedObjects` | 태그 ID가 있는데도 문자열로 다시 키잉 | `vector<vector<Entity*>>[tagId]` + swap-remove |
| `Engine/SceneRuntime/BlackBoard.h:59` | AI 틱 경로의 문자열 키(호출처 21곳) | Interned atom ID |
| `Engine/RenderEngine/Material.cpp:756` | cbuffer 이름 문자열 조회 | 레이아웃 시점에 슬롯으로 해석 |
| `ComponentFactory.h:22`, `RenderState.h:28`, `Player/PlayerCommands.cpp:782`, `InputActionManager.h:16`(`FindActionMap(std::string)` 값 전달 + 선형) | 키 집합이 고정됨 | 정적 perfect hash / interned ID / `string_view` 인자 |
| `Engine/RenderEngine/DataSystem.cpp:3479` 등 `Materials.find(std::string(name))` | 경로 문자열 키, 임시 문자열 생성 | 투명 조회 + FileGuid 키로 일원화 |
| `Engine/RenderEngine/AssetDepot/TextureAssetRuntime.h:50` `TextureImageKey` | 엔트리마다 `targetPlatform`/`targetAbi` 문자열 2개(프로세스 내 상수) | 플랫폼·ABI interned ID |
| `Engine/RenderEngine/RHI/DX12/DX12DescriptorHeaps.h:349` | `map<vector<uint64_t>>`: 조회마다 키 할당 | 합성 해시 + 정확 비교, inline small vector |

참고: `Engine/Utility_Framework/HashingString.h`는 문자열과 캐시된 해시를 함께 들고 다니는 구조이지, interning이 아니다. 문자열 소유와 비교 비용은 남는다. atom 테이블은 별도로 필요하다.

## 8. 축출 정책: 연령 버킷 / intrusive LRU / CLOCK

### 8.1 전체 검사 + 조건부 후보 정렬
- `Engine/RenderEngine/RHI/DX12/EnhancedSceneRendererLiveDX12Adapter.cpp:1909` → `DX12TextureCache::RetireUnused` / `DX12MeshCache::RetireUnused` → `Engine/RenderEngine/RHI/RHIAssetEvictionPolicy.cpp:46`.
  - 현재: 상주 엔트리로 구성한 입력 후보 전체를 검사한 뒤, 연령·축출 가능 여부·메모리 압박 조건을 통과한 `normal`/`pressured` 목록만 각각 `std::sort`한다. 전체 검사 비용과 실제 정렬 대상 수를 구분해야 한다(`RHIAssetEvictionPolicy.cpp:64`~`:96`).
  - 제안: `lastUsedFrame` 연령 버킷(timer wheel).
    - touch할 때 버킷을 옮긴다.
    - 축출은 임계 연령을 넘은 버킷만 비운다.
    - 압박 모드의 정렬은 버킷 안에서만 한다.
    - 축출 선택 단계의 빈 버킷 확인은 O(1)을 목표로 할 수 있다. 프레임 전체 비용에는 touch·버킷 이동·프레임 진행 처리가 남는다.
    - 안전 조건: 기존의 최근 사용 보호, 업로드 미완료 제외, 압박 목표 바이트, 동일 연령의 바이트 내림차순·assetId 오름차순 규칙을 보존하거나 변경 계약을 명시한다.

### 8.2 예산 초과 시 "가장 오래된 것" 전수 탐색 (10곳)
`while (charge > budget) { 전체를 훑어 lastUse 최소를 찾고 축출 }` 형태다. 축출 1회가 O(n), 트림 1회가 O(n·k)다.

| 지점 |
|---|
| `Engine/RenderEngine/AssetDepot/TextureAssetRuntime.cpp:515`, `:639`, `:661`, `:1149` |
| `Engine/RenderEngine/AssetDepot/MaterialAssetRuntime.cpp:256`, `:818` |
| `Engine/RenderEngine/AssetDepot/ModelAssetRuntime.cpp:686` |
| `Engine/RenderEngine/Assets/ModelAssetGeneration.cpp:1319` |
| `Engine/RenderEngine/MaterialGraphRuntime.cpp:509` |
| `Engine/RenderEngine/ShaderMetadataRuntime.cpp:125` |

- 제안: 엔트리에 prev/next를 두는 intrusive LRU(또는 hash + 리스트의 linked hash map). touch O(1), 축출 O(1). 기존 `clock`/`lastUse` 값은 진단용으로만 남긴다.
- 캐시가 `std::map` 노드에 들어 있으므로, 노드 안에 리스트 링크를 넣는 방식으로 바로 적용할 수 있다. 이터레이터가 안정적이기 때문이다.

### 8.3 기타 LRU
- `Engine/RenderEngine/MaterialGraphMeshSurface.cpp:1407`: touch할 때 `std::erase(insertionOrder, key)`로 deque를 선형 삭제하고 다시 넣는다. touch가 O(n)이다. 같은 intrusive LRU로 바꾼다.
- `Engine/RenderEngine/Assets/ClusterStreaming.h:373`: 재구성마다 `stable_sort(m_lastUse)`를 한다. 그룹 배열 intrusive LRU로 바꾸면 결정성을 유지하면서 O(1)/O(k)가 된다.
- `MaterialGraphRenderBindings.h:164` `descriptions_`(`lastUse` + 바이트 예산), `Editor/EngineGUIWindow/BrowserThumbnailCache.cpp`: 같은 대상이다.

공용 `intrusive_lru<Index>` 하나로 14곳 이상을 정리할 수 있다.

## 9. 퇴역 큐: 단일 패스 압축 / 조건부 FIFO 링 (hitch 우선 조사 후보)

- `Engine/RenderEngine/RHI/RHICompletionRetireQueue.h:55` `Collect`: `std::vector<Entry>`를 순회하며 원소마다 `it = m_entries.erase(it)`를 한다. O(n·k)다.
- 사용처는 매 프레임 실행된다: `DX12TextureCache`, `DX12MeshCache`, `DX12PSOManager`, `VulkanPipelineCache`, `VulkanRenderServices`.
- 대량 언로드나 압박 축출 때 1만 엔트리 중 절반을 회수하면 삭제 위치에 따라 수천만 회의 원소 이동이 생길 수 있다. 약 5천만 회는 거친 상한 규모 가정이며, 실제 Entry 크기·payload 이동/해제 비용은 별도 확인이 필요하다. 50~200ms는 실측하지 않은 추정으로, 실제 hitch 시간이나 순위를 입증하지 않는다.
- 제안:
  - 즉시: `std::erase_if` 또는 안정적 단일 패스 압축으로 O(n). releaser를 회수 대상마다 정확히 한 번 호출하고, 잔존 순서·pending bytes/count·quarantine count를 보존한다.
  - 구조: 모든 생산자의 enqueue 순서에서 completion 값이 단조 증가하고 같은 완료 타임라인을 사용함을 입증한 경우에만 FIFO 링의 앞쪽 pop으로 O(k)를 적용한다. 현재 컨테이너 자체는 이 순서를 강제하지 않는다. 순서가 보장되지 않으면 단일 패스 검사를 유지한다.
  - 안전 조건: completion 값 0인 quarantine은 Collect에서 절대 회수하지 않고 producer/consumer 정지 후 Drain에서만 해제한다. 별도 저장소로 분리해도 이 계약과 집계를 보존한다. 미완료·완료 항목 혼재, 역순 값, quarantine, 중복 해제 방지를 회귀 검증한다.
- 같은 패턴(원소 수가 작아 우선순위는 낮음): `RHISubmissionThread.cpp:1261`, `ScreenSizedResource.h:111`, `:157`, `EnhancedIBLGenerator.cpp:937`, `MaterialGraphSceneLookup.cpp:923`(vector 앞쪽 삭제).

## 10. 할당기·큐·태스크

### TLSF
- `Engine/RenderEngine/RHI/RHIPersistentHeapPolicy.h:136`: 세그먼트마다 `map` + `multimap` + `map`을 두고, 할당마다 mutex 안에서 `lower_bound`와 노드 2~3개 할당·해제를 한다.
- TLSF(2단계 비트맵 + 블록 헤더의 물리 이웃 링크)로 바꾸면 탐색·병합이 O(1)이고 노드 할당이 없다.
- 디스크립터 할당(`DX12DescriptorRecycler::Allocate`, `DX12TargetViewHeap::Allocate`)은 원자 bump라 이미 최적이다.

### 큐
MSVC `std::deque`는 원소가 8바이트를 넘으면 블록 하나에 원소 하나라서 원소마다 노드를 할당한다.

| 지점 | 현재 | 제안 |
|---|---|---|
| `Engine/RenderEngine/ProxyCommandQueue.h:384` `m_proxyCommands` | PPL `Concurrency::concurrent_queue`(MPMC, 무제한, 세그먼트 할당) — `Engine/Contracts/Creator.Concurrency.ixx` | 생산자가 게임 스레드 하나라면 프레임 이중 버퍼 명령 배열(arena) 또는 SPSC 링. 이미 배치 단위로 drain하는 구조와도 맞는다 |
| `ProxyCommandQueue.h:389` `m_deferredCommands` | deque | vector 두 개 swap |
| `Engine/EngineDiagnostics/ProfileService.h:467` | mutex + deque, 상한 있음 | 상한 MPSC 링 |
| `Engine/RenderEngine/Assets/ClusterStreaming.h:87` | `deque<std::function<void()>>` | 링 + SBO 태스크 |
| `RHISubmissionThread.cpp:183`, `EffectState.h:33`, `OperationTable.h:95`, `ConsoleCommandSystem.h:323`, `Player/PlayerCommands.h:99`, `Utility_Framework/LogStore.h:396`~`:403` | deque | 고정 링 / vector swap |
| `EnhancedSceneRenderer.cpp:2555` `renderQueue` | deque, 용량 1 | `std::optional` |

- 물리 디스패처 큐(`Engine/Physics/PhysicsScene.cpp:345`)는 이미 고정 링이다.
- 자체 `Engine/Utility_Framework/RingBuffer.h`는 다음 문제가 있어 그대로 재사용하기 어렵다.
  - `get_all`이 vector를 새로 만들어 반환한다.
  - pop이 없는 덮어쓰기 로그 버퍼다.
  - 주석 인코딩이 깨져 있다.

  상한 링은 새로 정의하는 편이 낫다.

### 태스크 객체와 콜백
- `Engine/Utility_Framework/JobScheduler.h:57`와 `ThreadPool.cpp:65`: 제출마다 `work` 객체(+ mutex)와 `std::function` 2~3개를 할당한다. 렌더 그래프, 씬 렌더러, 애니메이션 스케줄러, DataSystem이 매 프레임 제출한다.
  - 제안: `work` 풀링(free-list), inplace function, 그룹 태스크 벡터는 프레임 arena에 둔다. enkiTS 자체는 락프리라 유지한다.
- 페이지 리더가 `std::vector<std::byte>`를 값으로 반환한다. 페이지·청크 읽기마다 할당이 생긴다.
  - 위치: `Engine/RenderEngine/Assets/MeshletCompression.h:29`, `PreparedClusterPages.h:15`, `Experiment/Cooked/CookedModelSubAssetCodec.h:77`
  - 제안: 호출자가 준 `std::span<std::byte>` 버퍼에 쓰는 시그니처, 또는 스트리밍 전용 버퍼 풀.
- `Engine/Utility_Framework/Delegate.inl:132` `Broadcast`: 호출마다 `vector<CallbackInfo>`(`std::function` 포함)를 통째로 복사한다.
  - 프레임 이벤트 6종(`SceneManager.cpp:828`~`:879`)은 구독자가 1~2개라 현재 비용은 작다.
  - 구독자가 늘면 비례한다. 불변 콜백 목록을 `shared_owner<const vector>`로 COW하면 Broadcast는 포인터 획득 하나로 끝난다.

## 11. Sequence buffer (단조 증가 ID + 상한 있는 창)

| 지점 |
|---|
| `Engine/RenderEngine/GpuGeometryVisibility.h:329` `m_recordings` |
| `Engine/RenderEngine/MaterialGraphResidentBuffer.h:456` `recordings_` |
| `Engine/RenderEngine/MaterialGraphSceneHost.h:357` `recordings_`(순회 삭제 `:3933`, `:5049`) |
| `Engine/RenderEngine/MaterialGraphSceneLookup.h:139` `submitted_` + `:140` `acceptedRecordings_`(128 상한, `begin()`부터 삭제) |
| `MaterialGraphSceneLookup.cpp:250`, `MaterialGraphSceneRuntimeEffects.cpp:314` `uploads` |
| `MaterialGraphSceneRuntimeEffects.h:79` `depthBindings_` |
| `Engine/RenderEngine/RHI/Temporal/DlssTemporalAdapter.cpp:232` `frames` |

- 제안: `slot = id % N` 링 배열에 `id` 태그를 저장하고 조회 시 전체 ID를 비교한다. 노드 할당을 없앨 수 있지만, 덮어쓰기는 기존 슬롯의 수명이 끝났음이 보장될 때만 허용한다.
- 안전 조건: 최대 동시 미완료 수뿐 아니라 살아 있는 ID 사이의 간격과 modulo 충돌도 확인한다. ID의 단조 증가만으로 안전한 상한 창이 보장되지는 않는다. GPU 지연·취소·재시도·오래된 완료 통지를 고려해 미완료 슬롯 충돌 시 확장/별도 저장/대기 정책을 정한다. ID wraparound, 정리 순서, payload 퇴역 계약을 검증한다. 128개 이력 상한을 다른 미완료 저장소에도 그대로 적용하지 않는다.
- 함께 다니는 set은 같은 슬롯의 플래그 비트로 합친다.

## 12. 프록시 스냅샷: `borrowed` + 지연 퇴역

- `Engine/RenderEngine/RenderScene.cpp:91` `GetPrimitiveProxySnapshot`: 맵 노드를 따라가며 `shared_owner`를 복사한다. 확인한 호출처 4곳 중 3곳은 `EnhancedSceneRenderer.cpp`, 1곳은 에디터 테스트다. 프레임당 실행 횟수는 분기·뷰 수·테스트 실행 여부에 따라 달라지며 미계측이다.
- `ThirdParty/ownership_cpp/include/own/ownership.hpp:376`, `:449`를 보면 strong 복사는 relaxed `fetch_add`, 해제는 release `fetch_sub`(+ 마지막일 때 acquire)다. 원자 RMW 자체는 최소화되어 있지만, 프록시마다 control block 라인을 건드리는 비용은 남는다.
- 프록시 1만 개를 프레임당 4회 복사·해제한다고 가정하면 RMW 약 8만 회다. 이는 호출처 수를 실행 횟수로 둔 미검증 시나리오이며, 0.5~3ms도 해당 경로의 실측값이 아니다.
- 같은 라이브러리에 해법 재료가 이미 있다.
  - `borrowed<T>`(`:766`): 비소유 참조
  - `local_owner<T>`(`:1274`): 비원자 스레드 로컬 그룹
  - `retirement_hook` / `retirement_task`(`:263`, `:286`): 마지막 strong 해제를 지연 퇴역으로 돌리는 훅
- 제안:
  - 모든 소비자를 보호하는 에폭/읽기 가드를 먼저 설계한 뒤 프록시의 마지막 해제를 지연한다. `borrowed<PrimitiveRenderProxy>` 자체는 수명을 연장하지 않으며, 퇴역 훅의 존재만으로 안전해지지 않는다. 조건 충족 시 원소별 strong 복사·해제 RMW를 제거할 수 있으나 가드·동기화 비용은 남는다.
  - 안전 조건: 맵 락 안에서 읽기 보호와 스냅샷 획득을 연결하고, 렌더 워커·비동기 작업·에디터 소비자를 포함한 마지막 접근 이후에만 퇴역한다. 해제 스레드, 종료 시 drain, unregister/re-registration과 같은 주소 재사용을 검증한다. 수명 보호와 별개로 가변 프록시 데이터의 동시 접근 규약도 유지한다. `local_owner`는 스레드 로컬 계약을 입증한 범위에서만 사용한다.
  - 저장소 `ProxyMap`(노드 해시)은 밀집 배열 + 인덱스 맵으로 바꿔 순회를 연속 메모리로 만든다.
  - 대안: 청크 COW 배열(바뀐 청크만 새로 만듦).

## 13. 해시 품질, 정적 캐시, 프레임 핀

- **Identity hasher**: MSVC `std::hash<uint64_t>`는 8바이트 FNV-1a다. 키가 이미 해시값인 맵은 이중 해시다.
  - 대상: `DX12PSOManager::m_cache`, `DX12RootSignatureCache::m_cache`, `VulkanPipelineCache::m_layoutByHash` / `m_pipelineByHash`, `DX12DescriptorHeaps::m_cache`, `DX12PersistentHeap::m_segments`, 렌더 그래프 `freeList`·`allocationCache`(`EnhancedRenderGraph.h:177`, `:231`)
  - 제안: identity(또는 곱셈 mix) 해셔 + open addressing. 7워드 키는 XOR 결합 대신 wyhash/xxh3.
- **전역 mutex 정적 캐시**:
  - `Engine/RenderEngine/RHI/ModelVertexInputLayout.cpp:94`: 전역 mutex + `std::map<pair<mask,mask>>`를 조회마다 잡는다. 키 공간이 작고 읽기 위주다. 읽기를 락 없이 하는 COW 스냅샷(원자 포인터 교체)이나 사전 계산 표로 바꾼다.
  - `Engine/RenderEngine/LXMaterialRuntime.cpp:117`: 전역 mutex + `map<uint32_t slot, vector<weak_owner>>`. slot이 밀집 인덱스라면 slot 인덱스 vector로 바꾼다.
- **프레임 핀**: `Engine/RenderEngine/MaterialGraphInstancePins.h:91`은 mutex + `std::map<Key, index>`를 인스턴스마다 매 프레임 조회한다. open addressing으로 바꾸거나, 워커별 로컬 핀을 모은 뒤 끝에 병합한다.
- `MaterialGraphRenderBindings.h:160` `prepared_`(`map<Key, vector<weak_owner>>`)도 같은 키라 해시 대상이다.
- `Engine/RenderEngine/RHI/RHIDeviceMemoryBudgetCoordinator.h:124`~`:136`: 도메인·소유자가 작은 정수 ID라면 평탄 배열.
- `Engine/RenderEngine/RHI/DX12/DX12DeviceResources.h:645` `m_meshBatchSignatures`: 생성 시점 캐시. 순위 낮음.

## 14. GPU·스트리밍 자료구조

### Forward+ 광원 목록 (정확성 포함)
- `Dynamic_CPP/Assets/Shaders/DefaultPassShader/ForwardCull.slang`은 2D 화면 타일 + 근거리 `minViewZ = 0` / 불투명 최대 깊이의 프러스텀 + 타일당 고정 `kMaxLightsPerTile(32)` 목록이다. 하늘만 있는 타일은 원거리도 연다. 근거리는 불투명 최소 깊이가 아니며, 투명면의 광원을 보존하려고 카메라까지 열어 둔 것이다(`:65`~`:84`). 넘치면 잘린다(`:78` 주석, GetLastOverflowTileCount).
- CPU(`Engine/RenderEngine/Render/Core/EnhancedLightPacking.h:171`)는 모든 광원을 점수순으로 정렬해 전부 올린다. 타일마다 스레드가 전체 광원을 순회한다(`:111`).
- 문제:
  - 깊이 불연속 타일(전경 + 원경)은 카메라~불투명 최대 깊이 구간에서 실제 표면이 없는 깊이의 광원도 수용할 수 있다. 과다 셰이딩 규모는 미계측이다.
  - 32개를 넘으면 광원이 시각적으로 빠진다.
- 제안(택일 또는 조합):
  - **2.5D 깊이 마스크**: 타일 깊이 구간을 32비트 비트마스크로 나누고, 광원 구간 마스크와 AND로 판정한다.
  - **클러스터(froxel) 3D 격자** + 전역 압축 목록. 슬롯 대신 prefix-sum으로 가변 길이를 만들면 잘림이 사라진다.
  - **z-binning**: CPU에서 광원을 뷰 깊이로 정렬한 뒤 z-bin마다 인덱스 범위를 두고, 타일 비트마스크와 교차한다. 메모리가 O(타일 × 광원/32 + bin)이다.
- 광원 수가 적은(수십) 현재 장면에서는 비용보다 잘림·과다 셰이딩 정확성 측면이 크다.
- 안전 조건: 불투명 깊이만으로 만든 2.5D 마스크를 투명 패스에 그대로 적용하지 않는다. 불투명보다 앞선 투명면, 하늘 배경의 투명면을 보수적으로 수용하거나 별도 목록을 제공한다. 가변 목록도 전역 용량 초과·prefix-sum 크기·동기화·오버플로 fallback이 필요하다. 32개 초과 광원과 투명면 회귀 장면에서 누락을 검증한다.

### Sparse 페이지 풀
- `Engine/RenderEngine/RHI/RHISharedSparsePagePool.h:18`: 할당마다 `map<uint32_t tile, uint32_t physical>`와 `map<uint32_t tile, priority>`를 둔다. 가상 타일 인덱스는 `virtualBytes / 타일 크기` 이내의 밀집 범위다.
- 제안: 평탄 페이지 테이블(`vector<uint32_t>`, 무효값 표시)과 병렬 우선순위 배열. 축출 후보 수집은 상주 타일 목록(sparse set)만 훑는다.
- 정적 `registry`(`:46`)는 장치 수가 작아 문제없다.

### 클러스터 스트리밍 소규모
- `ClusterStreaming.h:406` `m_feedback`: `set<uint32_t>` → 비트셋 + vector.
- `:175`: 앞쪽 `insert`.
- `:418` `m_reads`: 상한 있는 flat vector로.

## 15. 정렬 키와 radix sort

| 지점 | 현재 | 제안 |
|---|---|---|
| `Engine/RenderEngine/Render/Passes/Geometry/EnhancedSpritePass.cpp:163` | 인덱스 배열 `stable_sort` + 간접 비교(`canvasOrder`, `layerOrder`) | `(canvasOrder, layerOrder, index)`를 64비트로 패킹해 LSD radix sort. 안정성이 자동으로 보장되고 간접 참조가 없음 |
| `Engine/RenderEngine/Render/Passes/UI/EnhancedUIPass.cpp:367` | 같은 패턴 | 같음 |
| `Engine/RenderEngine/MaterialGraphSceneHost.cpp:276` 반투명 | `(depth desc, geometryKey, index)` 비교 정렬 | float 깊이 → 정렬 가능한 uint 변환 + 키 패킹 radix |
| `EnhancedLightPacking.h:171` | 전체 정렬 | 상한 N만 쓰게 되면 `nth_element` + 부분 정렬 |

수천 항목 기준으로 비교 정렬 대비 2~4배, 항목 수가 적으면 차이가 미미하다고 추정한다.

## 16. 애니메이션 클립 데이터 배치

- `Engine/RenderEngine/Assets/ModelAssetGeneration.h:254` `ModelAnimationTrack`이 뼈마다 `translations`/`rotations`/`scales` vector 3개를 따로 할당한다. 키는 `double time` + 값(AoS)이다.
- 탐색 자체는 `Engine/RenderEngine/ClipSamplingCursor.h`의 커서 + `lower_bound` 폴백으로 이미 O(1) 근사다.
- 남은 비용은 "뼈 수 × 3개 vector"의 포인터 추적과 캐시 미스다.
- 제안:
  - 클립 하나를 단일 블롭으로 만든다(트랙 오프셋 표 + 시간 배열(float/정규화 uint16) SoA + 값 배열).
  - 더 나아가 균일 샘플링 클립이면 키 탐색 없이 `index = t × rate`로 바로 접근할 수 있고, 회전 양자화(ACL 계열)로 메모리를 2~4배 줄일 수 있다.

## 17. 기타 런타임

### 폰트 — skyline 패킹 + 페이지 LRU (정확성 포함)
- `Engine/RenderEngine/FontAsset.cpp:346`: 단일 줄 shelf, 글리프 축출 없음, 페이지 8개 상한. 한글 대량 텍스트에서 아틀라스가 고갈된다.
- 제안: skyline 패킹 + 페이지 LRU. 글리프 캐시는 open addressing.

### 오디오 — 증분 카운터 + 인덱스 힙
- `Engine/SceneRuntime/Audio/AudioRuntime.cpp:282` `PhysicalCount`가 전수 순회를 하고, `EnforceLimits`(`:773`)는 O(V²)다.
- 제안: 버스·그룹별 증분 카운터, 위치 역참조 4진 힙. 버스 ID 키 맵은 평탄 배열.

### SystemSchedule origin 집합
- `Engine/SceneRuntime/SystemSchedule.h:121`의 `unordered_set<Component*>` 2개는 `insert`/`erase`/`size`/`clear`로만 쓰인다(`:168`~`:208`). 컴포넌트 쪽 origin 플래그 비트 + 카운터 2개면 충분하다.

## 18. 관리 코드 (C#, F#)

- **`ScriptCore/BlackBoard.cs:30`**: `Value(type, object boxed)`라서 Set마다 박싱 할당이 생긴다.
  - 제안: 명시적 레이아웃 tagged union struct + interned key ID. 네이티브 BlackBoard(§7)와 같이 바꾼다.
- **`ScriptCore/Entity.cs:103` `GetComponentsInChildren`, `ScriptCore/ScriptRegistry.cs:41` `FindAll`**: 호출마다 새 `List<T>`를 할당하고, 계층 순회는 자식마다 네이티브 호출 + 래퍼 생성을 한다.
  - 제안: 호출자 버퍼 오버로드, 풀링 리스트, 계층을 네이티브에서 평탄 배열로 한 번에 받기.
- **`ScriptCore/BehaviorTree.cs:42`**: 노드가 클래스 계층 + 가상 `Tick`이다. `ScriptRuntime/Trees.fs:8`은 인스턴스마다 루트 객체 그래프를 보관한다.
  - 같은 트리를 쓰는 에이전트가 많으면 객체 그래프가 에이전트 수만큼 복제되어 GC 힙이 커진다.
  - 제안: 정의는 공유 평탄 배열(노드 종류·자식 범위·가중치 — 네이티브 `BTGraphFlatten.h`가 이미 이 형태로 넘김), 에이전트별 상태는 작은 struct 배열(실행 중 자식 인덱스 등).
- **`ScriptRuntime/Registry.fs:221`**: 제거 대상마다 `active.Remove`(O(n·k))를 한다. 실행 순서를 보존하는 mark-and-compact(`RemoveAll` 한 번)로 바꾼다.
- **`ScriptRuntime/Contacts.fs:165` `EndUpdate`**: 틱마다 모든 구독자의 모든 스트림을 훑어 `ClearTick`한다. `touched` 목록(`EndFrame`이 이미 사용 중)처럼 이번 틱에 건드린 스트림만 비운다.
  - 키 타입 `ShapeRole`(record struct)과 `ObjectHandle`(`IEquatable`)은 박싱이 없어 문제없다.
- **`GameScripts`**: 수용 프로브라 제품 비용은 아니다. 다만 위 API의 할당 패턴이 사용자 코드로 그대로 옮겨질 수 있으므로 API 쪽에서 막는다.

## 19. 에디터·도구·정확성

### Lattice 캔버스 (에디터 우선 조사 후보)
- `Lattice/ImGui/LXCanvas.cpp:891` `forEachHeaderPort`가 링크마다 `HeaderLinkEndpoints`(`:423`)를 부른다. 그 안의 `order` 람다(`:450`)가 다시 **전체 링크**를 순회하면서 링크마다 `FindPin` × 2(전체 핀 선형, `LXGraph.cpp:540`), `FindNode` × 2(선형, `:516`), `MeasureNode`를 호출한다.
- 이것만으로 O(L²·(P+N))다. 여기에 다음이 더해진다.
  - `:870` 핀마다 전체 링크 `any_of`: O(P·L)
  - `:910` 링크 그리기 루프의 `FindPin`/`Owner`: O(L·P)
  - `:835`, `:1154`, `:1168` 선택 목록 선형 `find`
- 링크 200, 핀 1천, 노드 100이면 프레임당 약 9천만 단계 연산으로, 수십 ms를 추정한다.
- 제안:
  - 그래프 리비전 단위로 인덱스를 만든다: 핀 ID → (노드, 핀), 노드 ID → (레이아웃, 측정 크기), 노드 × 측면별 포트 순서 목록.
  - 그리기는 이 인덱스로 O(L + P)에 끝낸다.
  - 선택 목록은 정렬 vector나 해시 집합으로.

### Lattice 그래프 검증·undo
- 링크 검증·컴파일 루프(`LXGraph.cpp:877`, `:934`, `:973`)가 링크마다 `FindPin`을 2회 호출해 O(L·P)다. ID 인덱스로 해결된다(`nextId`가 단조 증가하므로 정렬 vector도 가능).
- undo(`LXGraph.h:257` `Snapshot`, `:561`, `:2251`)는 편집마다 그래프 전체를 깊은 복사한다. 노드를 불변 블록으로 두는 구조 공유 스냅샷이나 역연산 기록으로 바꾼다.
- `Lattice/ImGui/LXStyle.h:162`~`:169`의 `std::map<Id/string, Style>`은 노드·핀·와이어마다 프레임 조회한다. 타입 스타일은 문자열 → 인덱스로 한 번만 해석한다.

### Scene 선택 목록
- `Engine/SceneRuntime/Scene.cpp:3614`, `:3624`의 `AddSelectedEntity`/`RemoveSelectedEntity`가 `m_selectedEntities` 선형 `find` + 중간 `erase`를 한다.
- 전체 선택(N=1만)이면 O(N²), 약 5천만 비교다.
- 제안: 순서 vector + 엔티티 인덱스 소속 비트셋(`DynamicBitset`). 제거는 지연 압축.

### 에디터 자산 DB
- `Editor/EngineEntry/EditorAssetDatabase.cpp:4304`~`:4325`의 `std::map<file::path, …>` 10개 이상: 정규화 경로 interning, 접두사 질의가 필요하면 경로 radix tree.
- `m_modifiedPaths`(`:1248`) debounce: 지연이 일정하므로 linked hash map으로 O(만료 수).

### 임포트·쿠킹
- `MaterialGraphSceneVolume.cpp:51` 용접: 비트 키 open addressing / 격자 공간 해시.
- `MeshLodBuilder.cpp:111` 간선: 64비트 키 정렬 후 스캔.
- `VertexWelding.cpp:134`, `TangentGeneration.cpp:282`, `FbxImporter.cpp:226`: open addressing 또는 meshoptimizer `generateVertexRemap`.

### 헤더 정적 집합 (정확성)
- `Engine/Utility_Framework/TypeTrait.h:250` `static std::set<HashedGuid> g_guids;`는 헤더의 namespace 범위 `static`이라 **포함하는 TU마다 별도 사본**이 생긴다.
- `GUIDCreator::MakeGUID`(`:318`)의 중복 검사와 `InsertGUID`/`EraseGUID`(Editor `GameObjectCommand.h:23`, `:25`, `:141`)가 서로 다른 집합을 볼 수 있어서, 의도한 중복 방지가 TU 경계에서 무력화된다. 동기화도 없다.
- 무작위 64비트 충돌 확률은 낮아 실제 영향은 작다. 그래도 단일 정의(`inline` 변수 또는 .cpp)로 고치거나 집합 자체를 제거해야 한다.

## 20. 엔진 안의 기존 도구 (재사용 우선)

| 도구 | 상태 | 재사용처 |
|---|---|---|
| `Engine/Utility_Framework/FixedPool.h` | O(1) free list + live 비트맵, 할당 없음. 사용처 1곳 | §3 slot map 저장소 |
| `Engine/Utility_Framework/DynamicBitset.h` | 사용처 16곳 | §5 `seen` 중복 검사, §14 피드백 비트셋, §19 선택 소속 |
| `Engine/RenderEngine/RenderArena.h` `render_pointer_set`, `render_arena_vector` | 프레임 수명 재사용 | §6, §10 태스크 벡터 |
| `ownership_cpp` `borrowed` / `local_owner` / `retirement_hook` | 존재 | §12 |
| `HashingString.h` | 해시 캐시일 뿐 interning이 아님 | §7 atom 테이블은 별도로 필요 |
| `RingBuffer.h`, `MemoryPool.h`, `LinkedListLib.hpp` | 각 사용처 1곳, 기능 제한 | 재사용 대신 새 상한 링·intrusive 리스트 정의 권장 |
| `ConcurrentQueue.h` | PPL `concurrent_queue` 별칭 | §10에서 용도별 링으로 교체 |

## 21. 이미 적절해서 바꾸지 않는 곳

| 지점 | 이유 |
|---|---|
| 엔티티 저장소, Vulkan 리소스 테이블 | slot + free 목록 |
| `Entity::GetComponent` / `HasComponent` | 타입 마스크 선판정 |
| 트랜스폼 계층(`Scene.cpp:595`) | 평탄화, 에폭·세대 dedup, 포화 시 전체 resolve |
| 에디터 하이어라키(`HierarchyFlatten.h`) | 리비전 기반 평탄 목록 + clipper. 실측 근거 주석 있음 |
| 물리 레이어(`PhysicsScene.cpp`) | 슬롯 배열, 센서 페어 슬롯 + active, 디스패처 링, 스냅샷 free-list |
| 애니메이션 키 탐색(`ClipSamplingCursor.h`) | 커서 힌트 + `lower_bound` 폴백 |
| 디스크립터 할당 | 원자 bump |
| 프로파일 마커·스레드 스트림, 로그 스냅샷 | thread-local 캐시, chunk pool, revision `IfChanged` |
| GCCE `block_allocator` | 크기 클래스 + 페이지 free list. `chunk_index_`는 디버그 전용 |
| Effect 정의·IR 검증, cooked manifest, `ModelStableKeys`, sidecar | 빌드·로드 시점. 결정적 순서 때문에 정렬 컨테이너 사용. 바꾸더라도 정렬 vector |
| `Engine/SceneRuntime/WorldState.h` `AIWorldState` | 사용처 0, 죽은 코드 |

## 22. B-tree 판정

- 정렬 + 대용량 + 장수명 맵: `CookedAssetCatalog::m_mountedEntries`, `ModelAssetGeneration::generations_` / `currentByAsset_`, `AssetIdentityRegistry::m_byInput` / `m_byUuid`, AssetDepot `*Entries` 맵, `DataSystem` model texture 맵, 에디터 `file::path` 맵.
- B-tree는 RB-tree 대비 메모리가 약 절반이고 지역성이 좋다. 하지만 로드·편집 시점 조회라 효과는 작다.
- AssetDepot 맵에서는 B-tree보다 §8.2의 intrusive LRU 효과가 훨씬 크다. 에디터 경로 맵에서는 interning이 더 크다.

## 23. 공용화 후보와 도입 원칙

- 외부 의존: boost는 걷어냈다(`vcpkg.json` `$comment`). 자체 구현을 기본으로 하고, 쓴다면 단일 헤더 MIT(`ankerl::unordered_dense`)까지만.
- `Engine/Utility_Framework` 추가 후보(기존 도구 §20 우선):
  - `slot_map<T>` (FixedPool 기반)
  - `sequence_buffer<T, N>`
  - `intrusive_lru<Index>`
  - `age_buckets`
  - `bounded_spsc_ring` / `bounded_mpsc_ring`
  - `flat_hash_map` + `identity_hash`
  - `string_atom` 테이블
  - `inplace_function<Sig, Size>`
  - TLSF
  - 패킹 키 `radix_sort`
- C++23 `std::flat_map`/`flat_set`은 현재 MSVC 툴체인 지원 여부를 먼저 확인한다.
- 결정적 출력이 필요한 경로(쿠킹·검증·LRU 이력·렌더 그래프 위상 순서·스프라이트 안정 정렬)는 교체 후에도 순서가 같도록 회귀 테스트로 고정한다.
