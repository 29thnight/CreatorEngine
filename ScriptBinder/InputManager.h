#pragma once
#include <GameInput.h>
#include "Core.Definition.h"
#include "Core.Mathf.h"
#include "KeyState.h"

//�ϴ� �׽�Ʈ������ ��� �־��
enum class KeyBoard
{
	A = 0x41,
	B = 0x42,
	C = 0x43,
	D = 0x44,
	E = 0x45,
	F = 0x46,
	G = 0x47,
	H = 0x48,
	I = 0x49,
	J = 0x4A,
	K = 0x4B,
	L = 0x4C,
	M = 0x4D,
	N = 0x4E,
	O = 0x4F,
	P = 0x50,
	Q = 0x51,
	R = 0x52,
	S = 0x53,
	T = 0x54,
	U = 0x55,
	V = 0x56,
	W = 0x57,
	X = 0x58,
	Y = 0x59,
	Z = 0x5A,
	LeftArrow = 0x25,
	UpArrow = 0x26,
	RightArrow = 0x27,
	DownArrow = 0x28,
	Space = 0x20,
	LeftControl = 0xA2,
	RightControl = 0xA3,
	LeftShift = 0xA0,
	RightShift = 0xA1,
	LeftAlt = 0xA4,
	RightAlt = 0xA5,
	Enter = 0x0D,
	Backspace = 0x08,
	Tab = 0x09,
	Escape = 0x1B,
	CapsLock = 0x14,
	Insert = 0x2D,
	Delete = 0x2E,
	Home = 0x24,
	End = 0x23,
	PageUp = 0x21,
	PageDown = 0x22,
	NumLock = 0x90,
	ScrollLock = 0x91,
	F1 = 0x70,
	F2 = 0x71,
	F3 = 0x72,
	F4 = 0x73,
	F5 = 0x74,
	F6 = 0x75,
	F7 = 0x76,
	F8 = 0x77,
	F9 = 0x78,
	F10 = 0x79,
	F11 = 0x7A,
	F12 = 0x7B,
	Numpad0 = 0x60,
	Numpad1 = 0x61,
	Numpad2 = 0x62,
	Numpad3 = 0x63,
	Numpad4 = 0x64,
	Numpad5 = 0x65,
	Numpad6 = 0x66,
	Numpad7 = 0x67,
	Numpad8 = 0x68,
	Numpad9 = 0x69,
	None = 9999,
};

extern std::vector<KeyBoard> keyboradsss;


using namespace Microsoft::WRL;
class InputManager : public Singleton<InputManager>
{
	friend class Singleton;
private:
	InputManager() = default;
	~InputManager() = default;
	HWND hwnd{};
public:
	bool Initialize(HWND _hwnd);

	void Update(float deltaTime);
	//void ImGuiUpdate();

	ComPtr<IGameInput> gameInput;

	//Ű���� ���콺 ***** Down Pressed hold�ΰ� Down ùƽ���ް� �����ʿ� �̸� �����ʿ�
public:
	void KeyBoardUpdate();
	//����
	bool IsKeyDown(unsigned int key) const;
	// ��������
	bool IsKeyPressed(unsigned int key) const;
	//��
	bool IsKeyReleased(unsigned int key) const;

	//Ű ���� �����?
	bool IsAnyKeyPressed();
	bool changeKeySet(KeyBoard& changekey);


	void MouseUpdate();

	void SetMousePos(POINT pos);
	float2 GetMousePos();
	float2 GetMouseDelta() const;
	bool IsWheelUp();
	bool IsWheelDown();
	//��������
	bool IsMouseButtonDown(MouseKey button);
	//�ѹ�������
	bool IsMouseButtonPressed(MouseKey button);
	//��
	bool IsMouseButtonReleased(MouseKey button);
	void HideCursor();
	void ShowCursor();
	void ResetMouseDelta();
	int16 GetWheelDelta() const;
	KeyboardState keyboardstate{};
	IGameInputDevice* _keyBoardDevice = nullptr;
	std::vector<GameInputKeyState> GkeyStates;
	GameInputMouseState GmouseState{};
	bool curkeyStates[KEYBOARD_COUNT] = {};

	bool curmouseState[mouseCount] = {};

	Mathf::Vector2 GameViewpos;
	Mathf::Vector2 GameViewsize;


private:

	//Ű����

	KeyBoard pressKey = KeyBoard::None;
	//���콺

	MouseState   mousestate;

	float2 _prevMousePos{};
	float2 _mousePos{};
	float2 _mouseDelta{};
	//���콺 ��?

	int16 _mouseWheelDelta{};
	int16 _premouseWheelDelta{};

	bool _isCursorHidden = false;
	//�� �Ʒ��� �е� ��Ʈ�ѷ�
public:


	void PadUpdate();
	void GamePadUpdate();

	bool IsControllerConnected(DWORD Index);
	bool IsControllerButtonDown(DWORD index, ControllerButton btn) const;
	bool IsControllerButtonPressed(DWORD index, ControllerButton btn) const;
	bool IsControllerButtonReleased(DWORD index, ControllerButton btn) const;

	bool IsControllerTriggerL(DWORD index) const;
	bool IsControllerTriggerR(DWORD index) const;
	Mathf::Vector2 GetControllerThumbL(DWORD index) const;
	Mathf::Vector2 GetControllerThumbR(DWORD index) const;

	void SetControllerVibration(DWORD Index, float leftMotorSpeed, float rightMotorSpeed,float lowFre,float highFre);
	void UpdateControllerVibration(float tick);
	void SetControllerVibrationTime(DWORD Index, float time);
	GameInputGamepadState GpadState[MAX_CONTROLLER];
	bool curpadState[MAX_CONTROLLER][padKeyCount]{};
	
	//�е� �ּڰ�
	float deadZone = 0.24f;
	float triggerdeadZone = 0.1f;
private:
	IGameInputDevice* device[4];

	PadState padState{};
	float2 _controllerThumbL[MAX_CONTROLLER]{};
	float2 _controllerThumbR[MAX_CONTROLLER]{};
	float _controllerTriggerL[MAX_CONTROLLER]{}; // ���� Ʈ����
	float _controllerTriggerR[MAX_CONTROLLER]{}; // ������ Ʈ����

	float _controllerVibrationTime[MAX_CONTROLLER]{};
};

inline static auto& InputManagement = InputManager::GetInstance();