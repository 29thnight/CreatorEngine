#include "../../Engine/Utility_Framework/Foundation/EngineOrderedMap.h"
#include "../../Engine/Utility_Framework/Reflection/ReflectionContainer.h"
#if defined(CE_CONTRACT_STANDALONE)
#include "../build/ModuleAdapters/Creator.Core.Arena.h"
#include "../build/ModuleAdapters/Creator.Core.Ownership.h"
#else
import Creator.Core.Arena;
import Creator.Core.Ownership;
#endif
#if defined(_DEBUG)
#include <crtdbg.h>
#endif

namespace
{
    unsigned checks = 0;
    void Check(bool value, const char* why) { ++checks; if (!value) throw std::runtime_error(why); }
    struct Owner { static inline int live = 0; Owner() { ++live; } ~Owner() { --live; } };
    struct Throwing
    {
        static inline bool reject = false;
        static inline int live = 0;
        explicit Throwing(int) { if (reject) throw std::runtime_error("rejected"); ++live; }
        Throwing(Throwing&&) = delete;
        ~Throwing() { --live; }
    };
    struct Domain { int allocations = 0, releases = 0, constructions = 0, destructions = 0; };
    struct Recursive { ce::engine_ordered_map<int, Recursive> children; };
    template<class T> struct DomainAllocator
    {
        using value_type = T;
        using is_always_equal = std::false_type;
        Domain* domain = nullptr;
        DomainAllocator() noexcept = default;
        explicit DomainAllocator(Domain* d) noexcept : domain(d) {}
        template<class U> DomainAllocator(const DomainAllocator<U>& other) noexcept : domain(other.domain) {}
        T* allocate(size_t n) { if (domain) ++domain->allocations; return ce::ordered_heap_allocator<T>{}.allocate(n); }
        void deallocate(T* p, size_t n) noexcept { if (domain) ++domain->releases; ce::ordered_heap_allocator<T>{}.deallocate(p, n); }
        template<class U, class... Args> void construct(U* p, Args&&... args) { if (domain) ++domain->constructions; std::construct_at(p, std::forward<Args>(args)...); }
        template<class U> void destroy(U* p) noexcept { if (domain) ++domain->destructions; std::destroy_at(p); }
        DomainAllocator select_on_container_copy_construction() const noexcept { return {}; }
        template<class U> bool operator==(const DomainAllocator<U>& other) const noexcept { return domain == other.domain; }
    };
#if defined(_DEBUG)
    thread_local bool observe = false;
    thread_local unsigned allocations = 0, releases = 0;
    int Hook(int kind, void*, size_t, int, long, const unsigned char*, int)
    {
        if (observe && (kind == _HOOK_ALLOC || kind == _HOOK_REALLOC)) ++allocations;
        if (observe && kind == _HOOK_FREE) ++releases;
        return 1;
    }
    struct Scope
    {
        _CRT_ALLOC_HOOK old;
        Scope() : old(_CrtSetAllocHook(Hook)) { allocations = releases = 0; observe = true; }
        ~Scope() { observe = false; _CrtSetAllocHook(old); }
    };
#endif
}

int main() try
{
    Recursive recursive;
    recursive.children[1].children[2];
    Check(recursive.children.at(1).children.contains(2), "incomplete mapped type becomes recursive owned tree");
    using Map = ce::engine_ordered_map<unsigned, unsigned>;
    static_assert(std::bidirectional_iterator<Map::iterator>);
    static_assert(std::is_same_v<decltype(std::declval<Map::iterator>()->second), unsigned>);
    static_assert(std::is_const_v<std::remove_reference_t<decltype((std::declval<Map::const_iterator>()->second))>>);
    static_assert(meta::container::FillableKeyed<Map>);
    Map actual;
    std::map<unsigned, unsigned> reference;
    unsigned random = 123;
    std::array<const Map::value_type*, 128> addresses{};
    for (unsigned i = 0; i != 8192; ++i)
    {
        random = random * 1664525u + 1013904223u;
        const unsigned key = (random >> 16) % 128;
        if (random & 1)
        {
            auto result = actual.insert_or_assign(key, i);
            auto model = reference.insert_or_assign(key, i);
            Check(result.second == model.second, "insert assignment model");
            if (result.second) addresses[key] = &*result.first;
        }
        else { Check(actual.erase(key) == reference.erase(key), "erase model"); addresses[key] = nullptr; }
        Check(actual.size() == reference.size(), "size model");
        Check(actual.validate_structure(), "red-black height color and parent links");
        auto expected = reference.begin();
        for (const auto& entry : actual)
        {
            Check(expected != reference.end() && entry.first == expected->first && entry.second == expected->second, "ordered value model");
            Check(&entry == addresses[entry.first], "surviving node address");
            ++expected;
        }
        auto a = actual.lower_bound(key), b = actual.upper_bound(key);
        auto x = reference.lower_bound(key), y = reference.upper_bound(key);
        Check((a == actual.end()) == (x == reference.end()), "lower end");
        if (a != actual.end()) Check(a->first == x->first, "lower value");
        Check((b == actual.end()) == (y == reference.end()), "upper end");
        if (b != actual.end()) Check(b->first == y->first, "upper value");
    }
    auto copied = actual;
    Check(copied == actual, "independent copy");
    auto retained = copied.begin();
    auto moved = std::move(copied);
    Check(copied.empty() && retained == moved.begin(), "move retains iterators");
    Map swapped;
    swapped.swap(moved);
    Check(retained == swapped.begin(), "swap retains iterators");

    ce::engine_ordered_map<std::string, int> strings;
    std::string source(192, 'x');
    strings.try_emplace(source, 17);
    const auto original = source;
    source.assign(192, 'z');
    const std::string envelope = "!" + original + "?";
    const std::string_view view(envelope.data() + 1, original.size());
    const std::string missing(192, 'q');
    const std::string embedded("a\0b", 3);
    strings.try_emplace(embedded, 9);
    Check(strings.at(view) == 17 && strings.contains(std::string_view(embedded)) && !strings.contains(std::string_view("a")), "owned view and embedded NUL");
    bool reads = true;
#if defined(_DEBUG)
    unsigned readAllocations;
    {
        Scope scope;
#endif
        for (unsigned i = 0; i != 8192; ++i)
            reads &= strings.at(view) == 17 && strings.count(view) == 1 && !strings.contains(std::string_view(missing));
#if defined(_DEBUG)
        readAllocations = allocations;
    }
    Check(readAllocations == 0, "query allocations zero");
#endif
    Check(reads, "all string queries");
    {
        ce::ordered_node_map<std::string, int, std::less<std::string>> typed;
        std::map<std::string, int> typedModel;
        typed.try_emplace(original, 1); typedModel.try_emplace(original, 1);
#if defined(_DEBUG)
        unsigned customConversions, standardConversions;
        bool customFound, standardFound;
        { Scope scope; customFound = typed.contains(original.c_str()); customConversions = allocations; }
        { Scope scope; standardFound = typedModel.contains(original.c_str()); standardConversions = allocations; }
        Check(customFound && standardFound && customConversions == standardConversions, "non-transparent key conversion occurs once as in STL");
#endif
        ce::engine_ordered_map<std::wstring, int> wide;
        const std::wstring wideKey(192, L'x'); wide.try_emplace(wideKey, 2);
        Check(wide.at(std::wstring_view(wideKey)) == 2, "wide borrowed query retains string type");
    }
    ce::engine_ordered_multimap<unsigned, unsigned> duplicates;
    std::multimap<unsigned, unsigned> duplicateModel;
    for (unsigned i = 0; i != 4096; ++i) { duplicates.emplace(i % 17, i); duplicateModel.emplace(i % 17, i); }
    Check(duplicates.validate_structure(), "duplicate tree invariants");
    auto modelIt = duplicateModel.begin();
    for (const auto& e : duplicates) { Check(e.first == modelIt->first && e.second == modelIt->second, "duplicate insertion order"); ++modelIt; }
    for (unsigned i = 0; i != 17; ++i) Check(duplicates.erase(i) == duplicateModel.erase(i), "duplicate group erase");

    Map transfer;
    transfer.try_emplace(7, 9);
    const auto* stable = &*transfer.begin();
    Map destination;
    destination.try_emplace(7, 99);
    bool transferOkay = false;
#if defined(_DEBUG)
    unsigned transferAllocations;
    {
        Scope scope;
#endif
        auto transferNode = transfer.extract(7u);
        auto insertionRejected = destination.insert(std::move(transferNode));
        transferOkay = !insertionRejected.inserted && bool(insertionRejected.node) && insertionRejected.node.mapped() == 9;
        auto rollback = transfer.insert(std::move(insertionRejected.node));
        transferOkay &= rollback.inserted && &*rollback.position == stable && rollback.position->second == 9;
        destination.erase(7u);
        destination.merge(transfer);
        transferOkay &= transfer.empty() && &*destination.begin() == stable;
#if defined(_DEBUG)
        transferAllocations = allocations;
    }
    Check(transferAllocations == 0, "node extraction duplicate rollback merge allocate zero");
#endif
    Check(transferOkay, "node ownership and identity rollback");
    struct DeferredOwner { bool* locked; bool* destroyed; ~DeferredOwner() { *destroyed = !*locked; } };
    bool locked = true, destroyedOutside = false;
    ce::engine_ordered_map<unsigned, std::unique_ptr<DeferredOwner>> owners;
    owners.try_emplace(1, std::make_unique<DeferredOwner>(&locked, &destroyedOutside));
    auto detached = owners.extract(1u);
    Check(owners.empty() && !destroyedOutside, "extract retains final owner");
    locked = false;
    detached.reset();
    Check(destroyedOutside, "detached final owner destroys after lock scope");
    {
        ce::engine_ordered_map<int, own::shared_owner<Owner>> shared;
        auto owner = own::make_shared<Owner>();
        shared.try_emplace(1, owner);
        auto external = shared.at(1);
        auto other = own::make_shared<Owner>();
        const auto* pointer = std::addressof(*other);
        Check(!shared.try_emplace(1, std::move(other)).second && other && std::addressof(*other) == pointer, "duplicate emplacement does not consume mapped owner");
        shared.erase(1);
        Check(Owner::live == 2 && owner.use_count() == 2, "erase preserves remaining engine shared owners");
    }
    Check(Owner::live == 0, "last engine shared owner destroys object");
    ce::engine_ordered_map<int, Throwing> failures;
    failures.try_emplace(1, 1);
    Throwing::reject = true;
    bool rejected = false;
    try { failures.try_emplace(2, 2); } catch (const std::runtime_error&) { rejected = true; }
    Throwing::reject = false;
    Check(rejected && failures.size() == 1 && Throwing::live == 1, "construction rollback");
    failures.clear(); Check(Throwing::live == 0, "failed construction leak free");

    Domain first, second;
    using Alloc = DomainAllocator<ce::pair<const unsigned, unsigned>>;
    using Bound = ce::ordered_node_map<unsigned, unsigned, std::less<unsigned>, Alloc>;
    {
        Bound a{Alloc(&first)}, b{Alloc(&second)};
        a.try_emplace(1u, 7u);
        auto node = a.extract(1u);
        bool mismatch = false;
        try { b.insert(std::move(node)); } catch (const std::invalid_argument&) { mismatch = true; }
        Check(mismatch && bool(node) && b.empty(), "allocator mismatch retains detached node");
        a.insert(std::move(node));
        auto independent = a;
        Check(!independent.get_allocator().domain && independent.at(1u) == 7, "copy selects independent allocator");
        b = std::move(a);
        Check(b.get_allocator().domain == &second && b.at(1u) == 7 && a.empty(), "unequal move keeps target domain");
    }
    Check(first.allocations == first.releases && second.allocations == second.releases, "domain deallocation balanced");
    Check(first.constructions == first.destructions && second.constructions == second.destructions, "custom allocator construction and destruction hooks balanced");

    ce::arena_resource arenaA, arenaB;
    using ArenaValue = ce::arena_vector<uint64_t>;
    using ArenaAlloc = ce::arena_allocator<std::pair<const int, ArenaValue>>;
    using ArenaMap = ce::ordered_node_map<int, ArenaValue, std::less<int>, ArenaAlloc>;
    static_assert(std::is_same_v<ce::arena_map<int, ArenaValue>, ArenaMap>);
    ArenaMap independent;
    void* oldDataA = nullptr;
    void* oldDataB = nullptr;
    {
        ArenaMap a{ArenaAlloc(&arenaA)}, b{ArenaAlloc(&arenaB)};
        a[7].assign(32, 123);
        oldDataA = a.at(7).data();
        Check(a[7].get_allocator().resource() == &arenaA, "nested value inherits writer arena");
        independent = a;
        auto plain = a;
        Check(!plain.get_allocator().resource() && !plain.at(7).get_allocator().resource(), "plain map copy owns independent domain");
        b = std::move(a);
        oldDataB = b.at(7).data();
        Check(independent.at(7).data() != oldDataA && independent.at(7).data() != oldDataB, "copy storage is independent of both arenas");
        Check(b.get_allocator().resource() == &arenaB && b.at(7).get_allocator().resource() == &arenaB && b.at(7)[31] == 123, "cross-arena map move preserves target nested domain");
    }
    arenaA.reset(); arenaB.reset();
    // reset retains these raw blocks. Overwrite ended source allocations to
    // ensure the surviving copy is not merely reading unreused arena bytes.
    std::memset(oldDataA, 0xa5, 32 * sizeof(uint64_t));
    std::memset(oldDataB, 0x5a, 32 * sizeof(uint64_t));
    Check(independent.at(7)[31] == 123 && !independent.at(7).get_allocator().resource(), "independent map copy survives arena reset");
    Map nodes;
    for (unsigned i = 0; i != 32; ++i) nodes.try_emplace(i, i);
#if defined(_DEBUG)
    unsigned oneRelease, allReleases;
    { Scope scope; nodes.erase(7u); oneRelease = releases; }
    { Scope scope; nodes.clear(); allReleases = releases; }
    Check(oneRelease == 1 && allReleases == 31, "heap erase and clear release each node immediately");
    std::printf("ORDERED_MAP_MIGRATION_OK checks=%u modelOperations=8192 stringReads=8192 nodeTransfer=1 allocatorDomains=1 allocationChecks=1\n", checks);
#else
    nodes.clear();
    std::printf("ORDERED_MAP_MIGRATION_OK checks=%u modelOperations=8192 stringReads=8192 nodeTransfer=1 allocatorDomains=1 allocationChecks=0\n", checks);
#endif
    return 0;
}
catch (const std::exception& error) { std::fprintf(stderr, "ORDERED_MAP_MIGRATION_FAIL %s\n", error.what()); return 1; }
