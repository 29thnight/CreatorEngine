# InputManager

**Header:** `ScriptBinder/InputManager.h`

**Inheritance:** `: public DLLCore::Singleton<InputManager>`

> 자동 생성된 문서입니다. 실제 사용 시 구현 파일을 함께 참고하세요.

## Public Methods
- `bool Initialize(HWND _hwnd);`
- `void Update(float deltaTime);`
- `void KeyBoardUpdate();`
- `bool IsAnyKeyPressed();`
- `void MouseUpdate();`
- `void SetMousePos(POINT pos);`
- `float2 GetMousePos();`
- `float2 GetMouseDelta() const;`
- `bool IsWheelUp();`
- `bool IsWheelDown();`
- `bool IsMouseButtonDown(MouseKey button);`
- `bool IsMouseButtonPressed(MouseKey button);`
- `bool IsMouseButtonReleased(MouseKey button);`
- `void HideCursor();`
- `void ShowCursor();`
- `void ResetMouseDelta();`
- `int16 GetWheelDelta() const;`
- `void PadUpdate();`
- `void GamePadUpdate();`
- `bool IsControllerConnected(DWORD Index);`
- `bool IsControllerButtonDown(DWORD index, ControllerButton btn) const;`
- `bool IsControllerButtonPressed(DWORD index, ControllerButton btn) const;`
- `bool IsControllerButtonReleased(DWORD index, ControllerButton btn) const;`
- `bool IsControllerTriggerL(DWORD index) const;`
- `bool IsControllerTriggerR(DWORD index) const;`
- `Mathf::Vector2 GetControllerThumbL(DWORD index) const;`
- `Mathf::Vector2 GetControllerThumbR(DWORD index) const;`
- `void SetControllerVibration(DWORD Index, float leftMotorSpeed, float rightMotorSpeed, float lowFre, float highFre, float time);`
- `void SetControllerVibration(DWORD Index, float leftMotorSpeed, float rightMotorSpeed, float lowFre, float highFre);`
- `void UpdateControllerVibration(float tick);`
- `void SetControllerVibrationTime(DWORD Index, float time);`

## Public Properties
- `float							deadZone = 0.24f;`
- `float							triggerdeadZone = 0.1f;`
