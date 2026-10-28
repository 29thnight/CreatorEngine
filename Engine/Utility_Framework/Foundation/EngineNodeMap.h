#pragma once
#include "StandardLibrary.h"

// Application-side policy bridge. Storage is the accepted ce implementation;
// std hash policies remain available for existing engine/SDK key types.
namespace ce {
template<class Key> struct engine_node_hash : std::hash<Key> {};
template<class Key> struct engine_node_equal : std::equal_to<Key> {};
template<> struct engine_node_hash<std::string> {
    using is_transparent = void;
    size_t operator()(std::string_view key) const noexcept { return std::hash<std::string_view>{}(key); }
};
template<> struct engine_node_equal<std::string> : std::equal_to<> {};
template<> struct engine_node_hash<std::wstring> {
    using is_transparent = void;
    size_t operator()(std::wstring_view key) const noexcept { return std::hash<std::wstring_view>{}(key); }
};
template<> struct engine_node_equal<std::wstring> : std::equal_to<> {};
template<class Allocator> struct engine_node_hash<basic_utf8_string<Allocator>> : string_hash {
    using is_transparent = void;
};
template<> struct engine_node_hash<utf8_string_view> : string_hash {
    using is_transparent = void;
};
template<class Allocator> struct engine_node_equal<basic_utf8_string<Allocator>> {
    using is_transparent = void;
    bool operator()(utf8_string_view left, utf8_string_view right) const noexcept { return left == right; }
};
template<> struct engine_node_equal<utf8_string_view> : engine_node_equal<utf8_string> {};
template<class Allocator> struct engine_node_hash<basic_utf16_string<Allocator>> : utf16_string_hash {
    using is_transparent = void;
};
template<> struct engine_node_hash<string_view> : utf16_string_hash {
    using is_transparent = void;
};
template<class Allocator> struct engine_node_equal<basic_utf16_string<Allocator>> {
    using is_transparent = void;
    bool operator()(string_view left, string_view right) const noexcept { return left == right; }
};
template<> struct engine_node_equal<string_view> : engine_node_equal<string> {};

template<class Key, class Value, class Hash=engine_node_hash<Key>, class Equal=engine_node_equal<Key>>
class engine_node_map : public node_hash_map<Key,Value,Hash,Equal,heap_allocator,false> {
    using base = node_hash_map<Key,Value,Hash,Equal,heap_allocator,false>;
public:
    using base::base;
    using base::operator=;
    // Preserve the existing engine-facing exception contract. Only a failed
    // checked access constructs an exception; successful queries allocate nothing.
    template<class Q> Value& at(const Q& key) {
        auto found=this->find(key);
        if(found==this->end()) throw std::out_of_range("engine_node_map::at");
        return found->second;
    }
    template<class Q> const Value& at(const Q& key) const {
        auto found=this->find(key);
        if(found==this->end()) throw std::out_of_range("engine_node_map::at");
        return found->second;
    }
};
}
