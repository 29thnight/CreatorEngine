# PhysicsManager

**Header:** `ScriptBinder/PhysicsManager.h`

**Inheritance:** `: public DLLCore::Singleton<PhysicsManager>`

> 자동 생성된 문서입니다. 실제 사용 시 구현 파일을 함께 참고하세요.

## Public Methods

### Public Methods 역할
| 멤버 | 예상 역할 |
| --- | --- |
| `Initialize` | initialize 동작을 수행합니다. |
| `Update` | update을(를) 갱신합니다. |
| `Shutdown` | shutdown 동작을 수행합니다. |
| `ChangeScene` | change scene 동작을 수행합니다. |
| `OnLoadScene` | on load scene 동작을 수행합니다. |
| `OnUnloadScene` | on unload scene 동작을 수행합니다. |
| `ProcessCallback` | process callback 동작을 수행합니다. |
| `RayCast` | ray cast 동작을 수행합니다. |
| `Raycast` | raycast 동작을 수행합니다. |
| `BoxSweep` | box sweep 동작을 수행합니다. |
| `SphereSweep` | sphere sweep 동작을 수행합니다. |
| `CapsuleSweep` | capsule sweep 동작을 수행합니다. |
| `BoxOverlap` | box overlap 동작을 수행합니다. |
| `SphereOverlap` | sphere overlap 동작을 수행합니다. |
| `CapsuleOverlap` | capsule overlap 동작을 수행합니다. |
| `SaveCollisionMatrix` | collision matrix을(를) 저장합니다. |
| `LoadCollisionMatrix` | collision matrix을(를) 불러옵니다. |
| `SetRigidBodyState` | rigid body state을(를) 설정합니다. |
| `IsRigidBodyKinematic` | rigid body kinematic 여부를 확인합니다. |
| `IsRigidBodyTrigger` | rigid body trigger 여부를 확인합니다. |
| `IsRigidBodyColliderEnabled` | rigid body collider enabled 여부를 확인합니다. |
| `IsRigidBodyUseGravity` | rigid body use gravity 여부를 확인합니다. |
| `ApplyForcedMoveToCCT` | forced move to cct을(를) 적용합니다. |
| `StopForcedMoveOnCCT` | stop forced move on cct 동작을 수행합니다. |
| `IsInForcedMove` | in forced move 여부를 확인합니다. |
| `SetControllerPosition` | controller position을(를) 설정합니다. |
## Public Properties

### Public Properties 역할
| 멤버 | 예상 역할 |
| --- | --- |
| `Scene` | scene 상태를 보관합니다. |
| `ColliderID` | collider id 상태를 보관합니다. |
