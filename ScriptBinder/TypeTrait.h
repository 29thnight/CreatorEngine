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

#define GENERATE_GUID ConvertGUIDToHash(GenerateGUID())
#define GENERATE_CLASS_GUID std::type_index(typeid(*this)).hash_code()

inline GUID GenerateGUID()
{
	GUID guid;
	CoCreateGuid(&guid);
	return guid;
}

inline size_t ConvertGUIDToHash(const GUID& guid)
{
	return guid.Data1 + guid.Data2 + guid.Data3;
}