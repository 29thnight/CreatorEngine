#include "WinProcProxy.h"

std::mutex message_mutex;

void WinProcProxy::PushMessage(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
	std::unique_lock lock(message_mutex);
	m_messageQueue.push({ hWnd, message, wParam, lParam });
}

WinProcProxy::Message WinProcProxy::PopMessage()
{
	std::unique_lock lock(message_mutex);
	Message msg = m_messageQueue.front();
	m_messageQueue.pop();
	return msg;
}

bool WinProcProxy::IsEmpty() const
{
	std::unique_lock lock(message_mutex);
	return m_messageQueue.empty();
}