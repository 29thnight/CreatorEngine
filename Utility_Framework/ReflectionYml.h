#pragma once
#include "ReflectionYamlTemplete.h"
// FindTypeByInstance가 IObject를 쓴다. IObject는 L1-2에서 코어로 내려왔다
// (순수 인터페이스 + HashedGuid뿐이라 ScriptBinder 소속일 이유가 없었다).
#include "IObject.h"
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
constexpr size_t ComponentTypeID = 3079321533;
namespace Meta
{
	inline void PropertyToYamlNode(const Meta::Property& prop, MetaYml::Node& node, std::any& value)
	{
		if (auto* entry = FindYamlSerializer(prop.typeID))
		{
			entry->toYaml(prop, node, value);
		}
		else
		{
			node[prop.name] = "[not support type]"; // 기타 미지원 타입
		}
	}

	inline void YamlNodeToProperty(const Meta::Property& prop, void* instance, const MetaYml::Node& node)
	{
		if (!node[prop.name])
			return;

		if (auto* entry = FindYamlSerializer(prop.typeID))
		{
			entry->fromYaml(prop, instance, node);
		}
		else
		{
			// 어떤 프로퍼티가 빠지는지 모르면 추적이 불가능하다. 이름과 타입을 남긴다.
			Debug->LogError(std::string("YamlNodeToProperty: Unsupported type - ")
				+ prop.name + " (" + prop.typeName + ")");
		}
	}

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

			// 기본 타입 처리
			PropertyToYamlNode(prop, node, value);
		}

		return node;
	}

    template<typename T>
    inline MetaYml::Node Serialize(T* instance)
    {
        return Serialize(reinterpret_cast<void*>(instance), T::Reflect());
    }

	inline const Type* ExtractTypeFromYAML(const MetaYml::Node& node)
	{
		if (!node || !node.IsMap())
			return nullptr;

		// 1. key가 typeName이고, value가 typeID일 가능성 → 우선순위 높게
		for (const auto& kv : node)
		{
			if (kv.first.IsScalar() && kv.second.IsScalar())
			{
				std::string typeName = kv.first.as<std::string>();
				std::size_t typeID = kv.second.as<std::size_t>();

				const Meta::Type* type = MetaDataRegistry->Find(typeName);
				if (type && type->typeID == typeID)
				{
					return type;  // 이름도 맞고 ID도 맞으면 확정
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

				if (const Type* subType = MetaDataRegistry->Find(prop.typeName))
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
					YamlNodeToProperty(prop, instance, node);
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
		Deserialize(reinterpret_cast<void*>(instance), T::Reflect(), node);
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
		DeserializePrefab(reinterpret_cast<void*>(instance), T::Reflect(), newNode, overriddenProperties);
	}
}