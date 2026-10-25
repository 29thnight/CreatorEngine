# 컨테이너·자료구조 최적화 분석 (2026-10-25)

기준: `master` `d86fe25e0` + 작업 트리(미커밋 변경 포함). 줄 번호는 작업 트리 기준이므로, 커밋된 리비전과 어긋날 수 있다.

성격: 소스 정적 분석에 근거한 **추정 연구 문서**다. 비용은 호출 빈도, 원소 규모(N), 할당 횟수, 복잡도, 메모리 접근 모델로 추정했다. 장면 규모 가정은 각 항목에 적었다. 비용 모델의 공통 전제는 다음과 같다.

| 연산 | 가정 비용 |
|---|---|
| MSVC `std::unordered_map` 조회 | FNV-1a 해시 + 버킷 리스트 노드 미스, 30~80ns |
| `std::map` 노드 삽입 | 할당 + 재균형, 50~100ns |
| 차가운 캐시 라인 원자 증감 | 40~100ns |
| 연속 배열 원소 | 수 ns |

## 0. 분석 범위

### 전수 분류
- `Engine/` 전체에서 연관 컨테이너(`map`/`set`/`multimap`/`unordered_*`)·`deque`·`priority_queue` 선언 약 470곳을 분류했다.
- 다음 비효율 패턴은 grep 휴리스틱으로 전체를 훑었다.
  - `vector` 중간 `erase` / 앞쪽 `insert`
  - `std::function` 컨테이너
  - 스냅샷 반환 함수

### 소스를 직접 읽은 영역

| 영역 | 읽은 범위 |
|---|---|
| RenderEngine | 씬 렌더러, MaterialGraph 호스트·룩업, 렌더 그래프, RenderScene, 프록시 큐, 클러스터 스트리밍, 폰트, DataSystem 캐시, RHI(영속 힙, 디스크립터, PSO, 텍스처·메시 캐시, 축출 정책, 퇴역 큐, 제출 스레드), DLSS 어댑터 |
| SceneRuntime | Scene, 물리 바인딩, Tag, BlackBoard, 폴리지, 트랜스폼 계층, 오디오 런타임, Entity 컴포넌트 조회 |
| Physics | `PhysicsScene`의 디스패처 큐·슬롯·센서 페어 |
| Effect | `EffectState` |
| EngineDiagnostics | 마커, 스레드 스트림, 집계, 작업 큐 |
| Utility_Framework | JobScheduler, ThreadPool, Arena, LogSystem |
| ThirdParty/GCCE | `block_allocator`, `domain` |
| ScriptRuntime (F#) | Registry |
| ScriptCore (C#) | Entity, ScriptRegistry, BlackBoard |
| Lattice/Core | LXGraph |
| Editor | 자산 DB, 썸네일 캐시, 콘솔 명령 큐 |

### 제외하거나 얕게 본 영역
| 영역 | 이유 |
|---|---|
| `BuildTool`(C#) | 오프라인 패키징 도구. 런타임 비용 없음 |
| `GameScripts` | 수용 프로브 스크립트. 제품 게임 코드가 아님. 할당 패턴만 확인(§16) |
| `Engine/Network` | 계약 인터페이스만 있음(`Network/Contracts`). 구현이 없어 대상 없음 |
| `Engine/Contracts`, `Engine/RuntimeHost`, `Player` | 플랫폼 창·명령 파싱·부트스트랩. 프레임 경로가 아님 |
| `Tools/`, `Dynamic_CPP`, GCCE 외 `ThirdParty` | 범위 밖 |
| `vector` 사용 약 2,910곳 | 개별 열람하지 않음. 위 휴리스틱으로 걸린 지점만 읽음 |
| Effect 정의·IR 검증, cooked manifest, 임포터 대부분 | 빌드·로드 시점 코드. 결정성 요구 확인 후 대상에서 뺌(§17) |

## 1. 결론 요약

- 비용이 큰 곳은 대부분 "STL 컨테이너 선택"보다 **접근 패턴과 맞지 않는 자료구조 계열**에서 생긴다.
  - 매 프레임 전수 순회와 정렬
  - 노드 기반 조회
  - 원자 refcount 복사
  - 선형 탐색 중첩
- 고빈도 비용의 최상위 추정은 다음 셋이다. 셋 다 장면 규모에 선형 이상으로 비례한다.
  - 폴리지 CPU 컬링: 0.4~4ms
  - 프록시 스냅샷 refcount: 0.5~3ms
  - GPU 자산 축출 전수 정렬: 0.2~1ms
- 순간 정지(hitch)로 가장 위험한 것은 **퇴역 큐 `vector` 루프 `erase`** 다. 씬을 언로드할 때 O(n·k)이고, 1만 엔트리라면 50~200ms로 추정한다.
- 정확성 문제를 2건 발견했다.
  - `TagManager`의 `string_view.data()` 조회: 범위 밖 읽기 가능성
  - 폰트 아틀라스: 글리프 축출이 없고 8페이지 상한이라 한글 대량 텍스트에서 수용이 실패할 수 있음
- B-tree가 맞는 곳은 로드 시점 카탈로그뿐이고 효과도 작다(§18).
- 이미 잘 설계된 곳은 §17에 정리했다. 엔티티 slot map, 컴포넌트 마스크, 트랜스폼 평탄화, 물리 레이어 슬롯·링 큐, 마커 캐시, 디스크립터 bump 할당이 여기에 해당한다.

## 2. 우선순위 표 (추정 비용순)

| 순위 | 지점 | 제안 계열 | 추정 비용(가정) | 절 |
|---|---|---|---|---|
| 1 | 퇴역 큐 `RHICompletionRetireQueue::Collect` | FIFO 링 / `erase_if` 한 패스 | 대량 언로드 시 50~200ms 1회 정지(1만 엔트리) | §9 |
| 2 | 폴리지 컬링 | 셀 격자/BVH + SoA + 캐시된 월드 AABB | 0.4~4ms/프레임(인스턴스 1만~10만) | §4, §5 |
| 3 | 프록시 스냅샷 | 에폭 회수 + raw 포인터 배열 / 청크 COW | 0.5~3ms/프레임(프록시 1만, 호출 4곳) | §12 |
| 4 | GPU 자산 축출 | 연령 버킷(timer wheel) / intrusive LRU | 0.2~1ms/프레임(상주 텍스처·메시 2천~1만) | §8 |
| 5 | 프레임 임시 set/map | 재사용 정렬 vector / open addressing / 고정 배열 | 0.05~0.5ms/프레임(고유 메시·스프라이트 수백~수천) | §6 |
| 6 | 폰트 아틀라스 | Skyline 패킹 + 페이지 LRU | 정확성: 한글 대량 텍스트에서 수용 실패 | §14 |
| 7 | TagManager | Interning + 투명 조회 | 정확성 + 조회마다 `std::string` 할당 | §7 |
| 8 | Lattice 그래프 | ID 인덱스 + 구조 공유 undo | 편집마다 1~3ms(링크 500·핀 2천), undo 메모리 O(편집 × 그래프) | §15 |
| 9 | 영속 힙 free-list | TLSF | 할당마다 1~3µs(mutex 안), 스트리밍 폭주 시 경합 | §10 |
| 10 | recording ID 키 `std::map` 7종 | Sequence buffer(링 + ID 태그) | 프레임마다 수십 회 노드 할당·조회, 0.01~0.05ms | §11 |
| 11 | 물리 바인딩 조회 | Slot map | 0.05~0.15ms/프레임(바디 1~2천) | §3 |
| 12 | C# BlackBoard / `GetComponents*` | Tagged union + key ID / 호출자 버퍼 | Set마다 박싱 1회 할당, 조회마다 `List` 할당 → GC 압력 | §16 |
| 13 | 미리 해시된 `uint64` 키 맵 | Identity hasher + open addressing | 조회마다 FNV 8바이트 재해시 낭비 | §13 |
| 14 | 클러스터 스트리밍 LRU | Intrusive LRU / CLOCK | 재구성마다 O(n log n) | §8 |
| 15 | 스레드 간 큐, MSVC deque | 상한 SPSC/MPSC 링 | 원소마다 노드 할당 + mutex | §10 |
| 16 | JobScheduler 태스크 | 태스크 객체 풀 + inplace function | 제출마다 `work` + `std::function` 2~3개 할당 | §10 |
| 17 | 오디오 보이스 제한 | 증분 카운터 + 인덱스 힙 | 제한 강제 시 O(V²), V=256이면 ~0.1ms | §14 |
| 18 | F# 스크립트 레지스트리 제거 | Mark-and-compact | 제거 경계마다 O(n·k) | §16 |
| 19 | Effect 슬롯 할당 | Free-list 스택 | 생성마다 O(n) 선형 탐색 | §3 |
| 20 | 에디터 경로 맵·debounce | 경로 interning + linked hash map / radix tree | 대량 파일 변경 시 틱마다 O(n log n · 경로 비교) | §14 |
| 21 | 임포트·쿠킹 해시 | 양자화 공간 해시 / 64비트 간선 키 | 임포트 시간 | §14 |

## 3. Slot map / generational index / free-list

ID → 객체 조회를 해시 대신 배열 인덱스 접근 한 번으로 바꾼다. 엔티티는 이미 이 패턴을 쓴다(`Engine/SceneRuntime/EntityHandle.h`).

| 지점 | 현재 | 관찰 |
|---|---|---|
| `Engine/SceneRuntime/Scene.cpp:2280` | `m_physicsBodies.find(pose.binding)` | `Physics.RenderInterpolation`에서 렌더 포즈마다 해시 조회 |
| `Engine/SceneRuntime/Scene.cpp:3328` | `m_physicsContactOwners.find` × 2 | 접촉 한 건마다 조회 2회 |
| `Engine/SceneRuntime/ScenePhysicsSimulation.h:173` | `m_entries`, `m_characters`, `m_handles`, `m_retiredContactHandles` | 같은 ID로 해시맵 4개 |
| `Engine/SceneRuntime/AnimationScheduler.h:72`, `AIManager.h:114`, `AnimatorSystem.h:205` | `unordered_map<id, …>` | 슬롯 페이지는 있는데 ID → 슬롯 매핑만 해시로 남은 경우도 있음 |
| `Engine/Effect/EffectState.cpp:64` | 빈 슬롯을 선형 탐색(`while (slots_[index].alive …) ++index`) | 생성마다 O(n). generation은 이미 있으므로 free-list 스택만 붙이면 O(1) |

- 원인: `binding_id`가 `++m_next`로 계속 증가한다(`ScenePhysicsSimulation.cpp:60`).
- 제안: `binding_id = (slot << 32) | generation`. 은퇴한 접촉 핸들은 generation 불일치로 자연스럽게 걸러진다.
- 물리 레이어 안쪽(`Engine/Physics/PhysicsScene.cpp:939` `body_slot`, `PhysicsSensorPairs.h:116`)은 이미 슬롯 구조다. 문제는 Scene 쪽 접착층에만 있다.

## 4. 공간 분할 (셀 격자 / BVH / 클러스터 계층)

- 엔진에 CPU 쪽 공간 구조가 없다. 메시는 GPU 컬링(`METEOR.Scene.Cull.First/Final`, HZB)이 처리한다.
- 폴리지는 `Engine/SceneRuntime/FoliageComponent.cpp:591`에서 인스턴스마다 `RebuildWorldMatrix()` → AABB 변환 → 프러스텀 판정을 매 프레임 반복한다. 인스턴스당 행렬 합성 약 20ns + AABB 변환 약 10ns + 판정 약 10ns로 보면 1만~10만 개에서 0.4~4ms다.

제안:
- 정적 폴리지를 셀 격자나 셀 단위 BVH로 묶는다.
  - 셀이 프러스텀 밖이면 셀 안의 인스턴스를 통째로 건너뛴다.
  - 셀이 완전히 안쪽이면 개별 판정 없이 전부 보이는 것으로 처리한다.
  - 보이는 셀만 개별 판정하므로 카메라 시야에 비례하는 비용으로 줄어든다.
- 월드 행렬과 월드 AABB는 배치·편집 시점에 한 번만 계산해 캐시한다.
- 같은 구조를 에디터 피킹, AI·오디오 근접 질의, 데칼·라이트 영향 범위 질의에 재사용할 수 있다.

## 5. SoA와 CSR

- 폴리지 SoA: 컬링 루프는 위치·바운드·플래그만 읽는데, 지금은 `FoliageInstance`(AoS)를 통째로 읽는다. center/extent와 컬링 비트를 분리하면 SIMD로 4~8개씩 판정할 수 있다. §4와 함께 적용하면 2~6배를 추정한다.
- CSR / counting sort:
  - `Engine/RenderEngine/PrimitiveRenderProxy.cpp:27` `instanceMap`(`unordered_map<uint32, vector<FoliageInstance*>>`): 키가 밀집된 타입 인덱스다. `offsets[]` + 평탄 배열 하나로 바꾸면 해시와 vector N개가 사라진다.
  - `Engine/RenderEngine/Render/Graph/EnhancedRenderGraph.cpp:2070`: 간선을 넣을 때마다 `none_of`로 선형 중복 검사를 한다(O(E·deg)).
    - 비트 인접 행렬로 중복을 검사하고, 간선은 CSR로 만든다.
    - `ready` `priority_queue<uint16_t>`는 "작은 인덱스 먼저"만 필요하다. 비트셋 + find-first-set으로 대체할 수 있다.

## 6. 프레임마다 다시 만드는 임시 컨테이너

| 지점 | 현재 | 제안 |
|---|---|---|
| `Engine/RenderEngine/Render/Scene/EnhancedSceneRenderer.cpp:5550` `activeMeshes` | 프록시 수만큼 `std::set` 삽입 | 재사용 vector + sort/unique |
| `EnhancedSceneRenderer.cpp:5645` `modelPinIndices` / `meshPinIndices` | 빌드마다 `std::map` 2개 | 재사용 open addressing 또는 정렬 vector |
| `EnhancedSceneRenderer.cpp:6040`, `:6131` `currentViews` / `currentSceneViews` | `std::set`, 상한 `kMaxLiveCameraViews` | 고정 배열에서 선형 탐색 |
| `Engine/RenderEngine/Render/Scene/EnhancedTemporalViewState.h:21`, `:28` `pendingSprites` / `pendingDecals` | 프레임마다 `clear` 후 원소마다 노드 할당 | 정렬 vector 2개 swap + `previous` 이진 탐색 |
| `Engine/RenderEngine/MaterialGraphSceneHost.cpp:1856` `sources` | draw마다 `std::map` 삽입 | 재사용 해시 |
| `MaterialGraphSceneHost.cpp:710` `packetBindings`, `Engine/RenderEngine/MaterialGraphSceneGpuData.h:53` `materialOffsets` | 포인터 키 `std::map` | 원래 주소 순서라 비결정적이다. 해시로 바꿔도 잃는 것이 없다 |

- 이 용도의 도구가 이미 있다: `Engine/RenderEngine/RenderArena.h:33` `render_pointer_set`.
- `arena_map`(`Engine/Utility_Framework/Arena.h:373`)은 할당 비용만 줄인다. RB-tree 포인터 추적과 캐시 미스는 그대로 남는다.

## 7. String interning / 투명 조회 / perfect hash

| 지점 | 문제 | 제안 |
|---|---|---|
| `Engine/SceneRuntime/TagManager.cpp:162`, `:176`, `:191`, `:202`, `:223` | `m_tagMap.find(tag.data())`: 조회마다 `std::string` 할당. **`string_view.data()`는 널 종료가 보장되지 않아 범위 밖을 읽을 수 있다** | 투명 해시(`is_transparent`)로 `string_view` 직접 조회 |
| `TagManager::m_taggedObjects` | 태그 ID가 있는데도 다시 문자열로 키잉 | `vector<vector<Entity*>>[tagId]`, 제거는 swap-remove + 위치 인덱스 |
| `Engine/SceneRuntime/BlackBoard.h:59` | AI 틱 경로에서 `unordered_map<string, …>`(호출처 21곳) | Interned atom ID 키 |
| `Engine/RenderEngine/Material.cpp:756` `m_cbufferValues` | cbuffer 이름 문자열 조회 | 레이아웃 생성 시 슬롯 인덱스로 미리 해석 |
| `Engine/SceneRuntime/ComponentFactory.h:22`, `Engine/RenderEngine/RenderState.h:28`, `Player/PlayerCommands.cpp:782` | 키 집합이 고정됨 | 정적 perfect hash 또는 interned ID 표 |
| `Engine/RenderEngine/DataSystem.cpp:3479` 등 `Materials.find(std::string(name))` | 경로 문자열 키 캐시, 조회 시 임시 문자열 생성 | 투명 조회 + 경로 interning(FileGuid 키로 일원화) |
| `Engine/RenderEngine/RHI/DX12/DX12DescriptorHeaps.h:349` `m_rangeCache` | `map<vector<uint64_t>>`: 조회마다 키 vector 할당 + 사전순 비교 log n회 | 합성 해시 조회 + 정확 비교, 키는 inline small vector |

## 8. 축출 정책: 연령 버킷 / intrusive LRU / CLOCK

| 지점 | 현재 | 제안 |
|---|---|---|
| `Engine/RenderEngine/RHI/DX12/EnhancedSceneRendererLiveDX12Adapter.cpp:1909` → `DX12TextureCache::RetireUnused`, `DX12MeshCache::RetireUnused` → `Engine/RenderEngine/RHI/RHIAssetEvictionPolicy.cpp:46` | **매 프레임** 상주 엔트리 전체를 후보 vector로 복사하고 `std::sort` | `lastUsedFrame` 기준 연령 버킷(timer wheel) |
| `Engine/RenderEngine/Assets/ClusterStreaming.h:373` | 재구성마다 `m_lastUse` 기준 `stable_sort` | 그룹 배열에 prev/next를 넣은 intrusive LRU. touch O(1), 축출 O(k). 결정성 유지 |
| `Editor/EngineGUIWindow/BrowserThumbnailCache.cpp` | `lastUsedFrame` 기록 후 전수 스캔 | 같은 intrusive LRU |

연령 버킷 방식의 동작:
- touch할 때 해당 엔트리를 현재 버킷으로 옮긴다.
- 축출은 임계 연령을 넘은 버킷만 비운다.
- 압박 모드의 정렬 기준(오래된 순 → 큰 순 → ID)은 버킷 안에서만 적용한다.
- 축출이 없는 프레임은 O(1)이다.

세 곳의 패턴이 같으므로 공용 `intrusive_lru<Index>` 하나로 묶을 수 있다.

## 9. 퇴역 큐: FIFO 링 (hitch 위험 1순위)

- `Engine/RenderEngine/RHI/RHICompletionRetireQueue.h:55` `Collect`가 `std::vector<Entry> m_entries`를 순회하며 회수 가능한 원소마다 `it = m_entries.erase(it)`를 한다. 원소마다 뒤쪽 전체를 시프트하므로 O(n·k)다.
- 사용처는 매 프레임 실행된다: `DX12TextureCache`, `DX12MeshCache`, `DX12PSOManager`, `VulkanPipelineCache`, `VulkanRenderServices`.
- 평소에는 n이 작다. 하지만 씬을 언로드하거나 메모리 압박으로 대량 축출하면 n과 k가 함께 커진다. 1만 엔트리 중 절반을 회수하면 원소 이동 약 5천만 회(엔트리 32~64B)로, 한 프레임에 50~200ms가 걸린다고 추정한다.
- 제안:
  - 즉시 할 수 있는 것: `std::erase_if` 한 패스로 O(n).
  - 구조적 해결: completion 값이 제출 순으로 단조 증가하므로 FIFO 링 버퍼에서 앞쪽만 pop하면 O(k)다. 순서가 어긋나는 quarantine 경로가 있다면 그 원소만 별도 리스트로 분리한다.
- 같은 패턴(루프 안 `vector::erase`)은 다음에도 있다. 원소 수가 작아 순위는 낮지만, 같은 방식으로 정리할 수 있다.
  - `Engine/RenderEngine/RHI/RHISubmissionThread.cpp:1261`
  - `Engine/RenderEngine/RHI/ScreenSizedResource.h:111`, `:157`
  - `Engine/RenderEngine/RHI/DX12/EnhancedIBLGenerator.cpp:937`
  - `Engine/RenderEngine/MaterialGraphSceneLookup.cpp:923` `published_.erase(begin)`(vector 앞쪽 삭제)

## 10. 할당기·큐·태스크

### TLSF
- `Engine/RenderEngine/RHI/RHIPersistentHeapPolicy.h:136`: 세그먼트마다 `freeByOffset`(map), `freeBySize`(multimap), `allocatedByOffset`(map)을 둔다. 할당마다 mutex를 잡은 채 모든 호환 세그먼트에서 `lower_bound`를 하고 노드를 2~3개 할당·해제한다.
- TLSF(2단계 크기 클래스 비트맵 + 블록 헤더의 물리 이웃 링크)로 바꾸면 탐색·병합 모두 O(1)이고 노드 할당이 없다. 세그먼트별 비트맵 요약으로 후보 세그먼트도 바로 걸러낼 수 있다.

### MSVC deque와 스레드 간 큐
MSVC `std::deque`는 원소가 8바이트를 넘으면 블록 하나에 원소 하나라서, 원소 수만큼 노드를 할당한다.

| 지점 | 현재 | 제안 |
|---|---|---|
| `Engine/EngineDiagnostics/ProfileService.h:467` `m_workQueue` | mutex + deque, 상한 있음 | 상한 MPSC 링 |
| `Engine/RenderEngine/Assets/ClusterStreaming.h:87` `m_queue` | `deque<std::function<void()>>` | 링 + 고정 크기 SBO 태스크 |
| `Engine/RenderEngine/RHI/RHISubmissionThread.cpp:183` `failures` | deque | 링 버퍼 |
| `Engine/RenderEngine/ProxyCommandQueue.h:389` `m_deferredCommands` | deque, 소비 스레드 전용 | vector 두 개 swap |
| `Engine/Effect/EffectState.h:33` `bursts_` | deque, 상한 `EffectPendingBurstLimit` | 고정 링 |
| `Engine/CommandService/OperationTable.h:95` `m_records` | deque, 상한 `kMaxRecords` | 고정 링 |
| `EnhancedSceneRenderer.cpp:2555` `renderQueue` | deque, 용량 1 | `std::optional` |
| `Editor/EngineEntry/ConsoleCommandSystem.h:323`, `Player/PlayerCommands.h:99` | deque | vector swap / 링 |

참고: `Engine/Physics/PhysicsScene.cpp:345` PhysX 디스패처 큐는 이미 고정 링 버퍼다.

### JobScheduler 태스크 객체
- `Engine/Utility_Framework/JobScheduler.h:57`: `job_group`이 `vector<std::function<void()>>`를 들고, 완료 관찰자도 `std::function`이다.
- `ThreadPool.cpp:65`의 `work`는 enkiTS `ITaskSet` 파생 객체로, 제출마다 `std::function` 2개와 mutex를 담아 할당된다.
- 렌더 그래프, 씬 렌더러, 애니메이션 스케줄러, DataSystem이 매 프레임 제출하므로, 프레임당 제출 수 × (객체 1 + 캡처가 큰 `std::function` 1~3)만큼 할당이 생긴다.
- 제안:
  - `work` 객체를 풀링한다(free-list).
  - 태스크 본체는 inplace function(고정 SBO)으로 바꾼다.
  - 그룹 태스크 벡터는 프레임 arena(`render_arena_vector`)에 둔다.
- 하위 스케줄러(enkiTS)는 이미 락프리라 교체 대상이 아니다.

## 11. Sequence buffer (단조 증가 ID + 상한 있는 창)

recording/frame ID는 단조 증가하고 in-flight 개수는 작다. 그런데도 `std::map`으로 들고 있다.

| 지점 | 비고 |
|---|---|
| `Engine/RenderEngine/GpuGeometryVisibility.h:329` `m_recordings` | |
| `Engine/RenderEngine/MaterialGraphResidentBuffer.h:456` `recordings_` | |
| `Engine/RenderEngine/MaterialGraphSceneHost.h:357` `recordings_` | 순회 삭제 2곳(`:3933`, `:5049`) |
| `Engine/RenderEngine/MaterialGraphSceneLookup.h:139` `submitted_` + `:140` `acceptedRecordings_` | 128개 상한을 넘으면 `begin()`부터 삭제 |
| `Engine/RenderEngine/MaterialGraphSceneLookup.cpp:250`, `MaterialGraphSceneRuntimeEffects.cpp:314` `uploads` | |
| `Engine/RenderEngine/MaterialGraphSceneRuntimeEffects.h:79` `depthBindings_` | |
| `Engine/RenderEngine/RHI/Temporal/DlssTemporalAdapter.cpp:232` `frames`(`unordered_map<uint64_t, Frame>`) | |

- 제안: `slot = id % N` 링 배열에 `id`를 태그로 저장한다(네트코드의 sequence buffer와 같은 구조).
  - 조회: 태그 비교 1회
  - 삽입: 슬롯 덮어쓰기
  - 가장 오래된 것 축출: 자동
  - 노드 할당 0
- `acceptedRecordings_`처럼 함께 다니는 set은 같은 슬롯의 플래그 비트로 합친다.

## 12. 영속 / copy-on-write 구조와 에폭 회수

- `Engine/RenderEngine/RenderScene.cpp:91` `GetPrimitiveProxySnapshot`: 맵 노드를 따라가며 `shared_owner`를 복사한다(원자 증가). 해제할 때 같은 수만큼 원자 감소가 일어나고, 호출처는 4곳이다.
  - 프록시 1만 개라면 프레임당 원자 연산 약 8만 회다. refcount 라인이 차가우면 0.5~3ms로 추정한다.
- 대안:
  - 에폭 회수: 렌더 프레임 에폭이 끝날 때까지 프록시 해제를 미룬다. 스냅샷은 raw 포인터 배열(memcpy 수준)이 된다.
  - 청크 COW 배열: 바뀐 청크만 새로 만든다.
  - 저장소 `ProxyMap`(노드 해시)을 밀집 배열 + 인덱스 맵으로 바꾸면 순회가 연속 메모리에서 일어난다.
- 에디터 undo(`OperationTable`)와 Lattice undo(§15)는 HAMT나 RRB vector 같은 영속 구조로 구조를 공유할 수 있다.

## 13. 해시 품질과 identity hasher

- MSVC `std::hash<uint64_t>`는 identity가 아니라 8바이트 FNV-1a다. 키가 이미 해시값인 맵은 조회마다 다시 해시한다. 해당 맵:
  - `DX12PSOManager::m_cache`
  - `DX12RootSignatureCache::m_cache`
  - `VulkanPipelineCache::m_layoutByHash` / `m_pipelineByHash`
  - `DX12DescriptorHeaps::m_cache`
  - `DX12PersistentHeap::m_segments`
- 제안: identity(또는 단일 곱셈 mix) 해셔 + open addressing(`unordered_dense` 계열)으로 노드 간접 참조까지 없앤다.
- 렌더 그래프 `freeList`·`allocationCache`(`EnhancedRenderGraph.h:177`, `:231`)도 프레임마다 조회하므로 같은 대상이다. 7워드 키는 XOR 결합 대신 일괄 해시(wyhash, xxh3)가 분포와 속도 모두 낫다.
- `RHIDeviceMemoryBudgetCoordinator.h:124`~`:136`의 `unordered_map<Domain/Owner, …>`: 도메인·소유자가 작은 정수 ID라면 평탄 배열로 충분하다.

## 14. 기타 영역

### 폰트 — skyline 패킹 + 페이지 LRU (정확성 포함)
- `Engine/RenderEngine/FontAsset.cpp:346`: "현재 줄 하나"만 쓰는 shelf 패킹이다. 줄이 바뀌면 이전 줄의 남은 공간을 다시 쓰지 않는다. 글리프 축출이 없고 페이지는 8개 상한이다.
- 한글 텍스트는 서로 다른 글리프가 수천 개 나올 수 있어서 아틀라스가 고갈되고 수용이 실패할 수 있다. 성능보다 기능 위험이다.
- 제안: skyline(bottom-left) 패킹 + 페이지 단위 LRU 재사용. 글리프 캐시는 open addressing으로.

### 오디오 — 증분 카운터 + 인덱스 힙
- `Engine/SceneRuntime/Audio/AudioRuntime.cpp:282` `PhysicalCount`가 전 보이스를 순회한다. `EnforceLimits`(`:773`)는 `while (PhysicalCount(bus) > cap)` 안에서 `FindVictim`(전수 순회)을 반복해 O(V²)다.
- 제안: 버스·그룹별 증분 카운터, 그리고 위치 역참조가 있는 4진 인덱스 힙으로 희생자를 O(log V)에 고른다. 버스 ID 키 맵은 평탄 배열로.

### 에디터 자산 DB — 경로 interning / linked hash map / radix tree
- `Editor/EngineEntry/EditorAssetDatabase.cpp:4304`~`:4325`: `std::map<file::path, …>`가 10개 이상 있다. `filesystem::path` 비교는 구성요소를 파싱하며 비교해서 비싸다. 정규화한 경로를 interned ID로 키잉하고, 접두사 질의가 필요하면 경로 radix tree를 쓴다.
- `m_modifiedPaths`(`:1248`) debounce: 지연이 일정(350ms)하므로 삽입 순서가 곧 만료 순서다. linked hash map으로 바꾸면 만료 처리가 O(만료 수)다.

### 임포트·쿠킹
- `Engine/RenderEngine/MaterialGraphSceneVolume.cpp:51` `map<array<float,3>>` 용접: 정확 일치라면 비트 키 + open addressing, 허용 오차가 있다면 격자 공간 해시.
- `Engine/RenderEngine/Experiment/Import/MeshLodBuilder.cpp:111` 경계 간선 `map<pair>`: 64비트 키로 정렬 후 스캔.
- `VertexWelding.cpp:134`, `TangentGeneration.cpp:282`, `FbxImporter.cpp:226`: open addressing, 또는 meshoptimizer `generateVertexRemap`.

### 클러스터 스트리밍 소규모
- `Engine/RenderEngine/Assets/ClusterStreaming.h:406` `m_feedback`(`set<uint32_t>`, 밀집 인덱스): 비트셋으로 중복을 거른 뒤 vector에 담는다.
- `:175`의 `m_pending.insert(m_pending.begin(), …)`는 앞쪽 삽입이다. 피드백을 뒤에 붙이고 우선순위 구분을 인덱스로 하면 이동이 없어진다.
- `:418` `m_reads`(상한 있음): 작은 flat vector.

## 15. Lattice 노드 그래프

- `Lattice/Core/LXGraph.cpp:540` `FindPin`은 모든 노드의 모든 핀을 선형 탐색하고, `FindNode`(`:516`)도 선형이다.
  - 링크 검증·컴파일 루프(`:877`, `:934`, `:973` 등)가 링크마다 `FindPin`을 2회 호출하므로 O(L·P)다.
  - 링크 500·핀 2천이면 검증 한 번에 비교 약 200만 회, 편집마다 1~3ms로 추정한다.
  - 제안: Id → (노드, 핀 인덱스) 해시 인덱스를 편집 트랜잭션 단위로 유지한다. `nextId`가 단조 증가하므로 정렬 vector 이진 탐색도 가능하다.
- undo(`LXGraph.h:257` `Snapshot`)는 편집마다 `nodes`·`links`·`layout`·`groups` 전체를 깊은 복사한다(`:561`, `:2251`, 상한 `kUndoLimit`).
  - 메모리가 O(편집 수 × 그래프 크기)다.
  - 제안: 노드를 불변 `shared_owner` 블록으로 두고 바뀐 노드만 교체하는 구조 공유 스냅샷(영속 vector), 또는 역연산 명령 기록.

## 16. 관리 코드 (C#, F#)

- `ScriptCore/BlackBoard.cs:30`
  - 문제: `Value(BlackBoardType, object boxed)`라서 `SetFloat`/`SetInt`/`SetBool`마다 박싱 할당이 생긴다. 키는 문자열 `Dictionary`다.
  - 제안: 명시적 레이아웃 tagged union struct(float/int/bool 공용 필드 + 참조 필드 하나)로 박싱을 없앤다. 키는 interned ID나 사전 해석 핸들로 바꾼다. 네이티브 `BlackBoard`(§7)와 함께 바꾸면 경계 변환도 줄어든다.
- `ScriptCore/Entity.cs:103` `GetComponentsInChildren`, `ScriptCore/ScriptRegistry.cs:41` `FindAll`
  - 문제: 호출마다 새 `List<T>`를 할당한다. 계층 순회는 자식마다 네이티브 호출(`Native.GetChild`)을 하고 `Entity` 래퍼를 만든다.
  - 제안: 호출자 버퍼를 받는 오버로드(`void GetComponentsInChildren<T>(List<T> into)`)와 풀링 리스트를 제공한다. 계층은 네이티브에서 평탄 배열 한 번으로 받는다.
- `ScriptRuntime/Registry.fs:221`
  - 문제: 제거 대상마다 `active.Remove`를 호출해 O(n·k)다.
  - 제안: 실행 순서를 보존하는 mark-and-compact(`RemoveAll` 한 번)로 O(n). 순서가 무관한 경로라면 위치 인덱스 sparse set.
- `GameScripts`: 수용 프로브라서 `new List`/`HashSet` 할당 13곳은 제품 비용이 아니다. 다만 위 API의 할당 패턴이 사용자 게임 코드로 그대로 옮겨질 수 있으므로, API 쪽에서 막는 것이 맞다.

## 17. 이미 적절해서 바꾸지 않는 곳

| 지점 | 이유 |
|---|---|
| 엔티티 저장소(`EntityHandle`) | slot map |
| `Entity::GetComponent` / `HasComponent`(`Entity.inl:39`, `:138`) | 타입 마스크로 거른 뒤 선형 탐색. 실측 근거 주석 있음 |
| 트랜스폼 계층(`Scene.cpp:595`) | `execToEntity` 평탄화, 에폭·세대 dedup, 포화 시 전체 resolve |
| 물리 레이어(`PhysicsScene.cpp`) | 슬롯 배열, 센서 페어 슬롯 + active 목록, 디스패처 링 버퍼, tick 스냅샷 free-list |
| 디스크립터 할당 | 원자 bump |
| 프로파일 마커(`ProfileMarker.cpp:89`) | thread-local 고정 캐시 + 컴파일 타임 마커 |
| 프로파일 스레드 스트림 | chunk pool. 청크 단위라 락 빈도가 낮음 |
| `ProfileAggregate`의 해시 | 오프라인 집계 |
| 로그 스냅샷 | revision 기반 `IfChanged`로 변경 없을 때 복사 없음 |
| `render_pointer_set`, `render_arena_vector`, 클러스터 페이지 마스크 | 프레임 수명 재사용 |
| GCCE `block_allocator` | 크기 클래스 + 페이지 free list. `chunk_index_`(map)는 `GC_DEBUG_CHECKS` 전용 `find_block`에서만 쓰임 |
| Effect 정의·IR 검증, cooked manifest, `ModelStableKeys`, sidecar | 빌드·로드 시점. 결정적 출력 순서 때문에 정렬 컨테이너를 씀. 바꾸더라도 해시가 아니라 정렬 vector |
| `Engine/SceneRuntime/WorldState.h` `AIWorldState` | 사용처 0, 죽은 코드 |

## 18. B-tree 판정

- 정렬 순서가 필요하고, 크기가 크고, 오래 유지되는 맵: `CookedAssetCatalog::m_mountedEntries`, `ModelAssetGeneration::generations_` / `currentByAsset_`, `AssetIdentityRegistry::m_byInput` / `m_byUuid`, `DataSystem`의 model texture 맵, 에디터 자산 DB의 `file::path` 맵.
- B-tree는 RB-tree 대비 메모리가 약 절반이고 조회 지역성이 좋다. 하지만 로드·편집 시점 조회라 효과는 작다.
- 에디터 경로 맵은 B-tree보다 interning(§14)의 효과가 더 크다.
- 순서 요구가 없는 것이 확인되면 해시로 간다.

## 19. 공용화 후보와 도입 원칙

- 외부 의존: boost는 이미 걷어냈다(`vcpkg.json` `$comment`). 자체 구현을 기본으로 하고, 쓴다면 단일 헤더 MIT(`ankerl::unordered_dense`) 정도까지만.
- `Engine/Utility_Framework` 공용화 후보:
  - `slot_map<T>` (generational, free-list)
  - `sequence_buffer<T, N>`
  - `intrusive_lru<Index>`
  - `age_buckets` (timer wheel)
  - `spsc_ring` / `mpsc_ring`
  - `flat_hash_map` + `identity_hash`
  - `string_atom` 테이블
  - `inplace_function<Sig, Size>`
  - TLSF 할당기
- C++23 `std::flat_map`/`flat_set`은 현재 MSVC 툴체인 지원 여부를 먼저 확인한다.
- 결정적 출력이 필요한 경로(쿠킹·검증·LRU 이력·렌더 그래프 위상 순서)는 교체 후에도 순서가 같도록 회귀 테스트로 고정한다.
