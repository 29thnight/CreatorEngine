# CharacterControllerComponent

**Header:** `ScriptBinder/CharacterControllerComponent.h`

**Inheritance:** `: public Component, public ICollider, public RegistableEvent<CharacterControllerComponent>`

> 자동 생성된 문서입니다. 실제 사용 시 구현 파일을 함께 참고하세요.

## Public Methods
- `void OnStart();`
- `void OnFixedUpdate(float fixedDeltaTime);`
- `void OnLateUpdate(float fixedDeltaTime);`
- `void ForcedSetPosition(const DirectX::SimpleMath::Vector3& pos);`
- `void SetAutomaticRotation(bool useAuto);`
- `void TriggerForcedMove(const DirectX::SimpleMath::Vector3& initialVelocity, float duration=0.0f, Mathf::Easing::EaseType curveType = Mathf::Easing::EaseType::None);`
- `void StopForcedMove();`
- `bool IsInForcedMove() const;`
- `void SetLookDirection(const DirectX::SimpleMath::Vector3& direction);`
- `void ClearLookDirection();`

## Public Properties
- `float m_radius = 0.55f;`
- `float m_height = 2.f;`
- `float maxSpeed = 1.025f;`
- `float acceleration = 1.0f;`
- `float staticFriction = 0.4f;`
- `float dynamicFriction = 0.1f;`
- `float jumpSpeed = 0.05f;`
- `float gravityWeight = 0.2f;`
