# BehaviorTreeComponent

**Header:** `ScriptBinder/BehaviorTreeComponent.h`

**Inheritance:** `: 
	public Component, public IAIComponent, public RegistableEvent<BehaviorTreeComponent>`

> 자동 생성된 문서입니다. 실제 사용 시 구현 파일을 함께 참고하세요.

## Public Methods
- `void Initialize() override;`
- `void Awake() override;`
- `void InternalAIUpdate(float deltaSecond) override;`
- `void OnDestroy() override;`
- `BlackBoard* GetBlackBoard();`
- `void GraphToBuild();`

## Public Properties
- `std::string name;`
- `std::string blackBoardName;`
- `FileGuid m_BehaviorTreeGuid;`
- `FileGuid m_BlackBoardGuid;`
