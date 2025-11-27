# ConditionDecoratorNode

**Header:** `ScriptBinder/BTHeader.h`

**Inheritance:** `: public DecoratorNode`

> 자동 생성된 문서입니다. 실제 사용 시 구현 파일을 함께 참고하세요.

## Public Methods
- `using ConditionFunc = std::function<bool(float, const BlackBoard&)>;`
- `ConditionDecoratorNode() = default;`
- `~ConditionDecoratorNode() override = default;`
- `virtual bool ConditionCheck(float deltatime, const BlackBoard& blackBoard) abstract;`

## Public Properties
- (none)
