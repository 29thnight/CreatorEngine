#include "InputManager.h"
#include "hidsdi.h"

#include <imgui.h>
#include <imgui_impl_dx11.h>
#include <imgui_impl_win32.h>

void InputManager::Initialize(HWND hwnd)
{
    _hWnd = hwnd;
    RegisterRawInputDevices();
}

HWND InputManager::GetHwnd() const
{
    return _hWnd;
}

void InputManager::Update()
{
	memcpy(_prevKeyState, _keyState, sizeof(bool) * 256);
	memcpy(_prevMouseState, _mouseState, sizeof(bool) * 3);
	ResetMouseDelta();

    memcpy(_prevControllerButtonState, _controllerButtonState, sizeof(bool) * 16);
    memcpy(_prevControllerState, _controllerState, sizeof(XINPUT_STATE) * MAX_CONTROLLER);
    UpdateControllerState();

}

bool InputManager::IsKeyDown(unsigned int key) const
{
    return key < KEY_COUNT && (_keyState[key] && !_prevKeyState[key]);
}

bool InputManager::IsKeyPressed(unsigned int key) const
{
    return key < KEY_COUNT && _keyState[key];
}

bool InputManager::IsKeyReleased(unsigned int key) const
{
    return key < KEY_COUNT && (!_keyState[key] && _prevKeyState[key]);
}

bool InputManager::IsCapsLockOn() const
{
	return _isCapsLockOn;
}

bool InputManager::IsMouseButtonDown(MouseKey button) const
{
    int buttonCasted = static_cast<int>(button);

    return buttonCasted < MOUSE_BUTTON_COUNT && _mouseState[buttonCasted];
}

bool InputManager::IsMouseButtonPressed(MouseKey button) const
{
    int buttonCasted = static_cast<int>(button);

    return buttonCasted < MOUSE_BUTTON_COUNT &&
        _mouseState[buttonCasted] && !_prevMouseState[buttonCasted];
}

bool InputManager::IsMouseButtonReleased(MouseKey button) const
{
    int buttonCasted = static_cast<int>(button);

    return buttonCasted < MOUSE_BUTTON_COUNT &&
		!_mouseState[buttonCasted] && _prevMouseState[buttonCasted];
}

void InputManager::SetMousePos(POINT pos)
{
    _mousePos = pos;
}

POINT InputManager::GetMousePos() const
{
    POINT mousePos;
    ::GetCursorPos(&mousePos);
    ScreenToClient(_hWnd, &mousePos);
    //return _mousePos;
	return mousePos;
}

float2 InputManager::GetMouseDelta() const
{
    return _mouseDelta;
}

void InputManager::SetCursorPos(int x, int y)
{
    ::SetCursorPos(x, y);
}

void InputManager::GetCursorPos(LPPOINT lpPoint)
{
    ::GetCursorPos(lpPoint);
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

short InputManager::GetMouseWheelDelta() const
{
    return _mouseWheelDelta;
}

void InputManager::ProcessRawInput(LPARAM lParam)
{
    UINT dwSize = 0;
    GetRawInputData(reinterpret_cast<HRAWINPUT>(lParam), RID_INPUT, nullptr, &dwSize, sizeof(RAWINPUTHEADER));

    // std::vector<BYTE> 사용
    std::vector<BYTE> lpb(dwSize);

    if (GetRawInputData(reinterpret_cast<HRAWINPUT>(lParam), RID_INPUT, lpb.data(), &dwSize, sizeof(RAWINPUTHEADER)) != dwSize)
    {
        return;
    }

    RAWINPUT* raw = reinterpret_cast<RAWINPUT*>(lpb.data());

    if (raw->header.dwType == RIM_TYPEKEYBOARD)
    {
        _keyState[raw->data.keyboard.VKey] = (raw->data.keyboard.Flags & RI_KEY_BREAK) ? 0 : 1;
		if (true == _keyState[VK_CAPITAL])
		{
			_isCapsLockOn = !_isCapsLockOn;
		}
    }

    if (raw->header.dwType == RIM_TYPEMOUSE)
    {
        // 마우스 이동 처리
        _mousePos.x += raw->data.mouse.lLastX;
        _mousePos.y += raw->data.mouse.lLastY;

        // 마우스 버튼 처리
        if (raw->data.mouse.usButtonFlags & RI_MOUSE_LEFT_BUTTON_DOWN)
        {
            _mouseState[0] = 1;  // Left button down
        }
        if (raw->data.mouse.usButtonFlags & RI_MOUSE_LEFT_BUTTON_UP)
        {
            _mouseState[0] = 0;  // Left button up
        }
        if (raw->data.mouse.usButtonFlags & RI_MOUSE_RIGHT_BUTTON_DOWN)
        {
            _mouseState[1] = 1;  // Right button down
        }
        if (raw->data.mouse.usButtonFlags & RI_MOUSE_RIGHT_BUTTON_UP)
        {
            _mouseState[1] = 0;  // Right button up
        }
		if (raw->data.mouse.usButtonFlags & RI_MOUSE_MIDDLE_BUTTON_DOWN)
		{
			_mouseState[2] = 1;  // Middle button down
		}
        if (raw->data.mouse.usButtonFlags & RI_MOUSE_MIDDLE_BUTTON_UP)
        {
			_mouseState[2] = 0;  // Middle button up
        }

        // 마우스 휠 처리
        if (raw->data.mouse.usButtonFlags & RI_MOUSE_WHEEL)
        {
            _mouseWheelDelta = static_cast<SHORT>(raw->data.mouse.usButtonData);
        }

        _mouseDelta.x = (_mousePos.x - _prevMousePos.x) * 0.5f;
        _mouseDelta.y = (_mousePos.y - _prevMousePos.y) * 0.5f;

    }
}

void InputManager::RegisterRawInputDevices()
{
    _rawInputDevices[0].usUsagePage = HID_USAGE_PAGE_GENERIC;
    _rawInputDevices[0].usUsage = HID_USAGE_GENERIC_KEYBOARD;
    _rawInputDevices[0].dwFlags = RIDEV_INPUTSINK;
    _rawInputDevices[0].hwndTarget = _hWnd;

    _rawInputDevices[1].usUsagePage = HID_USAGE_PAGE_GENERIC;
    _rawInputDevices[1].usUsage = HID_USAGE_GENERIC_MOUSE;
    _rawInputDevices[1].dwFlags = RIDEV_INPUTSINK;
    _rawInputDevices[1].hwndTarget = _hWnd;

    if (!::RegisterRawInputDevices(_rawInputDevices, 2, sizeof(RAWINPUTDEVICE)))
    {
        throw std::exception("Failed to register raw input devices.");
    }
}

void InputManager::ProcessControllerInput(DWORD controllerIndex)
{
	XINPUT_STATE& state = _controllerState[controllerIndex];

	//A button
    if (state.Gamepad.wButtons & XINPUT_GAMEPAD_A)
    {
        _controllerButtonState[controllerIndex][0] = true;
    }
    else 
	{
		_controllerButtonState[controllerIndex][0] = false;
    }

	//B button
	if (state.Gamepad.wButtons & XINPUT_GAMEPAD_B)
	{
		_controllerButtonState[controllerIndex][1] = true;
	}
	else {
		_controllerButtonState[controllerIndex][1] = false;
	}

	//X button
	if (state.Gamepad.wButtons & XINPUT_GAMEPAD_X)
	{
		_controllerButtonState[controllerIndex][2] = true;
	}
	else {
		_controllerButtonState[controllerIndex][2] = false;
	}

	//Y button
	if (state.Gamepad.wButtons & XINPUT_GAMEPAD_Y)
	{
		_controllerButtonState[controllerIndex][3] = true;
	}
	else {
		_controllerButtonState[controllerIndex][3] = false;
	}

	//DPad Up
	if (state.Gamepad.wButtons & XINPUT_GAMEPAD_DPAD_UP)
	{
		_controllerButtonState[controllerIndex][4] = true;
	}
	else {
		_controllerButtonState[controllerIndex][4] = false;
	}

	//DPad Down
	if (state.Gamepad.wButtons & XINPUT_GAMEPAD_DPAD_DOWN)
	{
		_controllerButtonState[controllerIndex][5] = true;
	}
	else {
		_controllerButtonState[controllerIndex][5] = false;
	}

	//DPad Left
	if (state.Gamepad.wButtons & XINPUT_GAMEPAD_DPAD_LEFT)
	{
		_controllerButtonState[controllerIndex][6] = true;
	}
	else {
		_controllerButtonState[controllerIndex][6] = false;
	}

	//DPad Right
	if (state.Gamepad.wButtons & XINPUT_GAMEPAD_DPAD_RIGHT)
	{
		_controllerButtonState[controllerIndex][7] = true;
	}
	else {
		_controllerButtonState[controllerIndex][7] = false;
	}
	//Start Button
	if (state.Gamepad.wButtons & XINPUT_GAMEPAD_START)
	{
		_controllerButtonState[controllerIndex][8] = true;
	}
	else
	{
		_controllerButtonState[controllerIndex][8] = false;
	}
	//Back Button
	if (state.Gamepad.wButtons & XINPUT_GAMEPAD_BACK)
	{
		_controllerButtonState[controllerIndex][9] = true;
	}
	else
	{
		_controllerButtonState[controllerIndex][9] = false;
	}

	//Left Shoulder
	if (state.Gamepad.wButtons & XINPUT_GAMEPAD_LEFT_SHOULDER)
	{
		_controllerButtonState[controllerIndex][10] = true;
	}
	else 
	{
		_controllerButtonState[controllerIndex][10] = false;
	}

	//Right Shoulder
	if (state.Gamepad.wButtons & XINPUT_GAMEPAD_RIGHT_SHOULDER)
	{
		_controllerButtonState[controllerIndex][11] = true;
	}
	else 
	{
		_controllerButtonState[controllerIndex][11] = false;
	}

	//Left Thumbstick X
	_controllerThumbLX[controllerIndex] = state.Gamepad.sThumbLX;
	//left Thumbstick Y
    _controllerThumbLY[controllerIndex] = state.Gamepad.sThumbLY;

	//right Thumbstick X
	_controllerThumbRX[controllerIndex] = state.Gamepad.sThumbRX;
	//right Thumbstick Y
	_controllerThumbRY[controllerIndex] = state.Gamepad.sThumbRY;

	//trigger left
	_controllerTriggerL[controllerIndex] = state.Gamepad.bLeftTrigger;
	//trigger right
    _controllerTriggerR[controllerIndex] = state.Gamepad.bRightTrigger;
    

}

void InputManager::UpdateControllerState()
{
	for (DWORD i = 0; i < MAX_CONTROLLER; ++i)
	{
		_controllerPrevConnected[i] = _controllerConnected[i];
		DWORD result = XInputGetState(i, &_controllerState[i]);
		XInputGetCapabilities(i, XINPUT_FLAG_GAMEPAD, &_controllerCapabilities[i]);
		if (result == ERROR_SUCCESS)
		{
			_controllerConnected[i] = true;

			ProcessControllerInput(i);
		}
		else
		{
			_controllerConnected[i] = false;
		}
	}
}

bool InputManager::IsControllerConnected(DWORD controllerIndex) const
{
	return _controllerConnected[controllerIndex];
}

bool InputManager::IsControllerButtonDown(DWORD controllerIndex, ControllerButton button) const
{
    return _controllerButtonState[controllerIndex][static_cast<int>(button)] && !_prevControllerButtonState[controllerIndex][static_cast<int>(button)];
}

bool InputManager::IsControllerButtonPressed(DWORD controllerIndex, ControllerButton button) const
{
	return _controllerButtonState[controllerIndex][static_cast<int>(button)] && _prevControllerButtonState[controllerIndex][static_cast<int>(button)];
}

bool InputManager::IsControllerButtonReleased(DWORD controllerIndex, ControllerButton button) const
{
	return !_controllerButtonState[controllerIndex][static_cast<int>(button)] && _prevControllerButtonState[controllerIndex][static_cast<int>(button)];
}

bool InputManager::IsControllerTriggerL(DWORD controllerIndex) const
{
	return _controllerTriggerL[controllerIndex] > XINPUT_GAMEPAD_TRIGGER_THRESHOLD;
}

bool InputManager::IsControllerTriggerR(DWORD controllerIndex) const
{
	return _controllerTriggerR[controllerIndex] > XINPUT_GAMEPAD_TRIGGER_THRESHOLD;
}

float2 InputManager::GetControllerThumbL(DWORD controllerIndex) const
{
	return float2(static_cast<float>(_controllerThumbLX[controllerIndex]) / 32768.0f, static_cast<float>(_controllerThumbLY[controllerIndex]) / 32768.0f);
}

float2 InputManager::GetControllerThumbR(DWORD controllerIndex) const
{
	return float2(static_cast<float>(_controllerThumbRX[controllerIndex]) / 32768.0f, static_cast<float>(_controllerThumbRY[controllerIndex]) / 32768.0f);
}

void InputManager::SetControllerVibration(DWORD controllerIndex, float leftMotorSpeed, float rightMotorSpeed)
{
	XINPUT_VIBRATION vibration{};
	vibration.wLeftMotorSpeed = static_cast<WORD>(leftMotorSpeed * 65535.0f);
	vibration.wRightMotorSpeed = static_cast<WORD>(rightMotorSpeed * 65535.0f);
	XInputSetState(controllerIndex, &vibration);
}

void InputManager::UpdateControllerVibration(float tick)
{
	for (DWORD i = 0; i < MAX_CONTROLLER; ++i)
	{
		if (_controllerConnected[i])
		{
			if (_controllerVibrationTime[i] > 0.0f)
			{
				_controllerVibrationTime[i] -= tick;
				//SetControllerVibration(i, _controllerVibration[i].wLeftMotorSpeed, _controllerVibration[i].wRightMotorSpeed);
				SetControllerVibration(i, 0.5f, 0.5f);
			}
			else
			{
				SetControllerVibration(i, 0.0f, 0.0f);
			}
		}
	}
}

void InputManager::SetControllerVibrationTime(DWORD controllerIndex, float time)
{
	_controllerVibrationTime[controllerIndex] = time;
}

