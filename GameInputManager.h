#pragma once
#include "Utility_Framework/Core.Definition.h"
//#include "SingletonBase.h"
#include <GameInput.h>
#include "KeyState.h"


//�ϴ� �׽�Ʈ������ ��� �־��
enum  KeyBoard
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


	None = 9999,

};

enum Mousekey
{
	LeftButton = 0,
	RightButton,
	MiddleButton,
};
enum GamePade
{
	AButton = 0,
	BButton = 1,
	XButton = 2,
	YButton = 3,
	UpPadButton = 4,
	DownPadButton = 5,
	LeftPadButton = 6,
	RightPadButton = 7,
	LeftShoulder = 8,
	RightShoulder = 9,
	LeftThumbstick = 10,
	RightThumbstick = 11,
	GamepadMenu = 12,  //
	GamepadView = 13,
	GamepadNone = 14,

};


using namespace Microsoft::WRL;
class GameInputManager : public Singleton<GameInputManager>
{
	friend class Singleton;
private:
	GameInputManager() = default;
	~GameInputManager() = default;
	HWND hwnd{};
public:
	bool Initialize(HWND _hwnd);

	void Update();



	ComPtr<IGameInput> gameInput;

	//Ű���� ���콺
public:
	void KeyBoardUpdate();
	//������������
	bool KeyHold(KeyBoard key);
	//�ѹ� ��������;
	bool KeyPress(KeyBoard key);
	//�H����
	bool KeyRelease(KeyBoard key);

	//Ű ���� �����?
	bool IsAnyKeyPressed();
	bool changeKeySet(KeyBoard& changekey);


	void MouseUpdate();

	float2 GetmousePos();
	short GetWheelDelta();
	bool IsWheelUp();
	bool IsWheelDown();
	bool IsMouseButtonPresse(Mousekey button);
	bool IsMouseButtonHold(Mousekey button);
	bool IsMouseButtonRelease(Mousekey button);
	KeyboardState keyboardstate;
	std::vector<GameInputKeyState> GkeyStates = {};
	GameInputMouseState GmouseState;
	bool  curkeyStates[KEYBOARD_COUNT] = {};

	bool curmouseState[mouseCount] = {};

private:

	//Ű����

	KeyBoard pressKey = KeyBoard::None;
	//���콺

	MouseState   mousestate;

	float2 _prevMousePos{};
	float2 _mousePos{};
	float2 _mouseDelta{};
	//���콺 ��?

	short _mouseWheelDelta{};
	short _premouseWheelDelta{};

	//�� �Ʒ��� �е� ��Ʈ�ѷ�
public:


	void PadUpdate();
	void GamePadUpdate();


	bool IsPadbtnPress(DWORD index, GamePade btn) const;
	bool IsPadbtnHold(DWORD index, GamePade btn) const;
	bool IsPadRelease(DWORD index, GamePade btn) const;

	bool IsControllerTriggerL(DWORD index) const;
	bool IsControllerTriggerR(DWORD index) const;

	//MathF:: �� �ٲ����
	float2 GetControllerThumbL(DWORD index) const;
	float2 GetControllerThumbR(DWORD index) const;
	GameInputGamepadState GpadState[MAX_CONTROLLER];
	bool curpadState[MAX_CONTROLLER][padKeyCount]{};

	//�е� �ּڰ�?
	float deadZone = 0.24f;
	float triggerdeadZone = 0.1f;
private:
	IGameInputDevice* device[4];

	PadState padState;
	float2 _controllerThumbL[MAX_CONTROLLER]{};
	float2 _controllerThumbR[MAX_CONTROLLER]{};
	float _controllerTriggerL[MAX_CONTROLLER]{}; // ���� Ʈ����
	float _controllerTriggerR[MAX_CONTROLLER]{}; // ������ Ʈ����

};

inline static auto& GInputManagement = GameInputManager::GetInstance();