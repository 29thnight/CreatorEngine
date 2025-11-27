# BoxColliderComponent

**Header:** `ScriptBinder/BoxColliderComponent.h`

**Inheritance:** `: public Component, public ICollider, public RegistableEvent<BoxColliderComponent>`

> 자동 생성된 문서입니다. 실제 사용 시 구현 파일을 함께 참고하세요.

## Public Methods
- `virtual ~BoxColliderComponent() = default;`
- `void SetPositionOffset(DirectX::SimpleMath::Vector3 pos) override;`
- `DirectX::SimpleMath::Vector3 GetPositionOffset() override;`
- `void SetRotationOffset(DirectX::SimpleMath::Quaternion rotation) override;`
- `DirectX::SimpleMath::Quaternion GetRotationOffset() override;`

## Public Properties
- `float staticFriction = 0.5f;`
- `float dynamicFriction = 0.4f;`
- `float restitution = 0.3f;`
- `float density = 10.0f;`
- `BoxColliderInfo m_Info;`
