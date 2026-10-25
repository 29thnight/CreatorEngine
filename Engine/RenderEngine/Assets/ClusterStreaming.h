#pragma once
#if defined(CE_CONTRACT_STANDALONE)
#include "../../../Tools/build/ModuleAdapters/Creator.Core.Ownership.h"
#else
import Creator.Core.Ownership;
#endif
#include "MeshletCompression.h"
#if defined(CE_CONTRACT_STANDALONE)
#include "../../../Tools/build/ModuleAdapters/Creator.Core.DynamicBitset.h"
#else
import Creator.Core.DynamicBitset;
#endif
#include "ClusterTopologyPage.h"
#include "../Experiment/MeshLodData.h"
#include <stdint.h>
#include "../../Utility_Framework/Foundation/StandardLibrary.h"

namespace assets::cluster
{
    class PageIo final
    {
      public:
        using Page = own::shared_owner<const std::vector<std::byte>>;
        using Result = std::shared_future<Page>;
        static PageIo& Shared();
        Result Request(own::shared_owner<const Archive> archive, uint32_t page)
        {
            auto task = own::make_shared<std::packaged_task<Page()>>(
                [archive = std::move(archive), page]
                {
                    auto bytes = archive->Read(page);
                    std::vector<std::byte> decoded;
                    std::string error;
                    const auto attributes =
                        archive->pagedTopology
                            ? InspectTopologyPage(bytes, archive->sourceVertices, UINT32_MAX).attributes
                            : std::span<const std::byte>(bytes);
                    quantized::Require(Decode(attributes, decoded, error), "Invalid streamed cluster page");
                    return own::make_shared<const std::vector<std::byte>>(std::move(bytes));
                });
            auto result = task->get_future().share();
            std::lock_guard lock(m_mutex);
            if (m_queue.size() >= kMaximumQueuedReads)
            {
                return {};
            }
            m_queue.push_back([task] { (*task)(); });
            m_wake.notify_one();
            return result;
        }

      private:
        static constexpr std::size_t kMaximumQueuedReads = 64;
        static constexpr unsigned kWorkerCount = 4;

        PageIo()
        {
            for (unsigned worker = 0; worker < kWorkerCount; ++worker)
            {
                m_workers.emplace_back(
                    [this](std::stop_token stop)
                    {
                        for (;;)
                        {
                            std::function<void()> task;
                            {
                                std::unique_lock lock(m_mutex);
                                m_wake.wait(lock, [&] { return stop.stop_requested() || !m_queue.empty(); });
                                if (m_queue.empty() && stop.stop_requested())
                                {
                                    return;
                                }
                                task = std::move(m_queue.front());
                                m_queue.pop_front();
                            }
                            task();
                        }
                    });
            }
        }
        ~PageIo()
        {
            for (auto& worker : m_workers)
            {
                worker.request_stop();
            }
            m_wake.notify_all();
        }
        std::mutex m_mutex;
        std::condition_variable m_wake;
        std::deque<std::function<void()>> m_queue;
        std::vector<std::jthread> m_workers;
    };
    struct StreamBudget
    {
        uint64_t residentBytes{32ull << 20}, uploadBytes{1ull << 20};
        uint64_t residencyAlignment{1};
    };
    struct ResidentSet
    {
        uint64_t revision{}, bytes{}, uploadedBytes{}, evictions{}, readFailures{};
        uint64_t residencyAlignment{1}, residentLimit{};
        std::vector<uint32_t> groups;
        std::vector<own::shared_owner<const std::vector<std::byte>>> packets;
    };
    // Lower the GPU cut without changing a submitted CPU snapshot or packet
    // addresses. Complete ancestors remain available when finer children vanish.
    inline own::shared_owner<const ResidentSet> CoarsenResidentCut(const ResidentSet& source,
                                                                   std::span<const experiment::MeshClusterGroup> groups,
                                                                   uint32_t minimumLod)
    {
        quantized::Require(source.groups.size() == groups.size(), "Coarse residency group count");
        auto cut = own::make_shared<ResidentSet>(source);
        for (uint32_t i = 0; i < groups.size(); ++i)
        {
            const auto& group = groups[i];
            quantized::Require(group.parentGroup == UINT32_MAX ||
                                   (group.parentGroup > i && group.parentGroup < groups.size()),
                               "Coarse residency parent range");
            if (group.parentGroup != UINT32_MAX && group.lodIndex < minimumLod)
            {
                cut->groups[i] = 0;
            }
        }
        for (size_t i = groups.size(); i-- > 0;)
        {
            const auto parent = groups[i].parentGroup;
            if (parent != UINT32_MAX && !cut->groups[parent])
            {
                cut->groups[i] = 0;
            }
        }
        cut->packets.assign(source.packets.size(), {});
        cut->bytes = 0;
        for (uint32_t i = 0; i < groups.size(); ++i)
        {
            const auto& group = groups[i];
            if (!cut->groups[i])
            {
                continue;
            }
            quantized::Require(uint64_t(group.firstMeshlet) + group.meshletCount <= cut->packets.size(),
                               "Coarse residency packet range");
            for (uint32_t page = group.firstMeshlet; page < group.firstMeshlet + group.meshletCount; ++page)
            {
                quantized::Require(bool(source.packets[page]), "Coarse residency requires complete groups");
                if (!cut->packets[page])
                {
                    cut->packets[page] = source.packets[page];
                    const auto bytes = source.packets[page]->size();
                    cut->bytes += (bytes + cut->residencyAlignment - 1) & ~(cut->residencyAlignment - 1);
                }
            }
        }
        return cut;
    }
    // An immutable snapshot is held by the submitted buffer's upload transaction.
    // Eviction changes future snapshots only; in-flight buffers remain owned by
    // the existing RHI completion/rejection protocol, never overwritten in place.
    class Stream final
    {
      public:
        uint64_t PinnedBytes(uint64_t alignment) const
        {
            uint64_t bytes{};
            for (const auto& group : m_groups)
            {
                if (group.parentGroup == UINT32_MAX)
                {
                    for (uint32_t page = 0; page < group.meshletCount; ++page)
                    {
                        bytes += (m_archive->PacketBytes(group.firstMeshlet + page) + alignment - 1) & ~(alignment - 1);
                    }
                }
            }
            return bytes;
        }
        uint64_t GpuFeedbackGroups() const
        {
            std::lock_guard lock(m_mutex);
            return m_feedbackGroups;
        }
        void SubmitGpuRequests(std::span<const uint32_t> groups)
        {
            std::lock_guard lock(m_mutex);
            for (const auto group : groups)
            {
                quantized::Require(group < m_groups.size(), "GPU cluster feedback group range");
                m_feedback.insert(group);
                ++m_feedbackGroups;
            }
        }
        Stream(own::shared_owner<const Archive> archive, std::vector<experiment::MeshClusterGroup> groups)
            : m_archive(std::move(archive)), m_groups(std::move(groups))
        {
            quantized::Require(m_archive && !m_archive->packets.empty(), "Empty cluster archive");
            if (m_groups.empty())
            {
                experiment::MeshClusterGroup group{};
                group.firstMeshlet = 0;
                group.meshletCount = static_cast<uint32_t>(m_archive->packets.size());
                group.parentGroup = UINT32_MAX;
                group.childGroup = UINT32_MAX;
                group.secondChild = UINT32_MAX;
                m_groups.push_back(group);
            }
            m_lastUse.resize(m_groups.size());
            for (uint32_t i = 0; i < m_groups.size(); ++i)
            {
                const auto& g = m_groups[i];
                quantized::Require(
                    g.meshletCount && uint64_t(g.firstMeshlet) + g.meshletCount <= m_archive->packets.size() &&
                        (g.parentGroup == UINT32_MAX || (g.parentGroup > i && g.parentGroup < m_groups.size())),
                    "Cluster stream group range");
            }
        }
        own::shared_owner<const ResidentSet> Prepare(uint64_t recording, std::span<const uint32_t> requests,
                                                     const StreamBudget& budget = {})
        {
            std::lock_guard lock(m_mutex);
            quantized::Require(recording && budget.residentBytes && budget.uploadBytes && budget.residencyAlignment &&
                                   !(budget.residencyAlignment & (budget.residencyAlignment - 1)),
                               "Cluster stream budget/recording");
            const auto charge = [&](uint64_t size)
            { return (size + budget.residencyAlignment - 1) & ~(budget.residencyAlignment - 1); };
            // Feedback from completed GPU views takes priority over CPU prefetch.
            // Keep it pending until a new recording seals the next residency cut.
            if (!m_current || m_recording != recording)
            {
                m_pending.insert(m_pending.begin(), m_feedback.begin(), m_feedback.end());
                m_feedback.clear();
            }
            // The pending vector remains the priority queue; the mask is membership
            // only. This includes feedback inserted ahead of older view requests.
            m_pendingGroups.assign(m_groups.size(), false);
            auto& pendingGroups = m_pendingGroups;
            for (const auto group : std::span{m_pending})
            {
                pendingGroups.set(group);
            }
            // All views prepared in one recording consume one immutable cut.
            // New view requests are retained and serviced in the next recording.
            for (auto group : requests)
            {
                quantized::Require(group < m_groups.size(), "Cluster stream request range");
                if (!pendingGroups.test(group))
                {
                    m_pending.push_back(group);
                    pendingGroups.set(group);
                }
            }
            if (m_current && m_recording == recording)
            {
                quantized::Require(budget.residentBytes == m_budget.residentBytes &&
                                       budget.uploadBytes == m_budget.uploadBytes &&
                                       budget.residencyAlignment == m_budget.residencyAlignment,
                                   "Cluster budgets cannot change within one recording");
                return m_current;
            }
            // A complete immutable cut needs no reconstruction when neither limits nor
            // transient statistics change. Still service pending requests in priority order
            // so a later budget reduction sees the exact same LRU history.
            if (m_current && m_allResident && m_reads.empty() && !m_current->uploadedBytes && !m_current->evictions &&
                !m_current->readFailures && budget.residentBytes == m_budget.residentBytes &&
                budget.uploadBytes == m_budget.uploadBytes && budget.residencyAlignment == m_budget.residencyAlignment)
            {
                for (const auto request : std::span{m_pending})
                {
                    m_lastUse[request] = ++m_clock;
                }
                m_pending.clear();
                m_recording = recording;
                return m_current;
            }
            // Page membership only; a flat mask avoids one tree node per page per recording.
            m_wantedPages.assign(m_archive->packets.size(), false);
            auto& wantedPages = m_wantedPages;
            for (auto request : std::span{m_pending})
            {
                for (auto group = request; group != UINT32_MAX; group = m_groups[group].parentGroup)
                {
                    const auto& g = m_groups[group];
                    for (uint32_t i = 0; i < g.meshletCount; ++i)
                    {
                        wantedPages.set(g.firstMeshlet + i);
                    }
                }
            }
            std::erase_if(m_reads,
                          [&](const auto& read)
                          {
                              return !wantedPages.test(read.first) &&
                                     read.second.wait_for(std::chrono::seconds(0)) == std::future_status::ready;
                          });
            auto next = own::make_shared<ResidentSet>();
            next->residencyAlignment = budget.residencyAlignment;
            next->residentLimit = budget.residentBytes;
            next->packets.resize(m_archive->packets.size());
            next->groups.resize(m_groups.size());
            auto remaining = budget.residentBytes;
            uint64_t scheduledBytes{};
            m_attempted.assign(m_groups.size(), false);
            auto& attempted = m_attempted;
            const auto admit = [&](uint32_t group, bool mandatory)
            {
                if (attempted.test(group))
                {
                    return;
                }
                attempted.set(group);
                const auto& g = m_groups[group];
                uint64_t bytes{};
                for (uint32_t i = 0; i < g.meshletCount; ++i)
                {
                    bytes += charge(m_archive->PacketBytes(g.firstMeshlet + i));
                }
                if (mandatory)
                {
                    quantized::Require(bytes <= remaining, "Pinned root clusters exceed residency budget");
                }
                else if ((g.parentGroup != UINT32_MAX && !next->groups[g.parentGroup]) || bytes > remaining)
                {
                    return;
                }
                // Reserve the complete replacement group. Partial pages may
                // upload over several recordings; the group is drawable only
                // after all of its pages have arrived.
                remaining -= bytes;
                bool complete = true;
                for (uint32_t i = 0; i < g.meshletCount; ++i)
                {
                    const auto id = g.firstMeshlet + i;
                    const auto old = m_current ? m_current->packets[id] : nullptr;
                    const auto pageBytes = m_archive->PacketBytes(id);
                    const auto available = budget.uploadBytes - (std::min)(budget.uploadBytes, next->uploadedBytes);
                    if (old)
                    {
                        next->packets[id] = old;
                    }
                    else if (mandatory || pageBytes <= available)
                    {
                        if (mandatory || !m_archive->pageReader)
                        {
                            next->packets[id] = own::make_shared<const std::vector<std::byte>>(m_archive->Read(id));
                        }
                        else
                        {
                            auto found = m_reads.find(id);
                            if (found != m_reads.end() &&
                                found->second.wait_for(std::chrono::seconds(0)) == std::future_status::ready)
                            {
                                try
                                {
                                    next->packets[id] = found->second.get();
                                }
                                catch (const std::exception&)
                                {
                                    ++next->readFailures;
                                }
                                m_reads.erase(found);
                            }
                            else if (found == m_reads.end() && m_reads.size() < kMaximumPendingReads &&
                                     pageBytes <= budget.uploadBytes - (std::min)(budget.uploadBytes, scheduledBytes))
                            {
                                auto read = PageIo::Shared().Request(m_archive, id);
                                if (read.valid())
                                {
                                    m_reads.emplace(id, std::move(read));
                                    scheduledBytes += pageBytes;
                                }
                            }
                        }
                        if (next->packets[id])
                        {
                            next->uploadedBytes += pageBytes;
                        }
                    }
                    complete = complete && bool(next->packets[id]);
                    if (next->packets[id])
                    {
                        next->bytes += charge(pageBytes);
                    }
                }
                next->groups[group] = complete;
            };
            for (uint32_t i = 0; i < m_groups.size(); ++i)
            {
                if (m_groups[i].parentGroup == UINT32_MAX)
                {
                    admit(i, true);
                }
            }
            // Service requests in view priority order, admitting their ancestor
            // chain first. Keep recently used old groups only in remaining space.
            auto& chain = m_chain;
            chain.reserve(16);
            for (auto request : std::span{m_pending})
            {
                chain.clear();
                for (auto g = request; g != UINT32_MAX; g = m_groups[g].parentGroup)
                {
                    chain.push_back(g);
                }
                for (std::size_t index = chain.size(); index > 0; --index)
                {
                    const auto group = chain[index - 1];
                    if (!next->groups[group])
                    {
                        admit(group, false);
                    }
                }
                if (next->groups[request])
                {
                    m_lastUse[request] = ++m_clock;
                }
            }
            auto& retained = m_retained;
            retained.clear();
            if (m_current)
            {
                for (uint32_t group = 0; group < m_groups.size(); ++group)
                {
                    if (m_current->groups[group] && !next->groups[group])
                    {
                        retained.push_back(group);
                    }
                }
                std::stable_sort(retained.begin(), retained.end(),
                                 [&](auto a, auto b) { return m_lastUse[a] > m_lastUse[b]; });
                for (auto group : retained)
                {
                    admit(group, false);
                }
            }
            if (m_current)
            {
                for (uint32_t g = 0; g < m_groups.size(); ++g)
                {
                    next->evictions += m_current->groups[g] && !next->groups[g];
                }
            }
            const bool changed = !m_current || next->groups != m_current->groups ||
                                 !std::ranges::equal(next->packets, m_current->packets, [](const auto& a, const auto& b)
                                                     { return a.operator->() == b.operator->(); }) ||
                                 next->residencyAlignment != m_current->residencyAlignment;
            next->revision = m_current ? m_current->revision + changed : 1;
            m_allResident = std::ranges::all_of(std::span(next->groups), [](auto resident) { return resident != 0; });
            m_current = next;
            m_recording = recording;
            m_budget = budget;
            m_pending.clear();
            return m_current;
        }

      private:
        static constexpr std::size_t kMaximumPendingReads = 8;

        own::shared_owner<const Archive> m_archive;
        std::vector<experiment::MeshClusterGroup> m_groups;
        mutable std::mutex m_mutex;
        std::vector<uint32_t> m_pending;
        std::set<uint32_t> m_feedback;
        uint64_t m_feedbackGroups{};
        // Group IDs are validated dense indices; no per-group tree lookup or node lifetime.
        std::vector<uint64_t> m_lastUse;
        // Mutex-owned scratch never escapes into immutable submitted ResidentSets.
        ce::dynamic_bitset m_pendingGroups, m_wantedPages, m_attempted;
        std::vector<uint32_t> m_chain, m_retained;
        uint64_t m_clock{};
        own::shared_owner<const ResidentSet> m_current;
        uint64_t m_recording{};
        bool m_allResident{};
        StreamBudget m_budget;
        std::map<uint32_t, PageIo::Result> m_reads;
    };
} // namespace assets::cluster
