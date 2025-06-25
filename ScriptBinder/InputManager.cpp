#include "InputManager.h"
#include <wrl.h>

#pragma comment(lib, "GameInput.lib")
using namespace Microsoft::WRL;

bool InputManager::Initialize(HWND _hwnd)
{
    if (FAILED(GameInputCreate(&gameInput))) {
        return false;
    }
    else
    {
        hwnd = _hwnd;
        std::cout << "GameInput System NewCreateSceneInitialize succeed" << std::endl;
        return true;
    }

    SetControllerVibrationTime(0,2.0f);
    return false;
}

void InputManager::Update(float deltaTime)
{
    PadUpdate();

    KeyBoardUpdate();
    MouseUpdate();
    GamePadUpdate();
 
}

void InputManager::KeyBoardUpdate()
{
    ComPtr<IGameInputReading> reading;

    //현재 키입력 초기화
    memset(curkeyStates, 0, sizeof(bool) * KEYBOARD_COUNT);
    HRESULT hr = gameInput->GetCurrentReading(GameInputKindKeyboard, nullptr, &reading);
    if (FAILED(hr)) {
        std::cout << "키보드 GetCurrentReading 실패!" << std::endl;
    }

    uint32_t keyCount = reading->GetKeyCount();
    GkeyStates.resize(keyCount);

    if (SUCCEEDED(reading->GetKeyState(keyCount, GkeyStates.data()))) {

        for (int i = 0; i < keyCount; i++)
        {
            curkeyStates[GkeyStates[i].virtualKey] = true;
        }
    }

    keyboardstate.Update();
}

bool InputManager::IsKeyDown(unsigned int key) const
{
    return keyboardstate.GetKeyState(key) == KeyState::Down;
}

bool InputManager::IsKeyPressed(unsigned int key) const
{
    return keyboardstate.GetKeyState(key) == KeyState::Pressed;
  
}

bool InputManager::IsKeyReleased(unsigned int key) const
{
    return keyboardstate.GetKeyState(key) == KeyState::Released;
}



bool InputManager::IsAnyKeyPressed()
{
    if (GkeyStates.size() != 0)
    {

        // std::cout <<  "키가 눌렸습니다 " << std::endl;
        return true;
    }
    return false;
}

bool InputManager::changeKeySet(KeyBoard& changekey)
{

   
    KeyBoardUpdate();
    if (IsAnyKeyPressed())
    {
        for (auto& keyState : GkeyStates)
        {

            pressKey = static_cast<KeyBoard>(keyState.virtualKey);
            break;
        }
        if (pressKey != KeyBoard::None)
        {
            changekey = pressKey;
            pressKey = KeyBoard::None;
            std::cout << "키 변경 완료: " << std::endl;
            //현재 키입력 초기화
            memset(curkeyStates, 0, sizeof(bool) * KEYBOARD_COUNT);
            return true;
        }
    }
    return false;
}

void InputManager::MouseUpdate()
{
    ComPtr<IGameInputReading> reading;
    memset(curmouseState, 0, sizeof(bool) * mouseCount);
    ResetMouseDelta();
    _premouseWheelDelta = _mouseWheelDelta;
    // 🔹 현재 마우스 입력 읽기
    HRESULT hr = gameInput->GetCurrentReading(GameInputKindMouse, nullptr, &reading);
    if (FAILED(hr)) {
        std::cout << "마우스 GetCurrentReading 실패!" << std::endl;
        return;
    }

    if (SUCCEEDED(reading->GetMouseState(&GmouseState))) {


        _mousePos.x = GmouseState.positionX;
        _mousePos.y = GmouseState.positionY;
        curmouseState[0] = (GmouseState.buttons & GameInputMouseLeftButton) != 0;
        curmouseState[1] = (GmouseState.buttons & GameInputMouseRightButton) != 0;
        curmouseState[2] = (GmouseState.buttons & GameInputMouseMiddleButton) != 0;
        //curmouseState[3] = (mouseState.buttons & GameInputMouseXButton1) != 0; 추가버튼 쓸거면 추가필요
        //curmouseState[4] = (mouseState.buttons & GameInputMouseXButton2) != 0;


    }
    
    _mouseWheelDelta = GmouseState.wheelY;
    _mouseDelta.x = (_mousePos.x - _prevMousePos.x) * 0.5f;
    _mouseDelta.y = (_mousePos.y - _prevMousePos.y) * 0.5f;
    mousestate.Update();
}

void InputManager::SetMousePos(POINT pos)
{
    _mousePos.x = pos.x;
    _mousePos.y = pos.y;
}

float2 InputManager::GetMousePos()
{
    POINT cursorPos;
    GetCursorPos(&cursorPos);
    ScreenToClient(hwnd, &cursorPos);
    _mousePos.x = cursorPos.x;
    _mousePos.y = cursorPos.y;
    return _mousePos;
}

float2 InputManager::GetMouseDelta() const
{
  
    return _mouseDelta;
}

short InputManager::GetWheelDelta() const
{
    return _mouseWheelDelta;
}

bool InputManager::IsWheelUp()
{
    return _premouseWheelDelta < _mouseWheelDelta;
}

bool InputManager::IsWheelDown()
{
    return _premouseWheelDelta > _mouseWheelDelta;
}

bool InputManager::IsMouseButtonDown(MouseKey button)
{
    return mousestate.GetKeyState(static_cast<size_t>(button)) == KeyState::Down;
}

bool InputManager::IsMouseButtonPressed(MouseKey button)
{
    return mousestate.GetKeyState(static_cast<size_t>(button)) == KeyState::Pressed;
   
}

bool InputManager::IsMouseButtonReleased(MouseKey button)
{
    return mousestate.GetKeyState(static_cast<size_t>(button)) == KeyState::Released;
}

void InputManager::HideCursor()
{
    if (!_isCursorHidden)
    {
        while (::ShowCursor(FALSE) >= 0);  // Keep hiding cursor until it's fully hidden
        _isCursorHidden = true;
    }
}

void InputManager::ShowCursor()
{
    if (_isCursorHidden)
    {
        while (::ShowCursor(TRUE) < 0);  // Keep showing cursor until it's fully shown
        _isCursorHidden = false;
    }
}

void InputManager::ResetMouseDelta()
{
    _prevMousePos = _mousePos;
    _mouseDelta = { 0, 0 };
    _mouseWheelDelta = 0;
}

void InputManager::PadUpdate()
{
    ComPtr<IGameInputReading> reading;
    HRESULT hr = gameInput->GetCurrentReading(GameInputKindGamepad, nullptr, &reading);
    if (FAILED(hr))
    {
        return;
    }
    ComPtr<IGameInputDevice> tempDevice;
    if (reading.Get() == nullptr)
        return;
    reading->GetDevice(&tempDevice);
    if (FAILED(hr) || tempDevice == nullptr)
    {
        return;
    }
    bool found = false;
    for (int i = 0; i < MAX_CONTROLLER; i++)
    {
        if (device[i] != nullptr && device[i] == tempDevice.Get())
        {
            found = true;
            break;
        }
    }
    if (!found)
    {
        for (int i = 0; i < MAX_CONTROLLER; i++)
        {
            if (device[i] == nullptr)
            {
                device[i] = tempDevice.Get();
                break;
            }
        }
    }
}

void InputManager::GamePadUpdate()
{
    // 연결된 게임패드 상태를 읽을 IGameInputReading 객체


    //memset(curpadState, 0, sizeof(bool) * padKeyCount);

    for (int i = 0; i < MAX_CONTROLLER; i++)
    {
        ComPtr<IGameInputReading> reading;
        memset(curpadState[i], 0, sizeof(bool) * padKeyCount);
        HRESULT hr = gameInput->GetCurrentReading(GameInputKindGamepad, device[i], &reading);
        if (FAILED(hr)) {
            //std::cout << "게임패드 GetCurrentReading 실패!" << std::endl;
            return;
        }

        if (device[i] == nullptr)
        {
            continue;
        }
        // 각 게임패드에 대해 상태를 추적
        reading->GetGamepadState(&GpadState[i]);
        
        
        curpadState[i][0] = (GpadState[i].buttons & GameInputGamepadA) != 0;
        curpadState[i][1] = (GpadState[i].buttons & GameInputGamepadB) != 0;
        curpadState[i][2] = (GpadState[i].buttons & GameInputGamepadX) != 0;
        curpadState[i][3] = (GpadState[i].buttons & GameInputGamepadY) != 0;
        curpadState[i][4] = (GpadState[i].buttons & GameInputGamepadDPadUp) != 0;
        curpadState[i][5] = (GpadState[i].buttons & GameInputGamepadDPadDown) != 0;
        curpadState[i][6] = (GpadState[i].buttons & GameInputGamepadDPadLeft) != 0;
        curpadState[i][7] = (GpadState[i].buttons & GameInputGamepadDPadRight) != 0;

        curpadState[i][8] = (GpadState[i].buttons & GameInputGamepadMenu) != 0;
        curpadState[i][9] = (GpadState[i].buttons & GameInputGamepadView) != 0;

        curpadState[i][10] = (GpadState[i].buttons & GameInputGamepadLeftShoulder) != 0;
        curpadState[i][11] = (GpadState[i].buttons & GameInputGamepadRightShoulder) != 0;
        curpadState[i][12] = (GpadState[i].buttons & GameInputGamepadLeftThumbstick) != 0;
        curpadState[i][13] = (GpadState[i].buttons & GameInputGamepadRightThumbstick) != 0;
        
        curpadState[i][14] = (GpadState[i].buttons & GameInputGamepadNone) != 0;

        _controllerThumbL[i].x = (GpadState[i].leftThumbstickX);
        _controllerThumbL[i].y = (GpadState[i].leftThumbstickY);
        _controllerThumbR[i].x = (GpadState[i].rightThumbstickX);
        _controllerThumbR[i].y = (GpadState[i].rightThumbstickY);
        _controllerTriggerL[i] = (GpadState[i].leftTrigger);
        _controllerTriggerR[i] = (GpadState[i].rightTrigger);
    }
    padState.Update();
}

bool InputManager::IsControllerConnected(DWORD Index)
{
    return device[Index] != nullptr;
}

bool InputManager::IsControllerButtonDown(DWORD index, ControllerButton btn) const
{
    return padState.GetKeyState(index, static_cast<size_t>(btn)) == KeyState::Down;
}

bool InputManager::IsControllerButtonPressed(DWORD index, ControllerButton btn) const
{
    return padState.GetKeyState(index, static_cast<size_t>(btn)) == KeyState::Pressed;
}

bool InputManager::IsControllerButtonReleased(DWORD index, ControllerButton btn) const
{
    return padState.GetKeyState(index, static_cast<size_t>(btn)) == KeyState::Released;
}

bool InputManager::IsControllerTriggerL(DWORD index) const
{
    return   _controllerTriggerL[index] > triggerdeadZone;
}

bool InputManager::IsControllerTriggerR(DWORD index) const
{
    return   _controllerTriggerR[index] > triggerdeadZone;
}

Mathf::Vector2 InputManager::GetControllerThumbL(DWORD index) const
{
    float2 stick(_controllerThumbL[index].x, _controllerThumbL[index].y);

    if (std::abs(stick.x) < deadZone) stick.x = 0.0f;
    if (std::abs(stick.y) < deadZone) stick.y = 0.0f;

    return stick;

}

Mathf::Vector2 InputManager::GetControllerThumbR(DWORD index) const
{
    float2 stick(_controllerThumbR[index].x, _controllerThumbR[index].y);
    if (std::abs(stick.x) < deadZone) stick.x = 0.0f;
    if (std::abs(stick.y) < deadZone) stick.y = 0.0f;

    return stick;
}

void InputManager::SetControllerVibration(DWORD Index, float leftMotorSpeed, float rightMotorSpeed, float lowFre, float highFre)
{
    GameInputRumbleParams vibration = {};
    vibration.lowFrequency = lowFre;      // 저주파 모터 진동 강도
    vibration.highFrequency = highFre;    // 고주파 모터 진동 강도
    vibration.leftTrigger = leftMotorSpeed;   // 왼쪽 트리거 진동 강도
    vibration.rightTrigger = rightMotorSpeed;
    for (int _index = 0;_index < MAX_CONTROLLER; _index++)
    {
        if (device[_index] == nullptr) continue;
        device[_index]->SetRumbleState(&vibration);
    }
}

void InputManager::UpdateControllerVibration(float tick)
{
    for (DWORD i = 0; i < MAX_CONTROLLER; ++i)
    {
    	if (device[i])
    	{
            if (device[i] == nullptr) continue;
    		if (_controllerVibrationTime[i] > 0.0f)
    		{
    			_controllerVibrationTime[i] -= tick;
    			//SetControllerVibration(i, _controllerVibration[i].wLeftMotorSpeed, _controllerVibration[i].wRightMotorSpeed);
    			SetControllerVibration(i, 0.5f, 0.5f,0.5f, 0.5f);
    		}
    		else
    		{
    			SetControllerVibration(i, 0.5f, 0.5f, 0.5f, 0.5f);
    		}
    	}
    }
}

void InputManager::SetControllerVibrationTime(DWORD Index, float time)
{
    
    _controllerVibrationTime[Index] = time;
}



