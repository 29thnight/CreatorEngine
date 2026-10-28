#pragma once
// Native container views resolve their current elements on every request. Map
// keys remain immutable and JSON cannot replace the container's allocation.
#include "../../Containers/include/ce/flat_hash_map.h"
#include <reflgen/runtime/value_descriptor.h>
#include <stdexcept>

namespace reflgen
{
    template<class Key, class Value, class Hash, class Equal, class Allocator>
    struct runtime_value_traits<ce::basic_flat_hash_map<Key, Value, Hash, Equal, Allocator>>
    {
        using map_type = ce::basic_flat_hash_map<Key, Value, Hash, Equal, Allocator>;
        static constexpr value_shape shape = value_shape::map;
        static constexpr bool deserializable = false;
        static constexpr bool assignment_safe = false;
        using children = type_list<Key, Value>;

        static std::size_t size(const map_type& value) noexcept { return value.size(); }

        static Value& at(map_type& value, std::size_t index)
        {
            if (index >= value.size()) throw std::out_of_range("native map index is outside its current size");
            auto entry = value.begin();
            while (index-- != 0) ++entry;
            return (*entry).value();
        }

        static const Value& at(const map_type& value, std::size_t index)
        {
            if (index >= value.size()) throw std::out_of_range("native map index is outside its current size");
            auto entry = value.begin();
            while (index-- != 0) ++entry;
            return (*entry).value();
        }

        static const Key& key(const map_type& value, std::size_t index)
        {
            if (index >= value.size()) throw std::out_of_range("native map index is outside its current size");
            auto entry = value.begin();
            while (index-- != 0) ++entry;
            return (*entry).key();
        }
    };
}
