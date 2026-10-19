#include "task_scheduler.h"
#include "script_boundary.h"
#include "gc_runtime.h"
#include "task_bridge.h"

#include <algorithm>
#include <optional>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <thread>
#include <unordered_set>
#include <utility>
#include <vector>

namespace prsm::host
{
namespace
{
bool Terminal(TaskScheduler::TaskState state)
{
    using S = TaskScheduler::TaskState;
    return state == S::Done || state == S::Cancelled || state == S::Panicked;
}
struct ResumeCall
{
    const PrsmModule* module;
    void* frame;
    PrsmWait wait{};
    PrsmTaskStatus result = PRSM_TASK_PANICKED;
    static void Run(void* data)
    {
        auto& call = *static_cast<ResumeCall*>(data);
        call.result = call.module->task_resume(call.frame, &call.wait);
    }
};
struct CancelCall
{
    const PrsmModule* module;
    void* frame;
    PrsmTaskStatus result = PRSM_TASK_PANICKED;
    static void Run(void* data)
    {
        auto& call = *static_cast<CancelCall*>(data);
        call.result = call.module->task_cancel(call.frame);
    }
};
struct PredicateCall
{
    int (*predicate)(void*);
    void* frame;
    int result = -1;
    static void Run(void* data)
    {
        auto& call = *static_cast<PredicateCall*>(data);
        call.result = call.predicate(call.frame);
    }
};
} // namespace

struct TaskScheduler::Impl
{
    struct Task
    {
        TaskId id = 0;
        void* frame = nullptr;
        void* owner = nullptr;
        TaskId parent = 0;
        GroupId branch_of = 0;
        std::vector<TaskId> children;
        std::vector<GroupId> groups;
        CeRoot root{};
        bool rooted = false;
        bool started = false;
        bool cancel = false;
        bool branch_failed = false;
        TaskState state = TaskState::Ready;
        PrsmWait wait{};
        std::uint64_t wake_frame = 0;
        double wake_time = 0;
        std::uint64_t last_call = 0;
        std::uint64_t completed_batch = 0;
    };
    struct Group
    {
        TaskId parent;
        GroupKind kind;
        std::vector<TaskId> branches;
        bool sealed = false;
        bool ready = false;
        bool failed = false;
        std::optional<TaskId> winner;
        std::optional<TaskId> error;
    };

    TaskScheduler& facade;
    const PrsmModule& module;
    const std::uint32_t bridge_identity = NextTaskBridgeIdentity();
    const std::thread::id owner_thread = std::this_thread::get_id();
    FailedCallback on_failed;
    std::vector<std::unique_ptr<Task>> tasks;
    std::vector<std::unique_ptr<Group>> groups;
    std::unordered_set<void*> failed_owners;
    std::uint64_t frame = 0;
    double seconds = 0;
    std::size_t live = 0;
    bool polling = false;

    Impl(TaskScheduler& owner, const PrsmModule& m, FailedCallback callback)
        : facade(owner), module(m), on_failed(std::move(callback))
    {
        GcRuntime::Instance().AcquireModuleLease(module);
    }
    ~Impl()
    {
        if (owner_thread != std::this_thread::get_id())
            std::terminate();
        // Destruction is mandatory host cleanup, never an implicit user callback.
        for (auto& task : tasks)
            Release(*task);
        GcRuntime::Instance().ReleaseModuleLease(module);
    }
    void CheckThread() const
    {
        if (owner_thread != std::this_thread::get_id())
            throw std::logic_error("scheduler is restricted to its owning game thread");
    }
    Task& Get(TaskId id) const
    {
        CheckThread();
        if (id == 0 || id > tasks.size())
            throw std::out_of_range("unknown task ID");
        return *tasks[static_cast<std::size_t>(id - 1)];
    }
    Group& GetGroup(GroupId id) const
    {
        CheckThread();
        if (id == 0 || id > groups.size())
            throw std::out_of_range("unknown group ID");
        return *groups[static_cast<std::size_t>(id - 1)];
    }
    void Release(Task& task)
    {
        if (task.rooted)
        {
            ce_gc_root_destroy(task.root);
            task.rooted = false;
            task.root = {};
            task.frame = nullptr;
        }
    }
    void Finish(Task& task, TaskState state)
    {
        if (Terminal(task.state))
            return;
        task.state = state;
        task.completed_batch = frame;
        Release(task);
        --live;
    }
    bool ChildrenComplete(const Task& task) const
    {
        return std::all_of(task.children.begin(), task.children.end(),
                           [this](TaskId id) { return Terminal(Get(id).state); });
    }
    bool Joined(const Task& task) const
    {
        return ChildrenComplete(task) &&
               std::all_of(task.groups.begin(), task.groups.end(),
                           [this](GroupId id) { return GetGroup(id).ready; });
    }
    void RequestCancel(TaskId id)
    {
        std::vector<TaskId> pending{id};
        while (!pending.empty())
        {
            auto& task = Get(pending.back());
            pending.pop_back();
            if (Terminal(task.state))
                continue;
            task.cancel = true;
            // A DONE result already ran normal user teardown. It is retained as
            // Joining until children drain, and must not call task_cancel again.
            if (task.state != TaskState::Joining)
                task.state = TaskState::Cancelling;
            pending.insert(pending.end(), task.children.begin(), task.children.end());
        }
    }
    void Fail(void* owner)
    {
        CheckThread();
        if (!failed_owners.insert(owner).second)
            return;
        for (auto& task : tasks)
            if (task->owner == owner && !Terminal(task->state))
                Finish(*task, TaskState::Panicked);
        for (auto& group : groups)
            if (Get(group->parent).owner == owner)
            {
                group->failed = true;
                group->ready = true;
                group->winner.reset();
                group->error.reset();
            }
        if (on_failed)
            on_failed(owner);
    }
    bool SetWait(Task& task, const PrsmWait& wait)
    {
        task.wait = wait;
        task.wake_frame = frame + 1;
        switch (wait.kind)
        {
        case PRSM_WAIT_NEXT_FRAME:
            break;
        case PRSM_WAIT_FRAMES:
            task.wake_frame = frame + std::max<std::uint32_t>(1, wait.u.frames);
            break;
        case PRSM_WAIT_SECONDS:
            if (!std::isfinite(wait.u.seconds) || wait.u.seconds < 0 ||
                !std::isfinite(seconds + wait.u.seconds))
                return false;
            task.wake_time = seconds + wait.u.seconds;
            break;
        case PRSM_WAIT_UNTIL:
            if (!wait.u.until.predicate)
                return false;
            break;
        case PRSM_WAIT_TASKS:
            break;
        default:
            return false;
        }
        return true;
    }
    bool Eligible(Task& task)
    {
        if (Terminal(task.state) || task.cancel || task.last_call == frame)
            return false;
        if (task.branch_of && !GetGroup(task.branch_of).sealed)
            return false;
        if (task.state == TaskState::Ready)
            return true;
        if (task.state != TaskState::Suspended || frame < task.wake_frame)
            return false;
        switch (task.wait.kind)
        {
        case PRSM_WAIT_NEXT_FRAME:
        case PRSM_WAIT_FRAMES:
            return true;
        case PRSM_WAIT_SECONDS:
            return seconds >= task.wake_time;
        case PRSM_WAIT_TASKS:
            return Joined(task);
        case PRSM_WAIT_UNTIL:
        {
            PredicateCall call{task.wait.u.until.predicate, task.frame};
            const bool invoked = [&] {
                TaskBridgeScope context(facade, task.id, TaskCallbackKind::Predicate);
                return InvokeScript(PredicateCall::Run, &call, &module);
            }();
            // Another host root may retain even a completed/failed frame.
            // No GC ran inside InvokeScript, so the original pointer is valid.
            ce_gc_regray(call.frame);
            if (!invoked || call.result < 0)
            {
                Fail(task.owner);
                return false;
            }
            return call.result != 0 && !task.cancel && !Terminal(task.state);
        }
        default:
            Fail(task.owner);
            return false;
        }
    }
    void Resume(Task& task)
    {
        task.last_call = frame;
        task.started = true;
        ResumeCall call{&module, task.frame};
        const bool invoked = [&] {
            TaskBridgeScope context(facade, task.id, TaskCallbackKind::Resume);
            return InvokeScript(ResumeCall::Run, &call, &module);
        }();
        // A callback may fail its owner and clear task.frame, while an external
        // host root still retains the original, newly mutated frame.
        ce_gc_regray(call.frame);
        if (Terminal(task.state))
            return; // host callback may have explicitly failed this owner
        if (!invoked || call.result == PRSM_TASK_PANICKED ||
            (task.branch_failed && call.result != PRSM_TASK_DONE))
        {
            Fail(task.owner);
            return;
        }
        switch (call.result)
        {
        case PRSM_TASK_SUSPENDED:
            if (!SetWait(task, call.wait))
                Fail(task.owner);
            else
                task.state = task.cancel ? TaskState::Cancelling : TaskState::Suspended;
            break;
        case PRSM_TASK_DONE:
            task.state = TaskState::Joining;
            if (Joined(task))
                Finish(task, task.cancel ? TaskState::Cancelled : TaskState::Done);
            break;
        case PRSM_TASK_CANCELLED:
            // If a fixture reports cancellation itself, still clean descendants.
            RequestCancel(task.id);
            task.state = TaskState::Joining;
            if (ChildrenComplete(task))
                Finish(task, TaskState::Cancelled);
            break;
        default:
            Fail(task.owner);
            break;
        }
    }
    void ResolveGroup(Group& group)
    {
        if (!group.sealed || group.ready || group.failed)
            return;
        const bool all_started = std::all_of(group.branches.begin(), group.branches.end(),
            [this](TaskId id) { const auto& t = Get(id); return t.started || Terminal(t.state); });
        if (!all_started)
            return;
        if (group.kind == GroupKind::First && !group.winner)
        {
            std::uint64_t earliest = std::numeric_limits<std::uint64_t>::max();
            // Strict comparison preserves branch source order for ties in one
            // completion batch, regardless of callback/traversal timing.
            for (TaskId id : group.branches)
            {
                const auto& task = Get(id);
                if (task.state == TaskState::Done && task.completed_batch < earliest)
                {
                    earliest = task.completed_batch;
                    group.winner = id;
                }
            }
            if (group.winner)
                for (TaskId id : group.branches)
                    if (id != *group.winner)
                        RequestCancel(id);
        }
        if (group.kind == GroupKind::Gather && !group.error)
        {
            std::uint64_t earliest = std::numeric_limits<std::uint64_t>::max();
            for (TaskId id : group.branches)
            {
                const auto& task = Get(id);
                if (task.state == TaskState::Done && task.branch_failed &&
                    task.completed_batch < earliest)
                {
                    earliest = task.completed_batch;
                    group.error = id;
                }
            }
            if (group.error)
                for (TaskId id : group.branches)
                    if (id != *group.error)
                        RequestCancel(id);
        }
        if (group.kind == GroupKind::First && group.winner && Get(*group.winner).branch_failed)
            group.error = group.winner;
        group.ready = std::all_of(group.branches.begin(), group.branches.end(),
                                  [this](TaskId id) { return Terminal(Get(id).state); });
    }
    void Settle()
    {
        // Every child is allocated after its parent. Resolve descendant groups
        // and joins before selecting an ancestor first's winner, so arbitrary
        // nesting cannot turn a same-batch source-order tie into a later finish.
        // This is host bookkeeping only: never spin or reenter user callbacks.
        for (auto it = tasks.rbegin(); it != tasks.rend(); ++it)
        {
            auto& task = **it;
            for (GroupId group : task.groups)
                ResolveGroup(GetGroup(group));
            if (task.state == TaskState::Joining &&
                (task.cancel ? ChildrenComplete(task) : Joined(task)))
                Finish(task, task.cancel ? TaskState::Cancelled : TaskState::Done);
        }
    }
    void CancelReady() { CancelPass(std::nullopt, true); }
    // One cancellation pass. `only` restricts it to one owner's task trees.
    // A poll pass calls each task at most once per frame; a synchronous owner
    // drain instead repeats passes without advancing the game frame, so other
    // owners' waits and frame counts are untouched. Returns whether any user
    // cancel callback ran or any task completed during this pass.
    bool CancelPass(std::optional<void*> only, bool once_per_frame)
    {
        bool progressed = false;
        cancel_suspended = false;
        // Explicit source-ordered postorder, so a child's cleanup must finish
        // before the parent's user cancel/defer function can be entered.
        std::vector<std::pair<TaskId, bool>> stack;
        for (auto it = tasks.rbegin(); it != tasks.rend(); ++it)
            if ((*it)->parent == 0 && (!only || (*it)->owner == *only))
                stack.emplace_back((*it)->id, false);
        while (!stack.empty())
        {
            const auto [id, visited] = stack.back();
            stack.pop_back();
            auto& task = Get(id);
            if (!visited)
            {
                stack.emplace_back(id, true);
                for (auto it = task.children.rbegin(); it != task.children.rend(); ++it)
                    stack.emplace_back(*it, false);
                continue;
            }
            if (!task.cancel || Terminal(task.state) || !ChildrenComplete(task))
                continue;
            if (task.state == TaskState::Joining)
            {
                Finish(task, TaskState::Cancelled);
                progressed = true;
                continue;
            }
            if (once_per_frame && task.last_call == frame)
                continue;
            task.last_call = frame;
            progressed = true;
            CancelCall call{&module, task.frame};
            const bool invoked = [&] {
                TaskBridgeScope context(facade, task.id, TaskCallbackKind::Cancel);
                return InvokeScript(CancelCall::Run, &call, &module, true);
            }();
            ce_gc_regray(call.frame);
            if (Terminal(task.state))
                continue;
            if (!invoked || call.result == PRSM_TASK_PANICKED)
                Fail(task.owner);
            else if (call.result == PRSM_TASK_SUSPENDED)
            {
                cancel_suspended = true;
                continue; // already regrayed; retry on a later Poll, never spin
            }
            else if (call.result == PRSM_TASK_CANCELLED || call.result == PRSM_TASK_DONE)
                Finish(task, TaskState::Cancelled);
            else
                Fail(task.owner);
        }
        return progressed;
    }
    bool cancel_suspended = false; // last CancelPass saw a cooperative SUSPENDED
    bool OwnerLive(void* owner) const
    {
        for (const auto& task : tasks)
            if (task->owner == owner && !Terminal(task->state))
                return true;
        return false;
    }
};

TaskScheduler::TaskScheduler(const PrsmModule& module, FailedCallback on_failed)
    : impl_(std::make_unique<Impl>(*this, module, std::move(on_failed)))
{}
TaskScheduler::~TaskScheduler() = default;

TaskScheduler::TaskId TaskScheduler::Start(void* frame, void* owner, TaskId parent)
{ return StartImpl(frame, owner, parent, false); }
TaskScheduler::TaskId TaskScheduler::StartPrepared(void* frame, void* owner)
{ return StartImpl(frame, owner, 0, true); }
TaskScheduler::TaskId TaskScheduler::StartImpl(void* frame, void* owner, TaskId parent, bool prepared)
{
    auto& impl = *impl_;
    impl.CheckThread();
    if (!prepared && !GcRuntime::Instance().ModuleDispatchEnabled(impl.module))
        throw std::invalid_argument("task generation dispatch is blocked");
    if (!impl.module.task_resume || !impl.module.task_cancel)
        throw std::invalid_argument("scheduler requires task resume and cancel functions");
    if (!frame)
        throw std::invalid_argument("task frame is null");
    if (impl.tasks.size() >= MaxTasks)
        throw std::length_error("harness task limit reached");
    if (parent)
    {
        auto& p = impl.Get(parent);
        if (Terminal(p.state) || p.cancel || p.state == TaskState::Joining)
            throw std::invalid_argument("parent task is closing");
        if (owner && owner != p.owner)
            throw std::invalid_argument("child must share parent owner");
        owner = p.owner;
    }
    if (impl.failed_owners.contains(owner))
        throw std::invalid_argument("task owner has failed");
    auto task = std::make_unique<Impl::Task>();
    task->id = impl.tasks.size() + 1;
    task->frame = frame;
    task->owner = owner;
    task->parent = parent;
    // Allocate container space before acquiring an external root.
    if (impl.tasks.size() == impl.tasks.capacity())
        impl.tasks.reserve(std::min(MaxTasks, std::max<std::size_t>(16, impl.tasks.size() * 2)));
    if (parent)
        impl.Get(parent).children.reserve(impl.Get(parent).children.size() + 1);
    task->root = ce_gc_root_create(frame);
    if (!task->root.opaque)
        throw std::runtime_error("could not root task frame");
    task->rooted = true;
    const auto id = task->id;
    impl.tasks.push_back(std::move(task));
    if (parent)
        impl.Get(parent).children.push_back(id);
    ++impl.live;
    return id;
}
TaskScheduler::GroupId TaskScheduler::CreateGroup(TaskId parent, GroupKind kind)
{
    auto& impl = *impl_;
    impl.CheckThread();
    if (!GcRuntime::Instance().ModuleDispatchEnabled(impl.module))
        throw std::invalid_argument("group generation dispatch is blocked");
    auto& p = impl.Get(parent);
    if (Terminal(p.state) || p.cancel || p.state == TaskState::Joining)
        throw std::invalid_argument("parent task is closing");
    if (impl.groups.size() >= MaxGroups)
        throw std::length_error("harness group limit reached");
    if (kind != GroupKind::First && kind != GroupKind::Gather)
        throw std::invalid_argument("unknown group kind");
    auto group = std::make_unique<Impl::Group>();
    group->parent = parent;
    group->kind = kind;
    const auto id = impl.groups.size() + 1;
    p.groups.reserve(p.groups.size() + 1);
    impl.groups.push_back(std::move(group));
    p.groups.push_back(id);
    return id;
}
TaskScheduler::TaskId TaskScheduler::AddBranch(GroupId group_id, void* frame)
{
    auto& group = impl_->GetGroup(group_id);
    if (group.sealed || group.failed)
        throw std::invalid_argument("group is already sealed or failed");
    group.branches.reserve(group.branches.size() + 1);
    const auto id = Start(frame, nullptr, group.parent);
    impl_->Get(id).branch_of = group_id;
    group.branches.push_back(id);
    return id;
}
void TaskScheduler::SealGroup(GroupId group_id)
{
    auto& group = impl_->GetGroup(group_id);
    if (group.sealed)
        throw std::invalid_argument("group is already sealed");
    if (group.kind == GroupKind::First && group.branches.empty())
        throw std::invalid_argument("first requires at least one branch");
    group.sealed = true;
    if (group.branches.empty())
        group.ready = true;
}
void TaskScheduler::Poll(double dt)
{
    auto& impl = *impl_;
    impl.CheckThread();
    if (impl.polling)
        throw std::logic_error("scheduler Poll is not reentrant");
    if (!std::isfinite(dt) || dt < 0 || !std::isfinite(impl.seconds + dt))
        throw std::invalid_argument("poll delta must be finite and nonnegative");
    struct PollGuard { bool& value; ~PollGuard() { value = false; } } guard{impl.polling};
    impl.polling = true;
    ++impl.frame;
    impl.seconds += dt;
    impl.Settle();
    // Stable Task allocations allow a resume to append children safely.
    for (std::size_t i = 0; i < impl.tasks.size(); ++i)
    {
        auto& task = *impl.tasks[i];
        if (impl.Eligible(task))
            impl.Resume(task);
    }
    impl.Settle();
    impl.CancelReady();
    impl.Settle();
}
void TaskScheduler::Cancel(TaskId task) { impl_->RequestCancel(task); }
void TaskScheduler::CancelOwner(void* owner)
{
    impl_->CheckThread();
    for (const auto& task : impl_->tasks)
        if (task->owner == owner && task->parent == 0)
            impl_->RequestCancel(task->id);
}
bool TaskScheduler::DrainOwner(void* owner)
{
    auto& impl = *impl_;
    impl.CheckThread();
    if (impl.polling)
        throw std::logic_error("scheduler DrainOwner cannot run inside Poll");
    CancelOwner(owner);
    // Each pass either runs a cancel callback or finishes a joined parent, and
    // a task can be cancelled at most once, so the bound is the task count.
    for (std::size_t pass = 0; pass <= impl.tasks.size(); ++pass)
    {
        impl.Settle();
        if (!impl.OwnerLive(owner))
            return true;
        // A suspended cancel waits for a later game frame; never re-enter it now.
        if (!impl.CancelPass(owner, false) || impl.cancel_suspended)
            break;
    }
    impl.Settle();
    return !impl.OwnerLive(owner);
}
void TaskScheduler::CancelAll()
{
    impl_->CheckThread();
    for (const auto& task : impl_->tasks)
        if (task->parent == 0)
            impl_->RequestCancel(task->id);
}
void TaskScheduler::FailOwner(void* owner) { impl_->Fail(owner); }
TaskScheduler::TaskState TaskScheduler::State(TaskId task) const { return impl_->Get(task).state; }
void* TaskScheduler::Owner(TaskId task) const { return impl_->Get(task).owner; }
bool TaskScheduler::IsComplete(TaskId task) const { return Terminal(State(task)); }
bool TaskScheduler::IsOwnerFailed(void* owner) const
{
    impl_->CheckThread();
    return impl_->failed_owners.contains(owner);
}
bool TaskScheduler::GroupReady(GroupId group) const { return impl_->GetGroup(group).ready; }
std::optional<TaskScheduler::TaskId> TaskScheduler::GroupWinner(GroupId group) const
{
    return impl_->GetGroup(group).winner;
}
void TaskScheduler::MarkBranchFailure(TaskId id)
{
    auto& task = impl_->Get(id);
    if (!task.branch_of || Terminal(task.state) || task.cancel || task.state == TaskState::Joining)
        throw std::logic_error("recoverable failure requires a live group branch");
    task.branch_failed = true;
}

std::optional<TaskScheduler::TaskId> TaskScheduler::GroupError(GroupId group) const
{
    return impl_->GetGroup(group).error;
}

std::size_t TaskScheduler::LiveTaskCount() const { impl_->CheckThread(); return impl_->live; }
std::uint64_t TaskScheduler::FrameNumber() const { impl_->CheckThread(); return impl_->frame; }
double TaskScheduler::GameSeconds() const { impl_->CheckThread(); return impl_->seconds; }
std::uint32_t TaskScheduler::BridgeIdentity() const
{
    impl_->CheckThread();
    return impl_->bridge_identity;
}

bool TaskScheduler::IsDirectChild(TaskId child, TaskId parent) const
{
    return impl_->Get(child).parent == parent;
}

bool TaskScheduler::GroupBelongsTo(GroupId group, TaskId parent) const
{
    return impl_->GetGroup(group).parent == parent;
}
} // namespace prsm::host
