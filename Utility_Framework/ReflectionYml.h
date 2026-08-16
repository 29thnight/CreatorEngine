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
// CT4-b: 구 typeid 해시 리터럴(3079321533) → 이름 FNV로. Component 타입은
// 상위 층(ScriptBinder)이라 여기서 참조할 수 없지만, 새 typeID가 "정규화된
// 이름의 FNV-1a 64"로 정의되므로 이름만으로 같은 값을 재현할 수 있다
// (TypeTrait.h type_name의 키워드 제거가 그 전제).
constexpr size_t ComponentTypeID = static_cast<size_t>(fnv1a_64("Component"));

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
		MetaYml::Node node;

		if (type.name == GAMEOBJECT_YAML_KEY)
		{
			node[type.name] = type.typeID.m_ID_Data;
		}
		else if(type.name == COMPONENT_YAML_KEY)
		{
			const Type& compRealType = *FindTypeByInstance(instance);
			node[compRealType.name] = compRealType.typeID.m_ID_Data;

			// K1-b: 영속 UUID를 이름해시 옆에 함께 적는다(§5 예외 2). 아직
			// ComponentTypeUUID 표에 없는 타입(등록 누락)이면 조용히 생략 —
			// 읽는 쪽은 그 경우 이름+숫자 폴백으로 내려간다.
			if (const Uuid::Uuid16* uuid = TypeTrait::ComponentUUIDRegistry::FindByName(compRealType.name))
			{
				node[kComponentTypeUUIDKey] = Uuid::ToString(*uuid);
			}
		}

		// 부모 먼저 직렬화
		if (type.parent)
		{
			MetaYml::Node parentNode = Serialize(instance, *type.parent);
			for (const auto& it : parentNode)
			{
				node[it.first.Scalar()] = it.second;
			}
		}

		// 프로퍼티 순회
		for (const auto& prop : type.properties)
		{
			std::any value = prop.getter(instance);

			// 벡터 처리
			if (prop.isVector)
			{
				auto iter = prop.createVectorIterator(instance);
				MetaYml::Node arrayNode;

				while (iter->IsValid())
				{
					void* element = iter->Get();

					if (const Type* subType = MetaDataRegistry->Find(prop.elementTypeID))
					{
						if (subType->typeID.m_ID_Data != ComponentTypeID)
						{
							arrayNode.push_back(Serialize(element, *subType));
						}
						else
						{
							const Type* compType = FindTypeByInstance(element);
							if (compType)
							{
								arrayNode.push_back(Serialize(element, *compType));
							}
							else
							{
								arrayNode.push_back(MetaYml::Node()); // unknown component
							}
						}
					}
					else
					{
						HashedGuid ty_id = prop.elementTypeID;
						if (auto* vecEntry = FindYamlVectorEntry(ty_id))
						{
							vecEntry->toYamlElement(arrayNode, element);
						}
						else
						{
							Debug->LogError("Serialize: Unsupported vector element type");
						}
					}

					iter->Next();
				}

				node[prop.name] = arrayNode;
				continue;
			}

			// 포인터 처리
			if (prop.isPointer)
			{
				void* ptr = TypeCast->ToVoidPtr(prop.typeInfo, value);
				if (ptr)
				{
					if (const Type* subType = MetaDataRegistry->Find(prop.typeName))
					{
						node[prop.name] = Serialize(ptr, *subType);
					}
					else
					{
						node[prop.name] = MetaYml::Node(); // unknown pointer
					}
				}
				else
				{
					node[prop.name] = MetaYml::Node(); // nullptr
				}
				continue;
			}

			// 스칼라 먼저 (CT1 — 조회 순서 역전). 예전에는 enum 맵 실패 →
			// struct 맵 실패를 거쳐야 int 하나를 썼다. 스칼라 테이블 23종과
			// Registry 등록 76타입의 교집합은 공집합(정찰 실측)이라, 순서를
			// 뒤집어도 다른 분기로 새는 타입은 없다.
			if (auto* entry = FindYamlSerializer(prop.typeID))
			{
				entry->toYaml(prop, node, value);
				continue;
			}

			// enum 처리
			if (MetaEnumRegistry->Find(prop.typeName))
			{
				node[prop.name] = std::any_cast<int>(value);
				continue;
			}

			// struct 처리
			if (const Type* subType = MetaDataRegistry->Find(prop.typeName))
			{
				void* subInstance = reinterpret_cast<void*>(reinterpret_cast<char*>(instance) + prop.offset);
				node[prop.name] = Serialize(subInstance, *subType);
				continue;
			}

			node[prop.name] = "[not support type]"; // 기타 미지원 타입
		}

		return node;
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
		// 부모 먼저 역직렬화
		if (type.parent)
		{
			Deserialize(instance, *type.parent, node);
		}
		// 프로퍼티 순회
		for (const auto& prop : type.properties)
		{
			if (node[prop.name])
			{
				if (prop.isPointer)
				{
					const MetaYml::Node& subNode = node[prop.name];
					if (!subNode || !subNode.IsMap()) continue;

					// 1. 타입 정보 추출
					const Type* subType = MetaDataRegistry->Find(prop.typeName);
					if (!subType)
					{
						Debug->LogError("Deserialize Pointer: Type not found");
						continue;
					}

					// 2. 인스턴스 생성
					void* newPtr = MetaFactoryRegistry->Create(prop.typeName);
					if (!newPtr)
					{
						Debug->LogError("Deserialize Pointer: Factory create failed");
						continue;
					}

					// 3. 역직렬화
					Deserialize(newPtr, *subType, subNode);

					std::any boxed = TypeCast->MakeAnyFromRaw(prop.typeInfo, newPtr);
					if (boxed.has_value())
						prop.setter(instance, boxed);
					continue;
				}

				if (prop.isVector && !prop.isElementPointer)
				{
					const YAML::Node& arrayNode = node[prop.name];
					if (!arrayNode || !arrayNode.IsSequence())
						continue;

					if (const auto* ve = FindYamlVectorEntry(prop.elementTypeID))
					{
						// offset 기반으로 instance 안의 vector<T> 를 직접 채운다
						ve->fromYamlVector(instance, prop.offset, arrayNode);
						continue;
					}
					else
					{
						void* rawVecPtr = VectorFactory->Create(prop.typeID);
						if (!rawVecPtr) continue;

						std::any boxed = TypeCast->MakeAnyFromRaw(prop.typeInfo, rawVecPtr);
						if (boxed.has_value())
							prop.setter(instance, boxed);

						const Type* elementType = MetaDataRegistry->Find(prop.elementTypeID);
						if (!elementType) continue;

						VectorInvoker->Invoke(prop.typeID, rawVecPtr, arrayNode, elementType);
						continue;
					}
				}

				// 스칼라 먼저 (CT1) — Serialize 쪽과 순서를 맞춘다. 종전에는
				// 여기만 struct→enum→스칼라라 양쪽 순서가 달랐다(문서화되지
				// 않은 비대칭 — 교집합 공집합이라 실해는 없었지만 정렬한다).
				if (auto* entry = FindYamlSerializer(prop.typeID))
				{
					entry->fromYaml(prop, instance, node);
				}
				else if (const Type* subType = MetaDataRegistry->Find(prop.typeName))
				{
					void* subInstance = reinterpret_cast<void*>(reinterpret_cast<char*>(instance) + prop.offset);
					Deserialize(subInstance, *subType, node[prop.name]);
				}
				else if (const EnumType* enumType = MetaEnumRegistry->Find(prop.typeName))
				{
					int enumValue = node[prop.name].as<int>();
					prop.setter(instance, enumValue);
				}
				else
				{
					// 어떤 프로퍼티가 빠지는지 모르면 추적이 불가능하다. 이름과 타입을 남긴다.
					Debug->LogError(std::string("Deserialize: Unsupported type - ")
						+ prop.name + " (" + prop.typeName + ")");
				}
			}
			else
			{
				continue;
			}
		}
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