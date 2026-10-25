#include "../../Engine/Utility_Framework/Reflection/TypeTrait.h"
#include <cstdlib>
#include <iostream>
#include <thread>

bool InsertGuidFromPeer(HashedGuid);
void EraseGuidFromPeer(HashedGuid);
HashedGuid MakeGuidFromPeer();

namespace
{
    thread_local bool countAllocations = false;
    thread_local size_t allocations = 0;

    void Require(bool condition, const char* message)
    {
        if (!condition)
        {
            std::cerr << message << '\n';
            std::exit(1);
        }
    }
}

void* operator new(size_t size)
{
    if (countAllocations) ++allocations;
    if (void* result = std::malloc(size ? size : 1)) return result;
    throw std::bad_alloc{};
}
void operator delete(void* value) noexcept { std::free(value); }
void operator delete(void* value, size_t) noexcept { std::free(value); }

int main()
{
    using Registry = TypeTrait::GUIDCreator;
    const HashedGuid reserved{0x12345678};
    Require(Registry::InsertGUID(reserved), "initial reservation failed");
    Require(!InsertGuidFromPeer(reserved), "peer TU did not observe reservation");
    allocations = 0;
    countAllocations = true;
    for (int i = 0; i != 1000; ++i)
        Require(!InsertGuidFromPeer(reserved), "duplicate reservation succeeded");
    countAllocations = false;
    Require(allocations == 0, "duplicate registration allocated");
    EraseGuidFromPeer(reserved);
    Require(Registry::InsertGUID(reserved), "peer erase was not visible");
    Registry::EraseGUID(reserved);
    Registry::EraseGUID(reserved); // Existing absent erase remains harmless.

    constexpr size_t threads = 8;
    constexpr size_t perThread = 512;
    std::array<std::thread, threads> workers;
    std::array<HashedGuid, threads * perThread> generated{};
    std::atomic<size_t> reservations{0};
    std::barrier ready(static_cast<std::ptrdiff_t>(threads));
    for (size_t thread = 0; thread != threads; ++thread)
        workers[thread] = std::thread([&, thread]
        {
            ready.arrive_and_wait();
            if (InsertGuidFromPeer(reserved)) ++reservations;
            for (size_t i = 0; i != perThread; ++i)
                generated[thread * perThread + i] = (thread & 1)
                    ? MakeGuidFromPeer() : Registry::MakeGUID();
        });
    for (auto& worker : workers) worker.join();
    Require(reservations == 1, "simultaneous same-ID reservation was not atomic");
    std::sort(generated.begin(), generated.end());
    Require(std::adjacent_find(generated.begin(), generated.end()) == generated.end(),
        "concurrent generation returned duplicate live IDs");
    for (const auto id : generated)
    {
        Require(!InsertGuidFromPeer(id), "generated ID was not registered");
        EraseGuidFromPeer(id);
        Require(Registry::InsertGUID(id), "generated ID could not be reserved after erase");
        Registry::EraseGUID(id);
    }
    Registry::EraseGUID(reserved);
    std::cout << "GUID_REGISTRY_OK peer_tu=1 threads=" << threads
        << " generated=" << generated.size() << " duplicate_allocations=" << allocations << '\n';
}
