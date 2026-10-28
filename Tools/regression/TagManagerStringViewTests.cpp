#include "TagManager.h"
#include <iostream>
#include <stdexcept>
#include <cstdlib>
#include <new>

namespace
{
thread_local bool countAllocations = false;
thread_local size_t allocationCount = 0;
}

void* operator new(std::size_t size)
{
    if (countAllocations) ++allocationCount;
    if (void* memory = std::malloc(size ? size : 1)) return memory;
    throw std::bad_alloc();
}
void* operator new[](std::size_t size) { return ::operator new(size); }
void operator delete(void* memory) noexcept { std::free(memory); }
void operator delete[](void* memory) noexcept { std::free(memory); }
void operator delete(void* memory, std::size_t) noexcept { std::free(memory); }
void operator delete[](void* memory, std::size_t) noexcept { std::free(memory); }

namespace
{
void Check(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}

void Exercise(TagManager& tags, std::string_view view)
{
    const std::string expected(view);
    tags.ClearTags();
    tags.SetTagDefinitions({ce::utf8_string("Untagged"), ce::utf8_string("Anchor")});
    tags.AddTag(view);
    Check(tags.GetTags().size() == 3 && tags.GetTags().back() == ce::utf8_string_view(expected.data(), expected.size()),
        "AddTag must preserve the exact view bytes");
    Check(tags.HasTag(view) && tags.GetTagIndex(view) == 2,
        "bounded definition lookup failed");
    tags.AddTag(view);
    Check(tags.GetTags().size() == 3, "duplicate definition admitted");

    // Valid entities exercise production membership APIs without initializing or
    // finalizing the asset service; the test never reads or saves project assets.
    Entity first;
    Entity second;
    tags.AddTagToObject(view, &first);
    tags.AddTagToObject("Anchor", &second);
    Check(tags.GetObjectWithTag(view) == &first, "bounded first-object lookup failed");
    const auto objects = tags.GetObjectsWithTag(view);
    Check(objects.size() == 1 && objects.front() == &first,
        "bounded object-list lookup failed");
    tags.RemoveTagFromObject(view, &first);
    Check(tags.GetObjectWithTag(view) == nullptr && tags.GetObjectsWithTag(view).empty(),
        "bounded membership removal failed");
    Check(tags.GetObjectWithTag("Anchor") == &second, "unrelated membership changed");
    tags.RemoveTag(view);
    Check(!tags.HasTag(view) && tags.GetTagIndex(view) == 0 && tags.GetTags().size() == 2,
        "bounded definition removal failed");
    tags.ClearTags();
}
}

int main()
{
    try
    {
        auto& tags = *TagManager::GetInstance();
        const char unterminated[] = {'P', 'l', 'a', 'y', 'e', 'r'};
        Exercise(tags, std::string_view(unterminated, sizeof(unterminated)));
        // Put the view immediately before an unreadable page: reading for a
        // terminator beyond size() must fault even without AddressSanitizer.
        SYSTEM_INFO systemInfo{};
        GetSystemInfo(&systemInfo);
        auto* pages = static_cast<char*>(VirtualAlloc(nullptr,
            size_t(systemInfo.dwPageSize) * 2, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
        Check(pages != nullptr, "guard-page allocation failed");
        DWORD previousProtection{};
        if (!VirtualProtect(pages + systemInfo.dwPageSize, systemInfo.dwPageSize,
                PAGE_NOACCESS, &previousProtection))
        {
            VirtualFree(pages, 0, MEM_RELEASE);
            throw std::runtime_error("guard-page protection failed");
        }
        auto* guarded = pages + systemInfo.dwPageSize - sizeof(unterminated);
        std::copy_n(unterminated, sizeof(unterminated), guarded);
        try { Exercise(tags, std::string_view(guarded, sizeof(unterminated))); }
        catch (...) { VirtualFree(pages, 0, MEM_RELEASE); throw; }
        VirtualFree(pages, 0, MEM_RELEASE);
        const std::string suffixed = "PlayerSuffix";
        Exercise(tags, std::string_view(suffixed.data(), 6));
        const char embeddedNull[] = {'A', '\0', 'B'};
        Exercise(tags, std::string_view(embeddedNull, sizeof(embeddedNull)));
        const std::string longBuffer = std::string(256, 'T') + "Suffix";
        const std::string_view longTag(longBuffer.data(), 256);
        Exercise(tags, longTag);

        std::string callerOwned(256, 'O');
        const std::string original = callerOwned;
        tags.AddTag(callerOwned);
        callerOwned.assign(256, 'X');
        Check(tags.HasTag(original) && !tags.HasTag(callerOwned),
            "definition retained a borrowed caller buffer");
        tags.ClearTags();

        tags.SetTagDefinitions({ce::utf8_string("Untagged"), ce::utf8_string(ce::utf8_string_view(longTag.data(), longTag.size()))});
        Entity member;
        tags.AddTagToObject(longTag, &member);
        tags.RemoveTagFromObject(longTag, &member); // retain membership capacity
        allocationCount = 0;
        countAllocations = true;
        bool allocationChecks = true;
        for (int i = 0; i < 1000; ++i)
        {
            allocationChecks &= tags.HasTag(longTag) && tags.GetTagIndex(longTag) == 1;
            tags.AddTag(longTag); // duplicate must not materialize a key
            tags.AddTagToObject(longTag, &member);
            allocationChecks &= tags.GetObjectWithTag(longTag) == &member;
            tags.RemoveTagFromObject(longTag, &member);
        }
        tags.RemoveTag(longTag);
        countAllocations = false;
        Check(allocationChecks && allocationCount == 0,
            "long-tag lookup/duplicate/warmed membership/removal allocated");

        tags.SetTagDefinitions({ce::utf8_string("Untagged"), ce::utf8_string("Player"),
            ce::utf8_string("PlayerSuffix"), ce::utf8_string("A")});
        Check(tags.HasTag(std::string_view(suffixed.data(), 6)) &&
            tags.GetTagIndex(std::string_view(suffixed.data(), 6)) == 1,
            "substring confused with suffixed definition");
        Check(!tags.HasTag(std::string_view(embeddedNull, sizeof(embeddedNull))),
            "embedded null confused with prefix definition");
        // RemoveTag must own a view into m_tags before vector compaction.
        tags.RemoveTag(std::string_view(tags.GetTags()[1].data(), tags.GetTags()[1].size()));
        Check(!tags.HasTag("Player") && tags.HasTag("PlayerSuffix"),
            "aliased definition removal corrupted another tag");
        const auto count = tags.GetTags().size();
        tags.AddTag({});
        tags.AddTag("Untagged");
        tags.RemoveTag({});
        tags.RemoveTag("Untagged");
        Check(tags.GetTags().size() == count && !tags.HasTag({}) &&
            !tags.HasTag("Untagged") && tags.GetTagIndex({}) == 0 &&
            tags.GetObjectsWithTag({}).empty() && tags.GetObjectWithTag({}) == nullptr,
            "empty/Untagged contract changed");
        tags.ClearTags();
        // ce heap allocations are checked with an allocator probe in
        // string_storage_contract_probe; this counter observes standard new only.
        std::cout << "TAG_MANAGER_STRING_VIEW_OK std_new_allocations=" << allocationCount << '\n';
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
