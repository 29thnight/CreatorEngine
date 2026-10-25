#include "../../Engine/RenderEngine/RHI/RHICompletionRetireQueue.h"
#include <cstdlib>
#include <iostream>
#include <memory>
#include <new>
#include <stdexcept>

namespace
{
thread_local bool counting = false;
thread_local size_t allocations = 0;
}
void* operator new(size_t size)
{
    if (counting) ++allocations;
    if (void* p = std::malloc(size ? size : 1)) return p;
    throw std::bad_alloc();
}
void* operator new[](size_t size) { return ::operator new(size); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, size_t) noexcept { std::free(p); }
void operator delete[](void* p, size_t) noexcept { std::free(p); }

namespace
{
void Check(bool ok, const char* message)
{
    if (!ok) throw std::runtime_error(message);
}
struct Events
{
    std::array<int, 50000> values{};
    size_t count = 0;
    void Add(int value) { values.at(count++) = value; }
};
struct Resource
{
    Events* events;
    int id;
    ~Resource() { events->Add(-id); }
};
struct Payload
{
    static inline size_t assignments = 0;
    static inline size_t constructions = 0;
    std::unique_ptr<Resource> resource;
    Payload(Events& events, int id) : resource(new Resource{&events, id}) {}
    Payload(Payload&& other) noexcept : resource(std::move(other.resource)) { ++constructions; }
    Payload& operator=(Payload&& other) noexcept
    {
        if (this == &other) std::abort();
        ++assignments;
        resource = std::move(other.resource);
        return *this;
    }
    Payload(const Payload&) = delete;
    Payload& operator=(const Payload&) = delete;
};
using Queue = RHICompletionRetireQueue<Payload>;

void MixedOrder()
{
    Events events;
    Queue queue;
    const std::array<uint64_t, 6> completions{9, 2, 0, 1, 8, 3};
    for (size_t i = 0; i < completions.size(); ++i)
        queue.Enqueue({completions[i]}, Payload(events, int(i + 1)), (i + 1) * 10);
    auto release = [&](Payload& payload) { events.Add(payload.resource->id); };
    const auto none = queue.Collect({0}, release);
    Check(none.count == 0 && events.count == 0 && queue.GetPendingBytes() == 210,
        "incomplete collection changed ownership");
    const auto first = queue.Collect({3}, release);
    const std::array<int, 6> expected{2, -2, 4, -4, 6, -6};
    Check(first.count == 3 && first.bytes == 120 && events.count == expected.size() &&
        std::equal(expected.begin(), expected.end(), events.values.begin()),
        "callback/RAII release order changed");
    Check(queue.GetPendingCount() == 3 && queue.GetPendingBytes() == 90 &&
        queue.GetQuarantinedCount() == 1, "mixed accounting incorrect");
    Check(queue.Collect({3}, release).count == 0, "collected resource released twice");
    const auto maximum = queue.Collect({~uint64_t{0}}, release);
    Check(maximum.count == 2 && maximum.bytes == 60 && queue.GetPendingCount() == 1 &&
        queue.GetPendingBytes() == 30 && queue.GetQuarantinedCount() == 1,
        "quarantine collected at maximum completion");
    const auto drained = queue.Drain(release);
    Check(drained.count == 1 && drained.bytes == 30 && queue.Empty() &&
        queue.GetPendingBytes() == 0 && queue.GetQuarantinedCount() == 0 &&
        events.values[events.count - 2] == 3 && events.values[events.count - 1] == -3,
        "quarantine drain incorrect");
    Check(queue.Drain(release).count == 0 && queue.Collect({10}, release).count == 0,
        "empty/repeated drain incorrect");
}

void SurvivorAndTransfer()
{
    Events events;
    Queue queue;
    for (int id = 1; id <= 5; ++id)
        queue.Enqueue({id % 2 ? 10u : 1u}, Payload(events, id), 1);
    std::unique_ptr<Resource> pinned;
    queue.Collect({1}, [&](Payload& p) {
        events.Add(p.resource->id);
        if (p.resource->id == 2) pinned = std::move(p.resource);
    });
    Check(pinned && pinned->id == 2 && events.count == 3 &&
        events.values[0] == 2 && events.values[1] == 4 && events.values[2] == -4,
        "transferred ownership destroyed by compaction");
    std::array<int, 3> survivors{};
    size_t count = 0;
    queue.Drain([&](Payload& p) { survivors.at(count++) = p.resource->id; });
    Check(survivors == std::array<int, 3>{1, 3, 5}, "survivor/drain order changed");
    pinned.reset();
    Check(events.values[events.count - 1] == -2, "transferred owner lost");
}

void ThrowingCallback()
{
    Events events;
    Queue queue;
    for (int id = 1; id <= 5; ++id)
        queue.Enqueue({id == 2 ? 9u : 1u}, Payload(events, id), uint64_t(id));
    bool caught = false;
    try {
        queue.Collect({1}, [&](Payload& p) {
            if (p.resource->id == 4) throw std::runtime_error("expected");
        });
    } catch (const std::runtime_error&) { caught = true; }
    Check(caught && queue.GetPendingCount() == 3 && queue.GetPendingBytes() == 11,
        "throwing callback lost queued entries/accounting");
    std::array<int, 3> ids{};
    size_t count = 0;
    queue.Drain([&](Payload& p) { ids.at(count++) = p.resource->id; });
    Check(ids == std::array<int, 3>{2, 4, 5}, "exception recovery changed remaining order");
}

void QuarantineAndCapacityReuse()
{
    Events events;
    Queue queue;
    for (int id = 1; id <= 4; ++id) queue.Enqueue({0}, Payload(events, id), 2);
    Payload::assignments = 0;
    allocations = 0;
    counting = true;
    const auto held = queue.Collect({~uint64_t{0}}, [](Payload&) { std::abort(); });
    counting = false;
    Check(held.count == 0 && events.count == 0 && Payload::assignments == 0 &&
        allocations == 0 && queue.GetQuarantinedCount() == 4 &&
        queue.GetPendingBytes() == 8, "quarantine-only collection changed state");
    queue.Drain([&](Payload& p) { events.Add(p.resource->id); });
    const std::array<int, 8> expected{1, 2, 3, 4, -1, -2, -3, -4};
    Check(events.count == expected.size() &&
        std::equal(expected.begin(), expected.end(), events.values.begin()),
        "Drain callback/destruction order changed");
    Payload next(events, 5);
    counting = true;
    queue.Enqueue({1}, std::move(next), 9);
    const auto all = queue.Collect({1}, [&](Payload& p) { events.Add(p.resource->id); });
    counting = false;
    Check(all.count == 1 && all.bytes == 9 && queue.Empty() &&
        queue.GetPendingBytes() == 0 && queue.GetQuarantinedCount() == 0 &&
        allocations == 0 && events.values[8] == 5 && events.values[9] == -5,
        "all-complete collection or retained capacity reuse failed");
}

void LinearMovesAndAllocations()
{
    Events events;
    Queue queue;
    constexpr size_t n = 10000;
    for (size_t i = 0; i < n; ++i)
        queue.Enqueue({i % 2 ? 10u : 1u}, Payload(events, int(i + 1)), 1);
    Payload::assignments = Payload::constructions = 0;
    allocations = 0;
    counting = true;
    const auto result = queue.Collect({1}, [](Payload&) {});
    counting = false;
    Check(result.count == n / 2 && queue.GetPendingCount() == n / 2 &&
        queue.GetPendingBytes() == n / 2 && Payload::assignments <= n / 2 &&
        Payload::constructions <= n / 2 && allocations == 0,
        "collection exceeded linear move bound or allocated");
    const size_t firstMoves = Payload::assignments;
    Payload::assignments = 0;
    counting = true;
    queue.Collect({1}, [](Payload&) {});
    counting = false;
    Check(Payload::assignments == 0 && allocations == 0, "no-op collection moved or allocated");
    counting = true;
    queue.Drain([](Payload&) {});
    counting = false;
    Check(allocations == 0 && events.count == n, "drain allocated or lost ownership");
    std::cout << " entries=" << n << " survivor_moves=" << firstMoves
        << " allocations=" << allocations;
}
}
int main()
{
    try {
        MixedOrder();
        SurvivorAndTransfer();
        ThrowingCallback();
        QuarantineAndCapacityReuse();
        LinearMovesAndAllocations();
        std::cout << " RHI_RETIRE_COMPACTION_OK\n";
        return 0;
    } catch (const std::exception& e) {
        counting = false;
        std::cerr << e.what() << '\n';
        return 1;
    }
}
