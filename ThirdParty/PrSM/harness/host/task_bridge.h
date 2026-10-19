#pragma once

#include <cstdint>

namespace prsm::host {
class TaskScheduler;

enum class TaskCallbackKind { Resume, Cancel, Predicate };

// Internal, stack-bound dynamic context. Nested scheduler callbacks restore the
// previous context, including on host exceptions. Never store a frame pointer.
class TaskBridgeScope {
public:
    TaskBridgeScope(TaskScheduler& scheduler, std::uint64_t task, TaskCallbackKind kind);
    ~TaskBridgeScope();
    TaskBridgeScope(const TaskBridgeScope&) = delete;
    TaskBridgeScope& operator=(const TaskBridgeScope&) = delete;

    static TaskBridgeScope* Current();
    TaskScheduler& scheduler;
    const std::uint64_t task;
    const TaskCallbackKind kind;

private:
    TaskBridgeScope* previous_;
};

std::uint32_t NextTaskBridgeIdentity();
}
