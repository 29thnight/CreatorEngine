#include "handle_table.h"
#include <limits>
#include <stdexcept>
#include "gc_runtime.h"

namespace prsm::host
{
namespace
{
constexpr std::uint32_t SlotOf(CeHandle h) { return static_cast<std::uint32_t>(h.bits); }
constexpr std::uint32_t GenerationOf(CeHandle h) { return static_cast<std::uint32_t>(h.bits >> 32); }
constexpr CeHandle Make(std::uint32_t slot, std::uint32_t generation)
{
    return CeHandle{(static_cast<std::uint64_t>(generation) << 32) | slot};
}
} // namespace

HandleTable& HandleTable::Instance()
{
    static HandleTable table;
    return table;
}

CeHandle HandleTable::Create(void* instance)
{
    (void)GcRuntime::Instance().ActiveDepth(); // Enforce the owning game thread.
    if (!instance) throw std::invalid_argument("null handle instance");
    std::uint32_t slot;
    if (!free_.empty())
    {
        slot = free_.back();
        free_.pop_back();
    }
    else
    {
        if (slots_.size() >= std::numeric_limits<std::uint32_t>::max()) throw std::overflow_error("handle slots exhausted");
        slot = static_cast<std::uint32_t>(slots_.size());
        slots_.push_back({});
    }
    Slot& s = slots_[slot];
    s.instance = instance;
    s.used = true;
    return Make(slot, s.generation);
}

void HandleTable::Destroy(CeHandle handle)
{
    (void)GcRuntime::Instance().ActiveDepth(); // Enforce the owning game thread.
    if (!IsAlive(handle))
        return;
    Slot& s = slots_[SlotOf(handle)];
    s.instance = nullptr;
    s.used = false;
    // Retire a slot on generation exhaustion; never revive an ancient handle.
    if (s.generation != std::numeric_limits<std::uint32_t>::max()) {
        ++s.generation;
        free_.push_back(SlotOf(handle));
    }
}

bool HandleTable::IsAlive(CeHandle handle) const
{
    (void)GcRuntime::Instance().ActiveDepth(); // Enforce the owning game thread.
    const std::uint32_t slot = SlotOf(handle);
    return slot != 0 && slot < slots_.size() && slots_[slot].used &&
           slots_[slot].generation == GenerationOf(handle);
}

void* HandleTable::Resolve(CeHandle handle) const
{
    return IsAlive(handle) ? slots_[SlotOf(handle)].instance : nullptr;
}
CeHandle HandleTable::HandleFor(const void* instance) const
{
    (void)GcRuntime::Instance().ActiveDepth(); // Enforce the owning game thread.
    if (!instance) return {};
    for (std::uint32_t n = 1; n < slots_.size(); ++n)
        if (slots_[n].used && slots_[n].instance == instance) return Make(n, slots_[n].generation);
    return {};
}
} // namespace prsm::host
