# PlayerInputComponent

**Header:** `ScriptBinder/PlayerInput.h`

**Inheritance:** `: public Component, public RegistableEvent<PlayerInputComponent>`

> 자동 생성된 문서입니다. 실제 사용 시 구현 파일을 함께 참고하세요.

## Public Methods
- `void Update(float tick) override;`
- `void SetActionMap(std::string mapName);`
- `void SetActionMap(ActionMap* _actionMap);`
- `void SetControllerVibration(float tick, float leftMotorSpeed, float rightMotorSpeed, float lowFre, float highFre);`
- `void SetControllerVibration(float tick, float power);`

## Public Properties
- `ActionMap* m_actionMap = nullptr;`
- `std::string m_actionMapName = "None";`
- `int controllerIndex = 0;`
