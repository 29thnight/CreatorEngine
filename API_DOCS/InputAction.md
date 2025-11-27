# InputAction

**Header:** `ScriptBinder/InputAction.h`

> 자동 생성된 문서입니다. 실제 사용 시 구현 파일을 함께 참고하세요.

## Public Methods
- `InputAction() = default;`
- `void SetControllerButton(ControllerButton _btn);`
- `std::function<void()> buttonAction;`
- `std::function<void(std::any)> valueAction;`

## Public Properties
- `std::string actionName;`
- `InputType inputType= InputType::KeyBoard;`
- `ActionType actionType = ActionType::Button;`
- `KeyState  keystate  = KeyState::Down;`
- `InputValueType valueType = InputValueType::Vector2;`
- `size_t playerIndex = 0;`
- `InputValue value;`
- `std::string objName;`
- `std::string m_scriptName  = "None";`
- `std::string funName = "None";`
- `ControllerButton m_controllerButton = ControllerButton::None;`
