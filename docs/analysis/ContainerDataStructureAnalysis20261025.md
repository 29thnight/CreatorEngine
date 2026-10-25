# 컨테이너·자료구조 최적화 분석 (2026-10-25)

기준: `master` `d86fe25e0` + 작업 트리(미커밋 변경 포함). 줄 번호는 작업 트리 기준이므로, 커밋된 리비전과 어긋날 수 있다.

범위: `Engine/`, `ThirdParty/GCCE`, `ScriptRuntime/`(F#), `Editor/`.

방법: 연관 컨테이너·deque·해시 선언 약 470곳을 전수 분류했다. 그중 프레임·틱마다 실행되는 경로와 상주 캐시는 소스를 직접 읽어 접근 패턴을 확인했다.

**주의 — 측정 없음.** 이 문서의 기대 효과는 모두 접근 패턴에서 추정한 값이다. 적용 전에 `ProfileMarker`/`profile_scope`로 해당 구간 비용을 먼저 측정해서 실제 병목인지 확인한다.

## 1. 결론 요약

- STL 컨테이너를 그대로 다른 STL 컨테이너로 바꾸는 것보다, **접근 패턴에 맞는 자료구조 계열**로 바꿀 때 얻는 것이 크다.
  - 계열: slot map, 공간 분할, SoA, CSR, intrusive LRU, 연령 버킷, interning, 링 큐, TLSF, skyline 패킹, 영속 구조.
- B-tree(정렬 + 대용량 + 장수명)가 맞는 지점은 자산 로드 시점의 카탈로그류뿐이다. 이 지점들도 순서가 필요 없다면 해시가 낫다. 우선순위는 낮다.
- 이미 잘 설계되어 손댈 필요가 없는 곳도 많다. 엔티티 slot map, 컴포넌트 타입 마스크, 트랜스폼 계층 평탄화, 디스크립터 bump 할당, render arena, GC 블록 할당기가 그렇다(§13).
- 분석 중 **정확성 문제 1건**을 발견했다: `TagManager`의 `string_view.data()` 조회(§7).

## 2. 우선순위 표

| 순위 | 지점 | 현재 | 제안 계열 | 빈도 | 근거 |
|---|---|---|---|---|---|
| 1 | 물리 바인딩 조회 | `unordered_map<binding_id, …>` 다수 | Slot map (generational index) | 렌더 프레임 × 바디 수, 접촉 × 2 | §3 |
| 2 | 폴리지 컬링 | 인스턴스마다 행렬 재구성 + AABB 변환 + 프러스텀 판정 | 셀 격자/BVH + SoA | 프레임 × 인스턴스 수 | §4, §5 |
| 3 | GPU 자산 축출 | 매 프레임 전 상주 자산 후보 수집 + `std::sort` | 연령 버킷(timer wheel) / intrusive LRU | 프레임 × 상주 텍스처·메시 수 | §8 |
| 4 | 프록시 스냅샷 | 프록시마다 `shared_owner` 복사(원자 refcount) × 호출 4곳 | 에폭 회수 + raw 포인터 배열 / 청크 COW | 프레임 × 프록시 수 × 4 | §11 |
| 5 | 프레임 임시 set/map | 프레임마다 노드 할당하는 `std::set`/`map` | 재사용 정렬 vector / open addressing / 고정 배열 | 프레임 × 메시·스프라이트 수 | §6 |
| 6 | 영속 힙 free-list | 세그먼트마다 `map` + `multimap` + `map` | TLSF (2단계 비트맵) | 할당·해제마다(mutex 안) | §9 |
| 7 | TagManager | 문자열 키 이중 조회 + 조회마다 `std::string` 생성 | Interning + 투명 조회 (**버그 수정 포함**) | 태그 질의마다 | §7 |
| 8 | 클러스터 스트리밍 LRU | 재구성마다 `stable_sort(m_lastUse)` | Intrusive LRU / CLOCK | 스트리밍 재구성마다 | §8 |
| 9 | 스레드 간 큐 | mutex + MSVC `std::deque` | 상한 SPSC/MPSC 링 큐 | 작업 투입마다 | §10 |
| 10 | 오디오 보이스 제한 | `PhysicalCount`가 전수 순회, `EnforceLimits`는 O(V²) | 증분 카운터 + 인덱스 힙 | 보이스 시작·제한 강제 시 | §12 |
| 11 | 스크립트 레지스트리 제거 | `active.Remove`를 원소마다 호출, O(n·k) | Mark-and-compact 한 패스 / sparse set | 스크립트 제거 경계마다 | §12 |
| 12 | 폰트 아틀라스 | 단일 shelf 패킹, 글리프 축출 없음, 페이지 8개 상한 | Skyline 패킹 + 페이지 단위 LRU | 신규 글리프마다(한글에서 심함) | §12 |
| 13 | 에디터 경로 맵 | `std::map<file::path, …>` 다수, 매 틱 debounce 전수 스캔 | 경로 interning + linked hash map | 파일 변경 폭주 시 | §12 |
| 14 | 임포트·쿠킹 해시 | `map<array<float,3>>`, `map<pair>` 간선 | 양자화 공간 해시 / 64비트 간선 키 정렬 | 임포트 시 | §12 |

## 3. Slot map / generational index

ID를 키로 하는 해시 조회를 배열 인덱스 접근 한 번으로 바꾼다. 엔티티는 이미 이 패턴을 쓰고 있다(`Engine/SceneRuntime/EntityHandle.h`).

| 지점 | 현재 | 관찰 |
|---|---|---|
| `Engine/SceneRuntime/Scene.cpp:2280` | `m_physicsBodies.find(pose.binding)` | `Physics.RenderInterpolation`에서 렌더 포즈마다 해시 조회 |
| `Engine/SceneRuntime/Scene.cpp:3328` | `m_physicsContactOwners.find` × 2 | 접촉 한 건마다 조회 2회 |
| `Engine/SceneRuntime/ScenePhysicsSimulation.h:173` | `m_entries`, `m_characters`, `m_handles`, `m_retiredContactHandles` | 같은 ID로 해시맵 4개 |
| `Engine/SceneRuntime/AnimationScheduler.h:72` | `unordered_map<size_t, Animator*>` | |
| `Engine/SceneRuntime/AIManager.h:114` | `unordered_map<uint64_t, IAIComponent*>` | |
| `Engine/SceneRuntime/AnimatorSystem.h:205` | `controllerSlotById` | 슬롯 페이지는 이미 있음. ID → 슬롯 매핑만 해시로 남아 있음 |

- 원인: `binding_id`가 `++m_next`로 계속 증가하는 값이다(`ScenePhysicsSimulation.cpp:60`). 그래서 배열 인덱스로 쓸 수 없다.
- 제안: `binding_id = (slot << 32) | generation`으로 바꾼다. Scene 쪽 `m_physicsBodies` 등은 같은 slot을 인덱스로 하는 병렬 배열로 바꿀 수 있다.
- 은퇴한 접촉 핸들(`m_retiredContactHandles`)은 generation 불일치로 자연스럽게 걸러진다.

## 4. 공간 분할 (셀 격자 / BVH / 클러스터 계층)

- 엔진에 CPU 쪽 공간 구조가 없다. 메시는 GPU 컬링(`METEOR.Scene.Cull.First/Final`, HZB)이 처리한다.
- 폴리지는 `Engine/SceneRuntime/FoliageComponent.cpp:591` `UpdateFoliageCullingData`가 인스턴스마다 다음을 수행한다.
  - `RebuildWorldMatrix()`
  - 바운드 → 월드 AABB 변환
  - 프러스텀 교차 판정

제안:
- 정적 폴리지를 셀 격자나 셀 단위 BVH로 묶는다. 셀 AABB가 프러스텀 밖이면 셀 안의 인스턴스를 통째로 건너뛴다. 셀 AABB가 완전히 안쪽이면 인스턴스 판정 없이 전부 보이는 것으로 처리한다.
- 월드 행렬과 월드 AABB는 배치·편집 시점에 한 번만 계산해 캐시한다. 지금은 매 프레임 다시 계산한다.
- 같은 구조를 에디터 피킹, AI·오디오 근접 질의, 데칼·라이트 영향 범위 질의에 재사용할 수 있다.

## 5. SoA (hot/cold 분리)와 CSR

- 폴리지 SoA: 컬링 루프는 위치·바운드·컬링 플래그만 읽는데, 지금은 `FoliageInstance`(AoS)를 통째로 읽는다.
  - center/extent와 컬링 비트를 별도 배열로 분리하면 SIMD로 4~8개씩 판정할 수 있다. §4와 함께 적용할 때 효과가 크다.
- CSR / counting sort:
  - `Engine/RenderEngine/PrimitiveRenderProxy.cpp:27` `instanceMap`(`unordered_map<uint32, vector<FoliageInstance*>>`): 키가 밀집된 타입 인덱스이므로 `offsets[]` + 평탄 배열 하나로 바꿀 수 있다. 해시와 vector N개가 사라진다.
  - `Engine/RenderEngine/Render/Graph/EnhancedRenderGraph.cpp:2070`: 간선을 넣을 때마다 `none_of`로 선형 중복 검사를 한다(O(E·deg)).
    - 패스 수가 작으므로 비트 인접 행렬로 중복을 검사하고, 간선은 CSR로 만든다.
    - `ready` `priority_queue<uint16_t>`는 "가장 작은 인덱스 먼저"만 필요하다. 비트셋 + find-first-set으로 대체할 수 있다.

## 6. 프레임마다 다시 만드는 임시 컨테이너

| 지점 | 현재 | 제안 |
|---|---|---|
| `Engine/RenderEngine/Render/Scene/EnhancedSceneRenderer.cpp:5550` `activeMeshes` | 프록시 수만큼 `std::set` 삽입 | 재사용 vector에 넣고 sort + unique |
| `EnhancedSceneRenderer.cpp:5645` `modelPinIndices` / `meshPinIndices` | 빌드마다 `std::map` 2개 | 재사용 open addressing 맵 또는 정렬 vector |
| `EnhancedSceneRenderer.cpp:6040`, `:6131` `currentViews` / `currentSceneViews` | `std::set`, 상한 `kMaxLiveCameraViews` | 고정 배열에서 선형 탐색 |
| `Engine/RenderEngine/Render/Scene/EnhancedTemporalViewState.h:21`, `:28` `pendingSprites` / `pendingDecals` | 프레임마다 `clear` 후 스프라이트·데칼마다 노드 할당 | 정렬 vector 2개를 swap하고 `previous`는 이진 탐색 |
| `Engine/RenderEngine/MaterialGraphSceneHost.cpp:1856` `sources` | draw마다 `std::map` 삽입 | 재사용 해시 |
| `MaterialGraphSceneHost.cpp:710` `packetBindings`, `Engine/RenderEngine/MaterialGraphSceneGpuData.h:53` `materialOffsets` | 포인터 키 `std::map` | 순서가 주소 순서라 원래 비결정적이다. 해시로 바꿔도 잃는 것이 없다 |

- 이 용도의 도구가 이미 `Engine/RenderEngine/RenderArena.h:33` `render_pointer_set`(노드·락이 없는 재사용 집합)으로 있다.
- `arena_map`(`Engine/Utility_Framework/Arena.h:373`)은 할당 비용만 줄인다. 내부는 RB-tree 그대로라 포인터 추적과 캐시 미스는 남는다.

## 7. String interning / 투명 조회 / perfect hash

| 지점 | 문제 | 제안 |
|---|---|---|
| `Engine/SceneRuntime/TagManager.cpp:162`, `:176`, `:191`, `:202`, `:223` | `m_tagMap.find(tag.data())`: `string_view`로 조회마다 `std::string`을 할당한다. **`string_view.data()`는 널 종료가 보장되지 않아 범위 밖을 읽을 수 있다** | 투명 해시(`is_transparent`)로 `string_view`를 직접 조회 |
| `TagManager::m_taggedObjects` | 태그 ID(`m_tagMap` 값)가 있는데도 다시 문자열로 키잉 | `vector<vector<Entity*>>[tagId]`, 제거는 swap-remove + 엔티티 쪽 위치 인덱스 |
| `Engine/SceneRuntime/BlackBoard.h:59` | AI 틱 경로에서 `unordered_map<string, …>` 조회(호출처 21곳) | 키를 interned atom ID로, 값은 작은 정렬 배열 또는 ID 인덱스 배열 |
| `Engine/RenderEngine/Material.cpp:756` `m_cbufferValues` | cbuffer 이름 문자열 조회 | 레이아웃 생성 시 슬롯 인덱스로 미리 해석 |
| `Engine/SceneRuntime/ComponentFactory.h:22`, `Engine/RenderEngine/RenderState.h:28` | 런타임에 키 집합이 고정됨 | 정적 perfect hash 또는 interned ID 표 |
| `Engine/RenderEngine/RHI/DX12/DX12DescriptorHeaps.h:349` `m_rangeCache` | `map<vector<uint64_t>>`: 조회마다 키 vector를 힙에 할당하고 사전순 비교를 log n번 | 합성 해시로 조회 + 정확 비교, 키는 inline small vector |

## 8. 축출 정책: 연령 버킷 / intrusive LRU / CLOCK

| 지점 | 현재 | 제안 |
|---|---|---|
| `Engine/RenderEngine/RHI/DX12/EnhancedSceneRendererLiveDX12Adapter.cpp:1909` → `DX12TextureCache::RetireUnused`, `DX12MeshCache::RetireUnused` → `Engine/RenderEngine/RHI/RHIAssetEvictionPolicy.cpp:46` | **매 프레임** 전 상주 엔트리를 후보 vector로 복사하고, 해당되는 것을 `std::sort` | `lastUsedFrame` 기준 **연령 버킷(timer wheel)**: touch할 때 버킷을 옮기고, 축출은 임계 연령을 넘은 버킷만 비운다. 압박 모드 정렬(오래된 순 → 큰 순 → ID)은 버킷 안에서만 하면 된다. 축출이 없는 프레임은 O(1) |
| `Engine/RenderEngine/Assets/ClusterStreaming.h:373` | 재구성마다 `retained`를 `m_lastUse` 기준 `stable_sort` | 그룹 배열에 prev/next 인덱스를 넣은 intrusive LRU. touch O(1), 축출 O(k). "같은 LRU 이력" 결정성은 그대로 유지 |
| `Editor/EngineGUIWindow/BrowserThumbnailCache.cpp` | `lastUsedFrame` 기록 후 전수 스캔으로 축출 | 같은 intrusive LRU(hash + 리스트) |

이 세 곳의 패턴이 같으므로, 공용 `intrusive_lru<Index>` 하나로 묶을 수 있다.

## 9. 할당기: TLSF

- `Engine/RenderEngine/RHI/RHIPersistentHeapPolicy.h:136`: 세그먼트마다 `freeByOffset`(map), `freeBySize`(multimap), `allocatedByOffset`(map)을 둔다.
- 할당할 때마다 mutex를 잡은 채 모든 호환 세그먼트에서 `lower_bound`를 하고, 노드를 2~3개 할당·해제한다.
- 제안: TLSF(2단계 크기 클래스 비트맵 + 블록 헤더 배열의 물리 이웃 링크)로 바꾼다. 크기 탐색은 O(1), 병합도 O(1)이고 노드 할당이 없다. 세그먼트를 고를 때도 세그먼트별 비트맵 요약으로 후보를 바로 걸러낼 수 있다.
- 참고: 디스크립터 할당(`DX12DescriptorRecycler::Allocate`, `DX12TargetViewHeap::Allocate`)은 원자 bump 방식이라 이미 최적이다.

## 10. 스레드 간 큐와 MSVC deque

MSVC `std::deque`는 원소가 8바이트를 넘으면 블록 하나에 원소 하나라서, 원소 수만큼 노드를 할당한다.

| 지점 | 현재 | 제안 |
|---|---|---|
| `Engine/EngineDiagnostics/ProfileService.h:467` `m_workQueue` | mutex + deque, 상한 `kMaximumQueuedCounterWork` | 상한 MPSC 링 큐 |
| `Engine/RenderEngine/Assets/ClusterStreaming.h:87` `m_queue` | `deque<std::function<void()>>` | 링 큐 + 고정 크기 SBO 태스크(`std::function` 할당 제거) |
| `Engine/RenderEngine/RHI/RHISubmissionThread.cpp:183` `failures` | deque | 링 버퍼 |
| `Engine/RenderEngine/ProxyCommandQueue.h:389` `m_deferredCommands` | deque, 렌더 소비 스레드 전용 | vector 두 개 swap 또는 링 버퍼 |
| `Engine/Effect/EffectState.h:33` `bursts_` | deque, 상한 `EffectPendingBurstLimit` | 고정 링 버퍼 |
| `Engine/CommandService/OperationTable.h:95` `m_records` | deque, 상한 `kMaxRecords` | 고정 링 버퍼 |
| `EnhancedSceneRenderer.cpp:2555` `renderQueue` | deque, `kRenderQueueCapacity = 1` | `std::optional<FrameSubmission>` |
| `Editor/EngineEntry/ConsoleCommandSystem.h:323` `m_editorMutations` | deque, swap 후 drain | vector 두 개 swap |

## 11. 영속 / copy-on-write 구조

- `Engine/RenderEngine/RenderScene.cpp:91` `GetPrimitiveProxySnapshot`: 맵 노드를 따라가며 `shared_owner`를 하나씩 복사한다(원자 증가). 스냅샷이 해제될 때 같은 수만큼 원자 감소가 일어난다. 호출처는 4곳이다.
  - 대안 1: 렌더 프레임 에폭이 끝날 때까지 프록시 해제를 미루는 에폭 회수. 이렇게 하면 스냅샷은 raw 포인터 배열로 충분하다.
  - 대안 2: 청크 단위 COW 배열. 바뀐 청크만 새로 만들고 나머지는 공유한다.
  - 저장소 자체도 `ProxyMap`(노드 해시)을 밀집 배열 + 인덱스 맵으로 바꾸면 순회가 연속 메모리에서 일어난다.
- 에디터 undo(`OperationTable`)와 씬 스냅샷이 커지면 HAMT나 RRB vector 같은 영속 구조가 구조 공유로 메모리와 복사 비용을 줄인다. 불변성 원칙과도 맞는다.

## 12. 기타 영역별 관찰

### 오디오 — 증분 카운터 + 인덱스 힙
- `Engine/SceneRuntime/Audio/AudioRuntime.cpp:282` `PhysicalCount`가 호출될 때마다 전 보이스를 순회한다.
- `EnforceLimits`(`:773`)는 `while (PhysicalCount(bus) > cap)` 루프 안에서 `FindVictim`(역시 전수 순회)을 반복한다. 버스마다 O(V²)이다.
- 제안: 버스·동시성 그룹별 물리 보이스 수를 증분 카운터로 유지하고, 희생자 선택은 그룹별 인덱스 힙(위치 역참조가 있는 이진/4진 힙)으로 O(log V)에 처리한다.
- `m_busLimits`·`m_busVolumes`(`unordered_map<uint16_t, …>`)는 버스 ID가 작은 정수라면 평탄 배열로 충분하다.

### 스크립트 런타임 (F#) — mark-and-compact
- `ScriptRuntime/Registry.fs:221`: 제거 경계에서 대기 중인 원소마다 `active.Remove(script)`를 호출한다(ResizeArray 선형 탐색 + 시프트). O(n·k)이다.
- 실행 순서를 보존해야 하므로 swap-remove 대신 mark-and-compact를 쓴다: 제거 대상을 표시한 뒤 `RemoveAll` 한 번으로 압축하면 O(n).
- 순서가 중요하지 않은 경로라면 위치 인덱스를 둔 sparse set으로 O(1) 제거가 가능하다.

### 폰트 — skyline 패킹 + 페이지 LRU
- `Engine/RenderEngine/FontAsset.cpp:346`: 패킹이 "현재 줄 하나"뿐인 shelf 방식이다(`cursorX/cursorY/rowHeight`). 줄이 바뀌면 이전 줄의 남은 공간을 다시 쓰지 않는다. 글리프 축출이 없고 페이지는 8개 상한이다.
- 한글처럼 글리프 종류가 많은 텍스트에서는 아틀라스가 빨리 찬다.
- 제안: skyline(bottom-left) 패킹으로 점유율을 높이고, 페이지 단위 LRU로 오래 안 쓴 페이지를 재사용한다.
- 글리프 캐시 `unordered_map<uint64_t, CachedGlyph>`는 open addressing 해시가 유리하다.

### 렌더 그래프 transient 풀
- `Engine/RenderEngine/Render/Graph/EnhancedRenderGraph.h:177` `freeList`(`unordered_map<uint64_t, vector<Entry>>`), `:231` `allocationCache`(`unordered_map<array<uint64_t,7>>`).
- 키 수가 작고 프레임마다 조회하므로 open addressing 해시(`unordered_dense` 계열)로 바꾸면 노드 간접 참조가 사라진다.
- `AllocationKeyHash`의 XOR 결합은 무난하다. 다만 7워드 키라면 wyhash나 xxh3 같은 일괄 해시가 분포와 속도 모두 낫다.

### 에디터 자산 DB — 경로 interning / linked hash map / radix tree
- `Editor/EngineEntry/EditorAssetDatabase.cpp:4304`~`:4325`: `std::map<file::path, …>`가 10개 이상 있다. `filesystem::path` 비교는 경로 구성요소를 파싱하며 비교하므로 비싸다.
  - 경로를 정규화한 뒤 interned ID(또는 GUID)로 키잉한다.
  - 디렉터리 하위 무효화처럼 접두사 질의가 필요하면 경로 radix tree(trie)를 쓴다.
- `m_modifiedPaths`(`:1248`) debounce: 틱마다 전체를 스캔한다. 지연(350ms)이 일정하므로 삽입 순서가 곧 만료 순서다. linked hash map(hash + 삽입순 리스트, 갱신 시 꼬리로 이동)으로 바꾸면 만료 처리가 O(만료 수)가 된다. git checkout 같은 대량 변경 때 효과가 크다.

### 임포트·쿠킹 시간
- `Engine/RenderEngine/MaterialGraphSceneVolume.cpp:51` `map<array<float,3>, uint32_t>` 용접: 정확 일치라면 비트 패턴 키 + open addressing, 허용 오차 용접이라면 격자 공간 해시.
- `Engine/RenderEngine/Experiment/Import/MeshLodBuilder.cpp:111` `map<pair<uint32_t,uint32_t>, uint32_t>` 경계 간선: `(min<<32)|max` 64비트 키를 정렬 후 스캔하거나 open addressing.
- `VertexWelding.cpp:134`, `TangentGeneration.cpp:282`, `FbxImporter.cpp:226`의 `unordered_map` 조회 테이블은 open addressing으로 바꿀 수 있다. meshoptimizer의 `generateVertexRemap`으로 대체할 수도 있다.

### 기타 소규모 지점
- `Engine/RenderEngine/Assets/ClusterStreaming.h:406` `m_feedback`(`set<uint32_t>`, 밀집 인덱스): 비트셋으로 중복을 거르고 vector에 담는다. `m_pendingGroups` 비트셋을 재사용할 수 있다.
- `ClusterStreaming.h:418` `m_reads`(상한 `kMaximumPendingReads`): 작은 flat vector.

## 13. 이미 적절해서 바꾸지 않는 곳

| 지점 | 이유 |
|---|---|
| 엔티티 저장소(`EntityHandle`, `m_generations` / `m_freeSlots`) | 이미 slot map |
| `Entity::GetComponent` / `HasComponent`(`Entity.inl:39`, `:138`) | 타입 마스크로 먼저 거른 뒤 선형 탐색. 실측 근거 주석 있음 |
| 트랜스폼 계층(`Scene.cpp:595`) | `execToEntity` 평탄화, 에폭·세대 스탬프 dedup, 포화 시 전체 resolve로 전환 |
| 디스크립터 할당 | 원자 bump |
| `render_pointer_set`, `render_arena_vector` | 프레임 수명 재사용 |
| 클러스터 페이지 membership(`m_wantedPages`) | 이미 평탄 마스크 |
| GCCE `block_allocator` | 크기 클래스 + 페이지 free list. `chunk_index_`(`std::map`)는 `find_block`에서만 쓰이고, `find_block`은 `GC_DEBUG_CHECKS` 전용이라 대상이 아니다 |
| Effect 정의·IR 검증, cooked manifest, `ModelStableKeys`, 각종 sidecar | 빌드·로드 시점 코드. 결정적 출력 순서 때문에 정렬 컨테이너를 쓴다. 바꾸더라도 해시가 아니라 정렬 vector여야 한다 |
| `Engine/SceneRuntime/WorldState.h` `AIWorldState` | 사용처 0, 죽은 코드 |

## 14. B-tree 판정

- 정렬 순서가 필요하고, 크기가 크고, 오래 유지되는 맵: `CookedAssetCatalog::m_mountedEntries`, `ModelAssetGeneration::generations_` / `currentByAsset_`, `AssetIdentityRegistry::m_byInput` / `m_byUuid`, `DataSystem`의 model texture 맵.
- 이 지점들에서 B-tree는 RB-tree 대비 메모리와 조회 지역성이 낫다. 하지만 모두 로드 시점 조회라 체감 효과는 작을 것으로 본다.
- 순서 요구가 없는 것이 확인되면 B-tree보다 해시로 바꾸는 편이 낫다.

## 15. 도입 방식 메모

- 외부 의존: boost는 이미 걷어냈다(`vcpkg.json` `$comment`). 새 구조는 자체 구현을 기본으로 한다. 쓴다면 단일 헤더 MIT(`ankerl::unordered_dense`) 정도까지만.
- 공용화 후보(`Engine/Utility_Framework`):
  - `slot_map<T>` (generational)
  - `intrusive_lru<Index>`
  - `age_buckets` (timer wheel)
  - 상한 `spsc_ring` / `mpsc_ring`
  - open addressing `flat_hash_map`
  - `string_atom` 테이블
- C++23 `std::flat_map`/`flat_set`은 현재 MSVC 툴체인의 지원 여부를 먼저 확인한다.
- 적용 순서: 각 항목마다 해당 프로파일 마커로 기준선을 잡고 → 교체 → 같은 시나리오로 재측정한다. 결정적 출력이 필요한 경로는 순서 보존 여부를 회귀 테스트로 고정한다.
