#pragma once

#include "prsm_abi.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>

namespace prsm::host
{
// Bounded, single-game-thread harness extension, NOT a compiler/language ABI.
// Frames must be GC objects. Register a returned frame before any GC safe point.
class CE_API TaskScheduler
{
public:
    using TaskId = std::uint64_t;
    using GroupId = std::uint64_t;
    using FailedCallback = std::function<void(void* owner)>;
    enum class TaskState { Ready, Suspended, Joining, Cancelling, Done, Cancelled, Panicked };
    enum class GroupKind { Gather, First };
    static constexpr std::size_t MaxTasks = 65536;
    static constexpr std::size_t MaxGroups = 65536;

    // The module must already be registered in GcRuntime. A lease pins its
    // callback table until this scheduler is destroyed, even when no tasks live.
    explicit TaskScheduler(const PrsmModule& module, FailedCallback on_failed = {});
    ~TaskScheduler();
    TaskScheduler(const TaskScheduler&) = delete;
    TaskScheduler& operator=(const TaskScheduler&) = delete;

    // A child inherits its parent's owner. Its frame receives a root immediately.
    // Start queues work; it does not run user code. IDs remain queryable after exit.
    TaskId Start(void* frame, void* owner = nullptr, TaskId parent = 0);
    GroupId CreateGroup(TaskId parent, GroupKind kind);
    TaskId AddBranch(GroupId group, void* frame); // source order = insertion order
    void SealGroup(GroupId group); // branches do not run before sealing

    // A poll is one deterministic game frame. New ready children can get their
    // initial opportunity in this poll. A task resumes/cancels at most once per
    // poll. Zero waits also yield until the next poll. dt must be finite and >= 0.
    // WAIT_TASKS joins the task's direct children and all of its sealed groups.
    // UNTIL predicates return 0/positive for false/true, negative for failure.
    void Poll(double dt);
    void Cancel(TaskId task); // only requests; cleanup runs in later Poll calls
    void CancelOwner(void* owner);
    void CancelAll();
    // Synchronously cancel and clean up one owner's tasks before the host runs
    // that owner's termination hooks (D-041), without advancing the game frame
    // for any other owner. Children clean up before their parents. Returns false
    // while a cancel callback reports SUSPENDED (cleanup continues in later
    // Poll calls) or a failure isolated the owner; roots are kept until terminal.
    bool DrainOwner(void* owner);
    // A reported failure skips all user cancel/defer code, drops every live root
    // for that owner, and notifies on_failed exactly once. ce_panic is still fatal;
    // this bounded harness handles status-returning failures, not stack recovery.
    void FailOwner(void* owner);

    TaskState State(TaskId task) const;
    // Owner recorded at Start (children inherit it). Hosts use it to restore the
    // owning component's engine context while a callback of this task runs.
    void* Owner(TaskId task) const;
    bool IsComplete(TaskId task) const;
    bool IsOwnerFailed(void* owner) const;
    bool GroupReady(GroupId group) const; // includes cancellation cleanup
    std::optional<TaskId> GroupWinner(GroupId group) const;
    // Draft recoverable branch propagation. Distinct from owner-fatal panic.
    void MarkBranchFailure(TaskId task);
    std::optional<TaskId> GroupError(GroupId group) const;
    std::size_t LiveTaskCount() const;
    std::uint64_t FrameNumber() const;
    double GameSeconds() const;

    // Draft bridge validation only; these do not change language scheduling.
    std::uint32_t BridgeIdentity() const;
    bool IsDirectChild(TaskId child, TaskId parent) const;
    bool GroupBelongsTo(GroupId group, TaskId parent) const;

private:
    friend class ReloadCoordinator;
    TaskId StartPrepared(void* frame, void* owner);
    TaskId StartImpl(void* frame, void* owner, TaskId parent, bool prepared);
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace prsm::host
