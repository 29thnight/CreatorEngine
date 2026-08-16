#pragma once
#include <typeinfo>
#include <typeindex>
#include <string_view>
#include <unordered_map>
#include <set>
#include <memory>
#include <vector>
#include <utility>
#include <cassert>
// FileGuid의 실체가 여기 있다. boost/uuid를 걷어낸 자리다 — 왜 걷었고
// 무엇으로 동일성을 확인했는지는 Uuid.h 머리에 적었다.
#include "Uuid.h"
#include "combaseapi.h"

// ������ FNV-1a 64��Ʈ constexpr �ؽ�
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

// �⺻: ���� �ƴ�
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

	// 자산 GUID의 네임스페이스. 이 값이 바뀌면 디스크의 .meta 전부가
	// 무효가 되므로 건드리지 않는다.
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

	// 못 읽으면 던진다 — boost::uuids::string_generator와 같은 거동이다.
	// 호출부 여섯 자리 어디도 잡지 않으므로 바꾸지 않았다.
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
	// ������Ÿ�� Ÿ�� ID ������(�غ���)
	template <class T>
	consteval HashedGuid MakeTypeID() {
		using U = std::remove_cvref_t<T>;
		// ����� ������Ÿ�� �̸� �Լ�. TU �� �����ؾ� ��.
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

	// 컴포넌트 타입의 런타임 순차 인덱스 + uint64 비트마스크 (SceneGraphRedesignPlan
	// K1-a). GUIDCreator::GetTypeID<T>()의 값(이름 해시 절단)은 마스크 비트 자리로
	// 쓰기엔 너무 크다 — 여기 인덱스는 "몇 번째로 물어봤는가"일 뿐이라 프로세스
	// 로컬이고 재시작하면 달라질 수 있다. 그래서 디스크에는 절대 적지 않는다(§5) —
	// 적을 대상은 K1-b의 영속 UUID다.
	class ComponentTypeIndex
	{
	public:
		static constexpr uint32_t kInvalid = 0xFFFFFFFFu;
		// uint64 마스크 한 비트당 타입 하나. 컴포넌트 타입이 약 30종(LifecycleRegistry
		// 등록 수)이라 여유 있게 들어간다 — 64종을 넘기면 이 표가 먼저 알려준다.
		static constexpr uint32_t kMaxTypes = 64;

		// 타입마다 처음 묻는 순간 다음 빈 인덱스를 배정하고, 그 뒤로는 같은 값을
		// 돌려준다(magic static — GUIDCreator::GetTypeID<T>()와 같은 관용구).
		template <typename T>
		static inline uint32_t Get()
		{
			static const uint32_t index = Assign(GUIDCreator::GetTypeID<T>());
			return index;
		}

		// T 없이 typeID만으로 찾는 자리(컴포넌트 벡터 순회처럼 런타임 다형이라 T를
		// 되살릴 수 없는 곳)에서 쓴다. 아직 한 번도 Get<T>()로 배정된 적 없는
		// typeID면 kInvalid.
		static inline uint32_t Find(HashedGuid typeID)
		{
			auto& table = Table();
			auto it = table.find(typeID);
			return (it != table.end()) ? it->second : kInvalid;
		}

	private:
		static inline std::unordered_map<HashedGuid, uint32_t>& Table()
		{
			static std::unordered_map<HashedGuid, uint32_t> table;
			return table;
		}

		static inline uint32_t Assign(HashedGuid typeID)
		{
			auto& table = Table();
			auto it = table.find(typeID);
			if (it != table.end())
			{
				return it->second;
			}

			const uint32_t index = static_cast<uint32_t>(table.size());
			assert(index < kMaxTypes && "ComponentTypeIndex: 마스크 비트(64종)를 넘겼다 - uint64 하나로는 더 못 들어간다");
			table.emplace(typeID, index);
			return index;
		}
	};

	// 컴포넌트 타입의 영속(디스크) UUID <-> 이름 조회 (K1-b). 표의 실제 값(리터럴
	// UUID 목록)은 ScriptBinder/ComponentTypeUUID.h에 있다 — 이 파일은
	// Utility_Framework 소속이라 ScriptBinder 헤더를 직접 물면 레이어가 뒤집힌다
	// (Utility_Framework.vcxproj의 include 경로에 ScriptBinder가 없다, 반대는 있다).
	// 그래서 데이터는 여기 두지 않고 등록 창구만 둔다 — 기동 시
	// LifecycleRegistry::RegisterAllComponents가 채운다.
	class ComponentUUIDRegistry
	{
	public:
		static inline void Register(std::string_view typeName, const Uuid::Uuid16& uuid)
		{
			Entries().emplace_back(std::string(typeName), uuid);
		}

		static inline const Uuid::Uuid16* FindByName(std::string_view typeName)
		{
			for (const auto& entry : Entries())
			{
				if (entry.first == typeName) return &entry.second;
			}
			return nullptr;
		}

		static inline const std::string* FindNameByUUID(const Uuid::Uuid16& uuid)
		{
			for (const auto& entry : Entries())
			{
				if (entry.second == uuid) return &entry.first;
			}
			return nullptr;
		}

	private:
		static inline std::vector<std::pair<std::string, Uuid::Uuid16>>& Entries()
		{
			static std::vector<std::pair<std::string, Uuid::Uuid16>> entries;
			return entries;
		}
	};
} // namespace TypeTrait

#define type_guid(T) TypeTrait::GUIDCreator::GetTypeID<T>()
#define make_guid() TypeTrait::GUIDCreator::MakeGUID()
#define make_file_guid(filePath) TypeTrait::GUIDCreator::MakeFileGUID(filePath)
