# StateMachineComponent

**Header:** `ScriptBinder/StateMachineComponent.h`

**Inheritance:** `:public Component, public IAIComponent`

> 자동 생성된 문서입니다. 실제 사용 시 구현 파일을 함께 참고하세요.

## Public Methods
- `using ConditionFunc = std::function<bool(const BlackBoard&)>;`
- `virtual ~StateMachineComponent() = default;`
- `void Initialize() override;`
- `FSM::FSMState* AddState(const std::string& name);`
- `void RemoveState(FSM::FSMState* state);`
- `FSM::Transition* AddTransition(FSM::FSMState* from, FSM::FSMState* to, ConditionFunc condition);`
- `void RemoveTransition(FSM::Transition* transition);`
- `FSM::FSMState* FindStateByName(const std::string& name) const;`

## Public Properties
- `std::string name;`
