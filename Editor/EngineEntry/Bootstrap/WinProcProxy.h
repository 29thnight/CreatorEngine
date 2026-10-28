#include "../../../Engine/Containers/ContainerAPI.h"
#pragma once
#include "../../../Engine/Utility_Framework/Foundation/EngineStorage.h"
#include "../../../Engine/Utility_Framework/Foundation/StandardLibrary.h"
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#if defined(CE_CONTRACT_STANDALONE)
#include "../../../Engine/Platform/WindowsSDK/Creator.WinAPI.h"
#else
import "Creator.WinAPI.h";
#endif
#include "../../../Engine/Utility_Framework/Reflection/ClassProperty.h"

class WinProcProxy : public Singleton<WinProcProxy>
{
public:
	using Message		= ce::tuple<HWND, UINT, WPARAM, LPARAM>;
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
        ce::fixed_array<BYTE, 256> keyboardState{};
        bool hasKeyboardState{};
    };
    ce::storage_queue<QueuedMessage> m_messageQueue;
};
