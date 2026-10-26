#include "../../Engine/Utility_Framework/Foundation/StandardLibrary.h"
#include "../../Engine/Contracts/External/Creator.Ownership.h"

// Only the input protocol and profiler are fixtures. The cache body is extracted
// verbatim from the production source by verify-mesh-request-cache.ps1.
namespace ce
{
    struct marker_name
    {
        template<std::size_t N>
        consteval marker_name(const char (&)[N])
        {
        }
    };
    template<marker_name>
    unsigned marker()
    {
        return 0;
    }
    struct profile_scope
    {
        explicit profile_scope(unsigned) {}
    };
} // namespace ce

namespace material_graph
{
    struct test_matrix
    {
        float m[4][4]{{1, 0, 0, 0}, {0, 1, 0, 0}, {0, 0, 1, 0}, {0, 0, 0, 1}};
    };
    struct test_view
    {
        std::uint64_t sceneEpoch{1};
        std::array<float, 4> eye{};
    };
    struct test_hierarchy
    {
        struct group
        {
            std::array<float, 4> sphere{};
        };
        std::uint32_t groupCount{};
        std::vector<group> groups;
    };
    struct MeshSurfaceInput
    {
        test_matrix world;
        test_view view;
        test_hierarchy hierarchy;
        explicit MeshSurfaceInput(unsigned count = 2)
        {
            hierarchy.groupCount = count;
            hierarchy.groups.resize(count);
            for (unsigned index = 0; index < count; ++index)
            {
                hierarchy.groups[index].sphere[0] = float(index);
            }
        }
        const auto& World() const { return world; }
        const auto& View() const { return view; }
        const auto& Hierarchy() const { return hierarchy; }
    };
#include "MeshSurfaceRequestCache.generated.inl"

    struct MeshSurfaceStaticBuffers
    {
        std::uint64_t lastLeasedRecording{}, lastLeaseSerial{};
    };
    struct test_lease_ledger
    {
#include "StaticCacheLease.generated.inl"
    };
} // namespace material_graph

void Require(bool value)
{
    if (!value)
    {
        throw std::runtime_error("Production request cache invariant failed");
    }
}

void CheckLinks(const material_graph::MeshSurfaceRequestCache& cache)
{
    const auto* previous = static_cast<const material_graph::MeshSurfaceRequestCache::Entry*>(nullptr);
    std::size_t count{};
    for (const auto* entry = cache.newest; entry; entry = entry->next)
    {
        Require(entry->previous == previous && count++ < cache.entries.size());
        previous = entry;
    }
    Require(count == cache.entries.size() && previous == cache.oldest && cache.bytes <= cache.maxBytes);
}

int main()
{
    try
    {
        using namespace material_graph;
        MeshSurfaceRequestCache cache;
        own::shared_owner<const MeshSurfaceInput> identity = own::make_shared<MeshSurfaceInput>();
        MeshSurfaceInput input;
        std::vector<const void*> addresses;
        for (unsigned generation = 1; generation <= 3; ++generation)
        {
            cache.Trim(generation);
            for (unsigned index = 0; index < 2048; ++index)
            {
                input.world.m[3][0] = float(index);
                auto request = cache.Acquire(identity, input, 1, generation);
                Require(request->size() == 2 && (*request)[0] == 0 && (*request)[1] == 1);
                if (generation == 1)
                {
                    addresses.push_back(request.operator->());
                }
                else
                {
                    Require(addresses[index] == request.operator->());
                }
            }
            CheckLinks(cache);
        }
        input.world.m[3][0] = 0;
        input.view.eye[0] = 10;
        auto reversed = cache.Acquire(identity, input, 1, 3);
        Require((*reversed)[0] == 1 && (*reversed)[1] == 0);
        input.view.eye[0] = .5f;
        auto tied = cache.Acquire(identity, input, 1, 3);
        Require((*tied)[0] == 0 && (*tied)[1] == 1);

        own::shared_owner<const MeshSurfaceInput> largeIdentity = own::make_shared<MeshSurfaceInput>(1024);
        MeshSurfaceInput large(1024);
        large.world.m[3][0] = -10000;
        const auto pinned = cache.Acquire(largeIdentity, large, 1, 3);
        for (unsigned index = 0; index < 4096; ++index)
        {
            large.world.m[3][0] = float(index);
            cache.Acquire(largeIdentity, large, 1, 3);
            if (index % 64 == 0)
            {
                large.world.m[3][0] = -10000;
                Require(cache.Acquire(largeIdentity, large, 1, 3).operator->() == pinned.operator->());
            }
        }
        CheckLinks(cache);
        Require(cache.entries.size() < 4096 && pinned->size() == 1024);
        cache.Trim(6);
        CheckLinks(cache);
        Require(cache.entries.empty() && cache.bytes == 0);
        input.view.eye[0] = 0;
        cache.Acquire(identity, input, 1, 6);
        identity = {};
        cache.Trim(6);
        CheckLinks(cache);
        Require(cache.entries.empty());
        test_lease_ledger ledger;
        auto entry = own::make_shared<MeshSurfaceStaticBuffers>();
        const own::weak_owner<MeshSurfaceStaticBuffers> observed = entry;
        ledger.Retain(7, entry);
        ledger.Retain(7, entry);
        Require(ledger.recordings.at(7).owners.size() == 1);
        ledger.recordings.erase(7);
        auto other = own::make_shared<MeshSurfaceStaticBuffers>();
        ledger.Retain(7, other);
        Require(!ledger.HasLease(7, *entry));
        ledger.Retain(7, entry);
        Require(ledger.recordings.at(7).owners.size() == 2);
        ledger.Retain(8, entry);
        ledger.Retain(7, entry);
        Require(ledger.recordings.at(7).owners.size() == 2 && ledger.recordings.at(8).owners.size() == 1);
        entry = {};
        ledger.recordings.erase(7);
        Require(!observed.expired());
        ledger.recordings.clear();
        Require(observed.expired());
        std::cout << "MESH_STATIC_LEASE_OK duplicate=1 recreatedLedger=1 interleavedRecording=1 retainedOwner=1\n";
        std::cout << "MESH_REQUEST_CACHE_OK stableKeys=2048 generations=3 budgetEviction=4096 order=1 tie=1 links=1 "
                     "expiry=1 retainedOutput=1\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
