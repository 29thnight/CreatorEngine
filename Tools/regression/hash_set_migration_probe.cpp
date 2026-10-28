#include "../../Engine/Utility_Framework/Foundation/EngineNodeSet.h"
#include "../../Engine/Utility_Framework/Reflection/ReflectionContainer.h"
#if defined(_DEBUG)
#include <crtdbg.h>
#endif

namespace
{
    unsigned checks = 0;
    void Check(bool value, const char* message)
    {
        ++checks;
        if (!value) throw std::runtime_error(message);
    }

    struct CollisionHash { size_t operator()(unsigned) const noexcept { return 3; } };
    struct Owner { static inline int live = 0; Owner() { ++live; } ~Owner() { --live; } };
    struct ThrowingKey
    {
        static inline bool rejectCopy = false;
        static inline unsigned live = 0;
        unsigned id;
        explicit ThrowingKey(unsigned value) : id(value) { ++live; }
        ThrowingKey(const ThrowingKey& other) : id(other.id)
        {
            if (rejectCopy) throw std::runtime_error("copy rejected");
            ++live;
        }
        ~ThrowingKey() { --live; }
        bool operator==(const ThrowingKey& other) const noexcept { return id == other.id; }
    };
    struct KeyHash { size_t operator()(const ThrowingKey& key) const noexcept { return key.id; } };

#if defined(_DEBUG)
    thread_local bool observe = false;
    thread_local unsigned allocations = 0, frees = 0;
    int Hook(int kind, void*, size_t, int, long, const unsigned char*, int)
    {
        if (observe && (kind == _HOOK_ALLOC || kind == _HOOK_REALLOC)) ++allocations;
        if (observe && kind == _HOOK_FREE) ++frees;
        return 1;
    }
    struct Scope
    {
        _CRT_ALLOC_HOOK old;
        Scope() : old(_CrtSetAllocHook(Hook)) { allocations = frees = 0; observe = true; }
        ~Scope() { observe = false; _CrtSetAllocHook(old); }
    };
#endif
}

int main() try
{
    static_assert(std::forward_iterator<ce::engine_node_set<int>::iterator>);
    static_assert(std::is_same_v<decltype(*std::declval<ce::engine_node_set<int>&>().begin()), const int&>);
    static_assert(meta::container::FillableSequence<ce::engine_node_set<std::string>>);
    static_assert(!meta::container::KeyedRange<ce::engine_node_set<std::string>>);

    ce::engine_node_set<unsigned, CollisionHash> actual;
    std::unordered_set<unsigned, CollisionHash> model;
    unsigned state = 123;
    for (unsigned i = 0; i != 8192; ++i)
    {
        state = state * 1664525u + 1013904223u;
        const unsigned key = (state >> 16) % 128;
        if (state & 1)
        {
            auto result = actual.insert(key);
            auto expected = model.insert(key);
            Check(result.second == expected.second && *result.first == *expected.first, "insert model");
        }
        else Check(actual.erase(key) == model.erase(key), "erase model");
        Check(actual.size() == model.size(), "size model");
        for (auto value : model) Check(actual.contains(value), "collision chain preserved");
    }

    ce::engine_node_set<std::string> names;
    std::string source(192, 'x');
    names.insert(source);
    const auto original = source;
    source.assign(192, 'z');
    const std::string envelope = "!" + original + "?";
    const std::string_view view(envelope.data() + 1, original.size());
    const std::string missing(192, 'q');
    const auto* address = &*names.find(view);
    names.reserve(1024);
    names.rehash(4096);
    Check(&*names.find(view) == address && *address == original, "owned key and stable rehash address");
    const std::string embedded("a\0b", 3);
    names.insert(embedded);
    Check(names.contains(std::string_view(embedded)) && !names.contains(std::string_view("a")), "embedded NUL identity");
    bool reads = true;
#if defined(_DEBUG)
    unsigned readAllocations;
    {
        Scope scope;
#endif
        for (unsigned i = 0; i != 8192; ++i)
            reads &= names.find(view) != names.end() && names.count(view) == 1 && !names.contains(std::string_view(missing));
#if defined(_DEBUG)
        readAllocations = allocations;
    }
    Check(readAllocations == 0, "hit miss view lookup allocations");
    ce::engine_node_set<unsigned> singleNode{7};
    unsigned nodeFrees, eraseAllocations;
    {
        Scope scope;
        singleNode.erase(7u);
        nodeFrees = frees;
        eraseAllocations = allocations;
    }
    Check(nodeFrees == 1 && eraseAllocations == 0, "erase releases one node without allocating");
#endif
    Check(reads, "lookup values");
    auto copied = names;
    Check(copied == names && &*copied.find(view) != &*names.find(view), "copy owns distinct nodes");
    auto moved = std::move(copied);
    Check(moved.contains(view) && copied.empty(), "move transfers ownership");
    ce::engine_node_set<std::string> empty;
    moved.swap(empty);
    Check(moved.empty() && empty.contains(view), "swap transfers nodes");
    ce::engine_node_set<unsigned> range{1, 2, 3, 4};
    Check(ce::erase_if(range, [](unsigned n) { return n % 2 == 0; }) == 2, "erase iterator traversal");
    range.erase(range.begin(), range.end());
    Check(range.empty(), "erase full range");
    range = {5, 5, 6};
    Check(range.size() == 2, "initializer assignment uniqueness");

    auto owner = std::make_shared<Owner>();
    ce::engine_node_set<std::shared_ptr<Owner>> owners;
    owners.insert(owner);
    auto survivor = owner;
    owners.erase(owner);
    Check(Owner::live == 1 && owner.use_count() == 2, "erase preserves external owner");
    owners.insert(owner);
    owner.reset(); survivor.reset();
    owners.clear();
    Check(Owner::live == 0, "clear destroys final owner");
    {
        ce::engine_node_set<ThrowingKey, KeyHash> keys;
        ThrowingKey first(1), second(2);
        keys.insert(first);
        const auto* stable = &*keys.begin();
        ThrowingKey::rejectCopy = true;
        bool rejected = false;
        try { keys.insert(second); }
        catch (const std::runtime_error&) { rejected = true; }
        ThrowingKey::rejectCopy = false;
        Check(rejected && keys.size() == 1 && keys.contains(first) && &*keys.begin() == stable,
              "failed key construction retains existing nodes");
        Check(ThrowingKey::live == 3, "failed key construction leaks no live key");
    }
    Check(ThrowingKey::live == 0, "key lifetime ends with owners");
#if defined(_DEBUG)
    ce::engine_node_set<unsigned> nodes;
    for (unsigned i = 0; i != 32; ++i) nodes.insert(i);
    unsigned clearFrees;
    {
        Scope scope;
        nodes.clear();
        clearFrees = frees;
    }
    Check(clearFrees == 32, "clear frees every node while retaining index capacity");
    std::printf("HASH_SET_MIGRATION_OK checks=%u modelOperations=8192 stringReads=8192 ownership=1 stableNodes=1 allocationChecks=1 nodeFreeChecks=2\n", checks);
#else
    std::printf("HASH_SET_MIGRATION_OK checks=%u modelOperations=8192 stringReads=8192 ownership=1 stableNodes=1 allocationChecks=0 nodeFreeChecks=0\n", checks);
#endif
    return 0;
}
catch (const std::exception& error)
{
    std::fprintf(stderr, "HASH_SET_MIGRATION_FAIL %s\n", error.what());
    return 1;
}
