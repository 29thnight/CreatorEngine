#pragma once
#include "../Utility_Framework/Core.Minimal.h"
#include "../Utility_Framework/Core.Thread.hpp"

struct MeshRenderJob
{
	Thread thread;
	ID3D11DeviceContext* deferredContext = nullptr;
};

class RenderThreadPool
{
public:
	RenderThreadPool(ID3D11Device* device, DWORD threadSize = 0)
	{
		m_numThreads = (threadSize > 0 && threadSize <= Thread::GetNumberOfProcessors())
			? threadSize
			: Thread::GetNumberOfProcessors();

		m_workers = new MeshRenderJob[m_numThreads];
		m_eventNotification.Initialize(m_numThreads);

		for (DWORD i = 0; i < m_numThreads; ++i)
		{
			m_workers[i].thread.m_thread._Index = i;
			m_workers[i].thread.m_thread._Notify = &m_eventNotification;

			// Create Deferred Context
			HRESULT hr = device->CreateDeferredContext(0, &m_workers[i].deferredContext);
			if (FAILED(hr))
			{
				Debug->LogError("[RenderThreadPool] Failed to create deferred context for thread " + std::to_string(i));
				continue;
			}

			m_workers[i].thread.Initialize([this]() { Execute(); });
		}
	}

	~RenderThreadPool()
	{
		for (DWORD i = 0; i < m_numThreads; ++i)
		{
			if (m_workers[i].deferredContext)
			{
				m_workers[i].deferredContext->Release();
				m_workers[i].deferredContext = nullptr;
			}
		}
		delete[] m_workers;
	}

	template<class F>
	void Enqueue(F&& f)
	{
		m_concurrentTasks.push(std::forward<F>(f));
	}

	void NotifyAllAndWait()
	{
		for (DWORD i = 0; i < m_numThreads; ++i)
		{
			m_workers[i].thread.m_thread.Joinable();
		}
		m_eventNotification.Wait();
	}

	void NotifyAll()
	{
		for (DWORD i = 0; i < m_numThreads; ++i)
		{
			m_workers[i].thread.m_thread.Joinable();
		}
	}

	ID3D11DeviceContext* GetDeferredContext(DWORD threadIndex)
	{
		if (threadIndex >= m_numThreads)
			return nullptr;
		return m_workers[threadIndex].deferredContext;
	}

	DWORD GetThreadCount() const
	{
		return m_numThreads;
	}

	DWORD GetCurrentThreadIndex() const
	{
		return g_currentThreadIndex;
	}

private:
	void Execute()
	{
		while (true)
		{
			TaskType task;
			if (m_concurrentTasks.empty())
				break;

			if (m_concurrentTasks.try_pop(task))
			{
				task(); // 사용자 정의 함수 안에서 GetDeferredContext(threadIndex) 사용 가능
			}
		}
	}

private:
	MeshRenderJob* m_workers{};
	ThreadNotification m_eventNotification;
	concurrent_queue<TaskType> m_concurrentTasks;
	DWORD m_numThreads{};
};

static inline ID3D11DeviceContext* GetLocalDefferdContext(RenderThreadPool* poolPtr)
{
	auto index = poolPtr->GetCurrentThreadIndex();
	auto defferdContext = poolPtr->GetDeferredContext(index);

	return defferdContext;
}