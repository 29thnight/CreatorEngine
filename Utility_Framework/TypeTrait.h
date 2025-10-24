#pragma once
#include <typeinfo>
#include <typeindex>
#include <string_view>
#include <unordered_map>
#include <set>
#include <memory>
#include <boost/uuid/uuid.hpp>
#include <boost/uuid/uuid_generators.hpp>
#include <boost/uuid/uuid_io.hpp>
#include "combaseapi.h"

// 간단한 FNV-1a 64비트 constexpr 해시
constexpr uint64_t fnv1a_64(std::string_view s) {
	uint64_t h = 14695981039346656037ull;
	for (unsigned char c : s) {
		h ^= c;
		h *= 1099511628211ull;
	}
	return h;
}

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

// 기본: 벡터 아님
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
	static constexpr size_t INVAILD_ID{ 0 };

	constexpr HashedGuid() = default;
	constexpr HashedGuid(size_t id) : m_ID_Data(id) {}
	~HashedGuid() = default;

	constexpr HashedGuid(const HashedGuid&) = default;
	constexpr HashedGuid(HashedGuid&&) = default;
	constexpr HashedGuid& operator=(const HashedGuid&) = default;
	constexpr HashedGuid& operator=(HashedGuid&&) = default;

	constexpr HashedGuid& operator=(size_t id) {
		m_ID_Data = id; return *this;
	}

	friend constexpr auto operator<=>(const HashedGuid& lhs, const HashedGuid& rhs)
	{
		return lhs.m_ID_Data <=> rhs.m_ID_Data;
	}
	friend constexpr bool operator==(const HashedGuid& lhs, const HashedGuid& rhs) 
	{
		return lhs.m_ID_Data == rhs.m_ID_Data;
	}

	constexpr bool operator==(const size_t& id) const { return m_ID_Data == id; }
	constexpr operator size_t() const { return m_ID_Data; }
};

struct FileGuid
{
	boost::uuids::uuid m_guid;

	static inline boost::uuids::uuid ns_filesystem() noexcept
	{
		return { { 0x12, 0x34, 0x56, 0x78, 0x9a, 0xbc, 0xde, 0xf0,
				  0x12, 0x34, 0x56, 0x78, 0x9a, 0xbc, 0xde, 0xf0 } };
	}

	FileGuid() = default;
	FileGuid(const std::string& str)
	{
		FromString(str);
	}
	FileGuid(const boost::uuids::uuid& guid) : m_guid(guid) {}

	FileGuid(const FileGuid&) = default;
	FileGuid(FileGuid&&) = default;
	~FileGuid() = default;

	FileGuid& operator=(const FileGuid&) = default;
	FileGuid& operator=(FileGuid&&) = default;

	FileGuid& operator=(const std::string& str)
	{
		FromString(str);
		return *this;
	}

	friend auto operator<=>(const FileGuid& lhs, const FileGuid& rhs)
	{
		return lhs.m_guid <=> rhs.m_guid;
	}

	friend bool operator==(const FileGuid& lhs, const FileGuid& rhs)
	{
		return lhs.m_guid == rhs.m_guid;
	}

	bool operator==(const boost::uuids::uuid& guid) const
	{
		return m_guid == guid;
	}

	std::string ToString()
	{
		return boost::uuids::to_string(m_guid);
	}

	void FromString(const std::string& str)
	{
		boost::uuids::string_generator gen;
		m_guid = gen(str);
	}

	void CreateFromName(const std::string& name)
	{
		boost::uuids::name_generator gen(ns_filesystem());
		m_guid = gen(name);
	}
};

static inline FileGuid nullFileGuid{ boost::uuids::nil_uuid() };

namespace std {
	template <>
	struct hash<FileGuid>
	{
		size_t operator()(const FileGuid& guid) const
		{
			const uint64_t* p = reinterpret_cast<const uint64_t*>(&guid);
			return std::hash<uint64_t>{}(p[0]) ^ std::hash<uint64_t>{}(p[1]);
		}
	};
}

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
	// 컴파일타임 타입 ID 생성기(준비중)
	template <class T>
	consteval HashedGuid MakeTypeID() {
		using U = std::remove_cvref_t<T>;
		// 당신의 컴파일타임 이름 함수. TU 간 동일해야 함.
		constexpr std::string_view name = type_name<U>();
		return HashedGuid{ static_cast<size_t>(fnv1a_64(name)) };
	}

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

		//static inline FileGuid MakeFileGUID()
		//{
		//	FileGuid guid();
		//	while (g_fileGuids.find(guid) != g_fileGuids.end())
		//	{
		//		guid = GenerateGUID();
		//	}
		//	g_fileGuids.insert(guid);

		//	return guid;
		//}

		static inline FileGuid MakeFileGUID(const std::string& filePath)
		{
			FileGuid guid;
			guid.CreateFromName(filePath);
			return guid;
		}
	};
} // namespace TypeTrait

#define type_guid(T) TypeTrait::GUIDCreator::GetTypeID<T>()
#define make_guid() TypeTrait::GUIDCreator::MakeGUID()
#define make_file_guid(filePath) TypeTrait::GUIDCreator::MakeFileGUID(filePath)
