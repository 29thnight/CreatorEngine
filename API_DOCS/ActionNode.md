# ActionNode

**Header:** `ScriptBinder/BTHeader.h`

**Inheritance:** `: public BTNode`

> 자동 생성된 문서입니다. 실제 사용 시 구현 파일을 함께 참고하세요.

## Public Methods
- `using ActionFunc = std::function<NodeStatus(float, BlackBoard&)>;`
- `ActionNode() = default;`
- `~ActionNode() override = default;`
- `NodeStatus Tick(float deltatime, BlackBoard& blackBoard) override abstract;`

## Public Properties
- (none)
