#pragma once

#include "RHI/IRenderDeviceServices.h"
#include "../Utility_Framework/StandardLibrary.h"

namespace material_graph
{
// Reuses per-frame scene buffers instead of creating a committed resource every
// frame. Owners return a buffer from their destructor, which runs only after the
// GPU completed the recording or when it was never submitted, so a returned
// buffer has no pending GPU use. Every pooled use rewrites the buffer through its
// own initialize/copy pass; callers must not rely on creation-time contents.
struct TransientBufferPool
{
    explicit TransientBufferPool(IRenderDeviceServices& owner) : device(owner) {}
    ~TransientBufferPool() { Close(); }
    TransientBufferPool(const TransientBufferPool&) = delete;
    TransientBufferPool& operator=(const TransientBufferPool&) = delete;

    struct Key
    {
        std::wstring name;
        std::uint64_t bytes{};
        bool unordered{}, indirect{};
        bool operator==(const Key&) const = default;
    };

    bool Acquire(const RHIBufferDesc& desc, RHIBufferHandle& handle, std::string& error)
    {
        Key key{desc.debugName ? desc.debugName : L"", desc.bytes, desc.allowUnorderedAccess,
                desc.allowIndirectArguments};
        {
            std::lock_guard lock(mutex);
            if (const auto found = std::ranges::find(free, key, &Entry::key); found != free.end())
            {
                handle = found->handle;
                cachedBytes -= found->key.bytes;
                free.erase(found);
                return true;
            }
        }
        if (!device.CreateBuffer(desc, handle, error))
            return false;
        std::lock_guard lock(mutex);
        owned.emplace(handle.id, std::move(key));
        return true;
    }

    void Return(RHIBufferHandle handle)
    {
        if (!handle.IsValid())
            return;
        std::lock_guard lock(mutex);
        const auto found = owned.find(handle.id);
        if (found == owned.end())
        {
            device.ReleaseBuffer(handle);
            return;
        }
        if (!closed && free.size() < kMaxEntries && found->second.bytes <= kMaxBytes - cachedBytes)
        {
            cachedBytes += found->second.bytes;
            free.push_back({found->second, handle});
            return;
        }
        owned.erase(found);
        device.ReleaseBuffer(handle);
    }

    void Close()
    {
        std::lock_guard lock(mutex);
        closed = true;
        for (const auto& entry : free)
        {
            owned.erase(entry.handle.id);
            device.ReleaseBuffer(entry.handle);
        }
        free.clear();
        cachedBytes = 0;
    }

  private:
    static constexpr std::size_t kMaxEntries = 64;
    static constexpr std::uint64_t kMaxBytes = 64ull << 20;
    struct Entry
    {
        Key key;
        RHIBufferHandle handle;
    };
    IRenderDeviceServices& device;
    std::mutex mutex;
    std::vector<Entry> free;
    std::unordered_map<std::uint32_t, Key> owned;
    std::uint64_t cachedBytes{};
    bool closed{};
};

// Owners call these so a missing pool keeps the original create/release path.
inline bool CreateSceneBuffer(IRenderDeviceServices& device, TransientBufferPool* pool, const RHIBufferDesc& desc,
                              RHIBufferHandle& handle, std::string& error)
{
    return pool ? pool->Acquire(desc, handle, error) : device.CreateBuffer(desc, handle, error);
}

inline void ReleaseSceneBuffer(IRenderDeviceServices& device, const std::shared_ptr<TransientBufferPool>& pool,
                               RHIBufferHandle handle)
{
    if (!handle.IsValid())
        return;
    if (pool)
        pool->Return(handle);
    else
        device.ReleaseBuffer(handle);
}
} // namespace material_graph
