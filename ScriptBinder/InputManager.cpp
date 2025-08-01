#include "InputManager.h"
#include <wrl.h>
#include <iostream>

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
    m_curKeyStates.Reset();

    ComPtr<IGameInputReading> reading;
    HRESULT hr = gameInput->GetCurrentReading(GameInputKindKeyboard, nullptr, &reading);

    if (FAILED(hr) || !reading)
        return;

    // 현재 눌러진 키만 가져오기
    uint32_t keyCount = reading->GetKeyCount();
    if (keyCount > 0)
    {
        m_GameInputKeyStates.clear();
        m_GameInputKeyStates.resize(keyCount);
        reading->GetKeyState(keyCount, m_GameInputKeyStates.data());

        for (uint32_t i = 0; i < keyCount; ++i)
        {
            uint8_t virtualKey = m_GameInputKeyStates[i].virtualKey;
            if (virtualKey < KEYBOARD_COUNT)
                m_curKeyStates.Set(virtualKey);
        }
    }

    m_keyboardState.Update();
    
}

bool InputManager::IsAnyKeyPressed()
{
    if (m_GameInputKeyStates.size() != 0)
    {

        // std::cout <<  "키가 눌렸습니다 " << std::endl;
        return true;
    }
    return false;
}

void InputManager::MouseUpdate()
{
    ComPtr<IGameInputReading> reading;
    //memset(curmouseState, 0, sizeof(bool) * MOUSE_COUNT);
    m_curMouseState.Reset();
    ResetMouseDelta();
    m_prevMouseWheelDelta = m_mouseWheelDelta;
    // 🔹 현재 마우스 입력 읽기
    HRESULT hr = gameInput->GetCurrentReading(GameInputKindMouse, nullptr, &reading);
    if (FAILED(hr)) 
    {
        std::cout << "마우스 GetCurrentReading 실패!" << std::endl;
        return;
    }

    if (reading->GetMouseState(&m_GameInputMouseState)) 
    {
        m_mousePos.x = m_GameInputMouseState.positionX;
        m_mousePos.y = m_GameInputMouseState.positionY;

        if (m_GameInputMouseState.buttons & GameInputMouseLeftButton)
        {
            m_curMouseState.Set((uint8)MouseKey::LEFT);
        }
        if (m_GameInputMouseState.buttons & GameInputMouseRightButton)
        {
            m_curMouseState.Set((uint8)MouseKey::RIGHT);
        }
        if (m_GameInputMouseState.buttons & GameInputMouseMiddleButton)
        {
            m_curMouseState.Set((uint8)MouseKey::MIDDLE);
        }
       /* curmouseState[1] = (m_GameInputMouseState.buttons & GameInputMouseRightButton) != 0;
        curmouseState[2] = (m_GameInputMouseState.buttons & GameInputMouseMiddleButton) != 0;*/
        //curmouseState[3] = (mouseState.buttons & GameInputMouseXButton1) != 0; 추가버튼 쓸거면 추가필요
        //curmouseState[4] = (mouseState.buttons & GameInputMouseXButton2) != 0;
    }
    
    m_mouseWheelDelta = m_GameInputMouseState.wheelY;
    m_mouseDelta.x = (m_mousePos.x - m_prevMousePos.x) * 0.5f;
    m_mouseDelta.y = (m_mousePos.y - m_prevMousePos.y) * 0.5f;
    m_mouseState.Update();
}

void InputManager::SetMousePos(POINT pos)
{
    m_mousePos.x = pos.x;
    m_mousePos.y = pos.y;
}

float2 InputManager::GetMousePos()
{
    POINT cursorPos;
    GetCursorPos(&cursorPos);
    ScreenToClient(hwnd, &cursorPos);
    m_mousePos.x = cursorPos.x;
    m_mousePos.y = cursorPos.y;
    return m_mousePos;
}

float2 InputManager::GetMouseDelta() const
{
    return m_mouseDelta;
}

short InputManager::GetWheelDelta() const
{
    return m_mouseWheelDelta;
}

bool InputManager::IsWheelUp()
{
    return m_prevMouseWheelDelta < m_mouseWheelDelta;
}

bool InputManager::IsWheelDown()
{
    return m_prevMouseWheelDelta > m_mouseWheelDelta;
}

bool InputManager::IsMouseButtonDown(MouseKey button)
{
    return m_mouseState.GetKeyState(static_cast<size_t>(button)) == KeyState::Down;
}

bool InputManager::IsMouseButtonPressed(MouseKey button)
{
    return m_mouseState.GetKeyState(static_cast<size_t>(button)) == KeyState::Pressed;
}

bool InputManager::IsMouseButtonReleased(MouseKey button)
{
    return m_mouseState.GetKeyState(static_cast<size_t>(button)) == KeyState::Released;
}

void InputManager::HideCursor()
{
    if (!m_isCursorHidden)
    {
        while (::ShowCursor(FALSE) >= 0);  // Keep hiding cursor until it's fully hidden
        m_isCursorHidden = true;
    }
}

void InputManager::ShowCursor()
{
    if (m_isCursorHidden)
    {
        while (::ShowCursor(TRUE) < 0);  // Keep showing cursor until it's fully shown
        m_isCursorHidden = false;
    }
}

void InputManager::ResetMouseDelta()
{
    m_prevMousePos = m_mousePos;
    m_mouseDelta = { 0, 0 };
    //m_mouseWheelDelta = 0;
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
    for (int i = 0; i < MAX_CONTROLLER; ++i)
    {
        m_curPadState[i].Reset();

        if (!device[i])
            continue;

        ComPtr<IGameInputReading> reading;
        HRESULT hr = gameInput->GetCurrentReading(GameInputKindGamepad, device[i], &reading);
        if (FAILED(hr) || !reading)
            continue;

        reading->GetGamepadState(&m_GameInputPadState[i]);
        const auto& buttons = m_GameInputPadState[i].buttons;

        if (buttons & GameInputGamepadA)               m_curPadState[i].Set(static_cast<size_t>(ControllerButton::A));
        if (buttons & GameInputGamepadB)               m_curPadState[i].Set(static_cast<size_t>(ControllerButton::B));
        if (buttons & GameInputGamepadX)               m_curPadState[i].Set(static_cast<size_t>(ControllerButton::X));
        if (buttons & GameInputGamepadY)               m_curPadState[i].Set(static_cast<size_t>(ControllerButton::Y));

        if (buttons & GameInputGamepadDPadUp)          m_curPadState[i].Set(static_cast<size_t>(ControllerButton::DPAD_UP));
        if (buttons & GameInputGamepadDPadDown)        m_curPadState[i].Set(static_cast<size_t>(ControllerButton::DPAD_DOWN));
        if (buttons & GameInputGamepadDPadLeft)        m_curPadState[i].Set(static_cast<size_t>(ControllerButton::DPAD_LEFT));
        if (buttons & GameInputGamepadDPadRight)       m_curPadState[i].Set(static_cast<size_t>(ControllerButton::DPAD_RIGHT));

        if (buttons & GameInputGamepadMenu)            m_curPadState[i].Set(static_cast<size_t>(ControllerButton::START_BUTTON));
        if (buttons & GameInputGamepadView)            m_curPadState[i].Set(static_cast<size_t>(ControllerButton::BACK_BUTTON));

        if (buttons & GameInputGamepadLeftShoulder)    m_curPadState[i].Set(static_cast<size_t>(ControllerButton::LEFT_SHOULDER));
        if (buttons & GameInputGamepadRightShoulder)   m_curPadState[i].Set(static_cast<size_t>(ControllerButton::RIGHT_SHOULDER));
        if (buttons & GameInputGamepadLeftThumbstick)  m_curPadState[i].Set(static_cast<size_t>(ControllerButton::LEFT_THUMB));
        if (buttons & GameInputGamepadRightThumbstick) m_curPadState[i].Set(static_cast<size_t>(ControllerButton::RIGHT_THUMB));

        if (buttons & GameInputGamepadNone)            m_curPadState[i].Set(static_cast<size_t>(ControllerButton::None));

        m_controllerThumbL[i].x = m_GameInputPadState[i].leftThumbstickX;
        m_controllerThumbL[i].y = m_GameInputPadState[i].leftThumbstickY;
        m_controllerThumbR[i].x = m_GameInputPadState[i].rightThumbstickX;
        m_controllerThumbR[i].y = m_GameInputPadState[i].rightThumbstickY;

        m_controllerTriggerL[i] = m_GameInputPadState[i].leftTrigger;
        m_controllerTriggerR[i] = m_GameInputPadState[i].rightTrigger;
    }

    m_padState.Update();
}

bool InputManager::IsControllerConnected(DWORD Index)
{
    return device[Index] != nullptr;
}

bool InputManager::IsControllerButtonDown(DWORD index, ControllerButton btn) const
{
    return m_padState.GetKeyState(index, static_cast<size_t>(btn)) == KeyState::Down;
}

bool InputManager::IsControllerButtonPressed(DWORD index, ControllerButton btn) const
{
    return m_padState.GetKeyState(index, static_cast<size_t>(btn)) == KeyState::Pressed;
}

bool InputManager::IsControllerButtonReleased(DWORD index, ControllerButton btn) const
{
    return m_padState.GetKeyState(index, static_cast<size_t>(btn)) == KeyState::Released;
}

bool InputManager::IsControllerTriggerL(DWORD index) const
{
    return m_controllerTriggerL[index] > triggerdeadZone;
}

bool InputManager::IsControllerTriggerR(DWORD index) const
{
    return m_controllerTriggerR[index] > triggerdeadZone;
}

Mathf::Vector2 InputManager::GetControllerThumbL(DWORD index) const
{
    float2 stick(m_controllerThumbL[index].x, m_controllerThumbL[index].y);

    if (std::abs(stick.x) < deadZone) stick.x = 0.0f;
    if (std::abs(stick.y) < deadZone) stick.y = 0.0f;

    return stick;

}

Mathf::Vector2 InputManager::GetControllerThumbR(DWORD index) const
{
    float2 stick(m_controllerThumbR[index].x, m_controllerThumbR[index].y);
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
    		if (m_controllerVibrationTime[i] > 0.0f)
    		{
    			m_controllerVibrationTime[i] -= tick;
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
    
    m_controllerVibrationTime[Index] = time;
}



