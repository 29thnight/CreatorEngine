//#include "pch.h"
#include "GameInputManager.h"
#include <wrl.h>

#pragma comment(lib, "GameInput.lib")
using namespace Microsoft::WRL;

bool GameInputManager::Initialize(HWND _hwnd)
{
    if (FAILED(GameInputCreate(&gameInput))) {
        return false;
    }
    else
    {
        hwnd = _hwnd;
        std::cout << "게임인풋 초기화 성공" << std::endl;
        return true;
    }

    return false;
}

void GameInputManager::Update()
{
    PadUpdate();

    KeyBoardUpdate();
    MouseUpdate();
    GamePadUpdate();

    if (IsPadbtnHold(0, GamePade::AButton))
    {
        std::cout << "승룡권!!" << std::endl;
    }


    if (IsPadbtnHold(0, GamePade::BButton))
    {
        std::cout << "초풍!!" << std::endl;
    }
}


void GameInputManager::KeyBoardUpdate()
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

bool GameInputManager::KeyHold(KeyBoard key)
{
    return keyboardstate.GetKeyState(key) == KeyState::Hold;
}

bool GameInputManager::KeyPress(KeyBoard key)
{
    return keyboardstate.GetKeyState(key) == KeyState::Pressed;
}

bool GameInputManager::KeyRelease(KeyBoard key)
{
    return keyboardstate.GetKeyState(key) == KeyState::Released;
}



bool GameInputManager::IsAnyKeyPressed()
{
    if (GkeyStates.size() != 0)
    {

        // std::cout <<  "키가 눌렸습니다 " << std::endl;
        return true;
    }
    return false;
}

bool GameInputManager::changeKeySet(KeyBoard& changekey)
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

void GameInputManager::MouseUpdate()
{
    ComPtr<IGameInputReading> reading;
    memset(curmouseState, 0, sizeof(bool) * mouseCount);
    _premouseWheelDelta = _mouseWheelDelta;
    // 🔹 현재 마우스 입력 읽기
    HRESULT hr = gameInput->GetCurrentReading(GameInputKindMouse, nullptr, &reading);
    if (FAILED(hr)) {
        std::cout << "마우스 GetCurrentReading 실패!" << std::endl;
        return;
    }

    if (SUCCEEDED(reading->GetMouseState(&GmouseState))) {


        curmouseState[0] = (GmouseState.buttons & GameInputMouseLeftButton) != 0;
        curmouseState[1] = (GmouseState.buttons & GameInputMouseRightButton) != 0;
        curmouseState[2] = (GmouseState.buttons & GameInputMouseMiddleButton) != 0;
        //curmouseState[3] = (mouseState.buttons & GameInputMouseXButton1) != 0; 추가버튼 쓸거면 추가필요
        //curmouseState[4] = (mouseState.buttons & GameInputMouseXButton2) != 0;


    }
    _mouseWheelDelta = GmouseState.wheelY;
    mousestate.Update();
}

float2 GameInputManager::GetmousePos()
{
    POINT cursorPos;
    GetCursorPos(&cursorPos);
    ScreenToClient(hwnd, &cursorPos);
    _mousePos.x = cursorPos.x;
    _mousePos.y = cursorPos.y;
    return _mousePos;
}

short GameInputManager::GetWheelDelta()
{
    return _mouseWheelDelta;
}

bool GameInputManager::IsWheelUp()
{
    return _premouseWheelDelta < _mouseWheelDelta;
}

bool GameInputManager::IsWheelDown()
{
    return _premouseWheelDelta > _mouseWheelDelta;
}

bool GameInputManager::IsMouseButtonPresse(Mousekey button)
{
    return mousestate.GetKeyState(button) == KeyState::Pressed;
}

bool GameInputManager::IsMouseButtonHold(Mousekey button)
{
    return mousestate.GetKeyState(button) == KeyState::Hold;
}

bool GameInputManager::IsMouseButtonRelease(Mousekey button)
{
    return mousestate.GetKeyState(button) == KeyState::Released;
}

void GameInputManager::PadUpdate()
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

void GameInputManager::GamePadUpdate()
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

        //std::cout << "게임패드 " << i << " 입력 감지됨" << std::endl;
        //std::cout << "번패드Thumbstick X: " << GpadState[i].leftThumbstickX << std::endl;
       // std::cout << "Thumbstick Y: " << GpadState[i].leftThumbstickY << std::endl;
       // std::cout << "Left Trigger: " << GpadState[i].leftTrigger << std::endl;
        GameInputLabelXboxGuide;
        curpadState[i][0] = (GpadState[i].buttons & GameInputGamepadA) != 0;
        curpadState[i][1] = (GpadState[i].buttons & GameInputGamepadB) != 0;
        curpadState[i][2] = (GpadState[i].buttons & GameInputGamepadX) != 0;
        curpadState[i][3] = (GpadState[i].buttons & GameInputGamepadY) != 0;
        curpadState[i][4] = (GpadState[i].buttons & GameInputGamepadDPadUp) != 0;
        curpadState[i][5] = (GpadState[i].buttons & GameInputGamepadDPadDown) != 0;
        curpadState[i][6] = (GpadState[i].buttons & GameInputGamepadDPadLeft) != 0;
        curpadState[i][7] = (GpadState[i].buttons & GameInputGamepadDPadRight) != 0;
        curpadState[i][8] = (GpadState[i].buttons & GameInputGamepadLeftShoulder) != 0;
        curpadState[i][9] = (GpadState[i].buttons & GameInputGamepadRightShoulder) != 0;
        curpadState[i][10] = (GpadState[i].buttons & GameInputGamepadLeftThumbstick) != 0;
        curpadState[i][11] = (GpadState[i].buttons & GameInputGamepadRightThumbstick) != 0;
        curpadState[i][12] = (GpadState[i].buttons & GameInputGamepadMenu) != 0;
        curpadState[i][13] = (GpadState[i].buttons & GameInputGamepadView) != 0;
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

bool GameInputManager::IsPadbtnPress(DWORD index, GamePade btn) const
{
    return padState.GetKeyState(index, btn) == KeyState::Pressed;
}

bool GameInputManager::IsPadbtnHold(DWORD index, GamePade btn) const
{
    return padState.GetKeyState(index, btn) == KeyState::Hold;
}

bool GameInputManager::IsPadRelease(DWORD index, GamePade btn) const
{
    return padState.GetKeyState(index, btn) == KeyState::Released;
}

bool GameInputManager::IsControllerTriggerL(DWORD index) const
{
    return   _controllerTriggerL[index] > triggerdeadZone;
}

bool GameInputManager::IsControllerTriggerR(DWORD index) const
{
    return   _controllerTriggerR[index] > triggerdeadZone;
}

float2 GameInputManager::GetControllerThumbL(DWORD index) const
{
    float2 stick(_controllerThumbL[index].x, _controllerThumbL[index].y);

    if (std::abs(stick.x) < deadZone) stick.x = 0.0f;
    if (std::abs(stick.y) < deadZone) stick.y = 0.0f;

    return stick;

}

float2 GameInputManager::GetControllerThumbR(DWORD index) const
{
    float2 stick(_controllerThumbR[index].x, _controllerThumbR[index].y);
    if (std::abs(stick.x) < deadZone) stick.x = 0.0f;
    if (std::abs(stick.y) < deadZone) stick.y = 0.0f;

    return stick;
}



