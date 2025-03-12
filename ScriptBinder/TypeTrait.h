#pragma once
#include <typeinfo>
#include <typeindex>
#include <string_view>
#include <unordered_map>

template<typename T>
struct MetaType
{
	static constexpr std::string_view type{ "Unknown" };
};

template<typename T>
using MetaRealType = std::remove_pointer_t<std::remove_reference_t<T>>;

template<typename T>
using MetaTypeName = MetaType<MetaRealType<T>>;

#define GENERATE_ID std::type_index(typeid(GameObject)).hash_code() + reinterpret_cast<uintptr_t>(this)