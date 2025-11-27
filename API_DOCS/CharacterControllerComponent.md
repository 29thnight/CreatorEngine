# CharacterControllerComponent

**Header:** `ScriptBinder/CharacterControllerComponent.h`

**Inheritance:** `: public Component, public ICollider, public RegistableEvent<CharacterControllerComponent>`

> 자동 생성된 문서입니다. 실제 사용 시 구현 파일을 함께 참고하세요.

## Public Methods

### Public Methods 역할
| 멤버 | 예상 역할 |
| --- | --- |
| `OnStart` | on start 동작을 수행합니다. |
| `OnFixedUpdate` | on fixed update 동작을 수행합니다. |
| `OnLateUpdate` | on late update 동작을 수행합니다. |
| `ForcedSetPosition` | forced set position 동작을 수행합니다. |
| `SetAutomaticRotation` | automatic rotation을(를) 설정합니다. |
| `TriggerForcedMove` | trigger forced move 동작을 수행합니다. |
| `StopForcedMove` | stop forced move 동작을 수행합니다. |
| `IsInForcedMove` | in forced move 여부를 확인합니다. |
| `SetLookDirection` | look direction을(를) 설정합니다. |
| `ClearLookDirection` | look direction을(를) 비웁니다. |
## Public Properties

### Public Properties 역할
| 멤버 | 예상 역할 |
| --- | --- |
| `m_radius` | m radius 상태를 보관합니다. |
| `m_height` | m height 상태를 보관합니다. |
| `maxSpeed` | max speed 상태를 보관합니다. |
| `acceleration` | acceleration 상태를 보관합니다. |
| `staticFriction` | static friction 상태를 보관합니다. |
| `dynamicFriction` | dynamic friction 상태를 보관합니다. |
| `jumpSpeed` | jump speed 상태를 보관합니다. |
| `gravityWeight` | gravity weight 상태를 보관합니다. |
