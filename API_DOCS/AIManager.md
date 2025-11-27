# AIManager

**Header:** `ScriptBinder/AIManager.h`

**Inheritance:** `: public Singleton<AIManager>`

> 자동 생성된 문서입니다. 실제 사용 시 구현 파일을 함께 참고하세요.

## Public Methods
- `BlackBoard* CreateBlackBoard(const std::string& aiName);`
- `void RemoveBlackBoard(const std::string& aiName);`
- `void RegisterAIComponent(GameObject* gameObject, IAIComponent* aiComponent);`
- `void UnRegisterAIComponent(GameObject* gameObject, IAIComponent* aiComponent);`
- `void InternalAIUpdate(float deltaSeconds);`
- `BT::BTNode::NodePtr CreateNode(std::string_view nodeName);`
- `void ClearTreeInAIComponent();`
- `void InitalizeBehaviorTreeSystem();`

## Public Properties
- (none)
