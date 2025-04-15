#pragma once
#include <typeinfo>
#include <typeindex>
#include <string_view>
#include <unordered_map>
#include <set>
#include <memory>
#include "combaseapi.h"

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

// ±‚∫ª: ∫§≈Õ æ∆¥‘
template<typename T>
struct VectorElementType { using Type = void; };

// std::vector<T>
template<typename T>
struct VectorElementType<std::vector<T>> { using Type = T; };

template<typename T>
using VectorElementTypeT = typename VectorElementType<T>::Type;

template<typename T>
constexpr bool is_shared_ptr_v = false;

template<typename T>
constexpr bool is_shared_ptr_v<std::shared_ptr<T>> = true;

template<typename T>
constexpr bool is_vector_v = false;

template<typename T>
constexpr bool is_vector_v<std::vector<T>> = true;

struct HashedGuid
{
	size_t m_ID_Data{ 0 };

	HashedGuid() = default;
	HashedGuid(size_t id) : m_ID_Data(id) {}
	~HashedGuid() = default;

	HashedGuid(const HashedGuid&) = default;
	HashedGuid(HashedGuid&&) = default;
	HashedGuid& operator=(const HashedGuid&) = default;
	HashedGuid& operator=(HashedGuid&&) = default;
	HashedGuid& operator=(size_t id)
	{
		m_ID_Data = id;
		return *this;
	}

	friend auto operator<=>(const HashedGuid& lhs, const HashedGuid& rhs)
	{
		return lhs.m_ID_Data <=> rhs.m_ID_Data;
	}

	friend bool operator==(const HashedGuid& lhs, const HashedGuid& rhs)
	{
		return lhs.m_ID_Data == rhs.m_ID_Data;
	}

	operator size_t() const
	{
		return m_ID_Data;
	}
};

namespace std
{
	template<>
	struct hash<HashedGuid>
	{
		size_t operator()(const HashedGuid& guid) const noexcept
		{
			return hash<size_t>{}(guid.m_ID_Data);
		}
	};
}

static std::set<HashedGuid> g_guids;

namespace TypeTrait
{
	class GUIDCreator
	{
	public:
		template <typename T>
		static inline HashedGuid GetTypeID()
		{
			static const HashedGuid typeID = static_cast<uint32_t>(std::type_index(typeid(T)).hash_code());
			return typeID;
		}

		static inline void InsertGUID(HashedGuid guid)
		{
			g_guids.insert(guid);
		}

		static inline void EraseGUID(HashedGuid guid)
		{
			g_guids.erase(guid);
		}

		static inline HashedGuid MakeGUID()
		{
			GUID guid = GenerateGUID();
			HashedGuid hash = ConvertGUIDToHash(guid);
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