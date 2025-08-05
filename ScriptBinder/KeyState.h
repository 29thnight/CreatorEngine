#pragma once
#include "Core.Definition.h"

constexpr DWORD KEYBOARD_COUNT = 255;
constexpr int MOUSE_COUNT = 3;
constexpr int GAMEPAD_KEY_COUNT = 15;
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
	Down, //한번 누른거
	Pressed,   //누르는중
	Released, //눌렀다가 뗏을때
};

//class KeyboardState
//{
//public:
//	//다 idle상태로 만들기
//	KeyboardState() {
//		memset(keyboardkeyStates, 0, sizeof(KeyState) * KEYBOARD_COUNT);
//	}
//	~KeyboardState() {};
//
//	void Update();
//	inline KeyState GetKeyState(size_t index) const { return keyboardkeyStates[index]; }
//
//private:
//	KeyState keyboardkeyStates[KEYBOARD_COUNT];
//};

class KeyboardState
{
public:
	KeyboardState()
	{
		memset(keyStateBits, 0, sizeof(keyStateBits));
	}

	void Update();
	KeyState GetKeyState(size_t index) const
	{
		const size_t byteIndex = (index * 2) / 8;
		const size_t bitOffset = (index * 2) % 8;
		uint8_t bits = (keyStateBits[byteIndex] >> bitOffset) & 0b11;
		return static_cast<KeyState>(bits);
	}

	void SetKeyState(size_t index, KeyState state)
	{
		const size_t byteIndex = (index * 2) / 8;
		const size_t bitOffset = (index * 2) % 8;
		keyStateBits[byteIndex] &= ~(0b11 << bitOffset); // clear
		keyStateBits[byteIndex] |= (static_cast<uint8_t>(state) << bitOffset); // set
	}

private:
	static constexpr size_t kBitCount = KEYBOARD_COUNT * 2;
	static constexpr size_t kByteCount = (kBitCount + 7) / 8;

	uint8_t keyStateBits[kByteCount]{};
};

class MouseState
{
public:
	MouseState() = default;

	void Update();

	KeyState GetKeyState(size_t index) const
	{
		const size_t byteIndex = (index * 2) / 8;
		const size_t bitOffset = (index * 2) % 8;
		uint8_t bits = (mouseBits[byteIndex] >> bitOffset) & 0b11;
		return static_cast<KeyState>(bits);
	}

	void SetKeyState(size_t index, KeyState state)
	{
		const size_t byteIndex = (index * 2) / 8;
		const size_t bitOffset = (index * 2) % 8;
		mouseBits[byteIndex] &= ~(0b11 << bitOffset); // Clear bits
		mouseBits[byteIndex] |= (static_cast<uint8_t>(state) << bitOffset); // Set new bits
	}

private:
	static constexpr size_t BitCount = MOUSE_COUNT * 2;
	static constexpr size_t ByteCount = (BitCount + 7) / 8;

	uint8_t mouseBits[ByteCount]{};
};

class PadState
{
public:
	PadState() = default;

	void Update();

	KeyState GetKeyState(int controllerIndex, size_t buttonIndex) const
	{
		size_t flatIndex = controllerIndex * GAMEPAD_KEY_COUNT + buttonIndex;
		size_t byteIndex = (flatIndex * 2) / 8;
		size_t bitOffset = (flatIndex * 2) % 8;
		uint8_t bits = padBits[byteIndex] >> bitOffset & 0b11;
		return static_cast<KeyState>(bits);
	}

	void SetKeyState(int controllerIndex, size_t buttonIndex, KeyState state)
	{
		size_t flatIndex = controllerIndex * GAMEPAD_KEY_COUNT + buttonIndex;
		size_t byteIndex = (flatIndex * 2) / 8;
		size_t bitOffset = (flatIndex * 2) % 8;
		padBits[byteIndex] &= ~(0b11 << bitOffset);
		padBits[byteIndex] |= (static_cast<uint8_t>(state) << bitOffset);
	}

private:
	static constexpr size_t TotalKeys = MAX_CONTROLLER * GAMEPAD_KEY_COUNT;
	static constexpr size_t BitCount = TotalKeys * 2;
	static constexpr size_t ByteCount = (BitCount + 7) / 8;

	uint8_t padBits[ByteCount]{};
};
