import std;
import Creator.Core.BoundedMemory;

namespace
{
    unsigned checks{};
    void require(bool condition, std::string_view label)
    {
        ++checks;
        if (!condition)
        {
            throw std::runtime_error(std::string(label));
        }
    }

    struct alignas(64) item
    {
        static inline int live{};
        int value;
        std::array<std::byte, 64 - sizeof(int)> padding{};
        explicit item(int input) : value(input)
        {
            if (input < 0)
            {
                throw std::runtime_error("payload construction failed");
            }
            ++live;
        }
        ~item() noexcept { --live; }
    };

    template<class Arena>
    void verify_storage()
    {
        static_assert(!std::is_copy_constructible_v<Arena> && !std::is_move_constructible_v<Arena>);
        alignas(64) std::array<std::byte, 256> storage{};
        Arena arena{storage};
        require(arena.capacity() == 256 && arena.used_bytes() == 0, "initial storage");
        require(arena.try_allocate(0, 1) == storage.data(), "zero request consumes one byte");
        require(arena.used_bytes() == 1, "zero request cursor");
        const auto before = arena.used_bytes();
        const auto count = arena.allocation_count();
        require(!arena.try_allocate(8, 0) && !arena.try_allocate(8, 3), "invalid alignment");
        require(arena.used_bytes() == before && arena.allocation_count() == count, "failed allocation is transactional");
        auto* aligned = arena.try_allocate(64, 64);
        require(aligned && reinterpret_cast<std::uintptr_t>(aligned) % 64 == 0, "aligned request");
        require(!arena.try_allocate((std::numeric_limits<std::size_t>::max)(), 64), "oversized request");
        bool exhausted{};
        try
        {
            (void)arena.allocate(1024);
        }
        catch (const std::bad_alloc&)
        {
            exhausted = true;
        }
        require(exhausted, "throwing exhaustion");
        const auto peak = arena.peak_bytes();
        const auto total = arena.allocation_count();
        arena.reset();
        require(arena.used_bytes() == 0 && arena.remaining_bytes() == 256, "reset cursor");
        require(arena.peak_bytes() == peak && arena.allocation_count() == total && arena.reset_count() == 1, "reset preserves counters");
        require(arena.try_allocate(256, 1) == storage.data(), "exact capacity reuse");
        require(!arena.try_allocate(1, 1), "full storage");
        Arena empty;
        require(!empty.try_allocate(1) && empty.capacity() == 0, "unbacked storage");
    }

    void verify_scopes()
    {
        std::array<std::byte, 256> storage{}, other_storage{};
        ce::stack_arena arena{storage}, foreign{other_storage};
        (void)arena.allocate(8, 1);
        auto outer = arena.checkpoint();
        (void)arena.allocate(16, 1);
        {
            auto inner = arena.checkpoint();
            (void)arena.allocate(32, 1);
            const auto used = arena.used_bytes();
            require(!arena.rewind(outer) && arena.used_bytes() == used, "out of order rewind rejected");
            require(!foreign.rewind(inner) && inner.active(), "foreign rewind rejected");
            require(!arena.try_reset() && arena.used_bytes() == used, "active checkpoint blocks reset");
            bool rejected{};
            try
            {
                arena.reset();
            }
            catch (const std::logic_error&)
            {
                rejected = true;
            }
            require(rejected, "throwing active reset");
        }
        require(arena.used_bytes() == 24 && arena.rewind_count() == 1, "nested RAII rollback");
        require(outer.rewind() && !outer.active(), "explicit outer rollback");
        require(arena.used_bytes() == 8 && arena.rewind_count() == 2, "outer checkpoint cursor");
        require(!outer.rewind() && !arena.rewind(outer), "checkpoint cannot be reused");
        require(arena.try_reset() && arena.used_bytes() == 0, "reset after checkpoints");
    }

    void verify_pool()
    {
        static_assert(!std::is_copy_constructible_v<ce::fixed_pool<item, 2>>);
        static_assert(!std::is_move_constructible_v<ce::fixed_pool<item, 2>>);
        {
            ce::fixed_pool<item, 2> pool;
            require(pool.empty() && pool.capacity() == 2 && pool.available() == 2, "initial pool");
            auto* first = pool.emplace(1);
            auto* second = pool.try_emplace(2);
            require(first && second && pool.full() && item::live == 2, "pool full");
            require(reinterpret_cast<std::uintptr_t>(first) % 64 == 0 && reinterpret_cast<std::uintptr_t>(second) % 64 == 0, "pool alignment");
            require(!pool.try_emplace(-1) && item::live == 2, "full pool skips constructor");
            bool exhausted{};
            try
            {
                (void)pool.emplace(3);
            }
            catch (const std::bad_alloc&)
            {
                exhausted = true;
            }
            require(exhausted, "throwing pool exhaustion");
            item foreign(4);
            require(!pool.destroy(nullptr) && !pool.destroy(&foreign), "foreign release rejected");
            auto* interior = reinterpret_cast<item*>(reinterpret_cast<std::byte*>(first) + 1);
            require(!pool.destroy(interior) && pool.size() == 2, "interior release rejected");
            require(pool.destroy(first) && !pool.owns(first), "release live slot");
            require(!pool.destroy(first) && pool.size() == 1, "double release rejected");
            bool failed{};
            try
            {
                (void)pool.emplace(-1);
            }
            catch (const std::runtime_error&)
            {
                failed = true;
            }
            require(failed && pool.size() == 1 && pool.available() == 1, "constructor failure restores free list");
            auto* reused = pool.emplace(5);
            require(reused == first && reused->value == 5 && pool.owns(second), "slot reuse");
            pool.clear();
            require(pool.empty() && pool.available() == 2 && item::live == 1, "clear destroys pool payloads only");
            pool.clear();
            require(pool.empty() && item::live == 1, "clear is idempotent");
            (void)pool.emplace(6);
        }
        require(item::live == 0, "pool destructor destroys remaining payloads");
        ce::fixed_pool<unsigned char, 65> bitmap;
        std::array<unsigned char*, 65> values{};
        for (std::size_t i = 0; i < values.size(); ++i)
        {
            values[i] = bitmap.emplace(static_cast<unsigned char>(i));
        }
        require(bitmap.full() && bitmap.owns(values[64]), "bitmap across word boundary");
        require(bitmap.destroy(values[63]) && bitmap.destroy(values[64]) && bitmap.available() == 2, "release across word boundary");
        bitmap.clear();
        require(bitmap.empty() && bitmap.available() == 65, "bitmap clear");
    }

    int lifetime_violation(std::string_view mode)
    {
        std::set_terminate([] { std::_Exit(73); });
        std::array<std::byte, 256> storage{};
        if (mode == "--death-arena-before-scope")
        {
            std::optional<ce::stack_arena> arena{std::in_place, std::span<std::byte>{storage}};
            auto checkpoint = arena->checkpoint();
            arena.reset();
            return 3;
        }
        if (mode == "--death-outer-before-inner")
        {
            ce::stack_arena arena{storage};
            alignas(ce::stack_arena::scope) std::array<std::byte, sizeof(ce::stack_arena::scope)> memory{};
            auto* outer = ::new (memory.data()) ce::stack_arena::scope(arena.checkpoint());
            auto inner = arena.checkpoint();
            std::destroy_at(outer);
            return 3;
        }
        return 2;
    }
}

int main(int argc, char** argv)
{
    if (argc == 2)
    {
        return lifetime_violation(argv[1]);
    }
    try
    {
        verify_storage<ce::linear_arena>();
        verify_storage<ce::stack_arena>();
        verify_scopes();
        verify_pool();
        std::cout << "BOUNDED_MEMORY_MODULES_OK checks=" << checks << '\n';
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
