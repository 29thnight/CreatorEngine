#pragma once
#include "MeshletCompression.h"
#include "../Experiment/MeshLodData.h"

namespace assets::cluster
{
// Optional worker-side preparation of the exact pages Stream always pins.
// The reader still authenticates every miss. Non-root pages retain normal
// streaming/eviction, and each immutable archive owns its own bounded cache.
class PreparedRootPageReader final
{
    using Page = std::shared_ptr<const std::vector<std::byte>>;
    struct State
    {
        std::function<std::vector<std::byte>(uint32_t)> read;
        std::vector<uint32_t> roots;
        std::mutex mutex;
        std::map<uint32_t, Page> pages;
    };
    std::shared_ptr<State> m_state;
public:
    PreparedRootPageReader(std::function<std::vector<std::byte>(uint32_t)> read,
        const Archive& archive, std::span<const experiment::MeshClusterGroup> groups,
        uint64_t byteLimit = 32ull << 20) : m_state(std::make_shared<State>())
    {
        m_state->read = std::move(read);
        std::set<uint32_t> roots;
        if (groups.empty())
            for (uint32_t page = 0; page < archive.packets.size(); ++page) roots.insert(page);
        else
            for (const auto& group : groups)
                if (group.parentGroup == UINT32_MAX)
                    for (uint32_t page = 0; page < group.meshletCount; ++page)
                        roots.insert(group.firstMeshlet + page);
        uint64_t bytes{};
        for (const auto page : roots)
        {
            const auto size = archive.PacketBytes(page);
            if (size <= byteLimit - bytes)
            {
                m_state->roots.push_back(page);
                bytes += size;
            }
        }
    }
    std::vector<std::byte> operator()(uint32_t page) const
    {
        Page cached;
        {
            std::lock_guard lock(m_state->mutex);
            if (const auto found = m_state->pages.find(page); found != m_state->pages.end()) cached = found->second;
        }
        return cached ? *cached : m_state->read(page);
    }
    void Prepare(const std::atomic<bool>* cancelled = nullptr) const
    {
        for (const auto page : m_state->roots)
        {
            if (cancelled && cancelled->load(std::memory_order_acquire)) return;
            {
                std::lock_guard lock(m_state->mutex);
                if (m_state->pages.contains(page)) continue;
            }
            // Never hold the cache mutex across I/O, validation or byte copies.
            // A concurrent miss can duplicate work, but cannot publish unverified bytes.
            auto bytes = std::make_shared<const std::vector<std::byte>>(m_state->read(page));
            std::lock_guard lock(m_state->mutex);
            m_state->pages.emplace(page, std::move(bytes));
        }
    }
};

inline void PrepareRootPages(const Archive& archive, const std::atomic<bool>* cancelled = nullptr)
{
    if (const auto* reader = archive.pageReader.target<PreparedRootPageReader>()) reader->Prepare(cancelled);
}
}
