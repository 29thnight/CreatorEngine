#pragma once
#include "../../Utility_Framework/Ownership.h"
#include "JobScheduler.h"
#include "RHIRecordingTaskGraph.h"
#include "../../Utility_Framework/StandardLibrary.h"

struct RHIRecordingExecutionOptions
{
    bool batchSubmit{true};
    own::shared_owner<job_graph_diagnostics> diagnostics;
    uint64_t* waitBeginNanoseconds{nullptr};
};

// RG4 and owned queue recording share the same engine jobs and join contract.
// A completed handle has released every callback, including on job failure.
inline void RHIRunCommandRecordingJobs(job_scheduler& scheduler, const std::function<void(uint32_t)>& job,
                                       uint32_t targetCount)
{
    if (targetCount == 0)
    {
        return;
    }
    if (thread_pool::is_worker_thread())
    {
        throw std::logic_error("command recording cannot wait from a job worker");
    }
    // Share one indexed callback instead of allocating a task vector and one
    // function wrapper per command target. Completion still releases all
    // callback captures before wait returns, including on failure.
    scheduler.submit_indexed(targetCount, [&job](std::size_t target) { job(static_cast<uint32_t>(target)); }).wait();
}

// 독립 작업은 먼저 진행하고 target·CPU 선행 조건만 합류한다. 소유자는 마지막에 한 번 기다린다.
inline void RHIRunCommandRecordingTaskGraph(job_scheduler& scheduler, const RHIRecordingTaskGraph& graph,
                                            const std::function<void(uint32_t)>& job,
                                            const RHIRecordingExecutionOptions& options = {})
{
    if (graph.nodes.empty())
    {
        return;
    }
    if (thread_pool::is_worker_thread())
    {
        throw std::logic_error("command recording cannot wait from a job worker");
    }
    for (size_t node = 0; node < graph.nodes.size(); ++node)
    {
        for (const auto dependency : graph.nodes[node].dependencies)
        {
            if (dependency >= node)
            {
                throw std::invalid_argument("recording dependencies must precede their task");
            }
        }
    }
    if (options.batchSubmit)
    {
        // 전체 의존 관계를 한 번에 수락한다. 후속 작업은 enki 의존성으로 이어져
        // 준비될 때마다 별도 dispatcher handoff를 다시 만들지 않는다.
        // Retain conversion storage; the native executor copies or reuses its
        // own topology before submit_graph returns. Owner-side joins exclude
        // recursive recording on this thread.
        static thread_local std::vector<job_graph_node> storage;
        if (storage.size() < graph.nodes.size())
        {
            storage.resize(graph.nodes.size());
        }
        auto nodes = std::span(storage.data(), graph.nodes.size());
        for (size_t index = 0; index < nodes.size(); ++index)
        {
            if (nodes[index].dependencies != graph.nodes[index].dependencies)
            {
                nodes[index].dependencies = graph.nodes[index].dependencies;
            }
        }
        auto handle = scheduler.submit_graph(
            nodes, [&job](size_t node) { job(static_cast<uint32_t>(node)); }, options.diagnostics);
        if (options.waitBeginNanoseconds)
        {
            *options.waitBeginNanoseconds = job_timestamp_ns();
        }
        handle.wait();
        return;
    }
    struct fallback_storage
    {
        std::vector<job_handle> handles;
        std::vector<job_handle> dependencies;
    };
    static thread_local fallback_storage storage;
    auto& handles = storage.handles;
    auto& dependencies = storage.dependencies;
    handles.clear();
    handles.reserve(graph.nodes.size());
    std::exception_ptr failure;
    try
    {
        for (uint32_t node = 0; node < graph.nodes.size(); ++node)
        {
            dependencies.clear();
            dependencies.reserve(graph.nodes[node].dependencies.size());
            for (const auto dependency : graph.nodes[node].dependencies)
            {
                dependencies.push_back(handles[dependency]);
            }
            job_group group;
            group.add([&job, node] { job(node); });
            handles.push_back(scheduler.submit_after(dependencies, std::move(group)));
        }
        auto handle = scheduler.submit_after(handles, job_group{});
        if (options.waitBeginNanoseconds)
        {
            *options.waitBeginNanoseconds = job_timestamp_ns();
        }
        handle.wait();
    }
    catch (...)
    {
        failure = std::current_exception();
    }
    // 중간 제출 실패나 의존 오류도 이미 수락된 콜백의 캡처를 모두 해제한 뒤 전파한다.
    if (failure)
    {
        for (const auto& handle : handles)
        {
            try
            {
                handle.wait();
            }
            catch (...)
            {
            }
        }
        dependencies.clear();
        handles.clear();
        std::rethrow_exception(failure);
    }
    dependencies.clear();
    handles.clear();
}
