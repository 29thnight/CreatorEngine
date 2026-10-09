#include "gc/domain.hpp"
#include "gc/detail/testing.hpp"
#include "gc/managed.hpp"
#include "gc/tracer.hpp"

#include "block_allocator.hpp"

#if defined(_WIN32)
#    ifndef NOMINMAX
#        define NOMINMAX
#    endif
#    ifndef WIN32_LEAN_AND_MEAN
#        define WIN32_LEAN_AND_MEAN
#    endif
#    include <windows.h>
#else
#    include <dlfcn.h>
#endif

#if defined(_MSC_VER) && !defined(__clang__) && (defined(_M_X64) || defined(_M_IX86))
#    include <intrin.h>
#    define GCCE_PREFETCH(p) _mm_prefetch(reinterpret_cast<const char*>(p), _MM_HINT_T0)
#elif defined(__GNUC__) || defined(__clang__)
#    define GCCE_PREFETCH(p) __builtin_prefetch(p)
#else
#    define GCCE_PREFETCH(p) ((void)(p))
#endif

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <new>
#include <optional>
#include <atomic>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

namespace gc
{
namespace
{
using clock = std::chrono::steady_clock;

struct slot
{
    detail::object_header* header = nullptr;
    // Starts at 1 so a default weak_ref (generation 0) never resolves.
    std::uint32_t generation = 1;
};

// Internal stages of a cycle. trace and verify are both phase::marking.
enum class stage : std::uint8_t
{
    idle,
    trace,  // drain the gray stack
    verify, // scan slots for unmarked objects with a cleanup obligation
    sweep
};

constexpr std::size_t default_allocation_threshold = std::size_t{4} * 1024 * 1024;
constexpr std::uint32_t max_generation = std::numeric_limits<std::uint32_t>::max();
// Objects popped from the gray stack wait this many traces in a ring while
// their memory is prefetched, overlapping the cache misses of marking.
constexpr std::size_t prefetch_depth = 8;
// Slots looked at per verify or sweep unit when no destructor runs.
constexpr std::size_t slot_scan_batch = 256;
// collect_step reads the clock after this many units, or after a heavy one.
constexpr std::size_t units_per_clock_check = 8;
// A trace unit with more edges than this counts as heavy.
constexpr std::size_t heavy_unit_edges = 256;
constexpr const char* quarantine_label = "gc.quarantine";

// Restore reentrancy flags even if a trace or allocation throws.
struct flag_reset
{
    bool& flag;
    ~flag_reset() { flag = false; }
};

// vector::reserve sets the capacity exactly, so reserving one more element
// at a time reallocates every time. Grow geometrically instead.
template <class T>
void reserve_at_least(std::vector<T>& v, std::size_t n)
{
    if (n > v.capacity())
        v.reserve(std::max(n, v.capacity() * 2));
}

std::size_t round_up(std::size_t value, std::size_t align) noexcept
{
    return (value + align - 1) / align * align;
}

void default_violation_handler(const violation& v)
{
    std::fprintf(stderr, "gc: protocol violation: %s (type: %s)\n", to_string(v.kind),
                 v.type_name ? v.type_name : "-");
    std::abort();
}

// The loaded module (DLL / shared object) containing an address.
const void* module_of(const void* address) noexcept
{
#if defined(_WIN32)
    HMODULE module = nullptr;
    if (!::GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                              static_cast<LPCWSTR>(address), &module))
        return nullptr;
    return module;
#else
    Dl_info info{};
    if (!::dladdr(address, &info))
        return nullptr;
    return info.dli_fbase;
#endif
}

// ---- process-wide state

std::mutex& global_mutex()
{
    static std::mutex m;
    return m;
}

violation_handler& global_handler()
{
    static violation_handler h = &default_violation_handler;
    return h;
}

void report_global(violation_kind kind) noexcept
{
    violation_handler handler;
    {
        std::lock_guard lock(global_mutex());
        handler = global_handler();
    }
    handler(violation{kind, nullptr});
}

// Live domains, so a weak_ref can tell its domain is gone (debug checks).
std::unordered_map<const domain*, std::uint64_t>& live_domains()
{
    static std::unordered_map<const domain*, std::uint64_t> m;
    return m;
}

std::atomic<std::uint64_t> next_domain_id{1};

struct registry_entry
{
    const domain* d;
    explicit registry_entry(const domain* owner, std::uint64_t id) : d(owner)
    {
        std::lock_guard lock(global_mutex());
        live_domains()[owner] = id;
    }
    ~registry_entry()
    {
        std::lock_guard lock(global_mutex());
        live_domains().erase(d);
    }
    registry_entry(const registry_entry&) = delete;
    registry_entry& operator=(const registry_entry&) = delete;
};

// Objects under construction by gc::make on this thread, innermost last.
struct construction_frame
{
    domain* d;
    const unsigned char* begin;
    const unsigned char* end;
    bool claimed;
};
thread_local std::vector<construction_frame> construction_frames;
} // namespace

void set_global_violation_handler(violation_handler handler)
{
    std::lock_guard lock(global_mutex());
    global_handler() = handler ? std::move(handler) : violation_handler(&default_violation_handler);
}

namespace detail
{
void push_construction(domain& d, void* object, std::size_t size)
{
    const auto* begin = static_cast<const unsigned char*>(object);
    construction_frames.push_back({&d, begin, begin + size, false});
}

void pop_construction() noexcept { construction_frames.pop_back(); }

void on_managed_constructed(const void* self) noexcept
{
    const auto* p = static_cast<const unsigned char*>(self);
    if (!construction_frames.empty())
    {
        construction_frame& f = construction_frames.back();
        if (p >= f.begin && p < f.end)
        {
            if (!f.claimed)
            {
                f.claimed = true; // the object gc::make is constructing
                return;
            }
            // A second managed subobject in the same block: a GC type held
            // by value inside the object being made.
            domain_access::report(*f.d, violation_kind::created_outside_make, nullptr);
            return;
        }
    }
    report_global(violation_kind::created_outside_make);
}
} // namespace detail

namespace detail
{
void managed_delete_called() noexcept
{
    std::fprintf(stderr, "gc: operator delete called on a gc::managed object; "
                         "GC objects are destroyed only by the collector\n");
    std::abort();
}
} // namespace detail

const char* to_string(violation_kind kind) noexcept
{
    switch (kind)
    {
    case violation_kind::wrong_thread: return "wrong_thread";
    case violation_kind::nested_collection: return "nested_collection";
    case violation_kind::allocation_during_trace: return "allocation_during_trace";
    case violation_kind::store_of_condemned_object: return "store_of_condemned_object";
    case violation_kind::unreachable_with_cleanup_obligation: return "unreachable_with_cleanup_obligation";
    case violation_kind::roots_remaining_at_shutdown: return "roots_remaining_at_shutdown";
    case violation_kind::objects_remaining_at_shutdown: return "objects_remaining_at_shutdown";
    case violation_kind::created_outside_make: return "created_outside_make";
    case violation_kind::root_inside_gc_object: return "root_inside_gc_object";
    case violation_kind::weak_ref_outlived_domain: return "weak_ref_outlived_domain";
    }
    return "unknown";
}

struct domain::impl
{
    std::thread::id owner = std::this_thread::get_id();

    detail::root_node roots; // circular list sentinel
    std::size_t root_count = 0;

    detail::block_allocator allocator;
    std::vector<slot> slots;
    std::vector<std::uint32_t> free_slots;

    // ---- cycle state
    stage current_stage = stage::idle;
    std::uint32_t epoch = 0;
    std::vector<detail::object_header*> gray;
    std::array<detail::object_header*, prefetch_depth> ring{};
    std::size_t ring_head = 0;
    std::size_t ring_count = 0;
    // Every object enters the gray stack at most once per cycle, so reserving
    // one entry per object that can be marked keeps pushes allocation-free.
    std::size_t gray_reserved = 0;
    std::size_t verify_cursor = 0;
    std::vector<detail::object_header*> suspects; // unmarked with obligation at verify time
    std::uint64_t sweep_cutoff = 0;               // objects with a later serial are not candidates
    std::size_t sweep_cursor = 0;
    std::size_t sweep_end = 0;
    clock::time_point cycle_start{};
    clock::time_point last_cycle_end = clock::now();
    std::uint64_t cycle_steps = 0;
    collect_result current;

    // ---- allocation and requests
    std::size_t live_objects = 0;
    std::size_t live_bytes = 0;
    std::size_t bytes_since_last_cycle = 0;
    std::size_t allocation_threshold = default_allocation_threshold;
    duration max_cycle_interval{};
    pacing pacing_config;
    obligation_policy policy = obligation_policy::quarantine;
    std::vector<detail::object_header*> quarantine;
    std::uint64_t next_serial = 0;
    unsigned construct_depth = 0;
    bool in_collection = false;
    bool tracing = false;
    bool requested = false;
    // Phase time is taken at boundaries (step start/end, reclaim decision,
    // cycle end), not per unit: reading the clock costs as much as a unit.
    clock::time_point phase_since{};
    bool heavy_unit = false; // the last unit was large enough to check the clock
    detail::collection_failure next_failure = detail::collection_failure::none;

    // ---- statistics
    std::uint64_t cycles_completed = 0;
    std::uint64_t cycles_aborted = 0;
    std::uint64_t steps = 0;
    std::uint64_t steps_over_budget = 0;
    std::uint64_t barrier_stores_during_mark = 0;
    std::uint64_t barrier_shades = 0;
    duration worst_step{};
    duration worst_batch{};
    duration worst_finalize{};
    duration last_cycle_wall{};
    std::uint64_t last_cycle_steps = 0;
    std::size_t peak_live_bytes = 0;
    std::uint64_t allocations_over_limit = 0;
    collect_result last;

    violation_handler handler = &default_violation_handler;
    // Unregistered when the impl is destroyed, after the domain's destructor
    // body (which may still run destructors that use weak references).
    std::optional<registry_entry> registration;

    explicit impl(const domain_config& config)
        : allocator(config.heap == heap_kind::system ? detail::pool_mode::system : detail::pool_mode::size_classes)
    {
        roots.prev = roots.next = &roots;
    }

    [[nodiscard]] bool gray_empty() const noexcept { return gray.empty() && ring_count == 0; }

    void clear_gray() noexcept
    {
        gray.clear();
        ring_count = 0;
    }

    // Next object to trace. Refills the ring from the gray stack first,
    // prefetching each object's header and the start of its payload.
    detail::object_header* next_gray() noexcept
    {
        while (ring_count < prefetch_depth && !gray.empty())
        {
            detail::object_header* h = gray.back();
            gray.pop_back();
            GCCE_PREFETCH(h);
            GCCE_PREFETCH(reinterpret_cast<const char*>(h) + 64);
            ring[(ring_head + ring_count) % prefetch_depth] = h;
            ++ring_count;
        }
        detail::object_header* h = ring[ring_head];
        ring_head = (ring_head + 1) % prefetch_depth;
        --ring_count;
        return h;
    }

    void note_batch(duration d, step_result& r) noexcept
    {
        r.longest_batch = std::max(r.longest_batch, d);
        r.longest_unit = r.longest_batch;
        worst_batch = std::max(worst_batch, d);
    }

    void fail_collection_allocation(detail::collection_failure point)
    {
        if (next_failure == point)
        {
            next_failure = detail::collection_failure::none;
            throw std::bad_alloc{};
        }
    }
};

domain::domain() : domain(domain_config{}) {}

domain::domain(const domain_config& config)
    : impl_(std::make_unique<impl>(config)), id_(next_domain_id.fetch_add(1, std::memory_order_relaxed))
{
    impl_->registration.emplace(this, id_);
}

domain::~domain()
{
    impl& s = *impl_;
    if (s.root_count != 0)
    {
        // Unsupported: roots outliving the domain. Detach them so their
        // destructors become no-ops and leak the objects instead of freeing
        // memory that is still referenced.
        report(violation_kind::roots_remaining_at_shutdown, s.roots.next->header);
        for (detail::root_node* n = s.roots.next; n != &s.roots;)
        {
            detail::root_node* next = n->next;
            n->prev = n->next = nullptr;
            n->header = nullptr;
            n = next;
        }
        s.roots.prev = s.roots.next = &s.roots;
        s.root_count = 0;
        if (s.live_objects != 0)
            report(violation_kind::objects_remaining_at_shutdown, nullptr);
        s.allocator.leak();
        return;
    }

    collect_full();
    if (s.live_objects != 0)
    {
        report(violation_kind::objects_remaining_at_shutdown, nullptr);
        s.allocator.leak();
    }
}

void domain::report(violation_kind kind, const detail::object_header* header) noexcept
{
    const violation v{kind, header && header->type ? header->type->name : nullptr};
    impl_->handler(v);
}

void domain::check_thread() noexcept
{
#if GC_DEBUG_CHECKS
    if (std::this_thread::get_id() != impl_->owner)
        report(violation_kind::wrong_thread, nullptr);
#endif
}

void domain::bind_to_current_thread() noexcept { impl_->owner = std::this_thread::get_id(); }

void domain::set_violation_handler(violation_handler handler)
{
    impl_->handler = handler ? std::move(handler) : violation_handler(&default_violation_handler);
}

void domain::request_collection() noexcept { impl_->requested = true; }

bool domain::collection_requested() const noexcept { return impl_->requested; }

void domain::set_allocation_threshold(std::size_t bytes) noexcept { impl_->allocation_threshold = bytes; }

void domain::set_max_cycle_interval(duration interval) noexcept { impl_->max_cycle_interval = interval; }

void domain::set_pacing(const pacing& p) noexcept { impl_->pacing_config = p; }

void domain::set_obligation_policy(obligation_policy policy) noexcept { impl_->policy = policy; }

statistics domain::stats() const
{
    const impl& s = *impl_;
    statistics st;
    st.current_phase = phase_;
    st.cycles_completed = s.cycles_completed;
    st.cycles_aborted = s.cycles_aborted;
    st.live_objects = s.live_objects;
    st.live_bytes = s.live_bytes;
    st.roots = s.root_count;
    st.bytes_since_last_cycle = s.bytes_since_last_cycle;
    st.allocation_threshold = s.allocation_threshold;
    st.collection_requested = s.requested;
    st.last = s.last;
    st.current = s.current;
    st.steps = s.steps;
    st.steps_over_budget = s.steps_over_budget;
    st.barrier_stores_during_mark = s.barrier_stores_during_mark;
    st.barrier_shades = s.barrier_shades;
    st.worst_step = s.worst_step;
    st.worst_batch = s.worst_batch;
    st.worst_unit = s.worst_batch;
    st.worst_finalize = s.worst_finalize;
    st.last_cycle_wall = s.last_cycle_wall;
    st.last_cycle_steps = s.last_cycle_steps;
    st.peak_live_bytes = s.peak_live_bytes;
    st.heap_committed_bytes = s.allocator.committed_bytes();
    st.heap_pages = s.allocator.page_count();
    st.heap_chunks = s.allocator.chunk_count();
    st.memory_limit = s.pacing_config.memory_limit;
    st.allocations_over_limit = s.allocations_over_limit;
    st.quarantined = s.quarantine.size();
    return st;
}

// ---------------------------------------------------------------- allocation

domain::pending_allocation domain::begin_allocation(const detail::type_descriptor& type)
{
    impl& s = *impl_;
    check_thread();
    if (s.tracing)
        report(violation_kind::allocation_during_trace, nullptr);

    const std::size_t align = type.align > alignof(detail::object_header) ? type.align : alignof(detail::object_header);
    const std::size_t offset = round_up(sizeof(detail::object_header), type.align);
    const std::size_t size = offset + type.size;

    const detail::block_allocator::allocation storage = s.allocator.allocate(size, align);
    void* block = storage.block;

    std::uint32_t index;
    try
    {
        if (!s.free_slots.empty())
        {
            index = s.free_slots.back();
            s.free_slots.pop_back();
        }
        else
        {
            s.slots.emplace_back();
            // Reserve so that releasing a slot during sweep never allocates.
            reserve_at_least(s.free_slots, s.slots.size());
            index = static_cast<std::uint32_t>(s.slots.size() - 1);
        }
        if (phase_ == phase::marking)
            reserve_at_least(s.gray, ++s.gray_reserved);
    }
    catch (...)
    {
        s.allocator.deallocate(block, size, align, storage.pool);
        throw;
    }

    auto* header = ::new (block) detail::object_header{};
    header->owner = this;
    header->type = &type;
    header->object = static_cast<unsigned char*>(block) + offset;
    header->block_size = size;
    header->block_align = align;
    header->pool = storage.pool;
    header->slot = index;

    ++s.construct_depth;
    return pending_allocation{header};
}

void domain::abort_allocation(pending_allocation& pending) noexcept
{
    impl& s = *impl_;
    detail::object_header* h = pending.header;
    --s.construct_depth;
    // The slot was never published; return it without bumping the generation.
    s.free_slots.push_back(h->slot);
    s.allocator.deallocate(h, h->block_size, h->block_align, h->pool);
    pending.header = nullptr;
}

detail::object_header* domain::publish(pending_allocation& pending) noexcept
{
    impl& s = *impl_;
    detail::object_header* h = pending.header;
    --s.construct_depth;

    slot& sl = s.slots[h->slot];
    h->generation = sl.generation;
    h->alloc_serial = ++s.next_serial;
    sl.header = h;

    ++s.live_objects;
    s.live_bytes += h->block_size;
    s.peak_live_bytes = std::max(s.peak_live_bytes, s.live_bytes);
    s.bytes_since_last_cycle += h->block_size;
    if (s.bytes_since_last_cycle >= s.allocation_threshold)
        s.requested = true;
    if (const std::size_t limit = s.pacing_config.memory_limit; limit != 0)
    {
        if (s.live_bytes > limit)
            ++s.allocations_over_limit;
        if (static_cast<double>(s.live_bytes) >= static_cast<double>(limit) * s.pacing_config.start_fraction)
            s.requested = true;
    }

    // An object completed during marking is scanned in this cycle. During
    // sweep its serial is past the cutoff, so it is never a candidate.
    if (phase_ == phase::marking)
        shade(h);
    return h;
}

// ------------------------------------------------------------ references

void domain::on_store(detail::object_header* target)
{
    impl& s = *impl_;
    check_thread();
    if (is_condemned(target))
        report(violation_kind::store_of_condemned_object, target);
    // Insertion barrier: a non-null store during marking shades the target.
    if (phase_ == phase::marking)
    {
        ++s.barrier_stores_during_mark;
        if (!is_marked(target))
        {
            ++s.barrier_shades;
            shade(target);
        }
    }
}

void domain::link_root(detail::root_node* node)
{
    impl& s = *impl_;
    check_thread();
#if GC_DEBUG_CHECKS
    // A root inside a GC object would keep its graph alive after the object
    // itself became unreachable.
    if (void* block = s.allocator.find_block(node))
    {
        const auto* h = static_cast<const detail::object_header*>(block);
        const auto* p = reinterpret_cast<const unsigned char*>(node);
        const auto* object = static_cast<const unsigned char*>(h->object);
        if (h->owner == this && h->slot < s.slots.size() && p >= object && p < object + h->type->size)
            report(violation_kind::root_inside_gc_object, h);
    }
#endif
    node->prev = &s.roots;
    node->next = s.roots.next;
    s.roots.next->prev = node;
    s.roots.next = node;
    ++s.root_count;
}

void domain::link_new_root(detail::root_node* node) noexcept
{
    impl& s = *impl_;
    node->prev = &s.roots;
    node->next = s.roots.next;
    s.roots.next->prev = node;
    s.roots.next = node;
    ++s.root_count;
}

void domain::unlink_root(detail::root_node* node) noexcept
{
    check_thread();
    node->prev->next = node->next;
    node->next->prev = node->prev;
    node->prev = node->next = nullptr;
    --impl_->root_count;
}

detail::object_header* domain::resolve_weak(domain* d, std::uint64_t id, std::uint32_t index,
                                            std::uint32_t generation)
{
#if GC_DEBUG_CHECKS
    {
        std::unique_lock lock(global_mutex());
        auto it = live_domains().find(d);
        if (it == live_domains().end() || it->second != id)
        {
            lock.unlock();
            report_global(violation_kind::weak_ref_outlived_domain);
            return nullptr;
        }
    }
#else
    (void)id;
#endif
    impl& s = *d->impl_;
    d->check_thread();
    if (index >= s.slots.size())
        return nullptr;
    const slot& sl = s.slots[index];
    if (!sl.header || sl.generation != generation)
        return nullptr;
    if (d->is_condemned(sl.header))
        return nullptr;
    return sl.header;
}

// ------------------------------------------------------------- lifecycle

bool domain::advance_lifecycle(detail::object_header* header, lifecycle_state next)
{
    check_thread();
    if (static_cast<int>(next) != static_cast<int>(header->lifecycle) + 1)
        return false;
    header->lifecycle = next;
    if (next == lifecycle_state::destroyed)
    {
        header->cleanup_obligation = false;
        release_quarantine(header);
    }
    return true;
}

bool domain::begin_cleanup_obligation(detail::object_header* header)
{
    check_thread();
    if (header->lifecycle == lifecycle_state::destroyed)
        return false;
    header->cleanup_obligation = true;
    // An obligation that appears during marking must not be missed by the
    // verify scan, which may already have passed this slot: keep the object
    // for this cycle. If it is unreachable, the next cycle reports it.
    if (phase_ == phase::marking)
        shade(header);
    return true;
}

// ------------------------------------------------------------- collection

bool domain::is_marked(const detail::object_header* header) const noexcept
{
    return header->mark_epoch == impl_->epoch;
}

bool domain::is_condemned(const detail::object_header* header) const noexcept
{
    return phase_ == phase::sweeping && !is_marked(header) && header->alloc_serial <= impl_->sweep_cutoff;
}

void domain::shade(detail::object_header* header)
{
    if (is_marked(header))
        return;
    header->mark_epoch = impl_->epoch;
    impl_->gray.push_back(header); // capacity reserved, see gray_reserved
}

void domain::reclaim(detail::object_header* h) noexcept
{
    impl& s = *impl_;
    h->type->destroy(h->object);

    // Look the slot up after the destructor: it may have allocated.
    slot& sl = s.slots[h->slot];
    sl.header = nullptr;
    if (sl.generation == max_generation)
    {
        // Retire the slot: reusing it would let an old weak_ref match again.
    }
    else
    {
        ++sl.generation;
        s.free_slots.push_back(h->slot); // capacity reserved in begin_allocation
    }

    --s.live_objects;
    s.live_bytes -= h->block_size;
    s.allocator.deallocate(h, h->block_size, h->block_align, h->pool);
}

bool domain::enter_collection(bool& refused) noexcept
{
    impl& s = *impl_;
    check_thread();
    refused = true;
    if (s.in_collection)
    {
        report(violation_kind::nested_collection, nullptr);
        return false;
    }
    if (s.construct_depth != 0)
    {
        // Constructors may only request; the engine loop collects later.
        report(violation_kind::nested_collection, nullptr);
        s.requested = true;
        return false;
    }
    refused = false;
    s.in_collection = true;
    return true;
}

void domain::start_cycle()
{
    impl& s = *impl_;
    s.cycle_start = clock::now();
    s.phase_since = s.cycle_start;
    s.current = collect_result{};
    s.cycle_steps = 0;
    if (++s.epoch == 0)
    {
        // Epoch wrap: clear stale marks once so no object looks marked.
        s.epoch = 1;
        for (slot& sl : s.slots)
            if (sl.header)
                sl.header->mark_epoch = 0;
    }
    s.requested = false;
    s.bytes_since_last_cycle = 0;
    s.suspects.clear();
    s.verify_cursor = 0;
    s.clear_gray();
    s.gray_reserved = s.live_objects;
    phase_ = phase::marking;
    s.current_stage = stage::trace;
    if (s.gray_reserved > s.gray.capacity())
        s.fail_collection_allocation(detail::collection_failure::gray_reserve);
    reserve_at_least(s.gray, s.gray_reserved);

    // Roots are scanned in one piece. Roots added later are shaded by the
    // barrier when they are linked, and removing a root needs no work.
    for (detail::root_node* n = s.roots.next; n != &s.roots; n = n->next)
        shade(n->header);
    for (detail::object_header* h : s.quarantine)
        shade(h);
    s.current.startup_time = clock::now() - s.cycle_start;
}

bool domain::do_unit(tracer& t, step_result& r)
{
    impl& s = *impl_;
    (void)r;
    s.heavy_unit = false;
    switch (s.current_stage)
    {
    case stage::trace:
        if (!s.gray_empty())
        {
            detail::object_header* h = s.next_gray();
            s.tracing = true;
            const flag_reset tracing_guard{s.tracing};
            h->type->trace(h->object, t);
            t.flush();
            ++s.current.marked;
            const std::size_t edges = std::exchange(t.edges_, 0);
            s.current.edges_visited += edges;
            s.heavy_unit = edges > heavy_unit_edges;
            return true;
        }
        s.current_stage = stage::verify;
        [[fallthrough]];

    case stage::verify:
        if (!s.gray_empty())
        {
            // The barrier added work since the trace stage ran dry.
            s.current_stage = stage::trace;
            return true;
        }
        if (s.verify_cursor < s.slots.size())
        {
            const std::size_t end = std::min(s.slots.size(), s.verify_cursor + slot_scan_batch);
            for (; s.verify_cursor < end; ++s.verify_cursor)
            {
                detail::object_header* h = s.slots[s.verify_cursor].header;
                if (h && h->cleanup_obligation && !is_marked(h))
                {
                    if (s.suspects.size() == s.suspects.capacity())
                        s.fail_collection_allocation(detail::collection_failure::suspects_growth);
                    s.suspects.push_back(h);
                }
            }
            return true;
        }
        finalize(t, r);
        return s.current_stage != stage::idle;

    case stage::sweep:
        for (std::size_t scanned = 0; s.sweep_cursor < s.sweep_end && scanned < slot_scan_batch; ++scanned)
        {
            detail::object_header* h = s.slots[s.sweep_cursor++].header;
            if (h && is_condemned(h))
            {
                s.current.reclaimed_bytes += h->block_size;
                reclaim(h);
                ++s.current.reclaimed;
                break; // at most one destructor per unit
            }
        }
        if (s.sweep_cursor < s.sweep_end)
            return true;
        finish_cycle(true);
        return false;

    case stage::idle:
        break;
    }
    return false;
}

// The reclaim decision. Runs inside one step with no game code in between:
// drain all remaining marking work, recheck the suspects against the final
// mark state, then fix the allocation cutoff and enter sweep.
void domain::finalize(tracer& t, step_result& r)
{
    impl& s = *impl_;
    const auto t0 = clock::now();

    {
        s.tracing = true;
        const flag_reset tracing_guard{s.tracing};
        while (!s.gray_empty())
        {
            detail::object_header* h = s.next_gray();
            h->type->trace(h->object, t);
            t.flush();
            ++s.current.marked;
        }
    }
    s.current.edges_visited += std::exchange(t.edges_, 0);

    // Objects that gained an obligation after the verify scan were shaded,
    // so the suspects collected by the scan are the complete set.
    for (detail::object_header* h : s.suspects)
    {
        if (h->cleanup_obligation && !is_marked(h))
        {
            report(violation_kind::unreachable_with_cleanup_obligation, h);
            ++s.current.violations;
            if (s.policy == obligation_policy::quarantine && !h->quarantined)
            {
                // From the next cycle on it is a root, so its subgraph stays
                // and the rest of the garbage is reclaimed again.
                if (s.quarantine.size() == s.quarantine.capacity())
                    s.fail_collection_allocation(detail::collection_failure::quarantine_growth);
                s.quarantine.push_back(h);
                h->quarantined = true; // publish only after the vector owns it
                ++s.current.quarantined;
            }
        }
    }
    s.suspects.clear();

    const auto finalize_end = clock::now();
    const duration elapsed = finalize_end - t0;
    s.current.mark_time += finalize_end - s.phase_since;
    s.phase_since = finalize_end;
    r.finalized = true;
    r.finalize_time = elapsed;
    s.worst_finalize = std::max(s.worst_finalize, elapsed);
    s.heavy_unit = true; // the decision can be long: check the budget after it

    if (s.current.violations != 0)
    {
        finish_cycle(false);
        return;
    }

    s.sweep_cutoff = s.next_serial;
    s.sweep_cursor = 0;
    s.sweep_end = s.slots.size();
    phase_ = phase::sweeping;
    s.current_stage = stage::sweep;
}

void domain::finish_cycle(bool completed)
{
    impl& s = *impl_;
    const auto maintenance_start = clock::now();
    if (completed)
        s.current.sweep_time += maintenance_start - s.phase_since;
    phase_ = phase::idle;
    s.current_stage = stage::idle;
    s.clear_gray();
    s.suspects.clear();

    s.allocator.end_cycle();
    const auto now = clock::now();
    s.current.maintenance_time += now - maintenance_start;

    s.current.completed = completed;
    s.current.aborted = !completed;
    if (completed)
        ++s.cycles_completed;
    else
        ++s.cycles_aborted;
    s.last = s.current;
    s.last_cycle_wall = now - s.cycle_start;
    s.last_cycle_steps = s.cycle_steps;
    s.last_cycle_end = now;
}

void domain::abandon_cycle() noexcept
{
    impl& s = *impl_;
    const auto now = clock::now();
    if (phase_ == phase::marking)
        s.current.mark_time += now - s.phase_since;
    else if (phase_ == phase::sweeping)
        s.current.sweep_time += now - s.phase_since;

    // The failed tracer may already have popped an object and buffered only
    // some edges. Never resume that mark or make a reclaim decision from it.
    phase_ = phase::idle;
    s.current_stage = stage::idle;
    s.clear_gray();
    s.suspects.clear();
    s.tracing = false;
    s.requested = true;
    s.current.completed = false;
    s.current.aborted = true;
    ++s.cycles_aborted;
    s.last = s.current;
    s.last_cycle_wall = now - s.cycle_start;
    s.last_cycle_steps = s.cycle_steps;
    s.last_cycle_end = now;
    // Do not allocate, trace, destroy, or run allocator maintenance here.
}

void domain::run_to_cycle_end()
{
    tracer t(*this);
    step_result r;
    ++impl_->cycle_steps;
    while (do_unit(t, r))
    {
    }
}

step_result domain::collect_step(const step_budget& budget)
{
    impl& s = *impl_;
    step_result r;
    r.before = phase_;

    bool refused = false;
    if (!enter_collection(refused))
    {
        r.refused = refused;
        r.after = phase_;
        return r;
    }

    const flag_reset collection_guard{s.in_collection};
    try
    {
        const auto start = clock::now();
        if (phase_ == phase::idle)
        {
            const bool interval_due = s.max_cycle_interval > duration::zero() &&
                                      start - s.last_cycle_end >= s.max_cycle_interval;
            if (!s.requested && !interval_due)
            {
                r.after = phase_;
                return r;
            }
            start_cycle();
            r.cycle_started = true;
            r.startup_time = s.current.startup_time;
        }

        // Memory pressure enlarges the budget so the cycle finishes sooner.
        if (const std::size_t limit = s.pacing_config.memory_limit; limit != 0)
        {
            const double used = static_cast<double>(s.live_bytes) / static_cast<double>(limit);
            const double from = s.pacing_config.start_fraction;
            const double t = from < 1.0 ? std::clamp((used - from) / (1.0 - from), 0.0, 1.0) : (used >= 1.0 ? 1.0 : 0.0);
            r.budget_scale = 1.0 + (std::max(s.pacing_config.max_budget_scale, 1.0) - 1.0) * t;
        }
        const auto time_budget = std::chrono::duration_cast<duration>(budget.time * r.budget_scale);
        const auto min_units = static_cast<std::size_t>(static_cast<double>(budget.min_units) * r.budget_scale);

        ++s.steps;
        ++s.cycle_steps;
        if (!r.cycle_started)
            s.phase_since = start; // resume phase accounting; mutator time is excluded
        tracer t(*this);
        auto last_check = clock::now(); // exclude startup from sampled batches
        std::size_t since_check = 0;
        auto check_clock = [&] {
            const auto now = clock::now();
            s.note_batch(now - last_check, r);
            last_check = now;
            since_check = 0;
            return now;
        };
        for (;;)
        {
            const bool more = do_unit(t, r);
            ++r.units;
            ++since_check;
            if (!more)
            {
                r.cycle_finished = true;
                r.maintenance_time = s.last.maintenance_time;
                check_clock();
                break;
            }
            const bool check = s.heavy_unit || since_check >= units_per_clock_check;
            if (r.units < min_units)
            {
                if (check)
                    check_clock();
                continue;
            }
            if (time_budget <= duration::zero())
            {
                check_clock();
                break;
            }
            if (check && check_clock() - start >= time_budget)
                break;
        }

        const auto end = clock::now();
        if (phase_ == phase::marking)
            s.current.mark_time += end - s.phase_since;
        else if (phase_ == phase::sweeping)
            s.current.sweep_time += end - s.phase_since;
        r.elapsed = end - start;
        r.over_budget = r.elapsed > time_budget;
        s.steps_over_budget += r.over_budget;
        s.worst_step = std::max(s.worst_step, r.elapsed);

        r.after = phase_;
        return r;
    }
    catch (...)
    {
        abandon_cycle();
        throw;
    }
}

collect_result domain::collect_full()
{
    impl& s = *impl_;
    bool refused = false;
    if (!enter_collection(refused))
    {
        collect_result result;
        result.refused = true;
        return result;
    }

    const flag_reset collection_guard{s.in_collection};
    try
    {
        if (phase_ != phase::idle)
        {
            s.phase_since = clock::now();
            run_to_cycle_end();
        }
        start_cycle();
        run_to_cycle_end();

        return s.last;
    }
    catch (...)
    {
        abandon_cycle();
        throw;
    }
}
// ------------------------------------------------------------ diagnostics

object_info domain::describe(const detail::object_header* h, const char* root_label) const noexcept
{
    object_info info;
    info.type_name = h->type ? h->type->name : nullptr;
    info.root_label = root_label;
    info.size = h->block_size;
    info.lifecycle = h->lifecycle;
    info.quarantined = h->quarantined;
    return info;
}

void domain::release_quarantine(detail::object_header* header) noexcept
{
    if (!header->quarantined)
        return;
    header->quarantined = false;
    auto& q = impl_->quarantine;
    q.erase(std::remove(q.begin(), q.end(), header), q.end());
}

void domain::for_each_root(const std::function<void(const object_info&)>& fn) const
{
    const impl& s = *impl_;
    for (const detail::root_node* n = s.roots.next; n != &s.roots; n = n->next)
        fn(describe(n->header, n->label));
}

void domain::for_each_quarantined(const std::function<void(const object_info&)>& fn) const
{
    for (const detail::object_header* h : impl_->quarantine)
        fn(describe(h, quarantine_label));
}

std::vector<object_info> domain::retention_path(const detail::object_header* target)
{
    impl& s = *impl_;
    check_thread();
    if (s.tracing)
    {
        report(violation_kind::allocation_during_trace, target);
        return {};
    }

    // Breadth-first from the roots; parents record the first path found.
    struct origin
    {
        const detail::object_header* parent;
        const char* label;
    };
    std::unordered_map<const detail::object_header*, origin> seen;
    std::vector<const detail::object_header*> frontier;
    auto add_root = [&](const detail::object_header* h, const char* label) {
        if (seen.emplace(h, origin{nullptr, label}).second)
            frontier.push_back(h);
    };
    for (const detail::root_node* n = s.roots.next; n != &s.roots; n = n->next)
        add_root(n->header, n->label);
    for (const detail::object_header* h : s.quarantine)
        add_root(h, quarantine_label);

    std::vector<detail::object_header*> edges;
    tracer t(*this, edges);
    for (std::size_t i = 0; i < frontier.size() && !seen.count(target) ; ++i)
    {
        const detail::object_header* h = frontier[i];
        edges.clear();
        {
            s.tracing = true;
            const flag_reset tracing_guard{s.tracing};
            h->type->trace(h->object, t);
        }
        for (const detail::object_header* child : edges)
            if (seen.emplace(child, origin{h, nullptr}).second)
                frontier.push_back(child);
    }

    std::vector<object_info> path;
    auto it = seen.find(target);
    if (it == seen.end())
        return path;
    for (const detail::object_header* h = target; h;)
    {
        const origin& o = seen.at(h);
        path.push_back(describe(h, o.label));
        h = o.parent;
    }
    std::reverse(path.begin(), path.end());
    return path;
}
// ------------------------------------------------------------ modules

std::size_t domain::objects_in_module(const void* address_in_module) const
{
    const impl& s = *impl_;
    const void* module = module_of(address_in_module);
    if (!module)
        return 0;
    // Looked up per call: a cache could outlive an unloaded module whose
    // addresses are later reused.
    std::unordered_map<const detail::type_descriptor*, const void*> type_modules;
    std::size_t count = 0;
    for (const slot& sl : s.slots)
    {
        if (!sl.header)
            continue;
        const detail::type_descriptor* type = sl.header->type;
        auto [it, inserted] = type_modules.try_emplace(type, nullptr);
        if (inserted)
            it->second = module_of(type);
        count += it->second == module;
    }
    return count;
}
// ------------------------------------------------------------ testing hooks

namespace detail
{
std::uint32_t domain_testing::mark_epoch(const domain& d) noexcept { return d.impl_->epoch; }

void domain_testing::set_mark_epoch(domain& d, std::uint32_t epoch) noexcept { d.impl_->epoch = epoch; }

std::uint32_t domain_testing::slot_generation(const domain& d, std::uint32_t slot) noexcept
{
    return d.impl_->slots[slot].generation;
}

void domain_testing::set_slot_generation(domain& d, std::uint32_t slot, std::uint32_t generation) noexcept
{
    auto& sl = d.impl_->slots[slot];
    sl.generation = generation;
    if (sl.header)
        sl.header->generation = generation;
}

void domain_testing::fail_next_collection_allocation(domain& d, collection_failure point) noexcept
{
    d.impl_->next_failure = point;
}
} // namespace detail
} // namespace gc
