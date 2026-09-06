# 물리 컴포넌트 구조 설계 — 저작은 컴포넌트, 런타임은 데이터

수립일: 2026-08-18 · 실행 계획: [PhysicsRedesignPlan.md](../plans/PhysicsRedesignPlan.md) (PHASE 19 트랙 B)
이 문서는 **어떻게·왜**를 담는다. **무엇을 언제**는 실행 계획에 있다.

---

## 0. 한 문장

**컴포넌트는 저작값과 핸들만 들고, 런타임 상태는 물리 월드의 평탄 스토어가 소유한다.
동기화는 바디 타입별 단방향 레인으로 갈리고, 변경은 커맨드로 모여 프레임당 한 번 병합 적용된다.**

---

## 1. 제약 — 설계가 지켜야 하는 것

이 설계는 백지에서 출발하지 않는다. 넷을 지킨다.

1. **ECS를 도입하지 않는다.** [SceneGraphRedesignPlan](../plans/archive/SceneGraphRedesignPlan.md) 게이트 D에서 EnTT/Flecs 채택 없음으로 결정됐다. GameObject + Component 저작 모델을 유지한다.
2. **기존 씬·프리팹이 로드돼야 한다.** 컴포넌트의 리플렉션 필드명을 보존한다.
3. **게임 스크립트 82곳이 살아야 한다.** 표면 의미론을 바꾸되 형태는 최소로 바꾼다.
4. **C# 경계 45개가 유지돼야 한다.** 함수 포인터 표의 시그니처는 대체로 그대로 쓸 수 있어야 한다.

즉 **저작 계층은 익숙하게 두고, 런타임 계층만 갈아엎는다.** 이 문서가 설계하는 것은 그 경계다.

---

## 2. 유니티에서 무엇을 가져오고 무엇을 버리는가

"유니티처럼 하지 않는다"는 것이 반(反)유니티를 뜻하지는 않는다. 유니티가 옳게 한 것이 있고, 유니티가 *자기 제약 때문에* 그렇게 한 것이 있다. 우리에게 그 제약이 없으면 따라갈 이유도 없다.

### 가져오는 것

| 유니티 | 왜 옳은가 |
|---|---|
| 콜라이더 단독 = 정적 충돌체 | 직관적이고, 레벨 지오메트리의 압도적 다수가 이것이다. (현 엔진은 이걸 **못 한다** — 유니티보다 후퇴해 있다) |
| 오브젝트당 콜라이더 N개 → 컴파운드 | 형상 저작의 기본 단위. (현 엔진은 두 번째를 조용히 무시한다) |
| Rigidbody / Collider 인스펙터 형상 | 익숙하고 기존 씬이 이 형태로 저장돼 있다 |
| FixedUpdate 고정 스텝 | 안정성의 전제 |

### 버리는 것

| 유니티(클래식) | 유니티가 그렇게 한 이유 | 우리에게 그 이유가 없는 까닭 |
|---|---|---|
| 컴포넌트가 런타임 상태를 소유 | 관리 코드가 임의 시점에 읽고 쓴다 | 우리는 양쪽을 다 소유한다. 상태를 두 벌 두고 매 프레임 복사할 이유가 없다 |
| 양방향 Transform 동기화 | 사용자가 언제든 Transform을 만질 수 있다 | 권위를 바디 타입으로 고정하면 방향이 하나로 정해진다 |
| 세터가 즉시 네이티브 호출 | 관리/네이티브 경계에서 지연시키면 읽기 일관성이 깨진다 | 섀도를 즉시 갱신하고 백엔드 호출만 미루면 일관성을 지키며 병합할 수 있다 |
| 콜라이더가 이벤트 인터페이스 보유 | 컴포넌트 = 객체라는 모델 | 이벤트는 바디 단위 사실이지 형상 단위 사실이 아니다 |
| 정적 바디도 씬 순회 대상 | (유니티는 실제로 이건 안 한다 — 현 엔진만 한다) | — |

**참고**: 유니티 자신도 DOTS / Unity Physics에서 이 방향으로 갔다. "유니티처럼 안 한다"는 실질적으로 **"클래식 유니티가 아니라 DOTS 쪽"** 이라는 뜻이다.

---

## 3. 원칙 다섯

### P1. 권위는 한쪽뿐이고, 레인이 그걸 표현한다

현 코드의 가장 큰 구조적 죄는 Transform과 PhysX가 **둘 다 자기가 주인이라고 생각하는 것**이다. `SetPhysicData`가 매 프레임 Transform→물리로 밀고, `GetPhysicData`가 매 프레임 물리→Transform으로 당긴다. 낭비이기 이전에 **누가 이기는지가 정의돼 있지 않다.**

바디 타입이 곧 권위다:

| 레인 | 권위 | 매 프레임 push | 매 프레임 pull | 순회 |
|---|---|---|---|---|
| **Static** | 게임 (생성 시 1회) | ✕ | ✕ | **없음** |
| **Kinematic** | 게임 | 커맨드만 | ✕ | 커맨드만 |
| **Dynamic** | 물리 | 커맨드만 | **활성만** | 활성만 |
| **Character** | 게임(입력) → 물리(해석) | 입력 | 결과 위치 | CCT 수만 |

Character가 독립 레인인 것이 S-6(CCT의 RigidBody 기생)의 구조적 해답이다. CCT는 리지드바디가 아니므로 리지드바디 레인에 넣지 않는다.

정적 바디를 움직이는 것은 **명시적 커맨드**다(브로드페이즈 엔트리 재구축을 수반). 유니티는 이걸 조용히 허용해서 유명한 성능 함정이 됐다. 우리는 드러낸다.

### P2. 컴포넌트는 핸들이지 저장소가 아니다

```cpp
struct BodyHandle {
    uint32_t bits;              // index:24 | generation:8
    bool IsValid() const noexcept { return bits != 0; }
};
```

컴포넌트는 **저작값**(직렬화 대상)과 `BodyHandle` 하나를 든다. 런타임 상태는 스토어에 한 벌만 있다.

지금 `RigidBodyComponent`에는 `m_linearVelocity`, `m_angularVelocity`, `m_scale`이 멤버로 있고 물리 쪽에도 같은 값이 있어 **매 프레임 양방향으로 복사된다.** 셋 다 사라진다 — 셋 다 `reflect()` 스키마에 없으므로 직렬화 호환에 영향이 없다.

### P3. 변경은 커맨드로 모인다

컴포넌트에 dirty 비트를 두면(현 `RB_DIRTY`) **아무것도 안 바뀐 프레임에도 전체를 순회해서 비트를 확인해야 한다.** O(바디)다.

대신 변경을 적재한다:

```cpp
enum class PhysOp : uint8_t {
    Teleport, SetVelocity, SetAngularVelocity, SetMass, SetDamping,
    AddForce, AddTorque, SetLayer, SetTrigger, SetGravity, SetBodyType,
    SetShapeDirty, Wake, Sleep,
};

struct PhysCommand {
    BodyHandle  target;     // 4
    PhysOp      op;         // 1
    uint8_t     aux;        // 1  (EForceMode 등)
    uint16_t    _pad;       // 2
    union { float f; Mathf::Vector3 v3; Mathf::Quaternion q; uint32_t u32; } arg;  // 16
};  // 24B → 32B 정렬. 캐시 라인에 2개
```

**병합 규칙:**
- *멱등* op(Teleport·SetMass·SetVelocity·SetLayer…) → 같은 `(handle, op)`은 **마지막 것만** 남는다
- *누산* op(AddForce·AddTorque) → **합산**된다

드레인 비용은 O(변경)이지 O(바디)가 아니다. 한 프레임에 위치를 10번 써도 백엔드 호출은 1회다.

**읽기 일관성**: 커맨드 적재 시 스토어의 섀도 값을 **즉시** 갱신한다(배열 쓰기 한 번). 비싼 것은 백엔드 호출뿐이고 그것만 미룬다. `rb.Mass = 5; print(rb.Mass)`는 5를 준다.

### P4. 콜라이더는 순수 데이터다

현 `ICollider`는 순수 가상 메서드 12개를 강요하는데, 그중 이벤트 6개(`OnTriggerEnter` 등)는 **모든 구현에서 빈 몸통**이다. 콜라이더마다 vtable을 하나씩 달아 아무것도 안 한다.

`ICollider`를 폐기한다. 콜라이더 컴포넌트는 형상을 서술할 뿐이다:

```cpp
enum class ShapeKind : uint8_t { Box, Sphere, Capsule, ConvexMesh };

struct PhysicsMaterial {              // 기존 필드명 보존
    float staticFriction  = 0.5f;
    float dynamicFriction = 0.4f;
    float restitution     = 0.3f;
    float density         = 10.0f;
};

struct ShapeDesc {
    ShapeKind         kind      = ShapeKind::Box;
    Mathf::Vector3    offsetPos {};
    Mathf::Quaternion offsetRot { 0, 0, 0, 1 };
    Mathf::Vector3    extent    { 1, 1, 1 };   // Box: 반크기 / Sphere: (r,·,·) / Capsule: (r, halfHeight, ·)
    MeshHandle        mesh      {};            // ConvexMesh 전용
    PhysicsMaterial   material  {};
};
```

`extent`를 union이 아니라 평범한 `Vector3`로 두는 것은 **리플렉션이 union을 직렬화하지 못하기 때문**이다. 종류별 의미는 주석과 접근자로 고정한다.

컴포넌트 4종은 각자 자기 저작 필드(`m_boxExtent`, `m_radius`, `m_height`, `m_posOffset`, `m_rotOffset`, 마찰 4종)를 그대로 유지하고, **비가상** `ShapeDesc Describe() const`만 제공한다.

컴파운드는 등록 시점에 `PhysicsScene`이 한 GameObject의 콜라이더 컴포넌트를 모아 `std::span<const ShapeDesc>`로 바디 하나를 만드는 것으로 자연히 얻어진다.

### P5. 이벤트는 스트림이지 콜백이 아니다

현재는 접촉이 생길 때마다 콜백 → `m_callbacks` 적재 → `ProcessCallback`이 **맵 조회 2회** 후 `Scene::OnTriggerEnter(Collision)` 브로드캐스트. 그리고 `Collision`은 `const std::vector<Vector3>&`를 든다 — 원본 컨테이너가 재할당되면 매달린 참조다.

```cpp
enum class ContactPhase : uint8_t { Enter, Stay, Exit };

struct ContactEvent {
    BodyHandle   a, b;          // 8
    ContactPhase phase;         // 1
    uint8_t      isTrigger;     // 1
    uint16_t     pointCount;    // 2
    uint32_t     pointOffset;   // 4  → m_contactPoints 인덱스
};  // 16B
```

접촉점은 별도의 평탄 `std::vector<Mathf::Vector3>`에 담고 프레임마다 비운다. `ContactEvent`가 고정 크기라 매달린 참조가 생길 수 없다.

**필터를 소스에 둔다.** 바디 플래그에 `WantsContactEvents` 비트가 있고, 그게 선 바디의 접촉만 기록한다. 관심 있는 스크립트가 하나도 없는 오브젝트의 충돌은 애초에 버퍼에 들어가지 않는다. 비트는 등록 시점에 생명주기 등록 정보로 정한다(PHASE 9의 `Lifecycle::Bit_*` 마스크와 같은 방식).

---

## 4. 자료구조

### 4.1 스토어 — 핫/콜드 분리

```cpp
class PhysicsBodyStore {
    // ── 핫: pull에서 매 프레임 만진다 ────────────────────────
    std::vector<Mathf::Vector3>    m_position;    // 12B
    std::vector<Mathf::Quaternion> m_rotation;    // 16B
    std::vector<Transform*>        m_target;      //  8B  되쓸 대상
    std::vector<uint8_t>           m_flags;       //  1B  Active|Trigger|WantsEvents|Sleeping...

    // ── 콜드: 변경 커맨드에서만 만진다 ──────────────────────
    std::vector<BodyParams>        m_params;      // mass, damping, max*, lockFlags
    std::vector<uint32_t>          m_layer;
    std::vector<GameObject*>       m_owner;
    std::vector<uint8_t>           m_generation;
    std::vector<BackendBodyId>     m_backendId;

    // ── 순회 목록: 레인 ─────────────────────────────────────
    std::vector<uint32_t> m_staticIds;         // 순회하지 않는다. 진단용
    std::vector<uint32_t> m_kinematicIds;
    std::vector<uint32_t> m_dynamicActiveIds;  // 매 프레임 백엔드가 채운다
    std::vector<uint32_t> m_freeList;
};
```

레인을 **별도 스토어가 아니라 인덱스 목록**으로 두는 이유: 바디 타입이 런타임에 바뀌어도(`SetBodyType`) 핸들이 그대로 유효하다. 목록 사이에서 인덱스만 옮긴다.

`m_dynamicActiveIds`는 매 프레임 백엔드의 활성 액터 보고로 다시 채운다 — PhysX `eENABLE_ACTIVE_ACTORS`(**지금 켜두고 안 쓰고 있다**) / Jolt `BodyActivationListener`.

핫/콜드 분리가 핵심인 이유: 현 `RigidBodyGetSetData`는 **~180바이트 한 구조체에 매 프레임 바뀌는 것과 거의 안 바뀌는 것을 섞어** 담고, 바디마다 스택에 새로 만들어 게터 ~25회로 채운 뒤 세터 ~20회로 푼다. 실제로 pull에 필요한 핫 데이터는 **바디당 28바이트**(위치 12 + 회전 16)다. 캐시 라인 하나에 두 바디가 들어간다.

### 4.2 되쓰기 — 분해된 채로 옮긴다

현재 pull 경로는 동적 바디마다 이렇다:

```
scale·quat·pos → CreateScale·CreateFromQuaternion·CreateTranslation
              → 행렬 곱 2회
              → SetAndDecomposeMatrix
                  → 행렬 비교 (16 float)
                  → XMMatrixDecompose (축마다 sqrt)
                  → XMVector4Normalize
                  → 부모 조회 + 로컬 행렬 재계산
```

**이미 분해된 형태로 들고 있던 데이터를 옮기려고 합성했다가 다시 분해한다.** 물리는 스케일을 바꾸지 않으므로 스케일은 애초에 왕복할 이유가 없다.

설계에서는 `Transform`에 위치·회전을 직접 대입하는 경로 하나만 쓴다(`SetWorldPositionRotation(pos, rot)` — 없으면 신설). 행렬 왕복 없음, 분해 없음, 스케일 무관.

### 4.3 커맨드 파이프

```
워커 스레드 ──→ [스레드별 선형 버퍼]  (bump pointer, 경합 없음)
메인 스레드 ──→ [pending[index] + touched 목록]  (직접 기록, 즉시 병합)

프레임 경계:  워커 버퍼들 → pending으로 드레인 → touched 순회 → 백엔드 적용
```

대부분의 변경은 메인 스레드에서 일어나므로 그쪽이 빠른 경로다. 워커는 적재만 한다.

---

## 5. 스레딩 계약

**물리 상태를 읽는 것은 메인 스레드뿐이다. 다른 스레드는 커맨드 적재만 한다.**

워커(예: AI 잡)가 물리 값을 읽어야 하면 잡 시작 시점에 발행된 **불변 스냅샷**을 읽는다. 잡이 도는 동안 스냅샷은 바뀌지 않으므로 결정적이고, 락이 없다.

이 계약 하나가 세 문제를 함께 닫는다:

- **Z-① AI 스레드 데이터 레이스** — BT 액션이 `Physics->StopForcedMoveOnCCT()`로 컨테이너를 직접 만지던 경로가 커맨드 적재로 바뀐다
- **읽기-쓰기 일관성** — 워커는 애초에 읽고 쓰지 않으므로 질문 자체가 없어진다
- **백엔드 락 정책** — `eREQUIRE_RW_LOCK` / `BodyLockInterface`가 불필요해진다. 락 자체를 안 쓴다

디버그 빌드에서 스토어 접근 시 스레드 ID를 assert해 위반을 즉시 잡는다.

---

## 6. 프레임 흐름

```
Scene::FixedUpdate
 ├ AllUpdateWorldMatrix()
 ├ PhysicsScene::Flush()          ← 등록/해제 + 커맨드 드레인 → 백엔드     O(변경)
 ├ PhysicsScene::Step(dt)         ← 시뮬레이션. 엔진 워커풀 공유(T1)
 ├ PhysicsScene::Pull()           ← 활성 동적 바디만 pos/rot → Transform   O(활성)
 ├ PhysicsScene::DispatchEvents() ← 평탄 버퍼 1회 순회, 관심 바디만        O(이벤트)
 └ 스크립트 FixedUpdate            ← 스텝 후 위치를 본다
```

`Pull`이 `DispatchEvents`보다 먼저인 것은 의도다 — `OnCollisionEnter`가 스텝 이후 위치를 보게 하려는 것으로, 현 코드의 의도와 같다.

---

## 7. 컴포넌트 표면

```cpp
class RigidBodyComponent : public meta::identity<RigidBodyComponent, Component>
{
public:
    static consteval auto reflect()   // 기존 필드명 보존 + AngularDamping 누락 정정
    {
        return meta::schema<Self>(
            meta::field<&Self::m_bodyType>,
            meta::field<&Self::LinearDamping>,
            meta::field<&Self::AngularDamping>,   // ★ 현 스키마에 빠져 있다 — 각 감쇠가 저장되지 않는다
            meta::field<&Self::m_mass>,
            meta::field<&Self::maxLinearVelocity>,
            meta::field<&Self::maxAngularVelocity>,
            meta::field<&Self::maxContactImpulse>,
            meta::field<&Self::maxDepenetrationVelocity>,
            meta::field<&Self::m_useGravity>,
            meta::field<&Self::m_setTrigger>,
            meta::field<&Self::m_setKinematic>,
            meta::field<&Self::m_collisionEnabled>,
            meta::field<&Self::m_lockFlags>);
    }

    // 읽기 — 스토어를 본다. 사본을 들지 않는다
    Mathf::Vector3 GetLinearVelocity() const;
    Mathf::Vector3 GetAngularVelocity() const;
    bool IsKinematic() const;

    // 쓰기 — 커맨드를 적재한다. 섀도는 즉시 반영된다
    void SetLinearVelocity(const Mathf::Vector3& v);
    void AddForce(const Mathf::Vector3& f, EForceMode mode = EForceMode::FORCE);
    void SetMass(float m);
    void SetKinematic(bool on);

private:
    // 저작값 (직렬화)
    EBodyType m_bodyType = EBodyType::DYNAMIC;
    float m_mass = 70.f, LinearDamping = 0.01f, AngularDamping = 0.05f;
    float maxLinearVelocity = 1e16f, maxAngularVelocity = 100.f;
    float maxContactImpulse = 1e32f, maxDepenetrationVelocity = 1e32f;
    bool  m_useGravity = true, m_setTrigger = false;
    bool  m_setKinematic = false, m_collisionEnabled = true;
    uint8_t m_lockFlags = 0;

    // 런타임 (직렬화 안 함)
    BodyHandle m_handle{};
};
```

사라지는 것: `m_linearVelocity` · `m_angularVelocity` · `m_scale` · `forceMode` · `velocity` · `RB_DIRTY` 비트.
`forceMode`가 멤버였던 것은 특히 나빴다 — 힘을 쓰면 멤버에 모드를 적어 두고 **다음 프레임 전량 push가 그걸 읽어 소비한 뒤 NONE으로 되돌리는** 구조였다. 커맨드에서는 그냥 `AddForce` 커맨드 하나다.

C# 경계 45개는 시그니처가 그대로다 — `Rigid_SetLinearVelocity(handle, Float3)`은 여전히 같은 모양이고, 내부가 즉시 호출에서 커맨드 적재로 바뀔 뿐이다.

---

## 8. 비용 대조

| 항목 | 현재 | 설계 |
|---|---|---|
| 정적 바디 N개 | 매 프레임 push + pull | **0** |
| 잠든 동적 바디 | 매 프레임 push + pull | **0** |
| 활성 동적 바디 pull 1건 | 행렬 3개 생성 + 곱 2회 + 행렬 비교(16f) + `XMMatrixDecompose` + 정규화 + 부모 조회 | pos 12B + rot 16B 직접 대입 |
| 활성 동적 바디 push 1건 | ~180B 구조체 + 게터 ~25 + 세터 ~20 + `getShapes` 힙 할당 1 | **0** (커맨드가 있을 때만) |
| 속성 변경 1건 | 다음 프레임 전량 push에 섞임 | 커맨드 1개, 같은 프레임 내 병합 |
| 타입 분기 | `dynamic_cast` ×2 | 없음 (레인이 분기다) |
| id → 바디 | `unordered_map` 조회 ×2 | 배열 인덱스 |
| 접촉 이벤트 1건 | 전건 콜백 → 맵 조회 ×2 → 브로드캐스트 | 관심 바디만 기록, 버퍼 1회 순회 |
| 콜라이더 vtable | 콜라이더마다 1개 (빈 메서드 6개) | 없음 |
| 오브젝트당 콜라이더 | 1개 (두 번째는 조용히 무시) | N개 → 컴파운드 |

수치 검증은 트랙 B2 Exit에서 `PhysicsDrop` 1,000개로 한다. 여기 적힌 것은 **무엇이 줄어드는가**이지 측정값이 아니다.

---

## 9. 마이그레이션

| 대상 | 조치 |
|---|---|
| 씬·프리팹 | 필드명 보존이라 그대로 로드된다. `AngularDamping`은 신규 필드라 기본값이 들어간다 |
| `RagdollComponent` | 제거. 로드 시 무시 규칙을 `SerializationPlan`에 등록 |
| C++ 스크립트 82곳 | CCT 56곳이 C0와 짝. 나머지는 기계적 치환 |
| C# 45개 | 시그니처 유지. 내부 구현만 교체 |
| BT 액션의 물리 접근 | 커맨드 적재로 전환 (§5 계약) |

---

## 10. 의도적으로 하지 않는 것

- **ECS 도입.** §1 제약 1.
- **컴포넌트 없는 물리 저작.** 씬 파일과 인스펙터 호환을 깬다.
- **백엔드 2종 동시 지원.** [실행 계획 §2.3](../plans/PhysicsRedesignPlan.md).
- **물리 전용 스레드.** 잡 시스템 위 병렬까지만. 프레임 비동기 물리는 결정론·입력 지연 설계가 따로 필요하다.
- **`ShapeDesc`의 union화.** 리플렉션이 union을 직렬화하지 못한다. 몇 바이트를 아끼자고 저작 경로를 특수화하지 않는다.
- **커맨드의 범용 정렬·해시 병합.** `pending[index]` + dirty 마스크로 충분하다. 변경 건수가 바디 수보다 훨씬 작다는 전제가 깨지면 그때 재검토한다.
