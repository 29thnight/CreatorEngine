# GameObject

**Header:** `ScriptBinder/GameObject.h`

**Inheritance:** `: public Object, public std::enable_shared_from_this<GameObject>`

> 자동 생성된 문서입니다. 실제 사용 시 구현 파일을 함께 참고하세요.

## Public Methods

### Public Methods 역할
| 멤버 | 예상 역할 |
| --- | --- |
| `INVALID_INDEX` | invalid index 동작을 수행합니다. |
| `GameObject` | game object 동작을 수행합니다. |
| `operator` | operator 동작을 수행합니다. |
| `~GameObject` | game object 동작을 수행합니다. |
| `RemoveSuffixNumberTag` | suffix number tag을(를) 제거합니다. |
| `SetTag` | tag을(를) 설정합니다. |
| `SetLayer` | layer을(를) 설정합니다. |
| `Destroy` | destroy을(를) 파괴합니다. |
| `AddComponent` | component을(를) 추가합니다. |
| `AddScriptComponent` | script component을(를) 추가합니다. |
| `GetComponent` | component을(를) 가져옵니다. |
| `GetComponentByTypeID` | component by type id을(를) 가져옵니다. |
| `RefreshComponentIdIndices` | component id indices을(를) 새로 고칩니다. |
| `AddChild` | child을(를) 추가합니다. |
| `GetComponentDynamicCast` | component dynamic cast을(를) 가져옵니다. |
| `GetComponentsInChildren` | components in children을(를) 가져옵니다. |
| `GetComponentsInchildrenDynamicCast` | components inchildren dynamic cast을(를) 가져옵니다. |
| `HasComponent` | has component 동작을 수행합니다. |
| `GetComponents` | components을(를) 가져옵니다. |
| `RemoveComponent` | component을(를) 제거합니다. |
| `RemoveComponentIndex` | component index을(를) 제거합니다. |
| `RemoveComponentTypeID` | component type id을(를) 제거합니다. |
| `RemoveScriptComponent` | script component을(를) 제거합니다. |
| `Find` | find을(를) 탐색합니다. |
| `FindIndex` | index을(를) 탐색합니다. |
| `FindInstanceID` | instance id을(를) 탐색합니다. |
| `FindAttachedID` | attached id을(를) 탐색합니다. |
| `OwnerSceneFind` | owner scene find 동작을 수행합니다. |
| `OwnerSceneFindIndex` | owner scene find index 동작을 수행합니다. |
| `OwnerSceneFindInstanceID` | owner scene find instance id 동작을 수행합니다. |
| `OwnerSceneFindAttachedID` | owner scene find attached id 동작을 수행합니다. |
| `SetEnabled` | enabled을(를) 설정합니다. |
| `SetCollisionType` | collision type을(를) 설정합니다. |
## Public Properties

### Public Properties 역할
| 멤버 | 예상 역할 |
| --- | --- |
| `Index` | index 상태를 보관합니다. |
| `m_collisionType` | m collision type 상태를 보관합니다. |
| `m_childrenIndices` | m children indices 상태를 보관합니다. |
