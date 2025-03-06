#pragma once
#include <string_view>

template<typename T>
struct MetaType
{
	static constexpr std::string_view type{ "Unknown" };
};

template<typename T>
using MetaRealType = std::remove_pointer_t<std::remove_reference_t<T>>;

template<typename T>
using MetaTypeName = MetaType<MetaRealType<T>>;

template<typename T>
constexpr std::string_view GetTypeName()
{
	return MetaTypeName<T>::type;
}