#include "WinProcProxy.h"

std::mutex message_mutex;

void WinProcProxy::PushMessage(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    QueuedMessage queued;
    queued.message = { hWnd, message, wParam, lParam };
    // GetKeyState is specific to the thread that dequeued the Win32 message.
    // Capture before handing it to ImGui on the presentation thread; otherwise
    // Ctrl+C/V and other short chords lose their modifier state during replay.
    queued.hasKeyboardState = GetKeyboardState(queued.keyboardState.data()) != FALSE;
	std::unique_lock lock(message_mutex);
    m_messageQueue.push(std::move(queued));
}

WinProcProxy::Message WinProcProxy::PopMessage()
{
	std::unique_lock lock(message_mutex);
    QueuedMessage queued = std::move(m_messageQueue.front());
	m_messageQueue.pop();
    lock.unlock();
    if (queued.hasKeyboardState)
    {
        // Only this consumer thread's input-state table is changed. The Win32
        // backend can now read modifiers in the original message order.
        SetKeyboardState(queued.keyboardState.data());
    }
    return queued.message;
}

bool WinProcProxy::IsEmpty() const
{
	std::unique_lock lock(message_mutex);
	return m_messageQueue.empty();
}
