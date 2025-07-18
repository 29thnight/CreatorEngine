#pragma once
#include <GameInput.h>
#include "Core.Definition.h"
#include "Core.Mathf.h"
#include "KeyState.h"

//일단 테스트용으로 몇개만 넣어둠
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

	//키보드 마우스 ***** Down Pressed hold인거 Down 첫틱도받게 수정필요 이름 통일필요
public:
	void KeyBoardUpdate();
	//누름
	bool IsKeyDown(unsigned int key) const;
	// 누르는중
	bool IsKeyPressed(unsigned int key) const;
	//뗌
	bool IsKeyReleased(unsigned int key) const;

	//키 세팅 변경용?
	bool IsAnyKeyPressed();
	bool changeKeySet(KeyBoard& changekey);


	void MouseUpdate();

	void SetMousePos(POINT pos);
	float2 GetMousePos();
	float2 GetMouseDelta() const;
	bool IsWheelUp();
	bool IsWheelDown();
	//누르는중
	bool IsMouseButtonDown(MouseKey button);
	//한번누른거
	bool IsMouseButtonPressed(MouseKey button);
	//뗌
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

	//키보드

	KeyBoard pressKey = KeyBoard::None;
	//마우스

	MouseState   mousestate;

	float2 _prevMousePos{};
	float2 _mousePos{};
	float2 _mouseDelta{};
	//마우스 휠?

	int16 _mouseWheelDelta{};
	int16 _premouseWheelDelta{};

	bool _isCursorHidden = false;
	//이 아래는 패드 컨트롤러
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
	
	//패드 최솟값
	float deadZone = 0.24f;
	float triggerdeadZone = 0.1f;
private:
	IGameInputDevice* device[4];

	PadState padState{};
	float2 _controllerThumbL[MAX_CONTROLLER]{};
	float2 _controllerThumbR[MAX_CONTROLLER]{};
	float _controllerTriggerL[MAX_CONTROLLER]{}; // 왼쪽 트리거
	float _controllerTriggerR[MAX_CONTROLLER]{}; // 오른쪽 트리거

	float _controllerVibrationTime[MAX_CONTROLLER]{};
};

inline static auto& InputManagement = InputManager::GetInstance();