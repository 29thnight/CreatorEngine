#pragma once
#include <typeinfo>
#include <typeindex>
#include <string_view>
#include <unordered_map>
#include <set>

template<typename T>
struct MetaType
{
	static constexpr std::string_view type{ "Unknown" };
};

template<typename T>
using MetaRealType = std::remove_pointer_t<std::remove_reference_t<T>>;

template<typename T>
using MetaTypeName = MetaType<MetaRealType<T>>;

inline GUID GenerateGUID()
{
	GUID guid;
	HRESULT hr = CoCreateGuid(&guid);
	return guid;
}

inline size_t ConvertGUIDToHash(const GUID& guid)
{
	return guid.Data1 + guid.Data2 + guid.Data3;
}

static std::set<size_t> g_guids;

namespace TypeTrait
{
	class GUIDCreator
	{
	public:
		static inline size_t GetGUID()
		{
			return MakeGUID();
		}

		template <typename T>
		static inline uint32_t GetTypeID()
		{
			static const uint32_t typeID = static_cast<uint32_t>(std::type_index(typeid(T)).hash_code());
			return typeID;
		}

		static inline void InsertGUID(size_t guid)
		{
			g_guids.insert(guid);
		}

		static inline void EraseGUID(size_t guid)
		{
			g_guids.erase(guid);
		}

		static inline size_t MakeGUID()
		{
			GUID guid = GenerateGUID();
			size_t hash = ConvertGUIDToHash(guid);
			while (g_guids.find(hash) != g_guids.end())
			{
				guid = GenerateGUID();
				hash = ConvertGUIDToHash(guid);
			}
			g_guids.insert(hash);

			return hash;
		}
	};
} // namespace TypeTrait