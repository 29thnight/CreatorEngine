#pragma once
#include <GameInput.h>
#include "Core.Definition.h"
#include "Core.Mathf.h"
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
	Space = 0x20,
	LeftControl = 0xA2,
	None = 9999,

};



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
	short GetWheelDelta() const;
	KeyboardState keyboardstate{};
	std::vector<GameInputKeyState> GkeyStates = {};
	GameInputMouseState GmouseState{};
	bool  curkeyStates[KEYBOARD_COUNT] = {};

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

	short _mouseWheelDelta{};
	short _premouseWheelDelta{};

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