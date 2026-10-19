#include "gc_runtime.h"

#include <gc/refs.hpp>
#include <gc/tracer.hpp>

#include <algorithm>
#include <bit>
#include <cstdint>
#include <cstring>
#include <limits>
#include <new>
#include <stdexcept>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

namespace prsm::host
{
namespace
{
[[noreturn]] void Invalid(const char* message) { throw std::logic_error(message); }

struct FlagScope
{
    bool& flag;
    explicit FlagScope(bool& value) : flag(value) { flag = true; }
    ~FlagScope() { flag = false; }
};

std::size_t AlignedSize(std::size_t size, std::size_t align)
{
    if (size > std::numeric_limits<std::size_t>::max() - (align - 1))
        throw std::bad_alloc();
    return (size + align - 1) & ~(align - 1);
}
} // namespace

struct GcRuntime::Impl
{
    struct Allocation;
    struct Prefix { Allocation* allocation; };
    struct Allocation
    {
        Impl* runtime = nullptr;
        const PrsmModule* module = nullptr;
        const PrsmModule* globals = nullptr;
        const PrsmTypeDesc* original_descriptor = nullptr;
        void* payload = nullptr;
        std::size_t payload_size = 0;
        gc::detail::object_header* header = nullptr;
        gc::detail::type_descriptor type{};
        gc::trace_ref<std::byte> edge;
        std::string name;
        std::vector<std::uint64_t> bitmap;
        PrsmTraceFn trace = nullptr;
        std::uint32_t scanned_epoch = 0;
    };
    struct TraceContext
    {
        Impl* runtime;
        gc::tracer* tracer;
    };
    struct ModuleState { CeRoot globals{}; bool registered = false; bool dispatch_enabled = true; std::size_t leases = 0; };

    std::thread::id owner = std::this_thread::get_id();
    std::size_t active_depth = 0;
    const PrsmModule* active_module = nullptr;
    bool collecting = false;
    bool tracing = false;
    TraceContext* trace_context = nullptr;
    std::size_t allocation_limit = 64u * 1024u * 1024u;
    std::uintptr_t next_root = 1;
    std::unordered_map<const void*, std::unique_ptr<Allocation>> objects;
    std::unordered_map<std::uintptr_t, gc::root_ref<std::byte>> roots;
    std::unordered_map<const PrsmModule*, ModuleState> modules;
    std::unordered_map<const PrsmTypeDesc*, const PrsmModule*> descriptor_owners;
    // Must be destroyed before descriptor storage and root bookkeeping.
    gc::domain domain;

    ~Impl()
    {
        if (owner != std::this_thread::get_id() || active_depth || collecting)
            std::terminate();
        roots.clear();
        // Module code must remain loaded until its successful DrainModule.
        // Finishes pending marks as well as a fresh, rootless cycle.
        domain.collect_full();
    }

    void CheckThread() const
    {
        if (owner != std::this_thread::get_id())
            Invalid("PrSM GC operation is restricted to the game thread");
    }
    void CheckMutation() const
    {
        CheckThread();
        if (tracing || collecting)
            Invalid("PrSM trace callbacks may only report references");
    }
    Allocation* Find(const void* object) const
    {
        const auto it = objects.find(object);
        if (it == objects.end())
            Invalid("PrSM reference is not a live object in this GC domain");
        return it->second.get();
    }
    static void DestroyThunk(void* storage) noexcept
    {
        auto* record = static_cast<Prefix*>(storage)->allocation;
        record->runtime->objects.erase(record->payload);
    }
    static void TraceThunk(const void* storage, gc::tracer& tracer)
    {
        auto& record = *static_cast<const Prefix*>(storage)->allocation;
        auto& runtime = *record.runtime;
        record.scanned_epoch = record.header->mark_epoch;
        TraceContext context{&runtime, &tracer};
        struct ContextScope
        {
            Impl& runtime;
            TraceContext* previous;
            bool previous_tracing;
            ContextScope(Impl& r, TraceContext* c)
                : runtime(r), previous(r.trace_context), previous_tracing(r.tracing)
            { r.trace_context = c; r.tracing = true; }
            ~ContextScope() { runtime.trace_context = previous; runtime.tracing = previous_tracing; }
        } scope(runtime, &context);

        if (record.globals)
        {
            if (record.globals->trace_globals)
                record.globals->trace_globals(&context);
            return;
        }
        for (std::size_t word = 0; word < record.bitmap.size(); ++word)
        {
            auto bits = record.bitmap[word];
            while (bits)
            {
                const auto bit = static_cast<std::size_t>(std::countr_zero(bits));
                void* child = nullptr;
                std::memcpy(&child, static_cast<const std::byte*>(record.payload) +
                                      (word * 64 + bit) * sizeof(void*), sizeof(child));
                if (child)
                    tracer.visit(runtime.Find(child)->edge);
                bits &= bits - 1;
            }
        }
        if (record.trace)
            record.trace(record.payload, &context);
    }

    void* Allocate(const PrsmTypeDesc* desc, std::size_t byte_size,
                   const PrsmModule* globals = nullptr)
    {
        CheckMutation();
        std::size_t align = alignof(std::max_align_t);
        std::size_t size = byte_size;
        if (desc)
        {
            size = desc->size;
            align = desc->align;
            if (!align || !std::has_single_bit(align))
                Invalid("PrSM type alignment must be a nonzero power of two");
            if (!size)
                Invalid("PrSM type size must be nonzero");
            if (desc->name.len && !desc->name.ptr)
                Invalid("PrSM type name has a null buffer");
            const std::size_t slots = size / sizeof(void*);
            const std::size_t max_words = slots / 64 + (slots % 64 != 0);
            if (desc->ref_bitmap_words > max_words ||
                (desc->ref_bitmap_words && !desc->ref_bitmap))
                Invalid("PrSM GC bitmap exceeds the type payload");
            if (desc->ref_bitmap_words && desc->ref_bitmap_words == max_words && slots % 64)
            {
                if ((desc->ref_bitmap[max_words - 1] >> (slots % 64)) != 0)
                    Invalid("PrSM GC bitmap references a partial or nonexistent slot");
            }
        }
        try
        {
            align = std::max(align, alignof(Prefix));
            const std::size_t offset = AlignedSize(sizeof(Prefix), align);
            if (size > std::numeric_limits<std::size_t>::max() - offset)
                return nullptr;
            const std::size_t storage_size = offset + std::max<std::size_t>(size, 1);
            const std::size_t header_size = AlignedSize(sizeof(gc::detail::object_header), align);
            if (storage_size > std::numeric_limits<std::size_t>::max() - header_size)
                return nullptr;
            const std::size_t block_size = header_size + storage_size;
            const auto live = domain.stats().live_bytes;
            if (allocation_limit && (block_size > allocation_limit || live > allocation_limit - block_size))
            {
                domain.request_collection();
                return nullptr;
            }

            auto record = std::make_unique<Allocation>();
            record->runtime = this;
            record->original_descriptor = desc;
            record->module = active_module;
            if (globals)
                record->module = globals;
            else if (desc)
            {
                const auto it = descriptor_owners.find(desc);
                if (it != descriptor_owners.end())
                    record->module = it->second;
            }
            record->globals = globals;
            record->payload_size = size;
            record->name = desc && desc->name.len ? std::string(desc->name.ptr, desc->name.len) :
                           globals ? "PrSM module globals" : "PrSM byte buffer";
            if (desc)
            {
                if (desc->ref_bitmap_words)
                    record->bitmap.assign(desc->ref_bitmap, desc->ref_bitmap + desc->ref_bitmap_words);
                record->trace = desc->trace;
            }
            record->type = {record->name.c_str(), storage_size, align, &TraceThunk, &DestroyThunk};
            // Intentional, version-pinned GCCE bridge: raw PrSM payloads are
            // C ABI storage, not gc::managed C++ classes. Never bypass GCCE's
            // allocation, publication, root registry or insertion barrier.
            auto pending = gc::detail::domain_access::begin_allocation(domain, record->type);
            Allocation* published = record.get();
            try
            {
                record->header = pending.header;
                record->payload = static_cast<std::byte*>(pending.header->object) + offset;
                std::memset(pending.header->object, 0, storage_size);
                ::new (pending.header->object) Prefix{published};
                objects.emplace(record->payload, std::move(record));
            }
            catch (...)
            {
                gc::detail::domain_access::abort_allocation(domain, pending);
                throw;
            }
            auto* header = gc::detail::domain_access::publish(domain, pending);
            published->edge = gc::detail::ref_access::make_trace(header,
                                      static_cast<std::byte*>(published->payload));
            return published->payload;
        }
        catch (const std::bad_alloc&)
        {
            domain.request_collection();
            return nullptr;
        }
    }
};

GcRuntime& GcRuntime::Instance() { static GcRuntime runtime; return runtime; }
GcRuntime::GcRuntime() : impl_(std::make_unique<Impl>()) {}
GcRuntime::~GcRuntime() = default;

void* GcRuntime::Allocate(const PrsmTypeDesc* desc)
{
    if (!desc)
        Invalid("PrSM GC allocation requires a type descriptor");
    return impl_->Allocate(desc, 0);
}
void* GcRuntime::AllocateBytes(std::size_t size) { return impl_->Allocate(nullptr, size); }

void GcRuntime::Store(void** slot, void* value)
{
    impl_->CheckMutation();
    if (!slot)
        Invalid("PrSM GC store requires a slot");
    if (value)
        gc::detail::domain_access::on_store(impl_->Find(value)->header);
    // Barrier first: a failed validation must not publish an invalid edge.
    std::memcpy(slot, &value, sizeof(value));
}
void GcRuntime::Regray(void* frame)
{
    impl_->CheckMutation();
    if (!frame)
        return;
    auto& record = *impl_->Find(frame);
    if (impl_->domain.current_phase() != gc::phase::marking)
        return;
    // GCCE has no public regray API in the pinned revision. Re-shade a
    // completed scan by clearing its nonzero mark epoch. A still-gray object
    // is already queued and must NOT be enqueued twice: this preserves the
    // collector's one-pending-entry-per-object reservation invariant.
    if (record.scanned_epoch != 0 && record.scanned_epoch == record.header->mark_epoch)
        record.header->mark_epoch = 0;
    record.scanned_epoch = 0;
    gc::detail::domain_access::on_store(record.header);
}
CeRoot GcRuntime::RootCreate(void* object)
{
    impl_->CheckMutation();
    if (!object)
        return {};
    auto* record = impl_->Find(object);
    if (impl_->next_root == 0)
        Invalid("PrSM GC root token space exhausted");
    const auto token = impl_->next_root++;
    impl_->roots.emplace(token, gc::detail::ref_access::make_root(record->header,
                                             static_cast<std::byte*>(object)));
    return {reinterpret_cast<void*>(token)};
}
void GcRuntime::RootDestroy(CeRoot root)
{
    impl_->CheckMutation();
    if (root.opaque && impl_->roots.erase(reinterpret_cast<std::uintptr_t>(root.opaque)) == 0)
        Invalid("PrSM GC root is invalid or was already destroyed");
}
void GcRuntime::TraceRef(void* tracer, void* object)
{
    impl_->CheckThread();
    if (!impl_->tracing || !tracer || tracer != impl_->trace_context)
        Invalid("PrSM GC tracer is only valid inside its trace callback");
    if (object)
        impl_->trace_context->tracer->visit(impl_->Find(object)->edge);
}
gc::step_result GcRuntime::CollectStep(const gc::step_budget& budget)
{
    impl_->CheckThread();
    if (impl_->active_depth || impl_->collecting)
    {
        impl_->domain.request_collection();
        gc::step_result result{};
        result.before = result.after = impl_->domain.current_phase();
        result.refused = true;
        return result;
    }
    FlagScope scope(impl_->collecting);
    return impl_->domain.collect_step(budget);
}
gc::collect_result GcRuntime::CollectFull()
{
    impl_->CheckThread();
    if (impl_->active_depth || impl_->collecting)
    {
        impl_->domain.request_collection();
        gc::collect_result result{};
        result.refused = true;
        return result;
    }
    FlagScope scope(impl_->collecting);
    return impl_->domain.collect_full();
}
void GcRuntime::RequestCollection() { impl_->CheckThread(); impl_->domain.request_collection(); }
void GcRuntime::SetAllocationThreshold(std::size_t bytes)
{ impl_->CheckMutation(); impl_->domain.set_allocation_threshold(bytes); }
void GcRuntime::SetAllocationLimit(std::size_t bytes) { impl_->CheckMutation(); impl_->allocation_limit = bytes; }
gc::statistics GcRuntime::Stats() const { impl_->CheckThread(); return impl_->domain.stats(); }
std::size_t GcRuntime::ActiveDepth() const { impl_->CheckThread(); return impl_->active_depth; }
bool GcRuntime::AtSafePoint() const
{ impl_->CheckThread(); return !impl_->active_depth && !impl_->collecting && !impl_->tracing && impl_->domain.current_phase() == gc::phase::idle; }
void GcRuntime::SetModuleDispatchEnabled(const PrsmModule& module, bool enabled)
{
    if (!AtSafePoint()) Invalid("PrSM dispatch gate requires a safe point");
    const auto found = impl_->modules.find(&module);
    if (found == impl_->modules.end() || !found->second.registered)
        Invalid("PrSM dispatch gate requires a registered module");
    found->second.dispatch_enabled = enabled;
}
bool GcRuntime::ModuleDispatchEnabled(const PrsmModule& module) const
{
    impl_->CheckThread();
    const auto found = impl_->modules.find(&module);
    return found != impl_->modules.end() && found->second.registered && found->second.dispatch_enabled;
}
bool GcRuntime::Contains(const void* object) const
{ impl_->CheckThread(); return impl_->objects.contains(object); }
bool GcRuntime::MatchesActiveDescriptor(const void* object, const PrsmTypeDesc* type) const
{
    impl_->CheckThread();
    const auto found = impl_->objects.find(object);
    const auto owner = impl_->descriptor_owners.find(type);
    return impl_->active_depth && found != impl_->objects.end() &&
        owner != impl_->descriptor_owners.end() &&
        found->second->original_descriptor == type &&
        found->second->module == owner->second && impl_->active_module == owner->second &&
        ModuleRegistered(*owner->second);
}
std::size_t GcRuntime::LiveObjects(const PrsmModule* module) const
{
    impl_->CheckThread();
    if (!module)
        return impl_->objects.size();
    return static_cast<std::size_t>(std::count_if(impl_->objects.begin(), impl_->objects.end(),
        [module](const auto& item) { return item.second->module == module; }));
}
void GcRuntime::RegisterModule(const PrsmModule& module)
{
    impl_->CheckMutation();
    if (impl_->active_depth)
        Invalid("PrSM module registration requires a safe point");
    if (module.abi_version != PRSM_ABI_VERSION || (module.type_count && !module.types))
        Invalid("PrSM module GC registration received an invalid table");
    if (const auto existing = impl_->modules.find(&module); existing != impl_->modules.end())
    {
        if (existing->second.registered)
            return; // Loader and lifecycle driver may establish the same registration.
        Invalid("PrSM module is still waiting to drain");
    }
    for (std::uint32_t i = 0; i < module.type_count; ++i)
    {
        if (!module.types[i] || impl_->descriptor_owners.contains(module.types[i]))
            Invalid("PrSM module has a null or already registered descriptor");
    }
    auto [it, inserted] = impl_->modules.emplace(&module, Impl::ModuleState{});
    (void)inserted;
    try
    {
        for (std::uint32_t i = 0; i < module.type_count; ++i)
            impl_->descriptor_owners.emplace(module.types[i], &module);
        if (module.trace_globals)
        {
            // Reserve the root node before creating callback-bearing storage.
            // After publication, linking this existing node cannot allocate.
            // Thus a registration failure is fully rolled back before the
            // loader may close the library.
            if (impl_->next_root == 0)
                Invalid("PrSM GC root token space exhausted");
            const auto token = impl_->next_root++;
            auto [root, root_inserted] = impl_->roots.emplace(token, gc::root_ref<std::byte>{});
            (void)root_inserted;
            it->second.globals = {reinterpret_cast<void*>(token)};
            void* sentinel = impl_->Allocate(nullptr, 1, &module);
            if (!sentinel)
                throw std::bad_alloc();
            root->second = gc::detail::ref_access::make_root(impl_->Find(sentinel)->header,
                                                            static_cast<std::byte*>(sentinel));
        }
        it->second.registered = true;
    }
    catch (...)
    {
        if (it->second.globals.opaque)
            impl_->roots.erase(reinterpret_cast<std::uintptr_t>(it->second.globals.opaque));
        for (auto owner = impl_->descriptor_owners.begin(); owner != impl_->descriptor_owners.end();)
            owner = owner->second == &module ? impl_->descriptor_owners.erase(owner) : std::next(owner);
        impl_->modules.erase(it);
        throw;
    }
}
void GcRuntime::UnregisterModule(const PrsmModule& module)
{
    impl_->CheckMutation();
    if (impl_->active_depth)
        Invalid("PrSM module unregistration requires a safe point");
    const auto it = impl_->modules.find(&module);
    if (it == impl_->modules.end())
        return;
    if (it->second.leases)
        Invalid("PrSM module global roots cannot be removed while callbacks are leased");
    RootDestroy(it->second.globals);
    it->second.globals = {};
    it->second.registered = false;
}
void GcRuntime::AcquireModuleLease(const PrsmModule& module)
{
    impl_->CheckMutation();
    const auto it = impl_->modules.find(&module);
    if (it == impl_->modules.end() || !it->second.registered)
        Invalid("PrSM module callback lease requires a registered module");
    if (it->second.leases == std::numeric_limits<std::size_t>::max())
        Invalid("PrSM module callback lease count overflow");
    ++it->second.leases;
}
void GcRuntime::ReleaseModuleLease(const PrsmModule& module)
{
    impl_->CheckMutation();
    const auto it = impl_->modules.find(&module);
    if (it == impl_->modules.end() || it->second.leases == 0)
        Invalid("PrSM module callback lease is not held");
    --it->second.leases;
}
bool GcRuntime::ModuleRegistered(const PrsmModule& module) const
{
    impl_->CheckThread();
    const auto it = impl_->modules.find(&module);
    return it != impl_->modules.end() && it->second.registered;
}
bool GcRuntime::HasModuleLeases(const PrsmModule& module) const
{
    impl_->CheckThread();
    const auto it = impl_->modules.find(&module);
    return it != impl_->modules.end() && it->second.leases != 0;
}
bool GcRuntime::DrainModule(const PrsmModule& module)
{
    impl_->CheckMutation();
    if (impl_->active_depth || HasModuleLeases(module))
        return false;
    UnregisterModule(module);
    const auto result = CollectFull();
    if (!result.completed || LiveObjects(&module) != 0)
        return false;
    for (auto owner = impl_->descriptor_owners.begin(); owner != impl_->descriptor_owners.end();)
        owner = owner->second == &module ? impl_->descriptor_owners.erase(owner) : std::next(owner);
    impl_->modules.erase(&module);
    return true;
}
const PrsmModule* GcRuntime::EnterScript(const PrsmModule* module, bool trusted_cleanup)
{
    impl_->CheckMutation();
    const auto* previous = impl_->active_module;
    const auto* selected = module ? module : previous;
    if (selected)
    {
        const auto found = impl_->modules.find(selected);
        if (found == impl_->modules.end() || !found->second.registered)
            Invalid("PrSM script entry requires a registered module");
        if (!found->second.dispatch_enabled && !trusted_cleanup)
            Invalid("PrSM script generation dispatch is blocked");
        impl_->active_module = selected;
    }
    ++impl_->active_depth;
    return previous;
}
void GcRuntime::LeaveScript(const PrsmModule* previous) noexcept
{
    if (impl_->owner != std::this_thread::get_id() || impl_->active_depth == 0)
        std::terminate();
    --impl_->active_depth;
    impl_->active_module = previous;
}
ScriptScope::ScriptScope(const PrsmModule* module, bool trusted_cleanup) : ScriptScope(GcRuntime::Instance(), module, trusted_cleanup) {}
ScriptScope::ScriptScope(GcRuntime& runtime, const PrsmModule* module, bool trusted_cleanup)
    : runtime_(runtime), previous_(runtime.EnterScript(module, trusted_cleanup)) {}
ScriptScope::~ScriptScope() { runtime_.LeaveScript(previous_); }
} // namespace prsm::host
