#pragma once
#include <queue>
#include <tuple>
#include <mutex>
#include <array>
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "ClassProperty.h"

class WinProcProxy : public Singleton<WinProcProxy>
{
public:
	using Message		= std::tuple<HWND, UINT, WPARAM, LPARAM>;
public:
	friend class Singleton;
	WinProcProxy()	= default;
	~WinProcProxy() = default;

	void PushMessage(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);

	// The presentation consumer receives the message-time keyboard state as well.
	Message PopMessage();

	bool IsEmpty() const;

private:
    struct QueuedMessage
    {
        Message message;
        std::array<BYTE, 256> keyboardState{};
        bool hasKeyboardState{};
    };
    std::queue<QueuedMessage> m_messageQueue;
};
