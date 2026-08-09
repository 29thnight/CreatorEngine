#pragma once
#include <typeinfo>
#include <typeindex>
#include <string_view>
#include <unordered_map>
#include <set>
#include <memory>
// FileGuidì˜ ì‹¤ì²´ê°€ ì—¬ê¸° ìˆë‹¤. boost/uuidë¥¼ ê±·ì–´ë‚¸ ìë¦¬ë‹¤ â€” ì™œ ê±·ì—ˆê³ 
// ë¬´ì—‡ìœ¼ë¡œ ë™ì¼ì„±ì„ í™•ì¸í–ˆëŠ”ì§€ëŠ” Uuid.h ë¨¸ë¦¬ì— ì ì—ˆë‹¤.
#include "Uuid.h"
#include "combaseapi.h"

// °£´ÜÇÑ FNV-1a 64ºñÆ® constexpr ÇØ½Ã
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

// ±âº»: º¤ÅÍ ¾Æ´Ô
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
	Uuid::Uuid16 m_guid;

	// ìì‚° GUIDì˜ ë„¤ì„ìŠ¤í˜ì´ìŠ¤. ì´ ê°’ì´ ë°”ë€Œë©´ ë””ìŠ¤í¬ì˜ .meta ì „ë¶€ê°€
	// ë¬´íš¨ê°€ ë˜ë¯€ë¡œ ê±´ë“œë¦¬ì§€ ì•ŠëŠ”ë‹¤.
	static inline Uuid::Uuid16 ns_filesystem() noexcept
	{
		return Uuid::Uuid16{ { 0x12, 0x34, 0x56, 0x78, 0x9a, 0xbc, 0xde, 0xf0,
							   0x12, 0x34, 0x56, 0x78, 0x9a, 0xbc, 0xde, 0xf0 } };
	}

	FileGuid() = default;
	FileGuid(const std::string& str)
	{
		FromString(str);
	}
	FileGuid(const Uuid::Uuid16& guid) : m_guid(guid) {}

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

	bool operator==(const Uuid::Uuid16& guid) const
	{
		return m_guid == guid;
	}

	std::string ToString() const
	{
		return Uuid::ToString(m_guid);
	}

	// ëª» ì½ìœ¼ë©´ ë˜ì§„ë‹¤ â€” boost::uuids::string_generatorì™€ ê°™ì€ ê±°ë™ì´ë‹¤.
	// í˜¸ì¶œë¶€ ì—¬ì„¯ ìë¦¬ ì–´ë””ë„ ì¡ì§€ ì•Šìœ¼ë¯€ë¡œ ë°”ê¾¸ì§€ ì•Šì•˜ë‹¤.
	void FromString(const std::string& str)
	{
		m_guid = Uuid::Parse(str);
	}

	void CreateFromName(const std::string& name)
	{
		m_guid = Uuid::FromName(ns_filesystem(), name);
	}
};

static inline FileGuid nullFileGuid{ Uuid::Nil() };

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
	// ÄÄÆÄÀÏÅ¸ÀÓ Å¸ÀÔ ID »ı¼º±â(ÁØºñÁß)
	template <class T>
	consteval HashedGuid MakeTypeID() {
		using U = std::remove_cvref_t<T>;
		// ´ç½ÅÀÇ ÄÄÆÄÀÏÅ¸ÀÓ ÀÌ¸§ ÇÔ¼ö. TU °£ µ¿ÀÏÇØ¾ß ÇÔ.
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
