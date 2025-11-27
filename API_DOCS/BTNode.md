# BTNode

**Header:** `ScriptBinder/BTHeader.h`

**Inheritance:** `: public Managed::HeapObject`

> 자동 생성된 문서입니다. 실제 사용 시 구현 파일을 함께 참고하세요.

## Public Methods
- `BTNode() = default;`
- `virtual ~BTNode() = default;`
- `virtual NodeStatus Tick(float deltatime,BlackBoard& blackBoard) = 0;`
- `virtual BehaviorNodeType GetNodeType() const = 0;`

## Public Properties
- `using NodePtr = std::shared_ptr<BTNode>;`
