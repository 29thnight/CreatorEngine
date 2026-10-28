#if defined(CE_CONTRACT_STANDALONE)
#include "../build/ModuleAdapters/Creator.Core.Ownership.h"
#include "../build/ModuleAdapters/Creator.Core.LifetimeToken.h"
#include "../build/ModuleAdapters/Creator.Core.HeapAllocator.h"
#include "../build/ModuleAdapters/Creator.Core.Arena.h"
#else
import std;
import Creator.Core.Ownership;
import Creator.Core.LifetimeToken;
import Creator.Core.HeapAllocator;
import Creator.Core.Arena;
#endif
#include "memory_ownership_probe_types.h"

struct arena_probe_object
{
    arena_probe_object(std::vector<int>* order, int id) noexcept : order_(order), id_(id) {}
    ~arena_probe_object() noexcept { order_->push_back(id_); }
    std::vector<int>* order_;
    int id_;
};

int main()
{
    unsigned checks{};
    const auto require = [&](bool value, const char* message)
    {
        ++checks;
        if (!value)
        {
            throw std::runtime_error(message);
        }
    };
    try
    {
        int deaths = 0;
        {
            auto owner = own::make_unique<memory_probe_object>(&deaths);
            auto moved = std::move(owner);
            require(!owner && bool(moved), "Exclusive move");
            moved.reset();
            require(deaths == 1 && !moved, "Exclusive reset");
            moved.reset();
            require(deaths == 1, "Repeated reset");
        }
        {
            auto owner = own::make_shared<memory_probe_object>(&deaths);
            own::weak_owner<memory_probe_object> weak(owner);
            std::unordered_map<own::object_id, int> identities;
            identities.emplace(own::identity(owner), 17);
            require(identities.at(own::identity(owner)) == 17, "Ownership hash specialization exported");
            auto token = token_from_peer(owner);
            ce::weak_lifetime_token erased_weak(token);
            require(owner.use_count() == 2 && token.use_count() == 2, "Shared peer/token count");
            auto token_copy = token;
            reset_from_peer(owner);
            require(!owner && !weak.expired() && !erased_weak.expired(),
                    "Tokens retain object across translation units");
            token.reset();
            require(deaths == 1 && token_copy.use_count() == 1, "Copied token owns lifetime");
            {
                auto locked = erased_weak.lock();
                require(bool(locked) && locked.use_count() == 2, "Erased weak lock");
            }
            token_copy.reset();
            require(deaths == 2 && weak.expired() && erased_weak.expired(), "Last owner destroys exactly once");
            require(!erased_weak.lock(), "Expired lock empty");
        }
        ce::stl_heap_allocator<std::uint64_t> heap;
        auto* storage = heap.allocate(8);
        require(reinterpret_cast<std::uintptr_t>(storage) % alignof(std::uint64_t) == 0, "Heap alignment");
        std::thread free_on_other_thread([&] { heap.deallocate(storage, 8); });
        free_on_other_thread.join();
        bool rejected = false;
        try
        {
            (void)heap.allocate((std::numeric_limits<std::size_t>::max)());
        }
        catch (const std::bad_array_new_length&)
        {
            rejected = true;
        }
        require(rejected, "Heap count overflow rejected");
        ce::arena_resource resource;
        for (const auto alignment : {std::size_t{1}, std::size_t{8}, std::size_t{64}, std::size_t{256}})
        {
            auto* value = resource.allocate(73, alignment);
            require(reinterpret_cast<std::uintptr_t>(value) % alignment == 0, "Arena alignment");
        }
        const auto retained = resource.stats().retained_bytes;
        const auto blocks = resource.stats().block_count;
        resource.reset();
        require(resource.stats().used_bytes == 0 && resource.stats().active_blocks == 0, "Arena reset counters");
        (void)resource.allocate(73, 256);
        require(resource.stats().retained_bytes == retained && resource.stats().block_count == blocks,
                "Retained block reuse");
        rejected = false;
        try
        {
            (void)resource.allocate(1, 3);
        }
        catch (const std::bad_alloc&)
        {
            rejected = true;
        }
        require(rejected, "Invalid alignment rejected");
        std::vector<int> order;
        order.reserve(4);
        {
            ce::arena_objects objects;
            objects.emplace<arena_probe_object>(&order, 1);
            objects.emplace<arena_probe_object>(&order, 2);
            objects.reset();
            require(order == std::vector<int>{2, 1}, "Reverse destruction order");
            objects.reset();
            require(order.size() == 2, "Repeated arena reset does not redestroy");
        }
        {
            ce::arena_buffer<int> values(resource);
            values.push_back(9);
            for (int i = 0; i < 64; ++i)
            {
                values.push_back(values[0]);
            }
            require(values.size() == 65 && values[64] == 9, "Self append survives buffer growth");
        }
        {
            ce::arena_vector<int> values{ce::arena_allocator<int>(&resource)};
            values.push_back(3);
            auto copy = values;
            require(copy.get_allocator().resource() == nullptr && copy[0] == 3, "Container copy detaches source arena");
        }
        std::cout << "MEMORY_OWNERSHIP_OK checks=" << checks << '\n';
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
