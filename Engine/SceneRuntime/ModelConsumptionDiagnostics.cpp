#include "ModelConsumptionDiagnostics.h"

#include <atomic>
#include <mutex>

namespace
{
    std::atomic<std::uint64_t> g_meshResolveGeneration{ 0 };
    std::atomic<std::uint64_t> g_meshResolveFailed{ 0 };
    std::atomic<std::uint64_t> g_instantiateGeneration{ 0 };
    std::atomic<std::uint64_t> g_instantiateRejected{ 0 };
    std::atomic<std::uint64_t> g_tickGeneration{ 0 };
    std::atomic<std::uint64_t> g_tickNone{ 0 };
    // 이름 하나만 잠금 아래 둔다 — 계수는 원자라 제품 경로가 잠금을 잡지 않는다.
    std::mutex g_nameMutex;
    std::string g_lastInstantiated;
}

namespace ModelConsumptionDiagnostics
{
    void NoteMeshResolved() noexcept
    {
        g_meshResolveGeneration.fetch_add(1, std::memory_order_relaxed);
    }

    void NoteMeshResolveFailed() noexcept
    {
        g_meshResolveFailed.fetch_add(1, std::memory_order_relaxed);
    }

    void NoteInstantiated(std::string_view modelName)
    {
        g_instantiateGeneration.fetch_add(1, std::memory_order_relaxed);
        std::lock_guard<std::mutex> guard(g_nameMutex);
        g_lastInstantiated.assign(modelName.data(), modelName.size());
    }

    void NoteInstantiateRejected() noexcept
    {
        g_instantiateRejected.fetch_add(1, std::memory_order_relaxed);
    }

    void NoteTickPath(bool generation) noexcept
    {
        (generation ? g_tickGeneration : g_tickNone).fetch_add(1, std::memory_order_relaxed);
    }

    ModelConsumptionSnapshot Snapshot()
    {
        ModelConsumptionSnapshot snapshot;
        snapshot.meshResolveGeneration = g_meshResolveGeneration.load(std::memory_order_relaxed);
        snapshot.meshResolveFailed = g_meshResolveFailed.load(std::memory_order_relaxed);
        snapshot.instantiateGeneration = g_instantiateGeneration.load(std::memory_order_relaxed);
        snapshot.instantiateRejected = g_instantiateRejected.load(std::memory_order_relaxed);
        snapshot.tickGeneration = g_tickGeneration.load(std::memory_order_relaxed);
        snapshot.tickNone = g_tickNone.load(std::memory_order_relaxed);
        std::lock_guard<std::mutex> guard(g_nameMutex);
        snapshot.lastInstantiated = g_lastInstantiated;
        return snapshot;
    }
}
