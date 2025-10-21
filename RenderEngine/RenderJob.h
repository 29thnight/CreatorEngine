#pragma once
#include "../Utility_Framework/Core.Minimal.h"
#include "../Utility_Framework/Core.Thread.hpp"

using RenderTask = std::function<void(ID3D11DeviceContext* deferredContext)>;

template<class F>
concept RenderCallableCopyable =
std::invocable<F&, ID3D11DeviceContext*>&&
std::is_copy_constructible_v<std::decay_t<F>>&&
std::is_move_constructible_v<std::decay_t<F>>;

class RenderThreadPool
{
public:
    RenderThreadPool(ID3D11Device* device, int numThreads = 0, DWORD_PTR affinityMask = 0, int priority = THREAD_PRIORITY_NORMAL)
        : m_device(device), m_pool(numThreads, affinityMask, priority)
    {
        const int count = m_pool.GetThreadCount();
        m_contexts.resize(count);

        for (int i = 0; i < count; ++i)
        {
            HRESULT hr = m_device->CreateDeferredContext(0, &m_contexts[i]);
            if (FAILED(hr))
            {
                m_contexts[i] = nullptr;
                DirectX11::ThrowIfFailed(hr);
            }
        }

        // 각 워커 스레드에서 1회 COM 초기화 수행
        m_pool.SetThreadInitCallback([] 
        {
            static thread_local bool s_comInitialized = [] 
            {
                HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
                if (FAILED(hr) && hr != RPC_E_CHANGED_MODE)
                    DirectX11::ThrowIfFailed(hr);
                return true;
            }();
        });

		// 각 워커 스레드에서 COM 해제
        m_pool.SetThreadExitCallback([] 
        {
            CoUninitialize();
		});
    }

    ~RenderThreadPool()
    {
        for (auto* ctx : m_contexts)
        {
            if (ctx) ctx->Release();
        }
    }

    template <RenderCallableCopyable F>
    void Enqueue(F&& f)
    {
        using Fn = std::decay_t<F>; // 람다 캡처에 보관될 실제 타입
        m_pool.Enqueue([this, fn = static_cast<Fn>(std::forward<F>(f))]() mutable
            noexcept(std::is_nothrow_invocable_v<Fn&, ID3D11DeviceContext*>)
            {
                const int index = GetThreadIndex();
                ID3D11DeviceContext* ctx = m_contexts[index];
                std::invoke(fn, ctx);
            });
    }

    void NotifyAllAndWait()
    {
        m_pool.NotifyAllAndWait();
    }

    int GetThreadCount() const { return m_pool.GetThreadCount(); }

private:
    int GetThreadIndex()
    {
        DWORD tid = GetCurrentThreadId();
        std::unique_lock lock(m_threadIdMutex);

        for (int i = 0; i < m_threadIds.size(); ++i)
        {
            if (m_threadIds[i] == tid)
                return i;
        }
        // 첫 호출 시 매핑
        m_threadIds.push_back(tid);
        return static_cast<int>(m_threadIds.size() - 1);
    }

private:
    ID3D11Device* m_device = nullptr;
    std::vector<ID3D11DeviceContext*> m_contexts;
    std::vector<DWORD> m_threadIds;
    std::mutex m_threadIdMutex;
    ThreadPool<std::function<void()>> m_pool;
};
