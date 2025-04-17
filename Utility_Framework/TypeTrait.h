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

	bool operator==(const size_t& id) const
	{
		return m_ID_Data == id;
	}

	operator size_t() const
	{
		return m_ID_Data;
	}
};

struct FileGuid
{
	unsigned long  Data1;
	unsigned short Data2;
	unsigned short Data3;
	unsigned char  Data4[8];

	FileGuid() = default;
	FileGuid(GUID guid)
	{
		Data1 = guid.Data1;
		Data2 = guid.Data2;
		Data3 = guid.Data3;
		for (int i = 0; i < 8; ++i)
		{
			Data4[i] = guid.Data4[i];
		}
	}

	FileGuid(const std::string& str)
	{
		FromString(str);
	}

	FileGuid(const FileGuid&) = default;
	FileGuid(FileGuid&&) = default;
	~FileGuid() = default;

	FileGuid& operator=(const FileGuid&) = default;
	FileGuid& operator=(FileGuid&&) = default;
	FileGuid& operator=(GUID guid)
	{
		Data1 = guid.Data1;
		Data2 = guid.Data2;
		Data3 = guid.Data3;
		for (int i = 0; i < 8; ++i)
		{
			Data4[i] = guid.Data4[i];
		}
		return *this;
	}

	FileGuid& operator=(const std::string& str)
	{
		FromString(str);
		return *this;
	}

	friend auto operator<=>(const FileGuid& lhs, const FileGuid& rhs)
	{
		return std::memcmp(&lhs, &rhs, sizeof(FileGuid)) <=> 0;
	}

	friend bool operator==(const FileGuid& lhs, const FileGuid& rhs)
	{
		return std::memcmp(&lhs, &rhs, sizeof(FileGuid)) == 0;
	}

	std::string ToString()
	{
		std::ostringstream oss;
		oss << std::hex << std::uppercase << std::setfill('0');
		oss << '{';
		oss << std::setw(8) << Data1 << '-';
		oss << std::setw(4) << Data2 << '-';
		oss << std::setw(4) << Data3 << '-';
		oss << std::setw(2) << static_cast<int>(Data4[0]);
		oss << std::setw(2) << static_cast<int>(Data4[1]) << '-';
		for (int i = 2; i < 8; ++i)
			oss << std::setw(2) << static_cast<int>(Data4[i]);
		oss << '}';
		return oss.str();
	}

	void FromString(const std::string& str)
	{
		std::string clean = str;

		if (!clean.empty() && clean.front() == '{') clean = clean.substr(1);
		if (!clean.empty() && clean.back() == '}') clean.pop_back();

		// sscanf_s용 형식 지정자: %8lx, %4hx, %2hhx
		unsigned int d1;
		unsigned short d2, d3;
		unsigned char d4[8];

		int result = sscanf_s(clean.c_str(), "%8x-%4hx-%4hx-%2hhx%2hhx-%2hhx%2hhx%2hhx%2hhx%2hhx%2hhx",
			&d1, &d2, &d3,
			&d4[0], &d4[1],
			&d4[2], &d4[3], &d4[4], &d4[5], &d4[6], &d4[7]);

		if (result == 11)
		{
			Data1 = d1;
			Data2 = d2;
			Data3 = d3;
			for (int i = 0; i < 8; ++i)
				Data4[i] = d4[i];
		}
		else
		{
			std::cerr << "[FileGuid::FromString] Failed to parse GUID: " << str << std::endl;
		}
	}
};

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
static std::set<FileGuid> g_fileGuids;

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

		static inline FileGuid MakeFileGUID()
		{
			FileGuid guid(GenerateGUID());
			while (g_fileGuids.find(guid) != g_fileGuids.end())
			{
				guid = GenerateGUID();
			}
			g_fileGuids.insert(guid);

			return guid;
		}
	};
} // namespace TypeTrait