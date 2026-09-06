# 물리 재설계 — 컴포넌트·백엔드·스레딩 전면 재작성 (PHASE 19)

수립일: 2026-08-18 · 근거: 같은 날 물리 계층 전수 조사
관련: [SceneGraphRedesignPlan](archive/SceneGraphRedesignPlan.md)(트랙 S 순서 제약) · [SerializationPlan](SerializationPlan.md)(마이그레이션) · [EngineLayerSeparationPlan](EngineLayerSeparationPlan.md)(계층 침범) · [NetworkFrameworkPlan](NetworkFrameworkPlan.md)(고정 Simulation Tick·prediction 경계)

---

## 0. 전략 요약

물리 컴포넌트와 물리 관련 구조를 **기존 설계를 남기지 않고 전면 재작성**한다.

세 문장으로 요약하면:

1. **지혈이 먼저다.** 지금 코드에 확정 결함 다섯이 있고, 그중 하나(AI 스레드 데이터 레이스)는 재현이 어려운 크래시로 이미 나타나고 있을 것이다. 재작성 일정과 무관하게 먼저 닫는다.
2. **백엔드 교체(PhysX → Jolt)는 게이트로 다룬다.** 래퍼 5,979줄을 어차피 새로 쓰므로 교체의 한계비용이 지금 가장 낮다. 다만 "GPU 규모 바디 수가 목표인가"라는 미해결 변수가 하나 있으므로, 말이 아니라 스파이크 벤치로 판정한다.
3. **병렬화의 선결조건은 스레드가 아니라 자료구조다.** `unordered_map` 순회 + 원소마다 `dynamic_cast` + 원소마다 `GetComponent` 위에서는 어떤 스레딩 전략도 못 얹는다. 평탄 스토어가 먼저다.

**이 계획이 뒤집는 것:** 소유권 4분할 · 오브젝트당 콜라이더 1개 제약 · 전량 푸시/풀 · 핫패스 RTTI · 콜라이더의 RigidBody 필수 의존 · PhysX 헤더의 엔진 전역 전이.

**이 계획이 지키는 것:** 씬·프리팹의 기존 콜라이더 파라미터(마찰·반발·밀도·크기), C# 스크립트 표면의 의미론, 레이어 충돌 매트릭스의 저작 결과.

---

## 1. 실측 — 2026-08-18 전수 조사

### 1.1 자산 지도

| 계층 | 위치 | 규모 | 내용 |
|---|---|---|---|
| L1 백엔드 래퍼 | `Physics/` (독립 vcxproj) | **5,979줄** | `PhysicX` 싱글턴(`Physx.cpp` 2,597줄 단일 파일), RigidBody 3종, CCT+Movement, Ragdoll 3종, Resource 3종, EventCallback |
| L2 컴포넌트 | `ScriptBinder/` | **~3,100줄** | `PhysicsManager` 싱글턴(267+1,100), 컴포넌트 8종 |
| L2.5 소유권 | `Scene.cpp:1525-1810`, `2147-2231` | ~370줄 | 콜라이더 7개 병렬 벡터 + `m_colliderContainer` + 생성 콜백표 |
| L3 스크립트 경계 | `ScriptCore/*.cs` + `ClrHost.cpp` | 374줄 + API 45개 | C# 표면 |

합계 **약 9,500줄**. PhysX 5.5.0 (vcpkg).

### 1.2 확정 결함 — 재설계와 무관하게 지금 값이 있는 것

**Z-①  AI 스레드 ↔ 물리 데이터 레이스 (CRITICAL)**

`Scene.cpp:1271`이 `std::async`로 AI를 띄우고, `Scene.cpp:1135`가 다음 프레임에 `wait_for(0s) == ready`일 때만 회수한다. **안 끝났으면 기다리지 않고 물리 스텝으로 진행한다.** 그런데 BT 액션이 AI 스레드에서 물리를 직접 만진다:

- `ChaseAction` · `Idle` · `WaitAction` → `CharacterControllerComponent::Move()`
- `DamegeAction` → `StopForcedMove()` → `Physics->StopForcedMoveOnCCT()` (`CharacterControllerComponent.cpp:391`) — PhysicX의 `m_characterControllerContainer`를 직접 변경
- `MageActtack` → `RigidBodyComponent` 접근

메인 스레드는 같은 시각 `PhysicX::Update`에서 그 컨테이너를 순회하며 `simulate()`를 돌린다. `sceneDesc.flags`에 `eREQUIRE_RW_LOCK`은 **없다**. 보호되지 않은 레이스다.

**Z-②  `CollisionData` 누수**

`new CollisionData()` 4곳(`Physx.cpp:359, 957, 995, 1656`). `delete`는 **생성 실패 경로에만** 있다(`:964`, `:1002`). 성공 경로는 `RemoveCollisionData`가 id를 목록에 넣고 `Update`가 맵에서 `erase`만 한다 — 해제 없음. 바디마다 하나씩, 씬 전환마다 누적.

**Z-③  `GetRigidBodyData` static 분기 널 역참조**

`Physx.cpp:1124` — `staticBody` 분기 안에서 `dynamicBody->GetScale()`. 그 시점 `dynamicBody`는 반드시 `nullptr`이다. 현재는 `GetPhysicData`가 DYNAMIC만 호출해 안 터지고 있을 뿐인 지뢰.

**Z-④  CUDA 실패 시 폴백 부재**

`Physx.cpp:199-210`이 CUDA 초기화 실패 시 `m_cudaContextManager = nullptr`로 두고 *"continuing without CUDA"* 로그만 찍는데, `:244-251`의 `eENABLE_GPU_DYNAMICS` + `broadPhaseType = eGPU` 플래그는 **그대로 남는다**. 그리고 `createScene` 반환값을 널 검사 없이 `PxCreateControllerManager(*m_scene)`에 역참조한다. 폴백이라 부를 수 없다.

**Z-⑤  솔버 워커 8 하드캡**

`Physx.cpp:165-172`:

```cpp
UINT MaxThread = 8;
UINT core = std::thread::hardware_concurrency();
if (core < 4) core = 2;
else if (core > MaxThread + 4) core = MaxThread;   // 12 초과면 무조건 8
else core -= 4;
```

32스레드 워크스테이션에서도 8개만 쓴다.

**Z-⑥  `AngularDamping`이 직렬화되지 않는다**

`RigidBodyComponent::reflect()`에 `LinearDamping`은 있는데 `AngularDamping`이 **빠져 있다**. 인스펙터에서 각 감쇠를 조정해도 저장되지 않고 로드 시 기본값(0.05)으로 되돌아온다. 한 줄 누락이고 저작 데이터가 지금 조용히 유실되고 있다.

### 1.3 구조적 결함 — 재작성이 뒤집어야 할 것

| # | 결함 | 근거 |
|---|---|---|
| S-1 | **소유권 4분할.** PhysX 액터=`PhysicX::m_rigidBodyContainer`, 컴포넌트=Scene 7개 병렬 벡터, 등록표=`Scene::m_colliderContainer`, 변경 큐=`PhysicsManager::m_pendingChanges`. 파괴 단일점 없음 — `bIsDestroyed`를 세 군데서 각자 검사 | `Scene.h:420-432` |
| S-2 | **ID = GameObject InstanceID → 오브젝트당 콜라이더 1개.** `m_ColliderTypeLinkCallback`이 `unordered_map<GameObject*, ...>`이고 `insert`라 두 번째 콜라이더가 조용히 무시된다 | `Scene.h:420`, `Scene.cpp:1566` |
| S-3 | **전량 푸시/풀.** `SetRigidBodyData`가 매 프레임 모든 바디에 `setActorFlag`×2, `setRigidBodyFlag`, 셰이프 순회 + `setFlag`×2, `setLinearVelocity/Angular`를 **조건 없이** 호출. `isDirty`는 mass/damping/lock 묶음에만 걸림. `getShapes`용 `std::vector` 힙 할당이 바디마다 프레임마다 | `Physx.cpp:1156-1284`, 할당 `:1206` |
| S-4 | **핫패스 RTTI.** `GetRigidBodyData`·`SetRigidBodyData`·`PhysicX::Update` 전부 `dynamic_cast<DynamicRigidBody*>`로 분기 | `Physx.cpp:1058, 1180, 330` |
| S-5 | **RigidBody 없는 콜라이더는 죽는다.** `SetPhysicData`가 `rigidbody == nullptr`이면 `continue`. "콜라이더만 = 정적 지오메트리" 개념이 없어 바닥 하나에도 RigidBody가 필요 | `PhysicsManager.cpp:742` |
| S-6 | **CCT가 RigidBodyComponent에 기생.** CCT 속도를 `rigidbody->GetLinearVelocity()`로 읽고 되돌려 쓴다. CCT는 리지드바디가 아니다 | `PhysicsManager.cpp:795, 924` |
| S-7 | **충돌 매트릭스 2중 진실.** `PhysicsManager`는 `vector<vector<uint8_t>>`(32×32), `PhysicX`는 `unsigned int[32]`. setter 시점에만 동기화, 레이어 수 32 하드코딩 | `PhysicsManager.h:145`, `Physx.h:188` |
| S-8 | **계층 침범.** ScriptBinder 12개 헤더가 `../Physics/Physx.h` / `PhysicsCommon.h`를 직접 include → 유니티 빌드에서 PhysX 헤더가 엔진 전역에 전이 | 12개 파일 |
| S-9 | **싱글턴 접근자가 헤더의 `static auto`.** `static auto Physics = PhysicX::GetInstance();`(Physx.h 말미), `static auto PhysicsManagers = ...`(PhysicsManager.h 말미) — TU마다 사본 | 헤더 말미 |
| S-10 | **생성이 3단계 지연.** `Awake` → `Scene::CollectColliderComponent`(정보만 채우고 람다 저장) → 다음 `FixedUpdate`의 `SetInternalPhysicData`가 람다 호출. `PhysicsManager::AddCollider`는 이름과 달리 아무것도 만들지 않고 오프셋만 계산 | `PhysicsManager.cpp:430` |
| S-12 | **되쓰기가 분해→합성→재분해를 돈다.** `GetPhysicData`가 scale·quat·pos로 행렬을 조립(`CreateScale`×`CreateFromQuaternion`×`CreateTranslation`, 곱 2회)한 뒤 `SetAndDecomposeMatrix`에 넘기고, 그 안에서 행렬 비교(16 float) → `XMMatrixDecompose`(축마다 sqrt) → `XMVector4Normalize` → 부모 조회를 한다. **이미 분해된 형태로 들고 있던 데이터를 옮기려고** 동적 바디마다 프레임마다. 물리는 스케일을 바꾸지 않으므로 스케일은 왕복할 이유조차 없다 | `PhysicsManager.cpp:955-962`, `Transform.cpp:364` |
| S-11 | **주석 소실.** `PhysicsManager.cpp`·`Physx.cpp` 상당 부분이 이중 mojibake(`占쏙옙`)로 **복구 불가**. 기존 의도를 주석에서 읽어낼 수 없다 | — |

부수: `OnUnloadScene`이 언로드 중에 `Physics->Update(1.0f)` — **1초짜리 스텝을 한 번 시뮬레이션**한다(`PhysicsManager.cpp:107`).

### 1.4 죽은 자산

| 자산 | 상태 |
|---|---|
| `ColliderDebugData.h` | 참조 0. 유일한 함수가 static 아니면 `throw`로 끝남 |
| `PhysicsDebug.cpp` | **0바이트** |
| `RagdollComponent` | 필드 1개, `ICollider` override 전부 빈 몸통. 리플렉션·Lifecycle에는 등록됨 |
| `Physics/Ragdoll*` 3종 (736줄) | 컴포넌트가 비어 있어 도달 불가. `PxArticulation*` 15회 사용처가 전부 여기 |
| `TerrainCollider` 경로 | `SetPhysicData`가 heightField id면 데이터 읽고 즉시 `continue` |
| `TriangleMeshResource` | `CreateStaticBody(TriangleMesh…)` 호출자 0 |
| GPU/CUDA 경로 | 켜져 있으나 실측된 적 없음. Z-④ 참조 |
| `eENABLE_ACTIVE_ACTORS` | 켜두고 활성 액터 목록을 안 씀(전수 순회 유지) |
| `PxDeformableSurface` | 코드 0줄. **주석 5곳에만** 존재 |

### 1.5 병렬화 가능성 — 층별 판정

| 층 | 백엔드 지원 | 현재 | 재작성 후 |
|---|---|---|---|
| ① 솔버 내부 병렬 | ○ | **켜져 있음**, 워커 8 하드캡 | ○ 즉시 (Z-⑤) |
| ② GPU 다이내믹스 | PhysX ○ / Jolt ✕ | 켜놓고 폴백 없음 | 백엔드 판정에 종속 |
| ③ simulate↔게임로직 오버랩 | PhysX ○ / Jolt은 잡 모델 | **전혀 안 씀** | 백엔드에 따라 형태가 다름 |
| ④ 씬 쿼리 병렬 | ○ (락 정책 필요) | 안 씀 | ○ |
| ⑤ 동기화 루프 병렬 | — | **자료구조가 막음** | ○ (트랙 B 선결) |
| ⑥ 외부 스레드 접근 | ○ (락 필요) | **락 없이 하고 있음** ⚠ | 계약으로 봉인 (T0) |

③에 대한 주의: 현재 `simulate()`와 `fetchResults(true)` 사이에 아무것도 없어(`Physx.cpp:442-451`) 메인 스레드가 시뮬레이션 내내 논다. 그런데 **Jolt에는 이 분할이 없다** — `PhysicsSystem::Update()`는 잡 시스템 위에서 내부 병렬로 돌지만 호출은 동기다. 백엔드 선택이 프레임 구조 설계를 바꾼다.

### 1.6 소비자 표면 — 재작성이 깨뜨릴 것

- **C++ 게임 스크립트**: `GetComponent<CharacterControllerComponent>` 56, `RigidBodyComponent` 17, Sphere/Box 콜라이더 9 → **82곳**
- **PhysicsManager 직접 호출**: `SphereOverlap` 16, `BoxSweep` 3, `CapsuleSweep` 1, 기타 4 → **24곳**
- **C# 경계**: Native 테이블 물리 항목 45개 (Cct 17 · Rigid 21 · Collider 12 · 질의 3)
- **직렬화**: 컴포넌트 8종이 `RegisterReflectManual.h`·`ComponentTypeUUID.h`에 등록, 기존 `.creator` 씬/프리팹이 이 필드명으로 저장됨

`Colliders.cs`가 이미 *"트리거 여부와 콜라이더 켜고 끄기는 여기가 아니라 RigidBodyComponent에 있다(엔진 구조가 그렇다)"* 고 기형을 주석으로 기록해 두었다 — 재설계에서 정상화할 지점.

---

## 2. 백엔드 판정 — PhysX 존치 vs Jolt 전환

### 2.1 실사용 표면

PhysX에 실제로 도달하는 지오메트리: Box 4 · Sphere 4 · Capsule 4 · ConvexMesh 4 · TriangleMesh 2 · HeightField 2 · Plane 1(주석).

**안 쓰는 것**: 차량 0 · 클로스 0 · 일반 조인트 0 · 디포머블 0(주석뿐). `PxArticulation` 15회는 전부 죽은 랙돌 경로.

즉 **PhysX의 차별점을 하나도 쓰고 있지 않다.** 남는 것은 강체 + 캐릭터 + 쿼리 + 접촉 이벤트 + 레이어 필터로, Jolt이 100% 커버한다. `joltphysics`는 vcpkg 포트에 존재한다.

### 2.2 Jolt 평가

**얻는 것** — 1.3의 구조적 결함 중 다섯이 라이브러리 차원에서 소멸한다:

| 결함 | Jolt에서는 |
|---|---|
| S-4 핫패스 `dynamic_cast` | `BodyID`(인덱스+세대) 단일 핸들. 타입 분기 없음 |
| S-1·⑤ 맵 순회 → 병렬 불가 | `BodyID`가 곧 배열 인덱스. 평탄 스토어가 기본 구조 |
| Z-② `CollisionData` 누수 | `ContactListener` 콜백이 값으로 넘어옴. 관리할 수명 없음 |
| S-2 오브젝트당 콜라이더 1개 | `StaticCompoundShape`/`MutableCompoundShape`가 일급 |
| S-7 매트릭스 2중 진실 | `ObjectLayerPairFilter` + `BroadPhaseLayer` 2단 필터 |

추가로 S-6(CCT 기생)은 `CharacterVirtual`이 게임플레이 주도 캐릭터 전제라 자연히 풀린다. 크로스플랫폼 결정론이 설계 목표(리플레이·네트워크 계획이 있으면 큼). CUDA 의존 소멸로 Z-④가 통째로 사라진다. MIT.

**포기하는 것**:

1. **GPU 다이내믹스** — Jolt은 CPU 전용. 다만 현재 GPU 경로는 실측된 적 없고 폴백도 없어, 실재하는 자산이 아니라 "켜둔 플래그"다. **목표 바디 수가 수천 이상이면 진짜 손실** — 이것이 유일한 결정 변수다.
2. **PVD** — PhysX Visual Debugger는 좋고 이 프로젝트가 쓴다(`ConnectPVD`, `DrawPVDLine`). Jolt은 `DebugRenderer`+JoltViewer로 덜 매끄럽다. 다만 콜라이더 디버그 드로우는 어차피 새로 만들어야 한다(`ColliderDebugData`는 죽은 코드).
3. **`simulate`/`fetchResults` 분할** — 1.5 ③ 참조. 손해가 아니라 다른 설계다.

### 2.3 결정 규칙 (게이트)

말로 정하지 않는다. **X0 스파이크가 판정한다.**

- 동일 시나리오(`Dynamic_CPP/Assets/Scenes/PhysicsDrop.creator` — 바닥 + 큐브 낙하)를 바디 수 100 / 1,000 / 10,000으로 올리며 프레임 시간 측정
- 비교 대상: (a) 현 PhysX CPU 경로, (b) 현 PhysX GPU 경로(동작한다면), (c) Jolt 단독 콘솔
- **판정**: 목표 바디 수에서 Jolt이 (a) 이상이고, (b)가 실제로 동작하지 않거나 목표 규모에서 우위가 없으면 → **Jolt 전환**. (b)가 목표 규모에서 유의미하게 우세하면 → **PhysX 존치**, 나머지 트랙은 그대로 진행

**두 백엔드 동시 유지는 하지 않는다.** 추상화 비용이 이득을 먹는다. 트랙 A가 어차피 불투명 핸들 경계를 세우므로 나중에 바꿀 여지는 남는다.

> **미결정 · 사용자 답변 필요:** 목표하는 동시 강체 수. 수백~2천이면 Jolt이 명백히 유리하고, 수만이면 X0 결과를 봐야 한다.

---

## 3. 목표 구조

> **상세 설계: [PhysicsComponentDesign.md](../design/PhysicsComponentDesign.md)** — 레인 모델, 커맨드 파이프, 핫/콜드 스토어, 이벤트 스트림, 컴포넌트 표면, 비용 대조표. 아래는 요약이다.

한 문장: **컴포넌트는 저작값과 핸들만 들고, 런타임 상태는 물리 월드의 평탄 스토어가 소유한다. 동기화는 바디 타입별 단방향 레인으로 갈리고, 변경은 커맨드로 모여 프레임당 한 번 병합 적용된다.**

```
Physics/                      ← 백엔드 격리. 외부에 PhysX/Jolt 헤더가 새지 않는다
  PhysicsTypes.h              ← BodyHandle, ShapeDesc, 쿼리 in/out. std + Mathf만 의존
  PhysicsWorld.h/.cpp         ← 월드 하나. 생성·파괴·스텝·쿼리·이벤트 수집
  PhysicsBodyStore            ← 평탄 SoA. BodyHandle = {index, generation}
  PhysicsCharacter            ← CCT. 리지드바디와 독립
  PhysCommand 파이프           ← 스레드별 선형 버퍼 → 프레임 경계 드레인·병합
  ContactEvent 버퍼            ← 고정 크기 이벤트 + 평탄 접촉점 배열
  backend/                    ← 구현 (여기서만 백엔드 헤더 include)

ScriptBinder/
  RigidBodyComponent          ← 저작 파라미터 + BodyHandle 보유
  ColliderComponent 4종       ← 형상만. 단독으로 정적 바디를 만든다
  CharacterControllerComponent← RigidBody 의존 없음
  PhysicsScene                ← 씬당 하나. 컴포넌트 ↔ BodyHandle 등록표 단일 소유
```

**불변식 다섯:**

1. `BodyHandle`(index:24 | generation:8)이 유일한 식별자다. GameObject InstanceID를 쓰지 않는다 → 오브젝트당 다중 콜라이더가 자연스럽다.
2. 등록·파괴는 `PhysicsScene` 한 곳에서만 일어난다(파괴 단일점).
3. **권위는 한쪽뿐이고 레인이 그걸 표현한다.** Static은 순회 대상이 아니고, Kinematic은 push 전용, Dynamic은 pull 전용(그것도 활성 바디만), Character는 독립 레인. 양방향 동기화가 사라진다.
4. **변경은 커맨드로 모인다.** 컴포넌트 dirty 비트를 확인하려 전체를 순회하지 않는다 — 비용이 O(바디)에서 O(변경)이 된다. 섀도 값은 즉시 갱신해 읽기 일관성을 지키고, 백엔드 호출만 병합·지연한다.
5. **물리 상태를 읽는 것은 메인 스레드뿐이고, 다른 스레드는 커맨드 적재만 한다.** 워커는 잡 시작 시 발행된 불변 스냅샷을 읽는다. 이 계약 하나가 Z-① 레이스·읽기 일관성·백엔드 락 정책을 함께 닫는다(위반은 디버그 빌드에서 assert).

---

## 4. 트랙 구조

| 트랙 | 이름 | 선행 | 병행 |
|---|---|---|---|
| **Z** | 지혈 — 확정 결함 수술 | 없음 (**순서 밖 최우선**) | 전부 |
| **X** | 백엔드 판정 게이트 | Z 무관 | A0과 병행 |
| **A** | 경계 밀봉 · 죽은 자산 은퇴 | A0은 즉시, A1은 X1 이후 | — |
| **B** | 바디·셰이프 코어 | A1 | — |
| **C** | 캐릭터 컨트롤러 | B0·B1 | T와 병행 |
| **T** | 스레딩 · 프레임 구조 | B0 (평탄 스토어) | C와 병행 |
| **M** | 소비자 이전 · 마이그레이션 | B·C 완료 | — |

**핵심 순서**: `Z` → `X0/A0` → `X1` → `A1·A2` → `B` → `C`·`T` → `M`

---

## 5. 단계별

### 트랙 Z — 지혈 (재설계 전, 지금 값이 있다)

#### Z0. AI 스레드 ↔ 물리 데이터 레이스 차단 — P0 · 1일

`Scene.cpp:1135`의 `wait_for(0s)`를 물리 스텝 **전 무조건 join**으로 바꾼다. 성능이 아까우면 AI 결과를 커맨드 큐에 적재하고 메인 스레드가 프레임 경계에서 적용하는 형태로. 어느 쪽이든 BT 액션이 `Physics->` / 컴포넌트 상태를 직접 만지는 경로를 끊는 게 목적. T0에서 세울 스레딩 계약의 선행 형태다.

#### Z1. `CollisionData` 누수 봉합 — P0 · 0.5일

`m_collisionDataContainer`를 `unordered_map<uint, std::unique_ptr<CollisionData>>`로. `erase`가 곧 해제가 되게. 실패 경로의 수동 `delete` 2곳도 함께 제거.

#### Z2. `GetRigidBodyData` static 분기 널 역참조 — P0 · 0.5일

`Physx.cpp:1124`. `staticBody->GetScale()`로 정정. 같은 함수의 `dynamic_cast` 이중 시도 구조도 `else if`로 정리.

#### Z3. CUDA 폴백 부재 + `createScene` 널 미검사 — P0 · 0.5일

CUDA 초기화 실패 시 `eENABLE_GPU_DYNAMICS`·`broadPhaseType`을 CPU로 되돌린다. `createScene` 반환을 검사하고 실패 시 `Initialize()`가 false를 반환하게. 지금은 실패가 곧 크래시다.

#### Z4. 솔버 워커 하드캡 해제 — P0 · 0.5일

`Physx.cpp:165-172`의 임의 산식을 제거하고 `max(1, hardware_concurrency() - 예약분)`으로. 예약분은 설정값. 개선 폭은 `PhysicsDrop` 씬으로 실측해 기록한다(X0 기준선과 겸함).

#### Z5. `AngularDamping` 직렬화 누락 정정 — P1 · 0.25일

`RigidBodyComponent::reflect()`에 `meta::field<&Self::AngularDamping>` 한 줄 추가. B4가 스키마를 다시 쓰지만, 그때까지 저작 데이터가 계속 유실된다.

> **Exit**: 6종 수정 후 `PhysicsDrop` 씬 30분 연속 구동에서 크래시 0, 씬 전환 20회 후 `CollisionData` 잔존 0, 인스펙터에서 조정한 각 감쇠가 재로드 후 보존.

### 트랙 X — 백엔드 판정 게이트

#### X0. Jolt vs PhysX 스파이크 벤치 — P0 · 2일

2.3의 시나리오를 실행한다. Jolt 쪽은 엔진에 붙이지 않고 **단독 콘솔 프로그램**으로 — 이 단계의 목적은 통합이 아니라 수치다. 측정 항목: 스텝 시간(평균·p99), 메모리, 스레드 점유. `mem.bench` 하네스 재활용.

#### X1. 결정 기록 — P0 · 0.5일

판정 결과와 근거 수치를 이 문서 §2.3 아래에 기록한다. **되돌릴 수 없는 지점**을 명시한다 — A1 착수 이후에는 백엔드 재판정을 하지 않는다.

> **Exit**: 목표 바디 수가 수치로 문서에 적히고, 백엔드가 하나로 확정된다.

### 트랙 A — 경계 밀봉 · 죽은 자산 은퇴

#### A0. 죽은 자산 은퇴 — P0 · 1일 · **X와 독립, 즉시 착수 가능**

1.4 표 전량 삭제. `RagdollComponent`는 리플렉션·`ComponentTypeUUID`·`LifecycleRegistry` 등록도 함께 뗀다 — 기존 씬에 인스턴스가 있으면 로드 시 무시되도록 마이그레이션 규칙을 `SerializationPlan`에 등록. 랙돌은 재작성 후 별건으로 다룬다(§8).

#### A1. 불투명 핸들 API로 `Physics/` 밀봉 — P0 · 3일 · 선행: X1

`PhysicsTypes.h`는 std + `Mathf`만 의존한다. 백엔드 헤더 include는 `Physics/backend/` 안으로만. ScriptBinder 12개 헤더의 `../Physics/Physx.h` 직접 include를 전부 끊는다. `check_include_boundary.py`에 물리 규칙을 추가해 CI 래칫으로 고정 — S-8이 되살아나지 않게.

동시에 S-9(헤더의 `static auto` 싱글턴) 제거: 월드는 `PhysicsScene`이 소유하고, 전역 접근자를 두지 않는다.

#### A2. 레이어 필터 단일 진실 — P1 · 2일

32×32 매트릭스 사본 둘을 하나로. 저작 결과(`.creator` 저장분)는 보존한다. 레이어 수 하드코딩을 상수로 승격하고, 필터 판정을 백엔드 필터(PhysX FilterShader / Jolt ObjectLayerPairFilter)에 **한 번만** 넘긴다.

> **Exit**: ScriptBinder 어디에도 백엔드 헤더가 없다(CI 검사 통과). 매트릭스 저작·저장·적용 왕복이 기존 씬에서 동일.

### 트랙 B — 바디·셰이프 코어

#### B0. `BodyHandle` + 평탄 스토어 — P0 · 3일

`{index:24, generation:8}` 핸들. `unordered_map` 전량 제거, 배열 인덱싱으로. 세대 카운터로 파괴된 핸들 접근을 검출한다. 이것이 ⑤ 병렬화와 S-4(RTTI 제거)의 선결조건.

#### B1. 셰이프 모델 — 다중 셰이프 · 콜라이더 단독 = 정적 — P0 · 4일

S-2, S-5를 함께 뒤집는다. 한 오브젝트의 콜라이더 N개가 컴파운드 셰이프 하나로 합쳐진다. RigidBodyComponent가 없으면 **정적 바디**를 만든다(에러가 아니라 정상 경로). `Scene`의 7개 병렬 벡터와 `m_ColliderTypeLinkCallback`을 은퇴시키고 `PhysicsScene` 등록표 하나로.

S-10(3단계 지연 생성)도 여기서 접는다 — `Awake`에서 등록, 다음 스텝 직전에 일괄 생성. 두 단계.

#### B2. dirty 기반 차등 동기화 — P0 · 3일

S-3 해체. 컴포넌트 → 물리는 변경 비트가 선 필드만. 물리 → 컴포넌트는 활성 바디만(PhysX `eENABLE_ACTIVE_ACTORS` / Jolt `BodyActivationListener` — 둘 다 이미 있는 기능을 안 쓰고 있었다). `getShapes` 프레임당 힙 할당 제거.

#### B3. 파괴 단일점 — P0 · 2일

`bIsDestroyed`를 세 곳에서 검사하던 구조를 `PhysicsScene::DestroyBody(BodyHandle)` 하나로. 씬 언로드 경로의 `Physics->Update(1.0f)`(1초 스텝)도 여기서 제거.

#### B4. 컴포넌트 재작성 — P0 · 3일

`RigidBodyComponent` + 콜라이더 4종을 새로 쓴다. 헤더에 인라인으로 몰려 있던 구현을 `.cpp`로 내린다(BoxCollider 197줄 중 대부분이 헤더 인라인). 리플렉션 스키마는 **기존 필드명을 유지**해 씬 호환을 지킨다 — 이름을 바꿔야 하는 것만 M2에서 마이그레이션.

> **Exit**: 한 오브젝트에 콜라이더 2개를 붙이면 둘 다 동작한다. RigidBody 없는 바닥이 정적 충돌체로 선다. `PhysicsDrop` 1,000개에서 동기화 구간 시간이 재작성 전 대비 기록된다.

### 트랙 C — 캐릭터 컨트롤러

#### C0. RigidBody 기생 분리 — P0 · 3일

S-6 해체. CCT가 자기 속도를 소유한다. `CharacterControllerComponent`에서 `RigidBodyComponent` 의존 제거 — 게임 스크립트 56곳이 여기에 걸려 있으므로 M0과 짝을 이룬다.

#### C1. 이동·중력·경사 모델 재작성 — P1 · 3일

`CharacterController.cpp`(359줄) + `CharacterMovement`(125줄)를 새로 쓴다. 백엔드가 Jolt이면 `CharacterVirtual` 기반. 기존 저작값(`maxSpeed`, `acceleration`, `jumpSpeed`, `gravityWeight`, `stepOffset`, `slopeLimit`)의 **의미를 보존**한다 — 값이 같으면 움직임이 같아야 한다.

#### C2. 강제 이동(ForcedMove) 재설계 — P2 · 1일

`ApplyForcedMoveToCCT`(curveType 정수 인자)를 명시적 타입으로. `m_pendingControllerPositions` 큐를 B3의 단일 커맨드 경로에 흡수.

> **Exit**: 기존 플레이어·몬스터 이동이 재작성 전과 육안 동일. CCT가 RigidBodyComponent 없이 동작.

### 트랙 T — 스레딩 · 프레임 구조

#### T0. 스레딩 계약 확정·강제 — P0 · 2일 · 선행: B0

물리 API 접근을 어느 스레드에 묶을지 확정한다. 기본안: **메인 스레드 전용 + 커맨드 큐**. 다른 스레드는 큐에 적재하고 프레임 경계에서 적용한다. 디버그 빌드에서 스레드 ID를 assert해 위반을 즉시 잡는다. Z0의 임시 조치가 여기서 정식 구조가 된다.

#### T1. 잡 시스템 통합 — P1 · 3일

백엔드에 따라 갈린다:

- **Jolt**: `JobSystem` 어댑터를 엔진 `WorkerPool`(`Enqueue` + `NotifyAllAndWait` 보유) 위에 구현. `JobSystemWithBarrier` 상속으로 `QueueJob`/`QueueJobs`/`GetMaxConcurrency` 정도만. 물리 잡이 엔진 워커와 풀을 공유한다.
- **PhysX**: `simulate()`와 `fetchResults()` 사이에 PxScene을 만지지 않는 작업(애니메이션 샘플링·오디오)을 배치. 스크립트 `FixedUpdate`는 물리 상태를 만지므로 옮기지 않는다.

어느 쪽이든 **"메인 스레드가 시뮬레이션 내내 논다"는 상태를 끝내는 것**이 Exit 조건이다.

#### T2. 쿼리 병렬화 — P2 · 2일

Raycast/Overlap/Sweep은 동시 읽기 안전(동시 쓰기 없음 전제). 24개 질의 호출부 중 배치 가능한 것(`SphereOverlap` 16)을 묶어 병렬 처리. 쿼리 구조 커밋 정책(`sceneQueryUpdateMode` 등)도 여기서 정한다.

> **Exit**: 스레딩 계약 위반이 디버그 빌드에서 assert로 잡힌다. `PhysicsDrop` 1,000개에서 프레임 시간이 T 착수 전 대비 기록된다.

### 트랙 M — 소비자 이전 · 마이그레이션

#### M0. C++ 게임 스크립트 이전 — P0 · 2일

82곳. CCT 56곳이 C0와 짝. 대부분 기계적 치환이지만, BT 액션의 물리 접근은 T0 계약에 맞춰 커맨드 큐 경유로 바뀐다.

#### M1. C# 경계 재배선 — P0 · 2일

Native 테이블 물리 45개. `Colliders.cs`의 기형(트리거·활성화가 RigidBody에 있는 것)을 콜라이더로 정상화한다 — 이 이전이 그 주석을 지우는 작업이다.

#### M2. 씬·프리팹 마이그레이션 — P0 · 2일

필드명이 바뀐 것과 `RagdollComponent` 제거분. `SerializationPlan`의 마이그레이션 규칙에 등록. PHASE 18의 typed 순회 위에서 수행하면 이름 기반 왕복이 안전하다.

#### M3. 회귀 세트 — P0 · 2일

`PhysicsDrop` 확장(다중 콜라이더 · 정적 단독 콜라이더 · CCT 경사/계단 · 트리거 이벤트 · 레이어 필터). pwsh로 돌린다(5.1은 한글 주석 인코딩이 깨져 거짓 실패가 난다). CI에 등록.

> **Exit**: 기존 게임 씬이 재작성 전과 동일하게 플레이된다. 회귀 세트 전항 통과.

---

## 6. 순서 제약 · 다른 페이즈와의 관계

| 상대 | 제약 |
|---|---|
| **SceneGraphRedesignPlan 트랙 S** (Transform 컴포넌트화) | 물리가 Transform을 매 스텝 읽고 쓴다. S가 Transform 저장 위치를 바꾸면 B2의 동기화 경로가 그 위에 얹혀야 한다. **S 확정 후 B2 착수**를 권장하되, S가 지연되면 B2는 현 Transform API 위에서 진행하고 접점만 얇게 유지한다 |
| **PHASE 17 직렬화** | M2가 `SerializationPlan`의 마이그레이션 규칙에 등록된다. 쿠킹 대상에 물리 컴포넌트가 포함됨 |
| **PHASE 18 리플렉션** (완결) | typed 순회가 이미 있으므로 M2가 이름 기반 왕복을 안전하게 할 수 있다. 선행 조건 충족 |
| **PHASE 12.5 빌드 파이프라인** | A1이 `Physics/`를 밀봉하면 Player 링크 경계가 정리된다. 상승 작용 |
| **PHASE 14 프로파일러** | B2·T1의 효과 측정에 프레임 캡처가 있으면 훨씬 정확하다. 없어도 진행 가능(벽시계 측정으로 대체) |
| **PHASE 20 네트워크 N3/N8** | N3가 render frame과 실제 fixed Simulation Tick을 분리하면 물리는 `fixedDelta` 소비자가 된다. T0의 소유 스레드·`simulate/fetchResults` 규칙을 우회하지 않는다. N8 prediction/rewind의 지원 범위는 backend 결정론 실측 뒤 정하며, 이 계획의 기본 완료 조건은 아니다 |

---

## 7. 함정

1. **"배선 완료 ≠ 화면에 나옴"의 물리판.** 바디가 만들어졌다고 충돌하는 게 아니다. 필터 데이터·셰이프 플래그·활성화 상태 중 하나만 틀려도 조용히 통과한다. B1 검증은 "바디 개수"가 아니라 **"충돌이 실제로 일어나는가"** 로 한다.
2. **CCT 저작값의 단위 함정.** 현 기본값(`maxSpeed = 0.025`, `jumpSpeed = 0.05`, `gravityWeight = 0.2`)은 초당 단위가 아니다 — 스텝당 값으로 튜닝된 흔적이다(`PhysicsCommon.h` 주석의 `&&&&&speed` 마커). C1에서 단위를 정규화하면 **기존 씬의 모든 캐릭터가 다르게 움직인다.** 정규화할지 값을 그대로 승계할지 C1 착수 전에 정한다.
3. **`m_ColliderTypeLinkCallback` 삭제 시점.** 이 맵은 `SetInternalPhysicData`에서 호출 후 `erase_if`로 지워진다. B1에서 이 구조를 걷어낼 때, 콜백이 캡처한 `ptr`이 이미 파괴된 컴포넌트를 가리키는 창이 있는지 먼저 확인한다 — `[=]` 값 캡처라 raw 포인터가 그대로 들어 있다.
4. **씬 전환 중 잔존 바디.** `OnUnloadScene`이 `bIsDestroyed`만 세우고 실제 제거는 다음 `Update`에 맡긴다. B3에서 단일점으로 모을 때 이 지연 창을 없애면 **씬 전환 직후 콜백이 죽은 GameObject를 참조**하던 경로가 드러날 수 있다(현재 `ProcessCallback`이 `LogError`로 흘리고 있는 것이 그 징후일 가능성).
5. **인코딩.** 새로 쓰는 파일은 UTF-8(BOM 포함, MSVC 한글 주석 안전)로. 기존 `Physx.cpp`·`PhysicsManager.cpp`의 mojibake 주석은 복구 대상이 아니라 **삭제 대상**이다.
6. **A0을 X 판정 전에 하는 이유.** 죽은 자산은 어느 백엔드로 가든 죽었다. 먼저 지우면 X0 스파이크의 비교 대상이 작아지고, A1 밀봉 범위도 줄어든다.

---

## 8. 이 계획이 의도적으로 하지 않는 것

- **랙돌·아티큘레이션 재구현.** 현재 죽은 코드이고, 되살리려면 스켈레톤·애니메이션 블렌딩과의 접점 설계가 먼저다. A0에서 지우고, 필요해지면 별건으로 세운다.
- **차량·클로스·디포머블.** 코드에 한 줄도 없다. 주석의 TODO는 근거가 아니다.
- **두 백엔드 동시 지원.** §2.3.
- **물리 스레드 분리**(전용 스레드에서 월드를 돌리는 구조). T0/T1이 잡 시스템 위 병렬까지만 다룬다. 프레임 비동기 물리는 결정론·입력 지연 설계가 따로 필요하고, 지금 얻을 이득보다 위험이 크다.
- **CCT를 완전한 캐릭터 무브먼트 컴포넌트로 확장.** C는 현재 기능을 정상 구조 위에 올리는 데까지다. 루트 모션·네트워크 예측 같은 것은 범위 밖.
