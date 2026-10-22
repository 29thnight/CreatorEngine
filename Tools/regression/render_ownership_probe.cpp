#include "../../Engine/Utility_Framework/LifetimeToken.h"
#include <atomic>
#include <thread>
#include <stdexcept>
#include <iostream>
#include <crtdbg.h>
namespace
{
    std::atomic<unsigned> destroyed{}, allocations{};
    struct Payload
    {
        ~Payload() { ++destroyed; }
    };
    void Check(bool ok)
    {
        if (!ok)
        {
            throw std::runtime_error("lifetime token check failed");
        }
    }
} // namespace
int main()
{
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
    try
    {
        struct AllocationStats
        {
            unsigned created{}, freed{};
        } stats;
        const own::allocator_ref allocator{&stats,
                                           [](void* context, size_t size, size_t alignment) -> void*
                                           {
                                               ++static_cast<AllocationStats*>(context)->created;
                                               return _aligned_malloc(size, alignment);
                                           },
                                           [](void* context, void* value, size_t, size_t) noexcept
                                           {
                                               ++static_cast<AllocationStats*>(context)->freed;
                                               _aligned_free(value);
                                           }};
        {
            auto checked = own::allocate_shared<Payload>(allocator);
            ce::lifetime_token held = checked;
            ce::weak_lifetime_token observer = held;
            for (unsigned i = 0; i < 128; ++i)
            {
                auto copy = held;
                auto locked = observer.lock();
                Check(copy && locked && stats.created == 1);
            }
            checked.reset();
            held.reset();
            Check(observer.expired() && destroyed == 1 && stats.freed == 0);
        }
        Check(stats.created == 1 && stats.freed == 1);
        destroyed = 0;
        auto owner = own::make_shared<Payload>();
        ce::lifetime_token pending = owner;
        ce::weak_lifetime_token weak = pending;
        ce::weak_lifetime_token direct = owner;
        owner.reset();
        Check(!weak.expired() && !direct.expired());
        auto copied = pending;
        auto moved = std::move(copied);
        Check(!copied && moved && moved.use_count() == 2);
        copied = moved;
        copied = copied;
        copied = std::move(copied);
        Check(copied && copied.use_count() == 3);
        copied.reset();
        std::thread worker(
            [hold = std::move(moved)]() mutable
            {
                Check(bool(hold));
                hold.reset();
            });
        pending.reset();
        worker.join();
        Check(destroyed == 1 && weak.expired() && direct.expired() && !weak.lock());
        auto empty = std::move(pending);
        Check(!empty);
        auto unique = own::make_unique<Payload>();
        ce::lifetime_token fromUnique = own::make_shared<Payload>();
        auto root = weak.lock();
        Check(!root);
        unique.reset();
        fromUnique.reset();
        Check(destroyed == 3);
        unsigned borrowedDestroyed{};
        struct BorrowPayload
        {
            unsigned* destroyed;
            ~BorrowPayload() { ++*destroyed; }
        };
        auto borrowedOwner = own::make_shared<BorrowPayload>(&borrowedDestroyed);
        own::weak_owner<BorrowPayload> borrowedObserver = borrowedOwner;
        const auto& immediate = borrowedOwner;
        for (unsigned i = 0; i < 4096; ++i)
        {
            const auto view = immediate.borrow();
            Check(view && view->destroyed == &borrowedDestroyed && borrowedOwner.use_count() == 1);
        }
        // A value capture of a reference alias retains the owner, not the alias.
        auto callback = [retained = immediate]() mutable
        {
            Check(retained && *retained->destroyed == 0);
            retained.reset();
        };
        Check(borrowedOwner.use_count() == 2);
        borrowedOwner.reset();
        Check(!borrowedObserver.expired() && borrowedDestroyed == 0);
        std::thread callbackWorker(std::move(callback));
        callbackWorker.join();
        Check(borrowedObserver.expired() && borrowedDestroyed == 1);
        std::cout << "RENDER_OWNERSHIP_OK inlineBytes=" << sizeof(ce::lifetime_token)
                  << " strongWeakThreadLifetime=true tokenAllocations=0 borrowedQueries=true callbackPins=true\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
