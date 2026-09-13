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
    // ★ Initialize 가 실패하면 gameInput 은 널로 남는다. 이 아래 넷이 전부
    //   그것을 역참조하므로 여기서 막는다.
    //
    //   실제로 그런 기계가 있다: GameInput 헤더가 API v3 를 요구하는데
    //   시스템의 gameinput.dll 이 구버전이면(GameInputInitialize 진입점이
    //   없다) 생성이 실패한다. 그때 입력만 죽어야지 엔진이 죽으면 안 된다.
    if (nullptr == gameInput)
    {
        return;
    }

    PadUpdate();

    KeyBoardUpdate();
    MouseUpdate();
    GamePadUpdate();
    UpdateControllerVibration(deltaTime);
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

math::vector2 InputManager::GetMousePos()
{
    POINT cursorPos;
    GetCursorPos(&cursorPos);
    ScreenToClient(hwnd, &cursorPos);
    m_mousePos.x = cursorPos.x;
    m_mousePos.y = cursorPos.y;
    return m_mousePos;
}

math::vector2 InputManager::GetMouseDelta() const
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

// ── 게임 입력 소유 관문 (PHASE 21 W5 선행 2) ──
//
// GameInput 은 창 포커스도 ImGui 도 보지 않는다 — 장치 상태를 그대로 읽는다.
// 그래서 에디터에서는 "지금 게임이 입력을 받아도 되는가" 를 아무도 답하지
// 않았다: Alt-Tab 으로 나가도, 인스펙터에 글자를 치는 중에도, 정지 상태에서도
// 게임 스크립트는 같은 키를 봤다. 이 관문이 그 답이다.
//
//   · 주인은 Editor 의 PlayModeController 하나다. Player 는 부르지 않으므로
//     기본값 true 그대로 — 출하 게임의 입력은 이 줄이 생기기 전과 같다.
//   · 관문은 **게임 소비처**만 본다(ActionMap · UIManager · C# Api_Input_*).
//     에디터 자신의 단축키·씬 카메라는 같은 장치 상태를 계속 읽는다 — 둘을
//     한 상태로 갈라 두면 게임에 넘긴 프레임에 에디터 단축키가 죽는다.
//   · 커서 숨김은 소유권에 묶인다. 게임이 숨기기를 **원한다**는 사실은 기억하되
//     소유가 아닐 때는 적용하지 않고, 소유가 돌아오면 다시 적용한다. 그래서
//     Eject·Pause·포커스 상실에서 커서가 사라진 채 남지 않는다.
//
// (헤더가 옛 CP949 이중 인코딩 잔재라 한글을 넣을 수 없어 설명이 여기 있다.)
void InputManager::HideCursor()
{
    // 의사와 적용을 가른다. 게임이 숨기기를 원한 사실은 소유가 아닐 때도 남고,
    // 소유가 돌아올 때 그대로 적용된다.
    m_wantCursorHidden = true;
    if (m_gameInputOwned) ApplyCursorHidden(true);
}

void InputManager::ShowCursor()
{
    m_wantCursorHidden = false;
    ApplyCursorHidden(false);
}

void InputManager::SetGameInputOwned(bool owned)
{
    if (m_gameInputOwned == owned) return;
    m_gameInputOwned = owned;
    ApplyCursorHidden(owned && m_wantCursorHidden);
}

void InputManager::ApplyCursorHidden(bool hidden)
{
    if (hidden == m_isCursorHidden) return;
    // ShowCursor 는 프로세스 전역 카운터라 한 번으로는 안 바뀔 수 있다 —
    // 원하는 쪽으로 넘어갈 때까지 돌린다.
    if (hidden) { while (::ShowCursor(FALSE) >= 0); }
    else        { while (::ShowCursor(TRUE) < 0); }
    m_isCursorHidden = hidden;
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

math::vector2 InputManager::GetControllerThumbL(DWORD index) const
{
    math::vector2 stick(m_controllerThumbL[index].x, m_controllerThumbL[index].y);

    if (std::abs(stick.x) < deadZone) stick.x = 0.0f;
    if (std::abs(stick.y) < deadZone) stick.y = 0.0f;

    return stick;

}

math::vector2 InputManager::GetControllerThumbR(DWORD index) const
{
    math::vector2 stick(m_controllerThumbR[index].x, m_controllerThumbR[index].y);
    if (std::abs(stick.x) < deadZone) stick.x = 0.0f;
    if (std::abs(stick.y) < deadZone) stick.y = 0.0f;

    return stick;
}

void InputManager::SetControllerVibration(DWORD Index, float leftMotorSpeed, float rightMotorSpeed, float lowFre, float highFre,float time)
{
    GameInputRumbleParams vibration = {};
    

    if (m_controllerVibrationTime[Index] > 0.f) //현재진행중인 바이브레이션이있으면 더 강한진동일떄만 적용
    {

        if (leftMotorSpeed >= vibrations[Index].z)
        {
            vibration.lowFrequency = lowFre;      // 저주파 모터 진동 강도
            vibration.highFrequency = highFre;    // 고주파 모터 진동 강도
            vibration.leftTrigger = leftMotorSpeed;   // 왼쪽 트리거 진동 강도
            vibration.rightTrigger = rightMotorSpeed;
            vibrations[Index].x = lowFre;
            vibrations[Index].y = highFre;
            vibrations[Index].z = leftMotorSpeed;
            vibrations[Index].w = rightMotorSpeed;
            if (device[Index] == nullptr) return;
            device[Index]->SetRumbleState(&vibration);
            m_controllerVibrationTime[Index] = time;
        }
    }
    else //없으면 그냥적용
    {
        vibration.lowFrequency = lowFre;      // 저주파 모터 진동 강도
        vibration.highFrequency = highFre;    // 고주파 모터 진동 강도
        vibration.leftTrigger = leftMotorSpeed;   // 왼쪽 트리거 진동 강도
        vibration.rightTrigger = rightMotorSpeed;

        vibrations->x = lowFre;
        vibrations->y = highFre;
        vibrations->z = leftMotorSpeed;
        vibrations->w = rightMotorSpeed;
        if (device[Index] == nullptr) return;
        device[Index]->SetRumbleState(&vibration);
        m_controllerVibrationTime[Index] = time;
    }
    
    
}

void InputManager::SetControllerVibration(DWORD Index, float leftMotorSpeed, float rightMotorSpeed, float lowFre, float highFre)
{
    GameInputRumbleParams vibration = {};

    vibration.lowFrequency = lowFre;      // 저주파 모터 진동 강도
    vibration.highFrequency = highFre;    // 고주파 모터 진동 강도
    vibration.leftTrigger = leftMotorSpeed;   // 왼쪽 트리거 진동 강도
    vibration.rightTrigger = rightMotorSpeed;

    vibrations->x = lowFre;
    vibrations->y = highFre;
    vibrations->z = leftMotorSpeed;
    vibrations->w = rightMotorSpeed;
    if (device[Index] == nullptr) return;
    device[Index]->SetRumbleState(&vibration);
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
    		}
    		else
    		{
                m_controllerVibrationTime[i] = 0.f;
                GameInputRumbleParams vibration = {};
                device[i]->SetRumbleState(&vibration);
    		}
    	}
    }
}

void InputManager::SetControllerVibrationTime(DWORD Index, float time)
{
    
    m_controllerVibrationTime[Index] = time;
}



