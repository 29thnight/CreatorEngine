#pragma once
// ReflectionYamlTemplete.h(레거시 스칼라·벡터 특수화 테이블)는 CT10 감사에서
// 파일째 삭제 — CT7 이후 자기참조만 남은 사강 코드였다. typed 직렬화기
// (ReflectionTypedYml.h)가 파리티 재구현을 소유한다. yaml-cpp는 직접 문다.
#include <yaml-cpp/yaml.h>
// FindTypeByInstance가 IObject를 쓴다. IObject는 L1-2에서 코어로 내려왔다
// (순수 인터페이스 + HashedGuid뿐이라 ScriptBinder 소속일 이유가 없었다).
#include "AuthoringReadNode.h" // D3-b-2b-1b
#include "AuthoringScalarConvert.h" // D3-b-2b-1a
#include "IObject.h"
// ComponentUUIDRegistry(K1-b) 조회 창구. IObject.h가 이미 물고 있어 사실상
// 중복 include지만, 이 파일이 직접 쓰는 것을 명시한다.
#include "TypeTrait.h"
// TypeOf<T>() 정의처 — 템플릿 래퍼 3곳이 T::Reflect() 대신 단일 창구를 쓴다(CT4-d).
#include "ReflectionMeta.h"
#include <unordered_set>

namespace Meta
{
	//FindTypeByInstance base IObject
	inline const Type* FindTypeByInstance(void* instance)
	{
		if (instance == nullptr)
			return nullptr;

		const IObject* comp = static_cast<const IObject*>(instance);
		std::size_t typeID = comp->GetTypeID();

		return MetaDataRegistry->Find(typeID);
	}
}

namespace MetaYml = YAML;

using namespace TypeTrait;
class Entity;

// YamlTemplete에서 이주(CT10) — Entity 직렬화 헤더 키. 쌍이던
// COMPONENT_YAML_KEY는 소비 0이라 함께 은퇴했다(컴포넌트는 실타입 이름 키).
inline constexpr const char GAMEOBJECT_YAML_KEY[] = "Entity";

// CT6-a 런타임 브리지: 타입이 런타임에 정해지는 소비자(씬 로드·컴포넌트
// 벡터)의 typed 디스패치 표. 타입당 함수 포인터 2개 — 레거시의 프로퍼티당
// std::function 2개와 대비된다. 썽크 정의는 ReflectionTypedYml.h, 등록은
// RegisterReflectManual.h(등록 정본).
namespace Meta::Typed
{
	struct TypeOps
	{
		MetaYml::Node (*serialize)(void* instance);
		void (*deserialize)(void* instance, const Authoring::ReadNode& node);

		// CT6-d: 역직렬화 후처리 훅 — ComponentFactory의 타입별 하드코딩
		// 분기(애셋 GUID 해석·리소스 로드)를 컴포넌트 소유의 OnDeserialized로
		// 옮기고, 팩토리는 이 포인터 하나로 디스패치한다. 훅이 없는 타입은
		// nullptr(썽크가 requires로 판별).
		void (*postLoad)(void* instance, const Authoring::ReadNode& node);
	};

	inline std::unordered_map<size_t, TypeOps>& OpsRegistry()
	{
		static std::unordered_map<size_t, TypeOps> s_map;
		return s_map;
	}

	inline const TypeOps* FindTypeOps(size_t typeID)
	{
		auto& m = OpsRegistry();
		auto it = m.find(typeID);
		return (it != m.end()) ? &it->second : nullptr;
	}
}

// 컴포넌트 헤더에 영속 UUID를 함께 적는 필드 키 (SceneGraphRedesignPlan K1-b,
// §5 예외 2). 쓰기(Serialize)와 읽기(ExtractTypeFromYAML) 양쪽이 이 상수 하나를
// 같이 봐야 한다 — 문자열을 따로 박아 두면 한쪽만 고쳤을 때 조용히 어긋난다.
inline constexpr const char* kComponentTypeUUIDKey = "m_typeUUID";

namespace Meta
{
	// PropertyToYamlNode/YamlNodeToProperty 헬퍼는 CT1 조회 순서 역전으로
	// Serialize/Deserialize 본문에 흡수됐다 (호출처가 각 1곳뿐이었다).

	inline MetaYml::Node Serialize(void* instance, const Type& type)
	{
		// CT7: 레거시 Property 워크(프로퍼티당 function/any/조회 폴백) 은퇴 —
		// 등록 타입 전부가 typed 썽크(CT6-a)로 간다. 여기 도달하는 미등록
		// 타입은 등록 정본(RegisterReflectManual) 누락이다.
		if (const Typed::TypeOps* ops = Typed::FindTypeOps(type.typeID.m_ID_Data))
		{
			return ops->serialize(instance);
		}

		Debug->LogError(std::string("Serialize: typed ops 미등록 타입 - ") + type.name
			+ " (RegisterReflectManual.h 목록을 확인하라)");
		return {};
	}


    template<typename T>
    inline MetaYml::Node Serialize(T* instance)
    {
        return Serialize(reinterpret_cast<void*>(instance), TypeOf<T>());
    }

	// 리네임된 타입의 **구 이름 → 현 이름** (§5 읽기 별칭).
	//
	// 디스크의 노드 키는 저작 당시의 타입 이름이다. E6가 GameObject를 Entity로
	// 개명하면서 씬 12개·프리팹 208개에 적힌 `- GameObject: <typeID>` 헤더가 어느
	// 등록 타입과도 안 맞게 됐다 — `.creator`는 gitignore 대상이라 되돌릴 수도 없다.
	// 새 이름으로 재저장되는 순간 이 표를 안 지나므로 자연히 치유된다.
	//
	// K1-b의 영속 UUID(위 0번)가 있는 컴포넌트는 애초에 여기까지 오지 않는다. 이 표가
	// 필요한 것은 UUID를 갖지 않는 오브젝트 노드 헤더다.
	inline std::string_view ResolveRenamedTypeName(std::string_view name, bool& outRenamed)
	{
		struct Rename { std::string_view from; std::string_view to; };
		static constexpr Rename kRenamed[] =
		{
			{ "GameObject", "Entity" },   // 트랙 E6 (2026-08-19)
		};

		for (const auto& rename : kRenamed)
		{
			if (name == rename.from)
			{
				outRenamed = true;
				return rename.to;
			}
		}

		outRenamed = false;
		return name;
	}

	inline const Type* ExtractTypeFromYAML(const Authoring::ReadNode& node)
	{
		if (!node || !node.IsMap())
			return nullptr;

		// 0. 영속 UUID가 있으면 최우선으로 확정한다(K1-b, §5 예외 2). 아래 1·3의
		//    이름·typeID는 둘 다 타입 "이름"에서 나온 값이라 리네임되면 함께
		//    끊기지만(TypeTrait.h GUIDCreator::GetTypeID), UUID는 손으로 박아
		//    리네임 불변이다. 구버전 파일은 이 필드가 없으니 자연히 아래
		//    이름+숫자 폴백으로 내려간다.
		if (const auto uuidNode = node[kComponentTypeUUIDKey])
		{
			Uuid::Uuid16 uuid;
			if (Uuid::TryParse(std::string(uuidNode.Scalar()), uuid))
			{
				if (const std::string* typeName = TypeTrait::ComponentUUIDRegistry::FindNameByUUID(uuid))
				{
					if (const Meta::Type* type = MetaDataRegistry->Find(*typeName))
					{
						return type;
					}
				}
			}
		}

		// 1. key가 typeName이고, value가 typeID일 가능성 → 우선순위 높게
		for (const auto kv : node.Map())
		{
			if (!kv.key.IsScalar() || !kv.value.IsScalar())
				continue;

			const std::string typeName{ kv.key.Scalar() };

			// K1-b가 얹은 m_typeUUID 필드는 값이 문자열이다 — 위 0번에서 이미 못
			// 살렸을 때만 여기까지 내려오므로 그냥 건너뛴다. 쓸 때 항상 타입 헤더
			// (이름→typeID) 바로 뒤에 붙여서(Serialize) 순회 순서상 헤더가 먼저
			// 걸리긴 하지만, 방어적으로 남긴다.
			if (typeName == kComponentTypeUUIDKey)
				continue;

			// ★ 이름 판정이 값 변환보다 **먼저**다. 예전에는 순서가 반대라, 헤더가
			// 어느 등록 타입과도 안 맞으면 루프가 다음 항목으로 내려가 **평범한 데이터
			// 필드의 값을 typeID로 읽으려 들었고** yaml-cpp가 "bad conversion"을 던졌다.
			// 그 예외는 SceneManager::LoadScene의 catch까지 올라가 씬 전체 로드를 널로
			// 끝낸다. E6 리네임 직후 `- GameObject:` 헤더에서 실제로 밟았고, 증상은
			// 헤더가 아니라 그 다음 줄(`m_name: Test1`)을 가리켜 원인을 가렸다.
			bool renamed = false;
			const Meta::Type* type = MetaDataRegistry->Find(ResolveRenamedTypeName(typeName, renamed));
			if (!type)
				continue;

			// 이름이 맞았을 때만 값을 typeID로 읽는다. 폴백 오버로드라 던지지 않는다 —
			// 숫자가 아니면 타입 헤더가 아니라 이름이 우연히 겹친 데이터 필드다.
			// 폴백 형태(`as<T>(0)`)의 등가물: 변환기가 실패하면 0을 쓴다. 던지지
			// 않는 것이 요점이다 — 숫자가 아니면 타입 헤더가 아니라 이름이 우연히
			// 겹친 데이터 필드다.
			std::size_t typeID = 0;
			{
				std::uint64_t parsed = 0;
				if (Authoring::Scalar::TryParseUInt64(kv.value.Scalar(), parsed))
				{
					typeID = static_cast<std::size_t>(parsed);
				}
			}
			if (0 == typeID)
				continue;

			// CT4-b 완화: typeID 정본 교체(FNV64)로 구 파일의 이름+구ID 헤더는 ID가
			// 어긋난다. UUID(위 0번)가 못 잡은 노드의 안전망은 이름뿐이므로 경고를
			// 남기고 수용한다 — 재저장 시 새 ID로 치유된다. 조용한 완화는 금물:
			// UUID 없는 구파일에서 이름 검증이 유일한 오식별 방지선이었다(CT4 함정 1).
			//
			// 단, 리네임 표를 지나온 이름은 ID가 어긋나는 것이 **정상**이다
			// (ID가 이름의 FNV-1a라 개명하면 반드시 달라진다). 그 자리까지 경고하면
			// 오브젝트마다 한 줄씩 나와 진짜 불일치가 묻힌다.
			if (type->typeID != typeID && !renamed)
			{
				Debug->LogWarning(std::string("ExtractTypeFromYAML: typeID 불일치 — 이름으로 수용(구 파일, 재저장 시 치유): ")
					+ typeName);
			}
			return type;
		}

		// 2. fallback: key가 typeName이고 value가 map인 경우 (Unreal 스타일)
		for (const auto kv : node.Map())
		{
			if (kv.key.IsScalar() && kv.value.IsMap())
			{
				const std::string typeName{ kv.key.Scalar() };
				bool renamed = false;
				return MetaDataRegistry->Find(ResolveRenamedTypeName(typeName, renamed));
			}
		}

		// 3. fallback: typeID 필드가 있는 경우
		if (node.HasChild("typeID"))
		{
			std::uint64_t id = 0;
			if (Authoring::Scalar::TryParseUInt64(node["typeID"].Scalar(), id))
			{
				return MetaDataRegistry->Find(static_cast<std::size_t>(id));
			}
		}

		return nullptr;
	}

	// ★ 전환기 오버로드. 아직 backend 노드를 들고 있는 호출부 15곳이 있고,
	//   그들은 `LoadFile`이 옮겨질 때 자연히 `ReadNode`를 받게 된다.
	//   **이 오버로드가 사라지는 것이 그 전환의 완료 신호다.**
	inline const Type* ExtractTypeFromYAML(const MetaYml::Node& node)
	{
		return ExtractTypeFromYAML(Authoring::ReadNode{ node });
	}

	// D3-b-2b-1b-2c: 어댑터를 받는 진입점. 소비자는 이쪽을 쓰고, 아래 backend
	// 오버로드는 아직 옮기지 않은 호출부를 위한 전환기다.
	inline void Deserialize(void* instance, const Type& type, const Authoring::ReadNode& node)
	{
		if (const Typed::TypeOps* ops = Typed::FindTypeOps(type.typeID.m_ID_Data))
		{
			ops->deserialize(instance, node);
			return;
		}

		Debug->LogError(std::string("Deserialize: typed ops 미등록 타입 - ") + type.name
			+ " (RegisterReflectManual.h 목록을 확인하라)");
	}

	template<class T>
	inline void Deserialize(T* instance, const Authoring::ReadNode& node)
	{
		Deserialize(reinterpret_cast<void*>(instance), TypeOf<T>(), node);
	}

	inline void Deserialize(void* instance, const Type& type, const MetaYml::Node& node)
	{
		// CT7: 레거시 워크 은퇴 (Serialize와 동일한 이유).
		if (const Typed::TypeOps* ops = Typed::FindTypeOps(type.typeID.m_ID_Data))
		{
			// D3-b-2b-1b: 썬크가 어댑터를 받는다. 이 경계가 아직 backend 노드를
			// 받는 마지막 지점이고, 1b-2가 그것까지 옮긴다.
			ops->deserialize(instance, Authoring::ReadNode{ node });
			return;
		}

		Debug->LogError(std::string("Deserialize: typed ops 미등록 타입 - ") + type.name
			+ " (RegisterReflectManual.h 목록을 확인하라)");
	}


	template<typename T>
	inline void Deserialize(T* instance, const MetaYml::Node& node)
	{
		Deserialize(reinterpret_cast<void*>(instance), TypeOf<T>(), node);
	}
}

namespace Meta
{
	// 프리팹 인스턴스 갱신 전용 역직렬화 (SceneGraphRedesignPlan P1).
	//
	// 예전에는 "오버라이드됐는가"를 currentNode/prevNode(직전에 알려진 프리팹 스냅샷)의
	// YAML::Dump 문자열 비교로 매번 추론했다. 이제 오버라이드는 호출자(PrefabUtility)가
	// Entity::m_prefabOverrides에서 뽑아 넘기는 명시 목록이다 — overriddenProperties에
	// 있는 프로퍼티 이름은 새 값 적용에서 제외하고(현재 값 그대로), 나머지만 newNode의
	// 값으로 갱신한다.
	inline void DeserializePrefab(void* instance, const Type& type,
		const MetaYml::Node& newNode,
		const std::unordered_set<std::string>& overriddenProperties)
	{
		MetaYml::Node currentNode = Serialize(instance, type);
		MetaYml::Node patchedNode = currentNode;

		if (type.parent)
		{
			DeserializePrefab(instance, *type.parent, newNode, overriddenProperties);
		}

		for (const auto& prop : type.properties)
		{
			if (!newNode[prop.name])
				continue;

			if (overriddenProperties.contains(prop.name))
				continue; // 오버라이드된 속성 — 현재 값을 그대로 둔다

			patchedNode[prop.name] = newNode[prop.name];
		}

		Deserialize(instance, type, patchedNode);
	}

	template<typename T>
	inline void DeserializePrefab(T* instance, const MetaYml::Node& newNode,
		const std::unordered_set<std::string>& overriddenProperties)
	{
		DeserializePrefab(reinterpret_cast<void*>(instance), TypeOf<T>(), newNode, overriddenProperties);
	}
}