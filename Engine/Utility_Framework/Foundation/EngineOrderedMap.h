#pragma once

#include "EngineOrderedSet.h"
#if defined(CE_CONTRACT_STANDALONE)
#include "../../Containers/ordered-map/ordered_map.h"
#else
import "Creator.Containers.h";
#endif

namespace ce
{
    template<class Key, class Value, class Compare = engine_ordered_compare<Key>,
             class Allocator = ordered_heap_allocator<ce::pair<const Key, Value>>>
    using engine_ordered_map = ordered_node_map<Key, Value, Compare, Allocator, false>;

    template<class Key, class Value, class Compare = engine_ordered_compare<Key>,
             class Allocator = ordered_heap_allocator<ce::pair<const Key, Value>>>
    using engine_ordered_multimap = ordered_node_map<Key, Value, Compare, Allocator, true>;

    template<class Key, class Value, class Compare, class Allocator, bool Multi, class Predicate>
    size_t erase_if(ordered_node_map<Key, Value, Compare, Allocator, Multi>& values, Predicate predicate)
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
