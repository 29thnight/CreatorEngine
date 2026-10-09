# 물리 컴포넌트 구조 설계 — 완결된 바디 정의와 씬 소유 실행

개정: 2026-10-01 · 실행 계획: [PhysicsRedesignPlan](../plans/PhysicsRedesignPlan.md)

## 0. 설계 기준

기존 저작환경의 익숙함은 구조 선택의 근거가 아니다. 완결된 물리 객체 정의, 단일 소유권,
불변 지오메트리 공유, 명시적 상태 권위와 변경량 중심 실행을 기준으로 한다.
이번 백엔드는 PhysX다. 삭제 전 P0 기준선을 보존하고 R0에서 기존 계층과 배선을 제거한다. C++23 API P1~P3는 새 컴포넌트 구현의 하드 선행이다. 전체 변경은 빅뱅이며 병렬 배선·구 경로 fallback·호환 어댑터는 없다.
Jolt 도입/비교/어댑터는 미래 계획이며 이번 범위 밖이다. ECS 전환은 요구하지 않는다.

## 1. 저작 구조

| 구성 | 역할 | 선택 이유 |
|---|---|---|
| PhysicsBodyComponent | 운동 종류·형상 집합·질량·감쇠·제약과 BodyHandle | Rigidbody/Collider 조합을 탐색하지 않고 바디 하나를 완전하게 정의 |
| CharacterMovementComponent | 이동 설정·입력과 CharacterHandle | 강체 운동과 캐릭터 충돌 이동을 구분 |
| geometry / CollisionGeometry | primitive variant 값 / 불변 공유 cooked 자산 | 단순 값 형상과 공유 SDK 자산의 수명 분리, 중복 cook/메모리 방지 |
| ShapeInstance | ShapeId·로컬 pose·재질·필터·solid/sensor | compound 안의 개별 충돌 의미 보존 |

PhysicsBodyComponent는 static/kinematic/dynamic을 명시한다. 콜라이더만 있으면 static이라는 추론을 하지 않는다.
Box/Sphere/Capsule/Mesh별 Component를 기본 모델에서 제거하고 바디 정의 안의 형상 항목으로 표현한다.
CharacterMovementComponent와 PhysicsBodyComponent의 동시 부착은 기본적으로 거부한다.
독립적으로 움직이는 센서/자식 형상은 소유 바디와 갱신 규칙을 명시하고 일괄 compound로 흡수하지 않는다.

```cpp
// P2 실제 API. shapes는 create_body 호출 중 빌린 뒤 내부 소유로 전환한다.
using namespace ce::physics;

std::array shapes{
    ShapeInstance{.id = shape_id{7}, .form = box_geometry{{.5f, .5f, .5f}}}
};

body_desc desc{
    .kind = body_kind::dynamic,
    .shapes = shapes,
    .mass = 10.f,
    .constraints = {.rotation = axis_lock::x | axis_lock::z}
};

auto handle = scene.create_body(desc);
```

primitive geometry는 종류별 강한 타입의 std::variant로 정의하고, convex/triangle mesh/heightfield는
불변 cooked 자산을 참조한다. 저작 직렬화는 tagged DTO로 변환하며 런타임 variant에 리플렉션 제약을 강제하지 않는다.
동적 triangle mesh 등 SDK 제한과 scale/mass/inertia 규칙은 P2에서 검증해 잘못된 정의를 거부한다.

## 2. 명명과 소유권

엔진의 Scene 용어에 맞춰 **PhysicsScene**을 사용한다. PhysicsWorld는 사용하지 않는다.
PhysicsScene은 엔진 Scene이 소유하는 시뮬레이션이고 별도의 공개 PhysicsManager를 두지 않는다.
물리 모듈은 Entity/Component를 모르며 SceneRuntime이 핸들과 엔티티를 연결한다.
연결 코드는 별도 소유자가 아니라 PhysicsScene과 컴포넌트 사이의 엔진 배선이다.

CharacterMovementComponent는 이동 책임을 직접 표현한다. CharacterMotorComponent는 사용하지 않는다.
PhysicsBodyComponent는 static까지 포함하므로 RigidBodyComponent보다 저작 역할이 명시적이다.
CollisionGeometry는 공유 형상 자산, ShapeInstance는 특정 바디에 배치된 형상이다.
BodyHandle/CharacterHandle은 실행 객체 수명 식별자, ShapeId는 바디 내부의 형상 식별자다.
씬 간 오용과 세대 순환을 검출하는 정책을 먼저 정하며 8비트 세대를 미리 고정하지 않는다.

## 3. 런타임 상태와 권위

| 종류 | 위치 권위 | 정상 동기화 |
|---|---|---|
| Static | 명시적 저작/변경 요청 | 생성·변경 시만 적용 |
| Kinematic | 게임의 target pose | 스텝 경계에 target 적용 |
| Dynamic | PhysX simulation | 활성 바디 결과만 Transform에 반영 |
| Character | 입력을 충돌 이동으로 해석한 결과 | 입력과 결과를 별도 기록 |

엔진은 PhysX 상태를 완전히 복제하는 두 번째 저장소를 만들지 않는다.
핸들·세대·백엔드 연결·수명 레코드는 작은 AoS로 시작하고, 활성 목록·보간 pose·불변 스냅샷은
접근 패턴에 맞춰 분리한다. AoS/SoA 선택은 측정으로 결정한다.
형상/바디의 저작 정의, 백엔드의 실제 상태, 요청 중인 값과 스냅샷은 역할이 다른 데이터다.

target pose와 simulated pose, 요청 속도와 확정 속도를 구분한다. setter 직후 실제 시뮬레이션 결과가
바뀌었다고 약속하지 않는다. 설정값 읽기와 결과 읽기를 API에서 분리하고 적용 tick을 명시한다.
월드 pose를 부모가 있는 Transform에 반영할 때 로컬 환산과 dirty 전파를 생략하지 않는다.
렌더는 이전/현재 스텝 pose를 보간하고 게임플레이는 확정 스텝 결과를 읽는다.

## 4. 커맨드·이벤트·쿼리

게임 소유 스레드에서 변경·생성·파괴를 커밋한다. AI 등 워커는 불변 스냅샷을 읽고 커맨드를 적재한다.
커맨드는 대상 핸들·적용 tick·연산·순서 정보를 갖고, span/임시 포인터를 지연 보관하지 않는다.
마지막 값 선택과 누산은 연산별로 정의한다. AddForce도 force/impulse 모드와 순서 의미가 다르면
무조건 합산하지 않는다. 재현 가능한 워커 간 병합 규칙을 정한다.

2026-10-08 목표 계약은 [물리 접촉 작업과 C# 실행 계약](PhysicsContactExecutionContract.md)을 따른다. 기본 저작 표면은 OnBeginSimulation에서 ContactStream을 한 번 등록하고 PostPhysics에서 이미 매칭된 배치를 소비하는 방식이다. Read의 전체 검색은 없으며 결과 K개 순회만 한다. 별도 Job/PhysicsBehaviour 저작 요구는 철회한다. 현행 구현과 목표 배선은 해당 문서 §1에서 구분한다.

접촉 이벤트는 BodyHandle 쌍에 **ShapeId 쌍**을 포함한다. 바디 단위 전달과 shape 단위 hitbox 판정을 모두 지원한다.
접촉점 버퍼의 소비 수명과 overflow 정책을 명시한다. WantsEvents는 실제 이벤트 구독 정보로 갱신하며
옛 생명주기 틱 비트를 접촉 관심도의 대용으로 사용하지 않는다.

PhysX solver 콜백은 엔진의 소유 스레드와 별개다. 콜백에서는 엔진 컴포넌트/CLR을 호출하지 않고,
동시성 안전한 수집 버퍼에 기록한 후 fetch 완료 뒤 소유 경계에서 라우팅한다. 소유자·역할·단계 인덱스로 매칭 스트림에만 전달하고 기존 PostPhysics owner thread에서 소비한다. 새 병렬 Job/명령 writer를 도입하지 않는다.
쿼리는 scene query 갱신이 완료된 읽기 창에서 수행하며, 빈 결과·버퍼 overflow·실패를 구분한다.

## 5. C++23 PhysX API 선행

P0~P3에서 SDK 소유권을 RAII와 release deleter로 닫고, 실패 경로는 std::expected로 반환한다.
공유 shape/material/cooked geometry의 SDK 참조 수명과 scene/actor/dispatcher의 종료 순서를 검증한다.
expected의 monadic 연산으로 오류를 합성하고 [[nodiscard]]로 결과 무시를 차단한다.
명령은 opcode/aux/union 대신 연산별 구조체의 variant이며 visit으로 처리한다.
concepts/requires는 typed batch와 핸들·형상 입력의 요구조건을 제한한다.
deducing this는 내부 뷰/builder의 const 및 value-category 중복을 제거한다.
std::ranges 알고리즘과 filter/transform/enumerate/zip/chunk를 검증·cook 준비·batch·진단·마이그레이션의 기본 선택으로 적극 수용한다. 핫패스도 금지하지 않고 실측으로 선택한다.
Mathematics의 math::components/math::rows/math::views::transform_fixed와 fixed terminal은 비핫패스의 성분 검사·단위 변환·행렬 진단·스키마 변환에 적극 사용한다. 저장소의 고정 헤더 API를 사용하며 라이브러리 갱신을 요구하지 않는다.
비핫패스 도입마다 벤치를 요구하지 않는다. lazy view와 원본/capture의 수명은 검증하고, 잡/프레임 경계를 넘는 결과는 소유 데이터로 확정한다. 대량 pose 반영 등 핫패스는 직접 연산과 비교한다.
충돌 매트릭스는 단일 저장소와 mdspan, 저빈도 자산 lookup은 flat_map/flat_set으로 표현한다.
format/source_location은 오류 진단에 사용한다. 상세 적용과 MSVC compile probe는 실행 계획 §3을 따른다.
std::span은 batch 입출력, std::variant는 형상 정의, std::pmr은 스텝 수명 scratch에 제한적으로 사용한다.
backend/physx 밖으로 SDK 헤더·포인터를 노출하지 않는다. API는 PhysX 기능을 안전하게 사용하는
단일 백엔드 계약이며 가상의 Jolt 공통 최소 인터페이스로 기능을 축소하지 않는다.

공용 enkiTS job_scheduler 계약을 유지한다. 미완료 잡의 워커 내부 wait는 금지한다.
PhysX dispatcher를 공용 풀에 연결하는 것은 task 의존/해제 계약을 검증한 뒤 결정하며,
검증 전에는 SDK 전용 dispatcher를 유지한다. simulate/fetch 사이 overlap도 PxScene 접근 없는 작업만 허용한다.

## 6. 생명주기·마이그레이션

신규 초기화는 OnInitialized → OnAddedToScene → OnBeginSimulation,
파괴는 OnEndSimulation → OnRemovingFromScene → OnUninitializing 순서다.
DDOL은 소속 제거/재편입 경로이고 초기화를 반복하지 않는다. 목적 PhysicsScene에 바디를 연결하는
상태 이전 정책을 정한다. unregister/destroy는 중복 요청에도 안전해야 한다.

구 RigidBody+Collider 필드는 마이그레이션에서 완결된 바디 정의로 변환한다.
기존 필드명과 컴포넌트 구조를 새 API의 제약으로 삼지 않는다. C++/C# 공개 표면 변경은 명시적으로 이전하고
호환 어댑터를 두지 않는다. 구 PhysicX/PhysicsManager/래퍼는 R0에서 제거하고 제품 호출부를 전량 재배선한다. 일회성 파일 변환 도구는 런타임 구 스키마 지원과 분리한다.

## 7. 검증과 범위 밖

핸들 수명, 초기화/cook 실패 rollback, compound shape별 sensor/filter, 실제 충돌/쿼리,
CCT 단위·경사·계단·강제 이동, 씬 전환/DDOL, 콜백과 자산 수명을 Debug/Release에서 확인한다.
성능은 전체/활성/변경 바디 수와 형상 공유 비율을 함께 기록한다. 구조 선택 자체를 성능 개선 증거로 삼지 않는다.
Jolt 도입, ECS 전환, 비동기 물리 전용 스레드, 새 랙돌/차량/클로스와 네트워크 예측은 이번 범위 밖이다.

## 8. 프로파일러 계층 노출

PhysicsScene의 실제 실행 계층과 PhysX dispatcher 워커를 기존 엔진 프로파일러에 배선한다.
커맨드 커밋·캐릭터 이동·kinematic 목표·simulate 제출·fetch 대기·query 구조 갱신/배치·활성 pose 수집·
Transform 반영·이벤트 수집/전달·스냅샷 발행을 구분한다. FrameId와 PhysicsTickId,
solver task 연관 ID로 스레드 간 실행을 연결한다. 워커 실행 시간과 소유 스레드 대기를 중복 합산하지 않는다.
SDK 내부에서 관찰할 수 없는 solver 단계를 가상 marker로 만들지 않는다.
바디/형상/활성/변경 수, 커맨드 지연·overflow, query·이벤트·scratch 사용량을 함께 계측한다.
P1~P3 구현과 동시에 배선하고 M3에서 실제 Debug/Release 캡처로 확인한다.
물리 전체 stopwatch만 있는 상태는 완료가 아니다. 상세 marker·카운터·수명 게이트는 실행 계획 §3을 따른다.

## 공통 레이어 책임 정리 (2026-10-01, L0 구현 선행)

기존 TagManager 레이어 기능은 이번 빅뱅에서 제거한다. 공통 LayerCatalog는 안정
layer_id와 명시 filter slot을, SceneLayerIndex는 Scene 소속 EntityHandle을,
PhysicsCollisionPolicy는 단일 충돌 정책을 관리한다. Entity는 layer_id를 저장하며
ShapeInstance는 Entity 레이어를 상속하거나 같은 Catalog의 ID를 override한다.
PhysicsScene은 revision snapshot을 SDK 필터로 적용한다. 상세 migration·수명·32비트
경계·실행 변경·제품 검증 계약은 PhysicsRedesignPlan의 L0 절을 따른다. B1은 이 계약을
선행으로 삼으며 이 문서 추가만으로 구현 완료를 뜻하지 않는다.

2026-10-01 L0 소비자 연결: Entity m_layerId(uint64 저장/강한 ID API), SceneLayerIndex 소속,
TagManager tags-only, Inspector/충돌 행렬 단일 정책/Undo, layer.* 및 entity.layer CLI를 구현했다.
실제 22개 Scene/Prefab 74 Entity를 변환했다. 파일·SDK 독립 회귀와 제품 TU 컴파일은
통과했으며 실제 Editor Play/Stop/Undo/DDOL 및 새 cooked Player 실행은 아직 검증하지 않았다.

## B1 primitive 형상 목록 연결 (2026-10-01, progress)

PhysicsBodyComponent는 schema 1의 PhysicsShapeDefinition 목록을 소유한다. 각 항목의
ShapeId/Box·Sphere·Capsule/로컬 pose/재질/sensor/queryEnabled를 실제 ShapeInstance와
geometry variant로 변환한다. 현재 Entity 공통 레이어를 모든 형상이 상속한다.

형상의 바디 귀속은 목록에 명시한다. Entity 자식 탐색이나 부모 바디 자동 귀속은 없다.
같은 바디의 sensor는 목록 항목이고 독립 sensor Entity는 바디를 직접 저작한다.
SDK는 현재 static sensor-only만 허용한다. 움직이는 바디에는 solid shape가 필요하다.

양수·유한 스케일만 허용한다. 회전 없는 Box는 비균일 스케일을 허용하고 Sphere/Capsule과
회전된 primitive는 균일 스케일을 요구한다. 타원체/shear 근사는 하지 않는다.
형상 ID/pose/치수/재질 전체 검증 후 owned 목록을 발행한다. SetShapes는 Editor에서만
검증 성공 후 정의를 교체하고 Play 중 변경은 거부한다. 실행 중 transactional 교체는 잔여다.

공유 cooked 자산의 영속 참조/저장·조회, per-shape layer override, 실제 Editor/HTTP
저장·Undo·Prefab 및 cooked 파일 왕복도 아직 완료하지 않았다. 상세 증거/잔여는 실행 계획을 따른다.

## B1 공유 source/cook 연결 (2026-10-01, progress)

Shape의 자산 경계는 UUID 문자열+revision이고, 내부 key는 Uuid16+uint64다.
Convex/TriangleMesh/Heightfield의 kind/scale을 검증하고 Scene 소유 CollisionGeometryLibrary에서
한 번 cook한 불변 SDK geometry를 공유한다. cache/cook Scene보다 바디 정의의 shared 자산
수명이 길어도 안전하며 Stop/재시작 때 자산을 재cook하지 않는다.

CECG v1 `.cegeometry`는 owned cook 입력을 담는 네이티브 source record다. 최종 SDK
사전 cook artifact가 아니다. 새 자산 생성은 geometry.create와 EditorAssetDatabase에 연결했다.
UUIDv4/revision1, cook 선검증, 기존 파일 거부, meta 먼저 게시 후 본문 실패 복구를 적용한다.
geometry.update로 기존 UUID의 revision 수정/Undo와 Derived의 불변 CECG source archive를 연결했다.
형상은 고정 revision을 유지하고 Editor는 archive로 이전 값을 읽는다. Player archive fallback은 없다.
SDK cook 산출물·BuildTool/CEMF/pak closure 및 실제 Editor/Player 왕복은 잔여다.

prepared flat_map과 expected 기반 Publish는 실패 시 이전 cache를 보존하고, 재진입과
다른 owner thread를 거부한다. 전체 shape preflight 뒤에만 loader/cook을 호출한다.
GeometryResolve/Publish/Cook 계층과 기존 SDK Cook 계층을 노출하며 Host 파일 게시 시간은
GeometryCook에 포함하지 않는다. 실제 독립 CPU/GPU 게이트와 제품 TU 컴파일 증거는 실행 계획을 따른다.

새 자산 Host 게시 시간은 Physics.GeometryAuthoringPublish로 별도 계측한다.
