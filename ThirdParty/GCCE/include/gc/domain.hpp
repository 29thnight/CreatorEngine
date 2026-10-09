#pragma once

#include "config.hpp"
#include "detail/header.hpp"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

#if defined(_MSC_VER)
#    pragma warning(push)
#    pragma warning(disable : 4251) // std::unique_ptr member in exported class
#endif

namespace gc
{
// marking also covers the incremental verification that precedes the
// reclaim decision; the barrier is active throughout. The decision itself and
// the switch to sweeping happen inside a single step and are not observable.
enum class phase : std::uint8_t
{
    idle,
    marking,
    sweeping
};

enum class violation_kind : std::uint8_t
{
    wrong_thread,                       // registry or graph change off the owner thread
    nested_collection,                  // collection started from a constructor, tracer or destructor
    allocation_during_trace,            // make() called from a trace function
    store_of_condemned_object,          // strong reference created to an object being reclaimed
    unreachable_with_cleanup_obligation,// engine cleanup unfinished but no longer reachable
    roots_remaining_at_shutdown,
    objects_remaining_at_shutdown,
    // Debug checks (GC_DEBUG_CHECKS):
    created_outside_make,     // a gc::managed object on the stack, global, member or copy
    root_inside_gc_object,    // a root_ref stored inside a GC object
    weak_ref_outlived_domain  // weak_ref used after its domain was destroyed
};

[[nodiscard]] GC_API const char* to_string(violation_kind kind) noexcept;

struct violation
{
    violation_kind kind;
    const char* type_name; // type of the object involved, or nullptr
};

// The default handler prints the violation and aborts. A replacement handler
// that returns lets the operation continue; that is meant for tests and tools.
// Handlers must not throw: reporting is a noexcept path.
using violation_handler = std::function<void(const violation&)>;

// Handler for violations that belong to no domain (created_outside_make
// outside any gc::make, weak_ref_outlived_domain). Process-wide; the default
// prints and aborts. Passing an empty handler restores the default.
GC_API void set_global_violation_handler(violation_handler handler);

struct collect_result
{
    bool completed = false; // the cycle reclaimed its candidates
    bool aborted = false;   // the cycle stopped without using an incomplete mark
    bool refused = false;   // the cycle was not started
    std::size_t marked = 0;
    std::size_t edges_visited = 0;
    std::size_t reclaimed = 0;
    std::size_t reclaimed_bytes = 0;
    std::size_t violations = 0;
    std::size_t quarantined = 0; // objects moved to quarantine by this cycle
    std::chrono::nanoseconds mark_time{};  // tracing, verification and the reclaim decision
    std::chrono::nanoseconds sweep_time{}; // destructors and slot release
    std::chrono::nanoseconds startup_time{}; // subset of mark_time: reserves and root scan
    std::chrono::nanoseconds maintenance_time{}; // allocator end-of-cycle work, separate from sweep_time
};

using duration = std::chrono::nanoseconds;

// Soft budget for one collect_step. Work is split at object boundaries, so a
// single large object (a huge container, a slow destructor) or the finalize
// transition can overrun it; overruns are recorded, not prevented. Startup,
// sampled batches (up to 8 units), and allocator maintenance can also overrun.
struct step_budget
{
    duration time = std::chrono::microseconds(250);
    // Work units done even when the time is already spent, so that a busy
    // frame cannot stall a cycle forever.
    std::size_t min_units = 16;
};

struct step_result
{
    phase before = phase::idle;
    phase after = phase::idle;
    bool cycle_started = false;
    bool cycle_finished = false; // completed or aborted in this step
    bool finalized = false;      // the reclaim decision was taken in this step
    bool over_budget = false;
    bool refused = false;
    std::size_t units = 0;
    duration elapsed{};
    duration longest_unit{}; // compatibility alias of longest_batch; NOT one object
    duration finalize_time{};
    double budget_scale = 1.0; // > 1 when memory pressure enlarged the budget
    duration longest_batch{}; // longest clock-sampled batch, excluding startup
    duration startup_time{};
    duration maintenance_time{}; // also included in elapsed and the last batch
};

// What to do when an object with an unfinished cleanup obligation becomes
// unreachable. Both abort the cycle in which the violation is detected.
enum class obligation_policy : std::uint8_t
{
    // Keep the violators and everything they reach as quarantine roots so
    // that later cycles reclaim the remaining garbage. An object leaves the
    // quarantine when its lifecycle reaches destroyed.
    quarantine,
    // Keep aborting every cycle until the violation is resolved.
    strict
};

// Memory-pressure pacing. With a memory limit set, a cycle is requested once
// live GC bytes pass start_fraction of the limit, and step budgets grow
// linearly up to max_budget_scale as live bytes approach the limit. The
// limit is a pacing target, not an allocation cap.
struct pacing
{
    std::size_t memory_limit = 0; // 0 disables pacing
    double start_fraction = 0.75;
    double max_budget_scale = 8.0;
};

// Where GC blocks come from. size_class_pools (default): blocks up to 2 KiB
// come from 64 KiB pages in OS-backed chunks, pooled by 24 size classes
// shared by all types. system sends every block to the global operator new
// and exists for comparison.
enum class heap_kind : std::uint8_t
{
    size_class_pools,
    system
};

struct domain_config
{
    heap_kind heap = heap_kind::size_class_pools;
};

// Diagnostic description of a GC object or root.
struct object_info
{
    const char* type_name = nullptr;
    const char* root_label = nullptr; // set for roots that carry a label
    std::size_t size = 0;             // block size including the header
    lifecycle_state lifecycle = lifecycle_state::alive;
    bool quarantined = false;
};

struct statistics
{
    phase current_phase = phase::idle;
    std::uint64_t cycles_completed = 0;
    std::uint64_t cycles_aborted = 0;
    std::size_t live_objects = 0;
    std::size_t live_bytes = 0;
    std::size_t roots = 0;
    std::size_t bytes_since_last_cycle = 0;
    std::size_t allocation_threshold = 0;
    bool collection_requested = false;
    collect_result last;    // last finished cycle
    collect_result current; // cycle in progress

    // Incremental work (cumulative).
    std::uint64_t steps = 0;
    std::uint64_t steps_over_budget = 0;
    std::uint64_t barrier_stores_during_mark = 0; // non-null stores while marking
    std::uint64_t barrier_shades = 0;             // stores that added gray work
    duration worst_step{};
    duration worst_unit{};     // compatibility alias of worst_batch; NOT one object
    duration worst_finalize{}; // longest reclaim-decision transition
    duration last_cycle_wall{}; // start to end of the last finished cycle
    std::uint64_t last_cycle_steps = 0;

    // Memory and policy.
    std::size_t peak_live_bytes = 0;
    std::size_t heap_committed_bytes = 0; // allocator accounting, not process RSS
    std::size_t heap_pages = 0;  // pages assigned to a pool
    std::size_t heap_chunks = 0; // 2 MiB OS chunks mapped
    std::size_t memory_limit = 0;
    std::uint64_t allocations_over_limit = 0;
    std::size_t quarantined = 0;
    duration worst_batch{}; // largest clock-sampled collect_step batch, excluding startup
};

namespace detail
{
struct domain_access;
struct domain_testing;
} // namespace detail

// A GC domain: object registry, root list, weak slot table and an
// incremental, nonmoving mark-and-sweep collector driven from one owner
// thread. Objects in one domain may reference each other freely; references
// across domains are not supported.
class GC_API domain
{
public:
    domain();
    explicit domain(const domain_config& config);
    ~domain();

    domain(const domain&) = delete;
    domain& operator=(const domain&) = delete;

    // Advances the collector within a soft budget. Starts a cycle when one
    // was requested or the maximum interval elapsed; otherwise does nothing.
    // Owner thread, engine safe points only; never from constructors,
    // tracers or destructors.
    // Allocation/trace exceptions abandon the current cycle, request a fresh
    // one, and propagate. No sweep starts from an incomplete mark. Destructors
    // and violation handlers must not throw; ~domain is noexcept.
    step_result collect_step(const step_budget& budget = {});

    // Finishes a cycle in progress, then runs a complete new cycle, for
    // loading and shutdown boundaries. The result describes the new cycle.
    collect_result collect_full();

    // Records a request; the engine loop decides when to act on it.
    void request_collection() noexcept;
    [[nodiscard]] bool collection_requested() const noexcept;

    // New GC bytes after which a collection is requested automatically.
    void set_allocation_threshold(std::size_t bytes) noexcept;

    // collect_step starts a cycle when this much time has passed since the
    // last one ended, even without new allocation. Zero disables it.
    void set_max_cycle_interval(duration interval) noexcept;

    void set_pacing(const pacing& p) noexcept;
    void set_obligation_policy(obligation_policy policy) noexcept;

    // Module (DLL / shared object) unload safety. Every GC object runs code
    // from the module that instantiated its type (gc_trace, the destructor),
    // so a module must not be unloaded while objects of its types exist.
    // Returns how many live objects have types from the module containing
    // `address_in_module` (any function or static data of that module).
    // Unload only when this is 0, typically after dropping the module's
    // objects and running collect_full. Owner thread, outside collection.
    [[nodiscard]] std::size_t objects_in_module(const void* address_in_module) const;

    // Diagnostics. Owner thread; not from inside trace functions.
    void for_each_root(const std::function<void(const object_info&)>& fn) const;
    void for_each_quarantined(const std::function<void(const object_info&)>& fn) const;

    [[nodiscard]] statistics stats() const;
    [[nodiscard]] phase current_phase() const noexcept { return phase_; }

    void set_violation_handler(violation_handler handler);

    // The owner thread defaults to the constructing thread.
    void bind_to_current_thread() noexcept;

private:
    friend struct detail::domain_access;
    friend struct detail::domain_testing;
    friend class tracer;

    struct pending_allocation
    {
        detail::object_header* header;
    };

    pending_allocation begin_allocation(const detail::type_descriptor& type);
    void abort_allocation(pending_allocation& pending) noexcept;
    detail::object_header* publish(pending_allocation& pending) noexcept;

    void on_store(detail::object_header* target);
    void link_root(detail::root_node* node);
    void link_new_root(detail::root_node* node) noexcept;
    void unlink_root(detail::root_node* node) noexcept;
    [[nodiscard]] static detail::object_header* resolve_weak(domain* d, std::uint64_t id, std::uint32_t slot,
                                                             std::uint32_t generation);
    [[nodiscard]] std::uint64_t id() const noexcept { return id_; }

    bool advance_lifecycle(detail::object_header* header, lifecycle_state next);
    bool begin_cleanup_obligation(detail::object_header* header);
    [[nodiscard]] std::vector<object_info> retention_path(const detail::object_header* target);
    [[nodiscard]] object_info describe(const detail::object_header* header,
                                       const char* root_label) const noexcept;
    void release_quarantine(detail::object_header* header) noexcept;

    void shade(detail::object_header* header);
    [[nodiscard]] bool is_marked(const detail::object_header* header) const noexcept;
    [[nodiscard]] bool is_condemned(const detail::object_header* header) const noexcept;
    void check_thread() noexcept;
    void report(violation_kind kind, const detail::object_header* header) noexcept;
    void reclaim(detail::object_header* header) noexcept;

    bool enter_collection(bool& refused) noexcept;
    void start_cycle();
    // Does one unit of work. Returns false when the cycle ended.
    bool do_unit(tracer& t, step_result& r);
    void finalize(tracer& t, step_result& r);
    void finish_cycle(bool completed);
    void abandon_cycle() noexcept;
    void run_to_cycle_end();

    struct impl;
    std::unique_ptr<impl> impl_;
    std::uint64_t id_ = 0; // unique per domain instance in the process
    phase phase_ = phase::idle;
};

namespace detail
{
// Internal bridge used by the reference templates and make().
struct domain_access
{
    static domain::pending_allocation begin_allocation(domain& d, const type_descriptor& type)
    {
        return d.begin_allocation(type);
    }
    static void abort_allocation(domain& d, domain::pending_allocation& p) noexcept { d.abort_allocation(p); }
    static object_header* publish(domain& d, domain::pending_allocation& p) noexcept { return d.publish(p); }

    static void on_store(object_header* target) { target->owner->on_store(target); }
    static void link_root(root_node* node) { node->header->owner->link_root(node); }
    static void link_new_root(root_node* node) noexcept { node->header->owner->link_new_root(node); }
    static void unlink_root(root_node* node) noexcept { node->header->owner->unlink_root(node); }
    static object_header* resolve_weak(domain* d, std::uint64_t id, std::uint32_t slot, std::uint32_t generation)
    {
        return domain::resolve_weak(d, id, slot, generation);
    }

    static bool advance_lifecycle(object_header* h, lifecycle_state next)
    {
        return h->owner->advance_lifecycle(h, next);
    }
    static bool begin_cleanup_obligation(object_header* h) { return h->owner->begin_cleanup_obligation(h); }
    static std::vector<object_info> retention_path(const object_header* h) { return h->owner->retention_path(h); }
    static std::uint64_t id(const domain& d) noexcept { return d.id_; }
    static void report(domain& d, violation_kind kind, const object_header* h) noexcept { d.report(kind, h); }
};

// Debug checks for GC types created outside gc::make. gc::make opens a
// construction scope around the constructor; the gc::managed constructor
// claims it. Thread-local, nestable.
GC_API void push_construction(domain& d, void* object, std::size_t size);
GC_API void pop_construction() noexcept;

struct construction_scope
{
    construction_scope(domain& d, void* object, std::size_t size) { push_construction(d, object, size); }
    ~construction_scope() { pop_construction(); }
    construction_scope(const construction_scope&) = delete;
    construction_scope& operator=(const construction_scope&) = delete;
};
} // namespace detail
} // namespace gc

#if defined(_MSC_VER)
#    pragma warning(pop)
#endif
