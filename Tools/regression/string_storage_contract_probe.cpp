#include "../../Engine/Utility_Framework/Foundation/StandardLibrary.h"
#include "../../Engine/Utility_Framework/Foundation/EngineNodeMap.h"
#include "../../Engine/Utility_Framework/Foundation/EngineNodeSet.h"
#include "../../Engine/Utility_Framework/Foundation/EngineOrderedMap.h"

namespace
{
    size_t stringAllocations = 0;
    struct StringAllocator
    {
        void* allocate(size_t bytes, size_t alignment)
        {
            ++stringAllocations;
            return ce::heap_allocator{}.allocate(bytes, alignment);
        }
        void deallocate(void* memory, size_t bytes, size_t alignment) noexcept
        {
            ce::heap_allocator{}.deallocate(memory, bytes, alignment);
        }
    };
}

int RunStringStorageContract()
{
    int failures = 0;
    const auto check = [&](bool value, const char* contract)
    {
        if (!value)
        {
            std::fprintf(stderr, "[STRING STORAGE] FAIL: %s\n", contract);
            ++failures;
        }
    };

    ce::utf8_string name;
    check(name.empty(), "empty names remain representable");
    const ce::utf8_string backing(ce::utf8_string_view("EnemyTail"));
    const ce::utf8_string_view partial(backing.data(), 5);
    name.assign(partial);
    check(name.view() == ce::utf8_string_view("Enemy") && name != backing, "partial views preserve length");
    check(name == partial, "lookup compares a view without an owning temporary");
    const ce::utf8_string_view embedded("a\0b", 3);
    name.assign(embedded);
    check(name.size() == 3 && name == embedded, "embedded NUL preserves bytes");
    name.assign("\xeb\xa3\xa8\xed\x8a\xb8");
    const auto copy = name;
    name.clear();
    name.resize(1024, 'x');
    check(copy.size() == 6 && name.size() == 1024, "owned names survive mutation and growth");
    ce::engine_node_map<ce::utf8_string, int> tags;
    tags.try_emplace(copy, 7);
    const auto tag = tags.find(copy.view());
    check(tag != tags.end() && tag->second == 7, "UTF8 owned keys and allocation-free view lookup");
    ce::string display;
    check(bool(ce::to_utf16(copy.view(), display)) && display.view() == ce::string_view(u"루트"),
        "UTF8 boundary decodes into UTF16 owned text");
    const ce::string original(display);
    ce::utf8_string encoded;
    check(bool(ce::to_utf8(display.view(), encoded)) && encoded == copy, "UTF16 text roundtrips through UTF8");
    check(!ce::to_utf16(ce::utf8_string_view("\xc0\x80"), display) && display == original,
        "malformed UTF8 leaves owned UTF16 text unchanged");
    ce::utf8_conversion_cache cache;
    check(bool(cache.update(display.view())) && cache.text() == copy, "display cache owns encoded bytes");
    ce::engine_node_map<ce::string, int> displayNames;
    displayNames.try_emplace(display, 9);
    const auto found = displayNames.find(display.view());
    check(found != displayNames.end() && found->second == 9, "UTF16 owned keys accept non-owning lookup");
    check(bool(ce::validate_utf8(copy.view())) && !ce::validate_utf8(ce::utf8_string_view("\xf4\x90\x80\x80")),
        "UTF8 validation rejects values beyond the Unicode scalar range");
    ce::engine_node_set<ce::utf8_string> utf8Set;
    utf8Set.emplace(embedded);
    check(utf8Set.contains(embedded) && !utf8Set.contains(ce::utf8_string_view("a")),
        "UTF8 node sets preserve bounded embedded-NUL keys");
    ce::engine_node_set<ce::string> utf16Set;
    utf16Set.emplace(display.view());
    check(utf16Set.contains(display.view()), "UTF16 node sets support view lookup");
    ce::engine_ordered_map<ce::utf8_string, int> ordered;
    ordered.emplace(ce::utf8_string(embedded), 3);
    ordered.emplace(ce::utf8_string("a"), 1);
    check(ordered.begin()->first.view() == ce::utf8_string_view("a") && ordered.find(embedded)->second == 3,
        "UTF8 ordered maps compare complete key bytes and support view lookup");
    ce::engine_ordered_set<ce::string> ordered16;
    ordered16.emplace(ce::string_view(u"\u0100"));
    ordered16.emplace(ce::string_view(u"\u00ff"));
    check(ordered16.begin()->view() == ce::string_view(u"\u00ff") && ordered16.contains(ce::string_view(u"\u0100")),
        "UTF16 ordered sets compare numeric code units, not host-endian bytes");

    // Count the ce allocators themselves. operator-new instrumentation cannot
    // observe ce::heap_allocator's malloc-backed string and node allocations.
    using CountedString = ce::basic_utf8_string<StringAllocator>;
    ce::node_hash_map<CountedString, int, ce::engine_node_hash<CountedString>,
        ce::engine_node_equal<CountedString>, StringAllocator, false> counted;
    CountedString longKey;
    longKey.resize(256, 'k');
    counted.try_emplace(longKey, 11);
    const auto beforeLookups = stringAllocations;
    for (int index = 0; index != 1000; ++index)
    {
        const auto item = counted.find(longKey.view());
        check(item != counted.end() && item->second == 11, "long key lookup");
        check(!counted.try_emplace(longKey.view(), 12).second, "duplicate view does not construct an owning key");
    }
    check(stringAllocations == beforeLookups, "long-key lookup and duplicate insertion allocate zero ce storage");
    std::printf("[STRING STORAGE] failures=%d\n", failures);
    return failures == 0 ? 0 : 1;
}

#if defined(CE_STRING_STORAGE_STANDALONE)
int main()
{
    return RunStringStorageContract();
}
#endif
