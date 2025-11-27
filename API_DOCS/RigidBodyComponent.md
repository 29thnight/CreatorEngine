# RigidBodyComponent

**Header:** `ScriptBinder/RigidBodyComponent.h`

**Inheritance:** `: public Component, public RegistableEvent<RigidBodyComponent>`

> 자동 생성된 문서입니다. 실제 사용 시 구현 파일을 함께 참고하세요.

## Public Methods
- `void Awake() override;`
- `void OnDestroy() override;`
- `void SetBodyType(const EBodyType& bodyType);`
- `void SetAngularDamping(float _AngularDamping = 0.05f);`
- `void SetLinearDamping(float _LinearDamping);`
- `void AddForce(const Mathf::Vector3& force, EForceMode forceMode = EForceMode::FORCE);`
- `void SetMass(float _mass);`
- `void SetKinematic(bool isKinematic);`
- `void SetIsTrigger(bool isTrigger);`
- `void SetColliderEnabled(bool enabled);`
- `void UseGravity(bool useGravity);`
- `void NotifyPhysicsStateChange(const Mathf::Vector3& position);`

## Public Properties
- (none)
