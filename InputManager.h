#pragma once
#include "Utility_Framework/Core.Definition.h"

#pragma comment(lib, "Xinput.lib")

constexpr int KEY_COUNT = 256;
constexpr int MOUSE_BUTTON_COUNT = 3;
constexpr DWORD MAX_CONTROLLER = 4; // �ִ� 4���� ��Ʈ�ѷ��� ����

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

	// XInput ��Ʈ�ѷ� ���� �Լ�
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

	//��Ʈ�ѷ� ����
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


	// XInput ��Ʈ�ѷ� ���� ����
private:
	XINPUT_STATE _controllerState[MAX_CONTROLLER]{}; //4���� ��Ʈ�ѷ� ���¸� ����
	XINPUT_CAPABILITIES _controllerCapabilities[MAX_CONTROLLER]{}; //4���� ��Ʈ�ѷ� ����� ����
	XINPUT_STATE _prevControllerState[MAX_CONTROLLER]{}; //4���� ��Ʈ�ѷ� ���� ���¸� ����
	
private:
	bool _controllerConnected[MAX_CONTROLLER]{}; // ��Ʈ�ѷ� ���� ����
	bool _controllerPrevConnected[MAX_CONTROLLER]{}; // ��Ʈ�ѷ� ���� ���� ����

private:
	bool _controllerButtonState[MAX_CONTROLLER][12]{}; // A, B, X, Y / DPad Up, Down, Left, Right / Start, Back / LShoulder, RShoulder
	bool _prevControllerButtonState[MAX_CONTROLLER][12]{}; // A, B, X, Y / DPad Up, Down, Left, Right / Start, Back / LShoulder, RShoulder
	short _controllerThumbLX[MAX_CONTROLLER]{}; // ���� ��ƽ X
	short _controllerThumbLY[MAX_CONTROLLER]{}; // ���� ��ƽ Y
	short _controllerThumbRX[MAX_CONTROLLER]{}; // ������ ��ƽ X
	short _controllerThumbRY[MAX_CONTROLLER]{}; // ������ ��ƽ Y
	BYTE _controllerTriggerL[MAX_CONTROLLER]{}; // ���� Ʈ����
	BYTE _controllerTriggerR[MAX_CONTROLLER]{}; // ������ Ʈ����

private:
	float _controllerVibrationTime[MAX_CONTROLLER]{};
	XINPUT_VIBRATION _controllerVibration[MAX_CONTROLLER]{};
};

inline static auto& InputManagement = InputManager::GetInstance();
