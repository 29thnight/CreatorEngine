#pragma once

#include "../../../Utility_Framework/JobScheduler.h"
#include "../../../EngineDiagnostics/ProfileScope.h"

inline bool RGParallelSetupEnabled()
{
    static const bool enabled = []
    {
        char* option{};
        size_t length{};
        const bool read = _dupenv_s(&option, &length, "CREATOR_RDG_PARALLEL_SETUP") == 0;
        const bool value = !(read && option && std::string_view{option} == "0");
        std::free(option);
        return value;
    }();
    return enabled;
}

enum class RGSetupWaitPoint
{
    Compile,
    Execute
};

struct RGSetupTaskHandle
{
    const void* owner{};
    uint64_t generation{};
    size_t index{SIZE_MAX};
};

// Admission and lifecycle belong to the graph owner. Workers only write their
// captured result; completion releases that capture before the owner can reset.
class RGSetupTaskGraph final
{
  public:
    explicit RGSetupTaskGraph(job_scheduler& scheduler) : m_scheduler(scheduler) {}
    ~RGSetupTaskGraph() { Drain(); }
    RGSetupTaskGraph(const RGSetupTaskGraph&) = delete;
    RGSetupTaskGraph& operator=(const RGSetupTaskGraph&) = delete;

    template<class F>
    RGSetupTaskHandle AddTask(ce::marker_id marker, std::span<const RGSetupTaskHandle> prerequisites,
                              RGSetupWaitPoint waitPoint, F&& callback)
    {
        RequireOwner();
        if (m_executeClosed || (waitPoint == RGSetupWaitPoint::Compile && m_compileClosed))
        {
            throw std::invalid_argument("RDG setup admission passed its wait point.");
        }
        auto& dependencies = m_dependencies;
        dependencies.clear();
        dependencies.reserve(prerequisites.size());
        for (const auto handle : prerequisites)
        {
            const auto& entry = Checked(handle);
            if (waitPoint == RGSetupWaitPoint::Compile && entry.waitPoint == RGSetupWaitPoint::Execute)
            {
                throw std::invalid_argument("Compile setup cannot depend on Execute setup.");
            }
            dependencies.push_back(entry.job);
        }
        ce::cpu_span_context context{};
#if !CE_SHIPPING
        context = ce::current_cpu_context;
#endif
        job_group group;
        group.add(
            [marker, context, callback = std::forward<F>(callback)]() mutable
            {
                ce::profile_context_scope identity{context};
                ce::profile_scope scope{marker};
                callback();
            });
        const auto index = m_entries.size();
        // Reserve ownership before submitting: a post-admission allocation
        // failure must never lose an accepted writer's completion token.
        m_entries.push_back({{}, waitPoint});
        try
        {
            m_entries[index].job = m_scheduler.submit_after(dependencies, std::move(group));
        }
        catch (...)
        {
            m_entries.pop_back();
            throw;
        }
        return {this, m_generation, index};
    }

    void Wait(RGSetupTaskHandle handle)
    {
        RequireOwner();
        Checked(handle).job.wait_assisting();
    }

    void Join(RGSetupWaitPoint waitPoint)
    {
        RequireOwner();
        m_compileClosed = true;
        m_executeClosed = m_executeClosed || waitPoint == RGSetupWaitPoint::Execute;
        std::exception_ptr failure;
        for (const auto& entry : m_entries)
        {
            if (waitPoint == RGSetupWaitPoint::Compile && entry.waitPoint != waitPoint)
            {
                continue;
            }
            try
            {
                entry.job.wait_assisting();
            }
            catch (...)
            {
                if (!failure)
                {
                    failure = std::current_exception();
                }
            }
        }
        if (failure)
        {
            std::rethrow_exception(failure);
        }
    }

    void Drain() noexcept
    {
        for (const auto& entry : m_entries)
        {
            if (thread_pool::is_worker_thread() && !entry.job.is_complete())
            {
                // A worker cannot drain pending writers. Never turn a lifecycle
                // violation into destruction of storage those writers still use.
                std::terminate();
            }
            try
            {
                entry.job.wait_assisting();
            }
            catch (...)
            {
            }
        }
    }

    void RecycleForOwner()
    {
        if (thread_pool::is_worker_thread())
        {
            throw std::logic_error("RDG setup recycling requires a graph owner thread.");
        }
        Drain();
        m_owner = std::this_thread::get_id();
        Reset();
    }

    void Reset()
    {
        RequireOwner();
        Drain();
        m_entries.clear();
        m_dependencies.clear();
        m_compileClosed = false;
        m_executeClosed = false;
        m_generation = NextGeneration();
    }

  private:
    static uint64_t NextGeneration()
    {
        static std::atomic<uint64_t> generation{1};
        const auto value = generation.fetch_add(1, std::memory_order_relaxed);
        if (value == 0)
        {
            std::terminate();
        }
        return value;
    }
    struct Entry
    {
        job_handle job;
        RGSetupWaitPoint waitPoint{};
    };
    void RequireOwner() const
    {
        if (std::this_thread::get_id() != m_owner || thread_pool::is_worker_thread())
        {
            throw std::logic_error("RDG setup lifecycle requires its owner thread.");
        }
    }
    const Entry& Checked(RGSetupTaskHandle handle) const
    {
        if (handle.owner != this || handle.generation != m_generation || handle.index >= m_entries.size())
        {
            throw std::invalid_argument("Foreign, stale or forward RDG setup dependency.");
        }
        return m_entries[handle.index];
    }
    job_scheduler& m_scheduler;
    std::thread::id m_owner{std::this_thread::get_id()};
    uint64_t m_generation{NextGeneration()};
    std::vector<Entry> m_entries;
    std::vector<job_handle> m_dependencies;
    bool m_compileClosed{}, m_executeClosed{};
};
