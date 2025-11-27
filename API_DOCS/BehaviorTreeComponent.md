# BehaviorTreeComponent

**Header:** `ScriptBinder/BehaviorTreeComponent.h`

**Inheritance:** `: 
	public Component, public IAIComponent, public RegistableEvent<BehaviorTreeComponent>`

> 자동 생성된 문서입니다. 실제 사용 시 구현 파일을 함께 참고하세요.

## Public Methods

### Public Methods 역할
| 멤버 | 예상 역할 |
| --- | --- |
| `Initialize` | initialize 동작을 수행합니다. |
| `Awake` | awake 동작을 수행합니다. |
| `InternalAIUpdate` | internal aiupdate 동작을 수행합니다. |
| `OnDestroy` | on destroy 동작을 수행합니다. |
| `GetBlackBoard` | black board을(를) 가져옵니다. |
| `GraphToBuild` | graph to build 동작을 수행합니다. |
## Public Properties

### Public Properties 역할
| 멤버 | 예상 역할 |
| --- | --- |
| `name` | name 상태를 보관합니다. |
| `blackBoardName` | black board name 상태를 보관합니다. |
| `m_BehaviorTreeGuid` | m behavior tree guid 상태를 보관합니다. |
| `m_BlackBoardGuid` | m black board guid 상태를 보관합니다. |
