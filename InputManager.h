#pragma once
#include "Utility_Framework/Core.Definition.h"

#pragma comment(lib, "Xinput.lib")

constexpr int KEY_COUNT = 256;
constexpr int MOUSE_BUTTON_COUNT = 3;
constexpr DWORD MAX_CONTROLLER = 4; // 최대 4개의 컨트롤러를 지원

class InputManager : public Singleton<InputManager>
{
private:
	friend class Singleton;

private:
	InputManager() = default;
	~InputManager() = default;

public:
	void Initialize(HWND hwnd);
	HWND GetHwnd() const;
	void Update();

public:
	bool IsKeyDown(unsigned int key) const;
	bool IsKeyPressed(unsigned int key) const;
	bool IsKeyReleased(unsigned int key) const;
	bool IsCapsLockOn() const;

public:
	bool IsMouseButtonDown(MouseKey button) const;
	bool IsMouseButtonPressed(MouseKey button) const;
	bool IsMouseButtonReleased(MouseKey button) const;

public:
	void SetMousePos(POINT pos);
	POINT GetMousePos() const;
	float2 GetMouseDelta() const;
	void SetCursorPos(int x, int y);
	void GetCursorPos(LPPOINT lpPoint);
	void HideCursor();
	void ShowCursor();
	void ResetMouseDelta();
	short GetMouseWheelDelta() const;

public:
	void ProcessRawInput(LPARAM lParam);
	void RegisterRawInputDevices();

	// XInput 컨트롤러 관련 함수
public:
	void ProcessControllerInput(DWORD controllerIndex);
	void UpdateControllerState();

public:
	bool IsControllerConnected(DWORD controllerIndex) const;
	bool IsControllerButtonDown(DWORD controllerIndex, ControllerButton button) const;
	bool IsControllerButtonPressed(DWORD controllerIndex, ControllerButton button) const;
	bool IsControllerButtonReleased(DWORD controllerIndex, ControllerButton button) const;
	bool IsControllerTriggerL(DWORD controllerIndex) const;
	bool IsControllerTriggerR(DWORD controllerIndex) const;
	float2 GetControllerThumbL(DWORD controllerIndex) const;
	float2 GetControllerThumbR(DWORD controllerIndex) const;

	//컨트롤러 진동
public:
	void SetControllerVibration(DWORD controllerIndex, float leftMotorSpeed, float rightMotorSpeed); 
	void UpdateControllerVibration(float tick);
	void SetControllerVibrationTime(DWORD controllerIndex, float time);
private:
	HWND _hWnd{ nullptr };
	RAWINPUTDEVICE _rawInputDevices[2]{};

private:
	bool _keyState[KEY_COUNT]{};
	bool _prevKeyState[KEY_COUNT]{};
	bool _isCapsLockOn{ false };

private:
	bool _mouseState[MOUSE_BUTTON_COUNT]{};
	bool _prevMouseState[MOUSE_BUTTON_COUNT]{};

private:
	POINT _prevMousePos{};
	POINT _mousePos{};
	float2 _mouseDelta{};
	short _mouseWheelDelta{};

private:
	bool _isCursorHidden{ false };


	// XInput 컨트롤러 관련 변수
private:
	XINPUT_STATE _controllerState[MAX_CONTROLLER]{}; //4개의 컨트롤러 상태를 저장
	XINPUT_CAPABILITIES _controllerCapabilities[MAX_CONTROLLER]{}; //4개의 컨트롤러 기능을 저장
	XINPUT_STATE _prevControllerState[MAX_CONTROLLER]{}; //4개의 컨트롤러 이전 상태를 저장
	
private:
	bool _controllerConnected[MAX_CONTROLLER]{}; // 컨트롤러 연결 여부
	bool _controllerPrevConnected[MAX_CONTROLLER]{}; // 컨트롤러 이전 연결 여부

private:
	bool _controllerButtonState[MAX_CONTROLLER][12]{}; // A, B, X, Y / DPad Up, Down, Left, Right / Start, Back / LShoulder, RShoulder
	bool _prevControllerButtonState[MAX_CONTROLLER][12]{}; // A, B, X, Y / DPad Up, Down, Left, Right / Start, Back / LShoulder, RShoulder
	short _controllerThumbLX[MAX_CONTROLLER]{}; // 왼쪽 스틱 X
	short _controllerThumbLY[MAX_CONTROLLER]{}; // 왼쪽 스틱 Y
	short _controllerThumbRX[MAX_CONTROLLER]{}; // 오른쪽 스틱 X
	short _controllerThumbRY[MAX_CONTROLLER]{}; // 오른쪽 스틱 Y
	BYTE _controllerTriggerL[MAX_CONTROLLER]{}; // 왼쪽 트리거
	BYTE _controllerTriggerR[MAX_CONTROLLER]{}; // 오른쪽 트리거

private:
	float _controllerVibrationTime[MAX_CONTROLLER]{};
	XINPUT_VIBRATION _controllerVibration[MAX_CONTROLLER]{};
};

inline static auto& InputManagement = InputManager::GetInstance();
