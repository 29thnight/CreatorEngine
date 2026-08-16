#pragma once
#include "ReflectionYamlTemplete.h"
// FindTypeByInstance가 IObject를 쓴다. IObject는 L1-2에서 코어로 내려왔다
// (순수 인터페이스 + HashedGuid뿐이라 ScriptBinder 소속일 이유가 없었다).
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
class GameObject;

// CT6-a 런타임 브리지: 타입이 런타임에 정해지는 소비자(씬 로드·컴포넌트
// 벡터)의 typed 디스패치 표. 타입당 함수 포인터 2개 — 레거시의 프로퍼티당
// std::function 2개와 대비된다. 썽크 정의는 ReflectionTypedYml.h, 등록은
// RegisterReflectManual.h(등록 정본).
namespace Meta::Typed
{
	struct TypeOps
	{
		MetaYml::Node (*serialize)(void* instance);
		void (*deserialize)(void* instance, const MetaYml::Node& node);

		// CT6-d: 역직렬화 후처리 훅 — ComponentFactory의 타입별 하드코딩
		// 분기(애셋 GUID 해석·리소스 로드)를 컴포넌트 소유의 OnDeserialized로
		// 옮기고, 팩토리는 이 포인터 하나로 디스패치한다. 훅이 없는 타입은
		// nullptr(썽크가 requires로 판별).
		void (*postLoad)(void* instance, const MetaYml::Node& node);
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

	inline const Type* ExtractTypeFromYAML(const MetaYml::Node& node)
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
			if (Uuid::TryParse(uuidNode.as<std::string>(), uuid))
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
		for (const auto& kv : node)
		{
			if (kv.first.IsScalar() && kv.second.IsScalar())
			{
				std::string typeName = kv.first.as<std::string>();

				// K1-b가 얹은 m_typeUUID 필드는 값이 문자열이라 as<size_t>()가
				// 던진다 — 위 0번에서 이미 못 살렸을 때만 여기까지 내려오므로
				// 그냥 건너뛴다. 쓸 때 항상 타입 헤더(이름→typeID) 바로 뒤에
				// 붙여서(Serialize) 순회 순서상 헤더가 먼저 걸리긴 하지만,
				// 방어적으로 남긴다.
				if (typeName == kComponentTypeUUIDKey)
					continue;

				std::size_t typeID = kv.second.as<std::size_t>();

				const Meta::Type* type = MetaDataRegistry->Find(typeName);
				if (type)
				{
					// CT4-b 완화: typeID 정본 교체(FNV64)로 구 파일의 이름+구ID
					// 헤더는 ID가 어긋난다. UUID(위 0번)가 못 잡은 노드의 안전망은
					// 이름뿐이므로 경고를 남기고 수용한다 — 재저장 시 새 ID로
					// 치유된다. 조용한 완화는 금물: UUID 없는 구파일에서 이름
					// 검증이 유일한 오식별 방지선이었다(계획 CT4 함정 1).
					if (type->typeID != typeID)
					{
						Debug->LogWarning(std::string("ExtractTypeFromYAML: typeID 불일치 — 이름으로 수용(구 파일, 재저장 시 치유): ")
							+ typeName);
					}
					return type;
				}
			}
		}

		// 2. fallback: key가 typeName이고 value가 map인 경우 (Unreal 스타일)
		for (const auto& kv : node)
		{
			if (kv.first.IsScalar() && kv.second.IsMap())
			{
				std::string typeName = kv.first.as<std::string>();
				return MetaDataRegistry->Find(typeName);
			}
		}

		// 3. fallback: typeID 필드가 있는 경우
		if (node["typeID"])
		{
			std::size_t id = node["typeID"].as<std::size_t>();
			return MetaDataRegistry->Find(id);
		}

		return nullptr;
	}

	inline void Deserialize(void* instance, const Type& type, const MetaYml::Node& node)
	{
		// CT7: 레거시 워크 은퇴 (Serialize와 동일한 이유).
		if (const Typed::TypeOps* ops = Typed::FindTypeOps(type.typeID.m_ID_Data))
		{
			ops->deserialize(instance, node);
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
	// GameObject::m_prefabOverrides에서 뽑아 넘기는 명시 목록이다 — overriddenProperties에
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