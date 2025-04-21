#pragma once
#include "Core.Definition.h"

constexpr DWORD KEYBOARD_COUNT = 255;
constexpr int mouseCount = 3;
constexpr int padKeyCount = 15;
constexpr DWORD MAX_CONTROLLER = 4;

enum class InputType
{
	Mouse,
	KeyBoard,
	GamePad,
};
enum class KeyState
{
	Idle,  //기본
	Pressed, //한번 누른거
	Hold,   //누르는중
	Released, //눌렀다가 뗏을때
};

class KeyboardState
{
public:
	//다 idle상태로 만들기
	KeyboardState() {
		memset(keyboardkeyStates, 0, sizeof(KeyState) * KEYBOARD_COUNT);
	}
	~KeyboardState() {};

	void Update();
	inline KeyState GetKeyState(size_t index) const { return keyboardkeyStates[index]; }

private:
	KeyState keyboardkeyStates[KEYBOARD_COUNT];
};


class MouseState
{
public:
	MouseState() {
		memset(mousekeyStates, 0, sizeof(KeyState) * mouseCount);
	}
	~MouseState() {};

	void Update();
	inline KeyState GetKeyState(size_t index) const { return mousekeyStates[index]; }
private:
	KeyState mousekeyStates[mouseCount];
};

class PadState
{
public:
	PadState() {
		for (int i = 0; i < MAX_CONTROLLER; i++)
		{
			memset(padkeyStates[i], 0, sizeof(KeyState) * padKeyCount);
		}
	};
	~PadState() {};

	void Update();
	inline KeyState GetKeyState(int index, size_t btn) const { return padkeyStates[index][btn]; }
private:
	bool padConnected[MAX_CONTROLLER]{};
	KeyState padkeyStates[MAX_CONTROLLER][padKeyCount];
};