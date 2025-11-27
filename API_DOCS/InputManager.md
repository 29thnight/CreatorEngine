# InputManager

**Header:** `ScriptBinder/InputManager.h`

**Inheritance:** `: public DLLCore::Singleton<InputManager>`

> 자동 생성된 문서입니다. 실제 사용 시 구현 파일을 함께 참고하세요.

## Public Methods

### Public Methods 역할
| 멤버 | 예상 역할 |
| --- | --- |
| `Initialize` | initialize 동작을 수행합니다. |
| `Update` | update을(를) 갱신합니다. |
| `KeyBoardUpdate` | key board update 동작을 수행합니다. |
| `IsAnyKeyPressed` | any key pressed 여부를 확인합니다. |
| `MouseUpdate` | mouse update 동작을 수행합니다. |
| `SetMousePos` | mouse pos을(를) 설정합니다. |
| `GetMousePos` | mouse pos을(를) 가져옵니다. |
| `GetMouseDelta` | mouse delta을(를) 가져옵니다. |
| `IsWheelUp` | wheel up 여부를 확인합니다. |
| `IsWheelDown` | wheel down 여부를 확인합니다. |
| `IsMouseButtonDown` | mouse button down 여부를 확인합니다. |
| `IsMouseButtonPressed` | mouse button pressed 여부를 확인합니다. |
| `IsMouseButtonReleased` | mouse button released 여부를 확인합니다. |
| `HideCursor` | hide cursor 동작을 수행합니다. |
| `ShowCursor` | show cursor 동작을 수행합니다. |
| `ResetMouseDelta` | reset mouse delta 동작을 수행합니다. |
| `GetWheelDelta` | wheel delta을(를) 가져옵니다. |
| `PadUpdate` | pad update 동작을 수행합니다. |
| `GamePadUpdate` | game pad update 동작을 수행합니다. |
| `IsControllerConnected` | controller connected 여부를 확인합니다. |
| `IsControllerButtonDown` | controller button down 여부를 확인합니다. |
| `IsControllerButtonPressed` | controller button pressed 여부를 확인합니다. |
| `IsControllerButtonReleased` | controller button released 여부를 확인합니다. |
| `IsControllerTriggerL` | controller trigger l 여부를 확인합니다. |
| `IsControllerTriggerR` | controller trigger r 여부를 확인합니다. |
| `GetControllerThumbL` | controller thumb l을(를) 가져옵니다. |
| `GetControllerThumbR` | controller thumb r을(를) 가져옵니다. |
| `SetControllerVibration` | controller vibration을(를) 설정합니다. |
| `UpdateControllerVibration` | controller vibration을(를) 갱신합니다. |
| `SetControllerVibrationTime` | controller vibration time을(를) 설정합니다. |
## Public Properties

### Public Properties 역할
| 멤버 | 예상 역할 |
| --- | --- |
| `deadZone` | dead zone 상태를 보관합니다. |
| `triggerdeadZone` | triggerdead zone 상태를 보관합니다. |
