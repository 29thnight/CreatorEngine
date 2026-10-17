#pragma once
#include "MeshletCompression.h"
#include "ClusterTopologyPage.h"
#include "../Experiment/MeshLodData.h"
#include <stdint.h>
#include "../../Utility_Framework/StandardLibrary.h"

namespace assets::cluster
{
class PageIo final
{
  public:
    using Page = std::shared_ptr<const std::vector<std::byte>>;
    using Result = std::shared_future<Page>;
    static PageIo& Shared()
    {
        static PageIo instance;
        return instance;
    }
    Result Request(std::shared_ptr<const Archive> archive, uint32_t page)
    {
        auto task = std::make_shared<std::packaged_task<Page()>>([archive = std::move(archive), page] {
            auto bytes = archive->Read(page);
            std::vector<std::byte> decoded;
            std::string error;
            const auto attributes = archive->pagedTopology
                ? InspectTopologyPage(bytes, archive->sourceVertices, UINT32_MAX).attributes : std::span<const std::byte>(bytes);
            quantized::Require(Decode(attributes, decoded, error), "Invalid streamed cluster page");
            return std::make_shared<const std::vector<std::byte>>(std::move(bytes));
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
            m_workers.emplace_back([this](std::stop_token stop) {
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
    std::vector<std::shared_ptr<const std::vector<std::byte>>> packets;
};
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
            if (group.parentGroup == UINT32_MAX)
                for (uint32_t page = 0; page < group.meshletCount; ++page)
                    bytes += (m_archive->PacketBytes(group.firstMeshlet + page) + alignment - 1) & ~(alignment - 1);
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
    Stream(std::shared_ptr<const Archive> archive, std::vector<experiment::MeshClusterGroup> groups)
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
        for (uint32_t i = 0; i < m_groups.size(); ++i)
        {
            const auto& g = m_groups[i];
            quantized::Require(
                g.meshletCount && uint64_t(g.firstMeshlet) + g.meshletCount <= m_archive->packets.size() &&
                    (g.parentGroup == UINT32_MAX || (g.parentGroup > i && g.parentGroup < m_groups.size())),
                "Cluster stream group range");
        }
    }
    std::shared_ptr<const ResidentSet> Prepare(uint64_t recording, std::span<const uint32_t> requests,
                                               const StreamBudget& budget = {})
    {
        std::lock_guard lock(m_mutex);
        quantized::Require(recording && budget.residentBytes && budget.uploadBytes && budget.residencyAlignment &&
                               !(budget.residencyAlignment & (budget.residencyAlignment - 1)),
                           "Cluster stream budget/recording");
        const auto charge = [&](uint64_t size) {
            return (size + budget.residencyAlignment - 1) & ~(budget.residencyAlignment - 1);
        };
        // Feedback from completed GPU views takes priority over CPU prefetch.
        // Keep it pending until a new recording seals the next residency cut.
        if (!m_current || m_recording != recording)
        {
            m_pending.insert(m_pending.begin(), m_feedback.begin(), m_feedback.end());
            m_feedback.clear();
        }
        // All views prepared in one recording consume one immutable cut.
        // New view requests are retained and serviced in the next recording.
        for (auto group : requests)
        {
            quantized::Require(group < m_groups.size(), "Cluster stream request range");
            if (std::find(m_pending.begin(), m_pending.end(), group) == m_pending.end())
            {
                m_pending.push_back(group);
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
        std::set<uint32_t> wantedPages;
        for (auto request : m_pending)
        {
            for (auto group = request; group != UINT32_MAX; group = m_groups[group].parentGroup)
            {
                const auto& g = m_groups[group];
                for (uint32_t i = 0; i < g.meshletCount; ++i)
                {
                    wantedPages.insert(g.firstMeshlet + i);
                }
            }
        }
        std::erase_if(m_reads, [&](const auto& read) {
            return !wantedPages.contains(read.first) &&
                   read.second.wait_for(std::chrono::seconds(0)) == std::future_status::ready;
        });
        auto next = std::make_shared<ResidentSet>();
        next->residencyAlignment = budget.residencyAlignment;
        next->residentLimit = budget.residentBytes;
        next->packets.resize(m_archive->packets.size());
        next->groups.resize(m_groups.size());
        auto remaining = budget.residentBytes;
        uint64_t scheduledBytes{};
        std::vector<bool> attempted(m_groups.size());
        const auto admit = [&](uint32_t group, bool mandatory) {
            if (attempted[group])
            {
                return;
            }
            attempted[group] = true;
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
                        next->packets[id] = std::make_shared<const std::vector<std::byte>>(m_archive->Read(id));
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
        for (auto request : m_pending)
        {
            std::vector<uint32_t> chain;
            for (auto g = request; g != UINT32_MAX; g = m_groups[g].parentGroup)
            {
                chain.push_back(g);
            }
            for (auto it = chain.rbegin(); it != chain.rend(); ++it)
            {
                if (!next->groups[*it])
                {
                    admit(*it, false);
                }
            }
            if (next->groups[request])
            {
                m_lastUse[request] = ++m_clock;
            }
        }
        std::vector<uint32_t> retained;
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
        const bool changed = !m_current || next->groups != m_current->groups || next->packets != m_current->packets ||
                             next->residencyAlignment != m_current->residencyAlignment;
        next->revision = m_current ? m_current->revision + changed : 1;
        m_current = next;
        m_recording = recording;
        m_budget = budget;
        m_pending.clear();
        return m_current;
    }

  private:
    static constexpr std::size_t kMaximumPendingReads = 8;

    std::shared_ptr<const Archive> m_archive;
    std::vector<experiment::MeshClusterGroup> m_groups;
    mutable std::mutex m_mutex;
    std::vector<uint32_t> m_pending;
    std::set<uint32_t> m_feedback;
    uint64_t m_feedbackGroups{};
    std::map<uint32_t, uint64_t> m_lastUse;
    uint64_t m_clock{};
    std::shared_ptr<const ResidentSet> m_current;
    uint64_t m_recording{};
    StreamBudget m_budget;
    std::map<uint32_t, PageIo::Result> m_reads;
};
} // namespace assets::cluster
