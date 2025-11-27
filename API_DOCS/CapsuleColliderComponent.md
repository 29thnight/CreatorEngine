# CapsuleColliderComponent

**Header:** `ScriptBinder/CapsuleColliderComponent.h`

**Inheritance:** `: public Component, public ICollider, public RegistableEvent<CapsuleColliderComponent>`

> 자동 생성된 문서입니다. 실제 사용 시 구현 파일을 함께 참고하세요.

## Public Methods
- `virtual ~CapsuleColliderComponent() = default;`
- `void OnTriggerEnter(ICollider* other) override;`
- `void OnTriggerStay(ICollider* other) override;`
- `void OnTriggerExit(ICollider* other) override;`
- `void OnCollisionEnter(ICollider* other) override;`
- `void OnCollisionStay(ICollider* other) override;`
- `void OnCollisionExit(ICollider* other) override;`

## Public Properties
- `float staticFriction = 0.5f;`
- `float dynamicFriction = 0.4f;`
- `float restitution = 0.3f;`
- `float density = 10.0f;`
