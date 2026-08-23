#pragma once
#include <queue>
#include <tuple>
#include <mutex>
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "ClassProperty.h"

class WinProcProxy : public Singleton<WinProcProxy>
{
public:
	using Message		= std::tuple<HWND, UINT, WPARAM, LPARAM>;
	using MessageQueue	= std::queue<Message>;
public:
	friend class Singleton;
	WinProcProxy()	= default;
	~WinProcProxy() = default;

	void PushMessage(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);

	Message PopMessage();

	bool IsEmpty() const;

private:
	MessageQueue m_messageQueue;
};