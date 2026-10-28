#pragma once
#include "StandardLibrary.h"
#if defined(CE_CONTRACT_STANDALONE)
#include "../../Containers/storage/storage.h"
#else
import "Creator.Containers.h";
#endif
#include "EngineNodeMap.h"
namespace ce
{
    template<class Key> struct engine_integer_hash
    {
        uint64_t operator()(Key key) const noexcept
        {
            const auto value = static_cast<uint64_t>(key);
            return (value ^ (value >> 4) ^ (value >> 32)) * 0x9e3779b97f4a7c15ULL;
        }
    };
    template<class Key> using engine_extractable_hash = std::conditional_t<std::is_integral_v<Key>, engine_integer_hash<Key>, engine_node_hash<Key>>;
    template<class Key, class Value, class Hash = engine_extractable_hash<Key>, class Equal = engine_node_equal<Key>>
    using engine_extractable_map = extractable_hash_map<Key, Value, Hash, Equal, heap_allocator, false>;
}
