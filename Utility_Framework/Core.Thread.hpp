#pragma once
#include <windows.h>
#include <process.h>
#include <functional>
#include "concurrent_queue.h"

using namespace Concurrency;

using ThreadID = unsigned int;
using ThreadIndex = unsigned int;
using TaskType = std::function<void()>;

class ThreadPool;
class Thread;
enum ThreadCondition : DWORD
{
	Joinable,
	Join,
	StatusEnd
};

class ThreadNotification
{
public:
	ThreadNotification() = default;

	~ThreadNotification()
	{
		for (DWORD i = 0; i < m_numThreads; i++)
		{
			CloseHandle(m_eventHandle[i]);
		}
		delete[] m_eventHandle;
	}

	void Initialize(DWORD numThreads)
	{
		m_numThreads = numThreads;
		m_eventHandle = new HANDLE[m_numThreads];
		for (DWORD i = 0; i < m_numThreads; i++)
		{
			m_eventHandle[i] = CreateEventW(nullptr, FALSE, FALSE, nullptr);
		}
	}

	void Notify(DWORD threadIndex) const
	{
		SetEvent(m_eventHandle[threadIndex]);
	}

	void Wait() const
	{
		WaitForMultipleObjects(m_numThreads, m_eventHandle, TRUE, INFINITE);
	}

private:
	HANDLE* m_eventHandle{};
	DWORD m_numThreads{};
};

struct ThreadType
{
	void* _Handle{};
	void* _EventHnd[2]{};
	ThreadNotification* _Notify{};
	ThreadID _ID{};
	ThreadIndex	_Index{};

	ThreadType()
		: _Handle(nullptr), _Notify(nullptr), _ID(0), _Index(0)
	{
		for (DWORD i = 0; i < ThreadCondition::StatusEnd; i++)
		{
			_EventHnd[i] = CreateEventW(nullptr, FALSE, FALSE, nullptr);
		}
	}
	~ThreadType()
	{
		SetEvent(_EventHnd[ThreadCondition::Join]);

		for (DWORD i = 0; i < ThreadCondition::StatusEnd; i++)
		{
			CloseHandle(_EventHnd[i]);
		}

		CloseHandle(_Handle);
	}

	DWORD WaitForCondition() const
	{
		return WaitForMultipleObjects(ThreadCondition::StatusEnd, _EventHnd, FALSE, INFINITE);
	}

	void Joinable() const
	{
		SetEvent(_EventHnd[ThreadCondition::Joinable]);
	}
};

class Thread
{
private:
	friend class ThreadPool;

public:
	Thread() = default;
	~Thread() = default;

	void Initialize(std::function<void()> executeFunction)
	{
		m_executeFunction = executeFunction;

		auto ThreadProc = [](void* pThis) -> unsigned { static_cast<Thread*>(pThis)->StatusHandler(); return 0U; };
		m_thread._Handle = reinterpret_cast<void*>(_beginthreadex(nullptr, 0, ThreadProc, this, 0, &m_thread._ID));
	}

	static inline DWORD GetNumberOfProcessors()
	{
		SYSTEM_INFO sysInfo;
		GetSystemInfo(&sysInfo);
		return sysInfo.dwNumberOfProcessors;
	}

private:
	void StatusHandler()
	{
		while (true)
		{
			DWORD condition = m_thread.WaitForCondition();
			switch (condition)
			{
			case ThreadCondition::Joinable:
				m_executeFunction();
				m_thread._Notify->Notify(m_thread._Index);
				break;
			case ThreadCondition::Join:
				return;
			default:
				break;
			}
		}
	}

private:
	ThreadType m_thread{};
	TaskType m_executeFunction{}; // 작업 함수
};

class ThreadPool
{
private:
	friend class Thread;
public:
	ThreadPool()
	{
		m_numThreads = Thread::GetNumberOfProcessors();
		m_workers = new Thread[m_numThreads];
		m_eventNotification.Initialize(m_numThreads);

		for (DWORD i = 0; i < m_numThreads; ++i)
		{
			m_workers[i].m_thread._Index = i;
			m_workers[i].m_thread._Notify = &m_eventNotification;
			m_workers[i].Initialize([this]() { Execute(); });
	
		}
	}

	ThreadPool(DWORD threadSize)
	{
		DWORD numProcessors = Thread::GetNumberOfProcessors();
		if (threadSize < 1 || threadSize > numProcessors)
		{
			m_numThreads = numProcessors;
		}
		else
		{
			m_numThreads = threadSize;
		}
		m_workers = new Thread[m_numThreads];
		m_eventNotification.Initialize(m_numThreads);

		for (DWORD i = 0; i < m_numThreads; ++i)
		{
			m_workers[i].m_thread._Index = i;
			m_workers[i].m_thread._Notify = &m_eventNotification;
			m_workers[i].Initialize([this]() { Execute(); });
		}
	}

	~ThreadPool()
	{
		delete[] m_workers;
	}

	template<class F>
	void Enqueue(F&& f)
	{
		m_concurrentTasks.push(std::forward<F>(f));
	}

	void NotifyAllAndWait()
	{
		for (DWORD i = 0; i < m_numThreads; i++)
		{
			m_workers[i].m_thread.Joinable();
		}

		m_eventNotification.Wait();
	}

	void NotifyAll()
	{
		for (DWORD i = 0; i < m_numThreads; i++)
		{
			m_workers[i].m_thread.Joinable();
		}
	}

	DWORD GetThreadCount() const
	{
		return m_numThreads;
	}

private:
	void Execute()
	{
		while (true)
		{
			TaskType task;
			if (m_concurrentTasks.empty())
			{
				break;
			}
			else
			{
				if (m_concurrentTasks.try_pop(task))
				{
					task();
				}
			}
		}
	}

private:
	Thread* m_workers{};
	ThreadNotification m_eventNotification;
	concurrent_queue<TaskType> m_concurrentTasks;
	DWORD m_numThreads{};
};