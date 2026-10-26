#include "../../Engine/RenderEngine/MaterialGraphGeometryPreparationIndex.h"

namespace
{
    void Require(bool value, const char* message)
    {
        if (!value)
        {
            throw std::runtime_error(message);
        }
    }
} // namespace

int Run()
{
    using material_graph::GeometryPreparationIndex;
    std::array<std::uint64_t, 8192> sources{};
    std::vector<GeometryPreparationIndex::Entry> storage(GeometryPreparationIndex::StorageSize(sources.size()));
    auto* backing = storage.data();
    std::uint64_t checks{};
    for (unsigned frame = 0; frame < 128; ++frame)
    {
        GeometryPreparationIndex index{storage, sources.size()};
        // Reverse insertion order proves the indices follow first use, not address.
        for (std::size_t current = 0; current < sources.size(); ++current)
        {
            const auto* source = &sources[sources.size() - current - 1];
            const auto first = index.FindOrAdd(source);
            Require(first.inserted && first.index == current, "Dense first-use index");
            for (unsigned duplicate = 0; duplicate < 3; ++duplicate)
            {
                const auto again = index.FindOrAdd(source);
                Require(!again.inserted && again.index == current, "Current/previous duplicate shares one slot");
                ++checks;
            }
            ++checks;
        }
        Require(index.Size() == sources.size() && storage.data() == backing, "Full working set and reused storage");
    }
    GeometryPreparationIndex empty{storage, sources.size()};
    Require(empty.Size() == 0 && empty.FindOrAdd(&sources.front()).index == 0, "Next frame discards borrowed keys");
    bool rejected{};
    try
    {
        empty.FindOrAdd(nullptr);
    }
    catch (const std::invalid_argument&)
    {
        rejected = true;
    }
    Require(rejected, "Null input must fail");
    std::array<GeometryPreparationIndex::Entry, 8> small{};
    GeometryPreparationIndex bounded{small, 1};
    bounded.FindOrAdd(&sources[0]);
    rejected = false;
    try
    {
        bounded.FindOrAdd(&sources[1]);
    }
    catch (const std::length_error&)
    {
        rejected = true;
    }
    Require(rejected && !bounded.FindOrAdd(&sources[0]).inserted, "Bound rejects overflow but allows duplicate");
    std::cout << "GEOMETRY_PREPARATION_INDEX_OK frames=128 unique=8192 checks=" << checks
              << " dense=1 reusedStorage=1 nullRejected=1 overflowRejected=1\n";
    return 0;
}

int main()
{
    try
    {
        return Run();
    }
    catch (const std::exception& error)
    {
        std::cerr << "GEOMETRY_PREPARATION_INDEX_FAIL " << error.what() << '\n';
        return 1;
    }
}
