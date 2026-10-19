#include "task_bridge.h"
#include "task_scheduler.h"
#include "gc_runtime.h"
#include "prsm_task_bridge.h"

#include <atomic>
#include <limits>
#include <stdexcept>

namespace prsm::host {
namespace {
thread_local TaskBridgeScope* current = nullptr;
std::atomic<std::uint64_t> next_identity{1};
}

std::uint32_t NextTaskBridgeIdentity()
{
    const auto id = next_identity.fetch_add(1);
    if (id > std::numeric_limits<std::uint32_t>::max())
        throw std::overflow_error("mock scheduler identity space exhausted");
    return static_cast<std::uint32_t>(id);
}

TaskBridgeScope::TaskBridgeScope(TaskScheduler& value, std::uint64_t id, TaskCallbackKind callback)
    : scheduler(value), task(id), kind(callback), previous_(current)
{
    (void)scheduler.State(task); // Includes the owning-game-thread check.
    current = this;
}

TaskBridgeScope::~TaskBridgeScope() { current = previous_; }
TaskBridgeScope* TaskBridgeScope::Current() { return current; }
}

namespace {
using prsm::host::TaskBridgeScope;
using prsm::host::TaskCallbackKind;
using Scheduler = prsm::host::TaskScheduler;
constexpr std::uint64_t group_bit = std::uint64_t{1} << 31;
constexpr std::uint64_t local_mask = group_bit - 1;

TaskBridgeScope& Context(bool mutation = false)
{
    auto* context = TaskBridgeScope::Current();
    if (!context || (mutation && context->kind != TaskCallbackKind::Resume))
        throw std::logic_error("mock task bridge requires a matching callback context");
    (void)context->scheduler.State(context->task);
    return *context;
}

std::uint64_t Encode(TaskBridgeScope& c, std::uint64_t id, bool group = false)
{
    return (std::uint64_t{c.scheduler.BridgeIdentity()} << 32) | (group ? group_bit : 0) | id;
}

std::uint64_t Decode(TaskBridgeScope& c, std::uint64_t handle, bool group)
{
    if ((handle >> 32) != c.scheduler.BridgeIdentity() ||
        ((handle & group_bit) != 0) != group || !(handle & local_mask))
        throw std::invalid_argument("foreign or wrong-kind mock task handle");
    return handle & local_mask;
}

std::uint64_t Group(TaskBridgeScope& c, std::uint64_t handle)
{
    auto id = Decode(c, handle, true);
    if (!c.scheduler.GroupBelongsTo(id, c.task))
        throw std::invalid_argument("mock group belongs to another task");
    return id;
}

void RequireFrame(void* frame)
{
    if (!frame || !prsm::host::GcRuntime::Instance().Contains(frame))
        throw std::invalid_argument("mock child frame is not a live GC object");
}

template<class F> PrsmCallStatus Checked(F&& body) noexcept
{
    try { body(); return PRSM_CALL_OK; }
    catch (...) { return PRSM_CALL_FAILED; }
}
}

extern "C" {
CE_API PrsmCallStatus prsm_host_current_task(PrsmHostTask* out)
{
    if (!out) return PRSM_CALL_FAILED;
    *out = 0;
    return Checked([&] { auto& c = Context(); *out = Encode(c, c.task); });
}

CE_API PrsmCallStatus prsm_host_wait_frames(uint32_t frames, PrsmWait* out)
{
    if (!out) return PRSM_CALL_FAILED;
    *out = {};
    return Checked([&] {
        (void)Context(true);
        out->kind = PRSM_WAIT_FRAMES;
        out->u.frames = frames;
    });
}

CE_API PrsmCallStatus prsm_host_task_start(void* frame, PrsmHostTask* out)
{
    if (!out) return PRSM_CALL_FAILED;
    *out = 0;
    return Checked([&] {
        auto& c = Context(true);
        RequireFrame(frame);
        *out = Encode(c, c.scheduler.Start(frame, nullptr, c.task));
    });
}

CE_API PrsmCallStatus prsm_host_task_status(PrsmHostTask handle, PrsmTaskStatus* out)
{
    if (!out) return PRSM_CALL_FAILED;
    *out = PRSM_TASK_PANICKED;
    return Checked([&] {
        auto& c = Context();
        auto id = Decode(c, handle, false);
        if (!c.scheduler.IsDirectChild(id, c.task))
            throw std::invalid_argument("mock task is not a direct child");
        switch (c.scheduler.State(id)) {
        case Scheduler::TaskState::Done: *out = PRSM_TASK_DONE; break;
        case Scheduler::TaskState::Cancelled: *out = PRSM_TASK_CANCELLED; break;
        case Scheduler::TaskState::Panicked: *out = PRSM_TASK_PANICKED; break;
        default: *out = PRSM_TASK_SUSPENDED; break;
        }
    });
}

CE_API PrsmCallStatus prsm_host_group_create(uint32_t kind, PrsmHostGroup* out)
{
    if (!out) return PRSM_CALL_FAILED;
    *out = 0;
    return Checked([&] {
        auto& c = Context(true);
        if (kind > PRSM_HOST_FIRST) throw std::invalid_argument("invalid mock group kind");
        *out = Encode(c, c.scheduler.CreateGroup(c.task,
            kind == PRSM_HOST_FIRST ? Scheduler::GroupKind::First : Scheduler::GroupKind::Gather), true);
    });
}

CE_API PrsmCallStatus prsm_host_group_add(PrsmHostGroup group, void* frame, PrsmHostTask* out)
{
    if (!out) return PRSM_CALL_FAILED;
    *out = 0;
    return Checked([&] {
        auto& c = Context(true);
        const auto id = Group(c, group);
        RequireFrame(frame);
        *out = Encode(c, c.scheduler.AddBranch(id, frame));
    });
}

CE_API PrsmCallStatus prsm_host_group_seal(PrsmHostGroup group)
{
    return Checked([&] { auto& c = Context(true); c.scheduler.SealGroup(Group(c, group)); });
}

CE_API PrsmCallStatus prsm_host_group_ready(PrsmHostGroup group, int* out)
{
    if (!out) return PRSM_CALL_FAILED;
    *out = 0;
    return Checked([&] { auto& c = Context(); *out = c.scheduler.GroupReady(Group(c, group)) ? 1 : 0; });
}

CE_API PrsmCallStatus prsm_host_group_winner(PrsmHostGroup group, PrsmHostTask* out)
{
    if (!out) return PRSM_CALL_FAILED;
    *out = 0;
    return Checked([&] {
        auto& c = Context();
        const auto id = Group(c, group);
        if (!c.scheduler.GroupReady(id)) throw std::logic_error("mock group is not ready");
        const auto winner = c.scheduler.GroupWinner(id);
        if (!winner) throw std::logic_error("mock group has no first winner");
        *out = Encode(c, *winner);
    });
}
CE_API PrsmCallStatus prsm_host_branch_fail(void)
{
    return Checked([&] { auto& c = Context(true); c.scheduler.MarkBranchFailure(c.task); });
}

CE_API PrsmCallStatus prsm_host_group_error(PrsmHostGroup group, PrsmHostTask* out)
{
    if (!out) return PRSM_CALL_FAILED;
    *out = 0;
    return Checked([&] {
        auto& c = Context();
        const auto id = Group(c, group);
        if (!c.scheduler.GroupReady(id)) throw std::logic_error("mock group is not ready");
        const auto error = c.scheduler.GroupError(id);
        if (error) *out = Encode(c, *error);
    });
}

}
