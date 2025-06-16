#pragma once
#include <windows.h>
#include <process.h>
#include <functional>
#include <atomic>
#include "Core.Assert.hpp"

class Thread
{
public:
	using TaskFunc = std::function<void()>;

	Thread() = default;
	~Thread()
	{
		Stop();
	}

	bool Start(TaskFunc task)
	{
		CORE_ASSERT_MSG(!m_threadHandle, "Thread already started.");
		if (!task) return false;

		m_task = std::move(task);
		m_stopRequested.store(false);

		m_threadHandle = reinterpret_cast<HANDLE>(
			_beginthreadex(nullptr, 0, &ThreadEntry, this, 0, &m_threadId)
			);

		return m_threadHandle != nullptr;
	}

	void Stop()
	{
		RequestStop();
		Join();
	}

	void RequestStop()
	{
		m_stopRequested.store(true, std::memory_order_release);
	}

	void Join()
	{
		if (m_threadHandle)
		{
			WaitForSingleObject(m_threadHandle, INFINITE);
			CloseHandle(m_threadHandle);
			m_threadHandle = nullptr;
		}
	}

	void SetAffinity(DWORD_PTR mask)
	{
		if (m_threadHandle)
			SetThreadAffinityMask(m_threadHandle, mask);
	}

	void SetPriority(int priority)
	{
		if (m_threadHandle)
			SetThreadPriority(m_threadHandle, priority);
	}

	bool IsStopRequested() const
	{
		return m_stopRequested.load(std::memory_order_acquire);
	}

	DWORD GetThreadId() const { return m_threadId; }
	HANDLE GetThreadHandle() const { return m_threadHandle; }

private:
	static unsigned __stdcall ThreadEntry(void* arg)
	{
		Thread* thread = static_cast<Thread*>(arg);
		if (thread && thread->m_task)
			thread->m_task();
		return 0;
	}

private:
	HANDLE				m_threadHandle{ nullptr };
	uint32_t			m_threadId = 0;
	TaskFunc			m_task;
	std::atomic_bool	m_stopRequested{ false };
};
