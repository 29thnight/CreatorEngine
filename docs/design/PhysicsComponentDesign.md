# 물리 컴포넌트 저작과 Scene 연결

기준: master `493536e300e274cbf59989cdd62b20310c8ae64f`의 소스와 호출부. 이 문서는 현재 구현을 설명하며, 이번 문서 갱신에서 빌드·테스트·제품 실행을 수행하지 않았다.

## 바디는 하나의 완전한 정의

`PhysicsBodyComponent`는 motion, mass, gravity, translation/rotation locks, damping, 초기 속도와 `m_shapes`를 보관한다. 자식 Entity의 collider를 검색해 부모 바디에 합치는 구조가 아니다. 각 컴포넌트의 형상 목록이 완전한 바디 정의이며 hierarchy는 Transform 관계를 제공한다.

`PhysicsShapeDefinition`에는 shape ID, contact role, 형상 종류·치수, geometry asset/revision/scale, local pose, 재질 계수, sensor/query enabled, layer override가 있다. shape schema는 별도 필드로 관리한다. runtime binding, 관측 행렬, 이전 스케일, transfer state는 저작 필드가 아니다.

## 등록에서 실행까지

1. `OnAddedToScene`에서 Scene 등록을 연결한다. `OnRemovingFromScene`·`OnUninitializing`에서 이 연결을 정리한다
2. `CaptureDefinition`이 현재 world transform, layer policy와 전체 형상 목록을 검증해 소유 정의를 만든다
3. `ScenePhysicsSimulation::Register`는 저작 멤버십을 만든다. SDK Scene은 `Start`에서 만들고 enabled 멤버를 실행 바디로 전환한다
4. `OnEnable`/`OnDisable`은 실행 멤버십을 바꾼다. 등록 존재와 SDK actor 존재를 같은 상태로 가정하면 안 된다
5. `Stop`은 실행 자원을 닫는다. Editor session은 저작 snapshot 복원을 별도로 수행하고 Player session은 복원하지 않는다

Scene의 stable binding ID와 SDK handle은 별개다. 교체·disable·이송으로 actor가 바뀌어도 컴포넌트 귀속을 추적해야 한다. 접촉의 마지막 End를 게시하기 위해 retired handle/binding 매핑이 다음 fetch까지 살아 있을 수 있다.

## 편집과 실행 중 변경을 분리한다

`SetShapes`는 저작 정의 갱신 표면이다. 실행 중에는 `ReplaceShapes` 또는 `RefreshRuntimeShapes`로 명시적인 전체 정의 교체를 사용한다. `ScenePhysicsSimulation::Replace`는 owner/idle 구간에서 pose·velocity와 enabled membership을 보존한다. 성공 전에 기존 actor를 무작정 버리는 방식으로 구현을 우회하지 않는다.

`RuntimeHandle`, `ReadState`, `SetVelocity`, `ApplyForce`는 실행 상태를 다룬다. 아직 시작하지 않은 편집 씬, disabled 바디, stale binding의 오류를 확인해야 한다. 네이티브 오류는 [API 계약](PhysicsAPIContract.md)의 expected 결과로 전달된다.

## Transform의 쓰기와 읽기

Scene은 local write를 물리 dirty queue에 모은다. FixedUpdate 시작에 월드 변환을 resolve하고 `CommitPhysicsTransforms`에서 현재 pose/scale 변화에 대응한다. 스텝 결과는 `ApplyWorldWriteBatch(..., Physics)`로 Scene Transform 저장소에 반영한다. 렌더는 별도 `PhysicsRenderMatrix` 경로의 previous/current 보간을 사용할 수 있다. 보간 행렬을 다시 simulation 저작 정본으로 저장하지 않는다.

비균일·음수·0 scale, 회전과 shear, cooked 형상 지원 조합은 입력 검증 문제다. 화면에 보이는 primitive preview가 곧 SDK 지원 보증은 아니다. hierarchy 아래에 두 바디가 있어도 형상 소유권을 합치지 않으며 각 바디 결과의 world write를 계층 규약에 맞춰 적용한다.

## 레이어와 접촉 역할

프로젝트 layer catalog/policy가 Entity의 기본 충돌 필터를 정한다. shape의 layerOverride=0은 Entity layer 상속이다. nonzero override는 유효한 안정 layer ID여야 하며 알 수 없거나 퇴역한 ID는 거부한다. layer 변경은 바디 정체성을 그대로 두고 refilter할 수 있다.

contact role은 공격/피격 같은 게임 의미 UUID다. 충돌을 허용하는 layer mask와 구독 라우팅 role은 독립이다. 저장된 role이 있으면 C#의 임시 `BindContactRole`로 덮어쓸 수 없다. 구독 수명과 overflow는 [접촉 계약](PhysicsContactExecutionContract.md)에 있다.

## 캐릭터는 별도의 실행 모델

`CharacterMovementComponent`는 capsule 기반 character binding과 이동 정책을 사용한다. ScenePhysicsSimulation이 fixed step에서 desired velocity, gravity/fall velocity, jump, forced motion을 처리한다. 이는 dynamic rigid body에 속도를 쓰는 것과 같은 API가 아니다. Transform의 facing/회전 저작과 충돌 capsule 위치를 분리해서 읽어야 한다.

## 마이그레이션과 확인할 범위

옛 rigidbody/collider 노드를 현 컴포넌트의 새 이름으로만 바꾸면 shape 소유권·layer·geometry source가 빠진다. 저장 스키마는 migration 도구와 corpus 검증을 함께 사용하고, 작성된 정의를 reload하여 확인해야 한다. static mesh/cooked geometry, CCT, runtime replacement, DDOL, stop 복원은 각각 다른 경계다. 이번 문서화에서는 이를 실행하지 않았다.

## 구현 근거

- [Engine/SceneRuntime/PhysicsBodyComponent.h](../../Engine/SceneRuntime/PhysicsBodyComponent.h)
- [Engine/SceneRuntime/PhysicsBodyComponent.cpp](../../Engine/SceneRuntime/PhysicsBodyComponent.cpp)
- [Engine/SceneRuntime/PhysicsShapeDefinition.h](../../Engine/SceneRuntime/PhysicsShapeDefinition.h)
- [Engine/SceneRuntime/ScenePhysicsSimulation.cpp](../../Engine/SceneRuntime/ScenePhysicsSimulation.cpp)
- [Engine/SceneRuntime/Scene.cpp](../../Engine/SceneRuntime/Scene.cpp)
- [Engine/SceneRuntime/CharacterMovementComponent.h](../../Engine/SceneRuntime/CharacterMovementComponent.h)
- [Tools/regression/migrate-physics-schema.py](../../Tools/regression/migrate-physics-schema.py)
