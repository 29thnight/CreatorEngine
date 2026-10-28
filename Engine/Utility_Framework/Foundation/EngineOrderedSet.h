#pragma once

#include "StandardLibrary.h"
#if defined(CE_CONTRACT_STANDALONE)
#include "../../Containers/ordered/ordered_set.h"
#else
import "Creator.Containers.h";
#endif

namespace ce
{
    template<class Key> struct engine_ordered_compare : std::less<Key> {};
    template<> struct engine_ordered_compare<std::string> : std::less<> {};
    template<> struct engine_ordered_compare<std::wstring> : std::less<> {};
    template<class Allocator> struct engine_ordered_compare<basic_utf8_string<Allocator>>
    {
        using is_transparent = void;
        bool operator()(utf8_string_view left, utf8_string_view right) const noexcept
        { return left.compare(right) < 0; }
    };
    template<> struct engine_ordered_compare<utf8_string_view> : engine_ordered_compare<utf8_string> {};
    template<class Allocator> struct engine_ordered_compare<basic_utf16_string<Allocator>>
    {
        using is_transparent = void;
        bool operator()(string_view left, string_view right) const noexcept
        { return left.compare(right) < 0; }
    };
    template<> struct engine_ordered_compare<string_view> : engine_ordered_compare<string> {};

    template<class Key, class Compare = engine_ordered_compare<Key>>
    using engine_ordered_set = ordered_node_set<Key, Compare, false>;

    template<class Key, class Compare = engine_ordered_compare<Key>>
    using engine_ordered_multiset = ordered_node_set<Key, Compare, true>;

    template<class Key, class Compare, bool Multi, class Predicate>
    size_t erase_if(ordered_node_set<Key, Compare, Multi>& values, Predicate predicate)
    {
        const auto before = values.size();
        for (auto it = values.begin(); it != values.end();)
        {
            if (predicate(*it)) it = values.erase(it);
            else ++it;
        }
        return before - values.size();
    }
}
