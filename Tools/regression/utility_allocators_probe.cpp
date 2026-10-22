// Production Utility allocators, linked without RenderEngine or a graphics API.
// GNU --wrap observes the real vendored mimalloc; no allocator shim is used.
#include "../../Engine/Utility_Framework/Arena.h"
#include "../../Engine/Utility_Framework/FixedPool.h"
#include "../../Engine/Utility_Framework/HeapAllocator.h"
#include "../../Engine/Utility_Framework/LinearArena.h"
#include "../../Engine/Utility_Framework/StackArena.h"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <new>
#include <optional>
#include <span>
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace allocation_probe
{
    struct counts
    {
        uint64_t standard_calls{}, standard_bytes{}, mimalloc_calls{}, mimalloc_bytes{};
    };
    std::atomic<bool> enabled{};
    counts observed;
    void begin()
    {
        observed = {};
        enabled.store(true, std::memory_order_relaxed);
    }
    counts end()
    {
        enabled.store(false, std::memory_order_relaxed);
        return observed;
    }
    void standard(size_t bytes)
    {
        if (enabled.load(std::memory_order_relaxed))
        {
            ++observed.standard_calls;
            observed.standard_bytes += bytes;
        }
    }
    void mimalloc(size_t bytes)
    {
        if (enabled.load(std::memory_order_relaxed))
        {
            ++observed.mimalloc_calls;
            observed.mimalloc_bytes += bytes;
        }
    }
} // namespace allocation_probe

void* operator new(std::size_t bytes)
{
    allocation_probe::standard(bytes);
    if (void* memory = std::malloc(bytes ? bytes : 1))
    {
        return memory;
    }
    throw std::bad_alloc();
}
void* operator new[](std::size_t bytes)
{
    return ::operator new(bytes);
}
[[gnu::noinline]] void operator delete(void* memory) noexcept
{
    std::free(memory);
}
[[gnu::noinline]] void operator delete[](void* memory) noexcept
{
    std::free(memory);
}
[[gnu::noinline]] void operator delete(void* memory, std::size_t) noexcept
{
    std::free(memory);
}
[[gnu::noinline]] void operator delete[](void* memory, std::size_t) noexcept
{
    std::free(memory);
}
void* operator new(std::size_t bytes, std::align_val_t alignment)
{
    allocation_probe::standard(bytes);
    void* memory{};
    if (posix_memalign(&memory, static_cast<size_t>(alignment), bytes ? bytes : 1) == 0)
    {
        return memory;
    }
    throw std::bad_alloc();
}
void* operator new[](std::size_t bytes, std::align_val_t alignment)
{
    return ::operator new(bytes, alignment);
}
[[gnu::noinline]] void operator delete(void* memory, std::align_val_t) noexcept
{
    std::free(memory);
}
[[gnu::noinline]] void operator delete[](void* memory, std::align_val_t) noexcept
{
    std::free(memory);
}
[[gnu::noinline]] void operator delete(void* memory, std::size_t, std::align_val_t) noexcept
{
    std::free(memory);
}
[[gnu::noinline]] void operator delete[](void* memory, std::size_t, std::align_val_t) noexcept
{
    std::free(memory);
}

extern "C" void* __real_mi_malloc(size_t);
extern "C" void* __real_mi_malloc_aligned(size_t, size_t);
extern "C" void* __wrap_mi_malloc(size_t bytes)
{
    allocation_probe::mimalloc(bytes);
    return __real_mi_malloc(bytes);
}
extern "C" void* __wrap_mi_malloc_aligned(size_t bytes, size_t alignment)
{
    allocation_probe::mimalloc(bytes);
    return __real_mi_malloc_aligned(bytes, alignment);
}

namespace
{
    uint64_t checks{};
    constexpr uint64_t repetitions = 128;
    struct measurement
    {
        const char* name{};
        allocation_probe::counts allocations;
    };
    std::array<measurement, 7> measurements;
    size_t measurementCount{};

    void require(bool value, const char* message)
    {
        ++checks;
        if (!value)
        {
            throw std::runtime_error(message);
        }
    }
    template<class Exception, class Callback>
    void rejects(Callback&& callback, const char* message)
    {
        bool rejected{};
        try
        {
            callback();
        }
        catch (const Exception&)
        {
            rejected = true;
        }
        require(rejected, message);
    }
    bool aligned(const void* value, size_t alignment)
    {
        return value && reinterpret_cast<uintptr_t>(value) % alignment == 0;
    }
    template<class Callback>
    void measure(const char* name, Callback&& callback)
    {
        callback(); // Explicit warmup, outside allocation observation.
        allocation_probe::begin();
        for (uint64_t i = 0; i < repetitions; ++i)
        {
            callback();
        }
        const auto counts = allocation_probe::end();
        require(counts.standard_calls == 0 && counts.mimalloc_calls == 0,
                "A bounded/warmed utility allocator requested upstream storage.");
        measurements[measurementCount++] = {name, counts};
    }

    void verify_observer_and_heap()
    {
        allocation_probe::begin();
        void* normal = ::operator new(17);
        void* overAligned = ::operator new(128, std::align_val_t{64});
        void* raw = mi_malloc(41);
        void* heap = mi_malloc_aligned(128, 64);
        const auto counts = allocation_probe::end();
        require(normal && overAligned && raw && heap, "Allocation observer setup failed.");
        require(counts.standard_calls == 2 && counts.standard_bytes == 145 && counts.mimalloc_calls == 2 &&
                    counts.mimalloc_bytes == 169,
                "Allocation observer missed actual new/mimalloc entry points.");
        require(mi_check_owned(raw) && mi_check_owned(heap), "The probe must use real mimalloc storage.");
        ::operator delete(normal);
        ::operator delete(overAligned, std::align_val_t{64});
        mi_free(raw);
        mi_free(heap);

        struct alignas(4096) item
        {
            std::byte bytes[4096];
        };
        ce::heap_allocator<item> allocator;
        auto* memory = allocator.allocate(2);
        require(aligned(memory, alignof(item)) && mi_check_owned(memory), "Heap allocator lost alignment or provider.");
        std::memset(memory, 0xA5, sizeof(item) * 2);
        allocator.deallocate(memory, 2);
        rejects<std::bad_array_new_length>([&] { (void)allocator.allocate((std::numeric_limits<size_t>::max)()); },
                                           "Heap array length overflow was accepted.");
    }

    void verify_retained_arena()
    {
        ce::arena_resource arena;
        require(arena.stats().blockCount == 0 && arena.stats().usedBytes == 0,
                "Arena construction eagerly allocated storage.");
        for (const size_t alignment : {size_t{1}, size_t{2}, size_t{8}, size_t{64}, size_t{4096}})
        {
            auto* memory = static_cast<std::byte*>(arena.allocate(19, alignment));
            require(aligned(memory, alignment), "Retained arena alignment failed.");
            std::memset(memory, 0x5A, 19);
        }
        auto* first = static_cast<std::byte*>(arena.allocate(96, 32));
        std::memset(first, 0x47, 96);
        (void)arena.allocate(200000, 4096);
        for (size_t i = 0; i < 96; ++i)
        {
            require(first[i] == std::byte{0x47}, "Growing an arena moved or overwrote an existing allocation.");
        }
        const auto before = arena.stats();
        rejects<std::bad_alloc>([&] { (void)arena.allocate(1, 0); }, "Zero alignment was accepted.");
        rejects<std::bad_alloc>([&] { (void)arena.allocate(1, 3); }, "Non-power-of-two alignment was accepted.");
        rejects<std::bad_alloc>([&] { (void)arena.allocate((std::numeric_limits<size_t>::max)(), 64); },
                                "Arena byte overflow was accepted.");
        require(arena.stats().usedBytes == before.usedBytes && arena.stats().blockCount == before.blockCount,
                "Rejected arena allocations mutated storage.");
        arena.reset();
        require(arena.stats().usedBytes == 0 && arena.stats().activeBlocks == 0 &&
                    arena.stats().retainedBytes == before.retainedBytes &&
                    arena.stats().blockCount == before.blockCount,
                "Arena reset discarded retained storage or left active counters.");
        void* fast{};
        require(!arena.try_allocate(7, 8, fast), "An inactive block must enter the accounted slow path first.");
        require(arena.allocate(0, 1) != nullptr, "Zero-sized retained arena allocation did not reserve a byte.");
        require(arena.try_allocate(7, 8, fast) && aligned(fast, 8), "Active retained-block fast path failed.");

        measure("retained_arena_warm",
                [&]
                {
                    arena.reset();
                    for (size_t i = 0; i < 96; ++i)
                    {
                        const size_t alignment = size_t{1} << (i % 7);
                        auto* memory = static_cast<std::byte*>(arena.allocate(7 + i % 11, alignment));
                        require(aligned(memory, alignment), "Warm arena allocation lost alignment.");
                        memory[0] = std::byte{0x31};
                    }
                });
    }

    struct tracked
    {
        int id;
        int* live;
        std::array<int, 16>* order;
        size_t* destroyed;
        tracked(int value, int& count, std::array<int, 16>& sequence, size_t& size)
            : id(value), live(&count), order(&sequence), destroyed(&size)
        {
            ++*live;
        }
        ~tracked() noexcept
        {
            --*live;
            (*order)[(*destroyed)++] = id;
        }
    };

    void verify_object_arena()
    {
        int live{};
        size_t destroyed{};
        std::array<int, 16> order{};
        ce::arena_objects objects;
        auto* first = objects.emplace<tracked>(1, live, order, destroyed);
        const auto firstAddress = reinterpret_cast<uintptr_t>(first);
        objects.emplace<tracked>(2, live, order, destroyed);
        struct alignas(4096) aligned_object
        {
            std::byte bytes[4096]{};
        };
        require(aligned(objects.emplace<aligned_object>(), 4096), "Object arena lost over-alignment.");
        int failedDestructors{};
        struct throws_on_construction
        {
            int* destructors;
            explicit throws_on_construction(int& count, bool fail) : destructors(&count)
            {
                if (fail)
                {
                    throw std::runtime_error("injected constructor failure");
                }
            }
            ~throws_on_construction() noexcept { ++*destructors; }
        };
        rejects<std::runtime_error>([&] { objects.emplace<throws_on_construction>(failedDestructors, true); },
                                    "Object arena swallowed construction failure.");
        require(live == 2, "Construction failure changed existing object lifetime.");
        const auto retained = objects.stats().retainedBytes;
        objects.reset();
        require(live == 0 && destroyed == 2 && order[0] == 2 && order[1] == 1,
                "Object arena did not destroy captures in reverse order.");
        require(failedDestructors == 0, "Object arena registered a destructor for a failed constructor.");
        require(objects.stats().retainedBytes == retained && objects.stats().usedBytes == 0,
                "Object arena reset discarded retained blocks.");
        require(reinterpret_cast<uintptr_t>(objects.emplace<tracked>(3, live, order, destroyed)) == firstAddress,
                "Object arena failed to reuse its first address.");
        objects.reset();

        int copies{};
        int copyLive{};
        struct copy_failure
        {
            int* copies;
            int* live;
            copy_failure(int& count, int& instances) : copies(&count), live(&instances) { ++*live; }
            copy_failure(const copy_failure& other) : copies(other.copies), live(other.live)
            {
                if (++*copies == 2)
                {
                    throw std::runtime_error("injected copy failure");
                }
                ++*live;
            }
            ~copy_failure() noexcept { --*live; }
        };
        {
            const std::array source{copy_failure(copies, copyLive), copy_failure(copies, copyLive),
                                    copy_failure(copies, copyLive)};
            rejects<std::runtime_error>([&] { objects.copy(std::span<const copy_failure>{source}); },
                                        "Object arena swallowed array-copy failure.");
            require(copyLive == 3, "Failed array copy leaked constructed elements.");
            objects.reset();
            require(copyLive == 3, "Reset destroyed a failed array copy twice.");
        }
        require(copyLive == 0, "Array-copy fixture leaked live source elements.");

        const std::array values{1, 3, 5, 7};
        const auto copied = objects.copy(std::span<const int>{values});
        require(copied.data() != values.data() && copied.size() == values.size() && copied[2] == 5,
                "Object arena trivial copy borrowed caller storage.");
        struct counter
        {
            int* live;
            explicit counter(int& value) : live(&value) { ++*live; }
            ~counter() noexcept { --*live; }
        };
        measure("object_arena_warm",
                [&]
                {
                    objects.reset();
                    for (size_t i = 0; i < 64; ++i)
                    {
                        objects.emplace<counter>(live);
                    }
                    require(live == 64, "Warm object arena lost a live instance.");
                    require(objects.copy(std::span<const int>{values})[3] == 7, "Warm trivial copy failed.");
                    objects.reset();
                    require(live == 0, "Warm object arena did not destroy instances.");
                });
    }

    void verify_buffers_and_containers()
    {
        ce::arena_resource first, second;
        {
            ce::arena_buffer<int> buffer{first};
            buffer.reserve(2);
            buffer.push_back(11);
            buffer.push_back(22);
            buffer.push_back(buffer[0]); // Self-reference survives reserve growth.
            require(buffer.size() == 3 && buffer[2] == 11, "Arena buffer lost self-append during growth.");
            buffer.assign(7, buffer[1]);
            require(buffer.size() == 7 && buffer[6] == 22, "Arena buffer assign alias was invalidated by growth.");
            buffer.resize(9);
            require(buffer[7] == 0 && buffer[8] == 0, "Arena buffer resize did not initialize new elements.");
            buffer.erase_if([](int value) { return value == 0; });
            require(buffer.size() == 7, "Arena buffer erase lost retained elements.");
            buffer.clear();
            require(buffer.empty() && buffer.capacity() >= 9, "Arena buffer clear discarded capacity.");
            rejects<std::bad_array_new_length>([&] { buffer.reserve((std::numeric_limits<size_t>::max)()); },
                                               "Arena buffer count overflow was accepted.");
        }
        ce::arena_vector<uint64_t> independent;
        {
            ce::arena_vector<ce::arena_vector<uint64_t>> nested(3, &first);
            nested[1].assign(64, 123);
            require(nested[1].get_allocator().resource() == &first, "Nested vector lost its allocator domain.");
            ce::arena_map<int, ce::arena_vector<uint64_t>> groups{&first};
            groups[7].assign(32, 456);
            require(groups[7].get_allocator().resource() == &first, "Map value lost its allocator domain.");
            ce::arena_vector<uint64_t> moved{&second};
            moved = std::move(nested[1]);
            require(moved.get_allocator().resource() == &second && moved[63] == 123,
                    "Cross-arena move stole source-owned storage.");
            const auto copy = moved;
            require(!copy.get_allocator().resource() && copy[63] == 123,
                    "Plain vector copy retained a borrowed arena allocator.");
            independent = copy;
        }
        first.reset();
        second.reset();
        require(independent[63] == 123, "Plain copy did not outlive source arena reset.");

        measure("arena_buffer_warm",
                [&]
                {
                    first.reset();
                    ce::arena_buffer<uint32_t> buffer{first};
                    buffer.reserve(256);
                    for (uint32_t i = 0; i < 256; ++i)
                    {
                        buffer.push_back(i);
                    }
                    require(buffer[255] == 255, "Warm trivial buffer data mismatch.");
                });
        measure("nested_stl_arena_warm",
                [&]
                {
                    first.reset();
                    ce::arena_vector<ce::arena_vector<uint64_t>> nested(4, &first);
                    ce::arena_map<int, ce::arena_vector<uint64_t>> groups{&first};
                    for (int i = 0; i < 4; ++i)
                    {
                        nested[i].assign(16, i);
                        groups[i].assign(16, i + 4);
                        require(nested[i].get_allocator().resource() == &first &&
                                    groups[i].get_allocator().resource() == &first && groups[i][15] == uint64_t(i + 4),
                                "Warmed nested container lost arena storage or data.");
                    }
                });
    }

    template<class Arena>
    void verify_bounded_raw_arena()
    {
        std::array<std::byte, 129> bytes;
        bytes.fill(std::byte{0xA7});
        Arena arena{std::span<std::byte>{bytes}.subspan(1, 127)};
        require(arena.capacity() == 127 && arena.used_bytes() == 0, "Bounded arena initial state is incorrect.");
        auto* first = static_cast<std::byte*>(arena.allocate(3, 1));
        std::memset(first, 0x19, 3);
        require(arena.try_allocate(0, 1) != nullptr && arena.used_bytes() == 4,
                "Bounded zero-sized allocation must consume one byte.");
        const auto before = arena.used_bytes();
        const auto beforeAllocations = arena.allocation_count();
        require(!arena.try_allocate(1, 0) && !arena.try_allocate(1, 3) &&
                    !arena.try_allocate((std::numeric_limits<size_t>::max)(), 8) && !arena.try_allocate(128, 1) &&
                    !arena.try_allocate(1, size_t{1} << ((std::numeric_limits<size_t>::digits) - 1)),
                "Bounded arena accepted invalid alignment, overflow or exhaustion.");
        require(arena.used_bytes() == before && arena.allocation_count() == beforeAllocations,
                "Failed bounded allocation advanced the cursor or successful-allocation counter.");
        rejects<std::bad_alloc>([&] { (void)arena.allocate(128, 1); },
                                "Throwing bounded allocation did not report exhaustion.");
        arena.reset();
        require(arena.used_bytes() == 0 && arena.remaining_bytes() == 127 && arena.peak_bytes() >= 4,
                "Bounded reset did not rewind cursor while retaining peak.");
        require(arena.reset_count() == 1 && arena.allocation_count() == beforeAllocations,
                "Bounded reset did not preserve lifetime allocation totals.");
        auto* all = static_cast<std::byte*>(arena.allocate(127, 1));
        std::memset(all, 0x39, 127);
        require(!arena.try_allocate(1, 1) && arena.remaining_bytes() == 0, "Exact exhaustion escaped caller storage.");
        require(bytes.front() == std::byte{0xA7} && bytes.back() == std::byte{0xA7},
                "Bounded arena overwrote caller-buffer canaries.");

        alignas(4096) std::array<std::byte, 8192> alignedBytes{};
        Arena overAligned{std::span<std::byte>{alignedBytes}.subspan(1)};
        require(aligned(overAligned.allocate(32, 4096), 4096),
                "Bounded arena lost over-alignment from an unaligned base.");
        Arena empty;
        require(!empty.try_allocate(0, 1) && !empty.try_allocate(1, 1),
                "Empty bounded arena allocated upstream storage.");
    }

    void verify_linear_arena()
    {
        static_assert(!std::is_copy_constructible_v<ce::linear_arena> &&
                      !std::is_move_constructible_v<ce::linear_arena>);
        verify_bounded_raw_arena<ce::linear_arena>();
        alignas(64) std::array<std::byte, 8192> storage{};
        ce::linear_arena arena{storage};
        measure("linear_arena_bounded",
                [&]
                {
                    arena.reset();
                    for (size_t i = 0; i < 128; ++i)
                    {
                        auto* memory = static_cast<std::byte*>(arena.allocate(17, 16));
                        require(aligned(memory, 16), "Linear arena warm alignment failed.");
                        memory[16] = std::byte{0x23};
                    }
                });
    }

    void verify_stack_arena()
    {
        static_assert(!std::is_copy_constructible_v<ce::stack_arena> && !std::is_move_constructible_v<ce::stack_arena>);
        verify_bounded_raw_arena<ce::stack_arena>();
        alignas(64) std::array<std::byte, 8192> storage{}, foreignStorage{};
        ce::stack_arena arena{storage}, foreign{foreignStorage};
        (void)arena.allocate(7, 1);
        {
            auto outer = arena.checkpoint();
            static_assert(!std::is_copy_constructible_v<decltype(outer)> &&
                          !std::is_move_constructible_v<decltype(outer)>);
            (void)arena.allocate(11, 1);
            const auto outerUsed = arena.used_bytes();
            {
                auto inner = arena.checkpoint();
                (void)arena.allocate(19, 1);
                const auto innerUsed = arena.used_bytes();
                require(!arena.rewind(outer) && !foreign.rewind(inner) && !arena.try_reset(),
                        "Stack arena accepted non-top/foreign rewind or reset with live checkpoints.");
                require(arena.used_bytes() == innerUsed && outer.active() && inner.active(),
                        "Rejected checkpoint operation mutated an active scope.");
                rejects<std::logic_error>([&] { arena.reset(); }, "Stack reset invalidated an active scope.");
            }
            require(arena.used_bytes() == outerUsed, "Nested checkpoint destructor did not restore its cursor.");
            require(outer.rewind() && !outer.active() && arena.used_bytes() == 7,
                    "Explicit outer checkpoint rewind failed.");
            (void)arena.allocate(17, 1);
            const auto afterReuse = arena.used_bytes();
            require(!outer.rewind() && !arena.rewind(outer) && arena.used_bytes() == afterReuse,
                    "A stale checkpoint rewound newly reused storage.");
        }
        require(arena.used_bytes() == 24, "Inactive checkpoint destructor rewound a second time.");
        rejects<std::runtime_error>(
            [&]
            {
                auto scope = arena.checkpoint();
                (void)arena.allocate(57, 8);
                throw std::runtime_error("injected nested operation failure");
            },
            "Stack scope exception fixture failed.");
        require(arena.used_bytes() == 24, "Exception unwinding leaked stack arena cursor.");
        require(arena.try_reset() && arena.used_bytes() == 0, "Stack reset rejected an idle arena.");
        measure("stack_arena_nested",
                [&]
                {
                    auto outer = arena.checkpoint();
                    (void)arena.allocate(128, 64);
                    for (size_t i = 0; i < 32; ++i)
                    {
                        auto inner = arena.checkpoint();
                        require(aligned(arena.allocate(97, 32), 32), "Stack nested allocation alignment failed.");
                    }
                    require(arena.used_bytes() == 128, "Nested stack iterations did not reuse their storage.");
                });
        require(arena.used_bytes() == 0, "Measured outer stack scope did not restore its cursor.");
    }

    struct alignas(64) pool_item
    {
        int* live;
        int value;
        explicit pool_item(int& count, int id, bool fail = false) : live(&count), value(id)
        {
            if (fail)
            {
                throw std::runtime_error("injected pool construction failure");
            }
            ++*live;
        }
        ~pool_item() noexcept { --*live; }
    };

    void verify_fixed_pool()
    {
        static_assert(!std::is_copy_constructible_v<ce::fixed_pool<pool_item, 8>> &&
                      !std::is_move_constructible_v<ce::fixed_pool<pool_item, 8>>);
        int live{};
        {
            ce::fixed_pool<pool_item, 8> pool;
            std::array<pool_item*, 8> items{};
            for (size_t i = 0; i < items.size(); ++i)
            {
                items[i] = pool.try_emplace(live, int(i));
                require(aligned(items[i], alignof(pool_item)) && pool.owns(items[i]) && items[i]->value == int(i),
                        "Fixed pool construction/alignment/ownership failed.");
            }
            require(live == 8 && pool.size() == 8 && pool.available() == 0 && !pool.try_emplace(live, 99),
                    "Fixed pool did not enforce capacity.");
            rejects<std::bad_alloc>([&] { (void)pool.emplace(live, 99); }, "Fixed pool exhaustion did not throw.");
            pool_item external(live, 100);
            auto* interior = reinterpret_cast<pool_item*>(reinterpret_cast<std::byte*>(items[0]) + 1);
            require(!pool.destroy(nullptr) && !pool.destroy(&external) && !pool.destroy(interior) &&
                        !pool.owns(&external) && !pool.owns(interior),
                    "Fixed pool accepted a foreign/interior/nonexistent object.");
            require(pool.destroy(items[3]) && !pool.owns(items[3]) && !pool.destroy(items[3]),
                    "Fixed pool did not reject a duplicate release.");
            const auto available = pool.available();
            rejects<std::runtime_error>([&] { (void)pool.emplace(live, 33, true); },
                                        "Fixed pool swallowed constructor failure.");
            require(pool.available() == available && pool.size() == 7 && live == 8,
                    "Throwing pool constructor consumed a free slot or published a live object.");
            auto* reused = pool.emplace(live, 33);
            require(reused == items[3] && reused->value == 33 && pool.owns(reused),
                    "Fixed pool did not reuse its released slot.");
            pool.clear();
            require(pool.size() == 0 && pool.available() == 8 && live == 1,
                    "Fixed pool clear did not destroy every live object.");
            (void)pool.emplace(live, 8);
            (void)pool.emplace(live, 9);
        }
        require(live == 0, "Fixed pool destruction leaked outstanding objects.");
        {
            // Cross a bitmap word boundary with objects smaller than a free-list
            // link. The slot layout must not assume sizeof(T) == sizeof(slot).
            ce::fixed_pool<uint8_t, 65> small;
            std::array<uint8_t*, 65> values{};
            for (size_t i = 0; i < values.size(); ++i)
            {
                values[i] = small.emplace(static_cast<uint8_t>(i));
                require(small.owns(values[i]) && *values[i] == i, "Small fixed-pool slot/bitmap mismatch.");
            }
            require(small.destroy(values[64]) && small.destroy(values[0]) && small.owns(values[63]),
                    "Fixed-pool release corrupted an adjacent bitmap word.");
            small.clear();
            require(small.available() == 65, "Small fixed-pool clear lost capacity.");
        }
        {
            struct immutable
            {
                const int value;
                int* live;
                immutable(int id, int& count) : value(id), live(&count) { ++*live; }
                ~immutable() noexcept { --*live; }
            };
            ce::fixed_pool<immutable, 1> single;
            for (int i = 0; i < 64; ++i)
            {
                auto* value = single.emplace(i, live);
                require(value->value == i && live == 1 && single.destroy(value) && live == 0,
                        "Fixed-pool union reuse failed for a const-member payload.");
            }
        }
        ce::fixed_pool<pool_item, 64> pool;
        std::array<pool_item*, 64> objects{};
        measure("fixed_pool_reuse",
                [&]
                {
                    for (size_t i = 0; i < objects.size(); ++i)
                    {
                        objects[i] = pool.emplace(live, int(i));
                    }
                    require(live == 64 && pool.available() == 0, "Fixed pool warm fill lost an object.");
                    for (size_t i = 0; i < objects.size(); ++i)
                    {
                        const size_t shuffled = (i * 17) % objects.size();
                        require(pool.destroy(objects[shuffled]), "Fixed pool shuffled free failed.");
                    }
                    require(live == 0 && pool.available() == 64, "Fixed pool warm reuse leaked storage.");
                });
    }

    int verify_stack_lifetime_violation(const char* mode)
    {
        std::set_terminate([] { std::_Exit(73); });
        std::array<std::byte, 256> storage{};
        if (std::strcmp(mode, "--death-arena-before-scope") == 0)
        {
            std::optional<ce::stack_arena> arena{std::in_place, std::span<std::byte>{storage}};
            auto scope = arena->checkpoint();
            arena.reset(); // The public contract rejects an owner dying first.
            return 3;
        }
        if (std::strcmp(mode, "--death-outer-before-inner") == 0)
        {
            ce::stack_arena arena{storage};
            alignas(ce::stack_arena::scope) std::array<std::byte, sizeof(ce::stack_arena::scope)> outerMemory{};
            auto* outer = ::new (outerMemory.data()) ce::stack_arena::scope(arena.checkpoint());
            auto inner = arena.checkpoint();
            std::destroy_at(outer); // Reject a dangling checkpoint chain.
            return 3;
        }
        return 2;
    }
} // namespace

int main(int argc, char** argv)
{
    if (argc == 2)
    {
        return verify_stack_lifetime_violation(argv[1]);
    }
    try
    {
        verify_observer_and_heap();
        verify_retained_arena();
        verify_object_arena();
        verify_buffers_and_containers();
        verify_linear_arena();
        verify_stack_arena();
        verify_fixed_pool();
        require(measurementCount == measurements.size(), "Missing utility allocator allocation measurement.");
        std::printf("{\"status\":\"passed\",\"checks\":%llu,\"iterations_per_measurement\":%llu,\"measurements\":[",
                    static_cast<unsigned long long>(checks), static_cast<unsigned long long>(repetitions));
        for (size_t i = 0; i < measurementCount; ++i)
        {
            const auto& row = measurements[i];
            std::printf("%s{\"name\":\"%s\",\"standard_calls\":%llu,\"standard_bytes\":%llu,"
                        "\"mimalloc_calls\":%llu,\"mimalloc_bytes\":%llu}",
                        i ? "," : "", row.name, static_cast<unsigned long long>(row.allocations.standard_calls),
                        static_cast<unsigned long long>(row.allocations.standard_bytes),
                        static_cast<unsigned long long>(row.allocations.mimalloc_calls),
                        static_cast<unsigned long long>(row.allocations.mimalloc_bytes));
        }
        std::printf("]}\n");
        return 0;
    }
    catch (const std::exception& exception)
    {
        allocation_probe::end();
        std::fprintf(stderr, "UTILITY_ALLOCATORS_FAILED after %llu checks: %s\n",
                     static_cast<unsigned long long>(checks), exception.what());
        return 1;
    }
}
