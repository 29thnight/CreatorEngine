#pragma once
#include "ReflectionFunction.h"
#include "ReflectionRegister.h"
#include "../ScriptBinder/IObject.h"
#include <yaml-cpp/yaml.h>

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
		HashingString ty_name = prop.typeName.c_str();
		HashedGuid typeID = prop.typeID;

		if (typeID == GUIDCreator::GetTypeID<int>())
		{
			node[prop.name] = std::any_cast<int>(value);
		}
		else if (typeID == GUIDCreator::GetTypeID<float>())
		{
			node[prop.name] = std::any_cast<float>(value);
		}
		else if (typeID == GUIDCreator::GetTypeID<bool>())
		{
			node[prop.name] = std::any_cast<bool>(value);
		}
		else if (typeID == GUIDCreator::GetTypeID<uint32_t>())
		{
			node[prop.name] = std::any_cast<uint32_t>(value);
		}
		else if (typeID == GUIDCreator::GetTypeID<int64_t>())
		{
			node[prop.name] = std::any_cast<int64_t>(value);
		}
		else if (typeID == GUIDCreator::GetTypeID<uint64_t>())
		{
			node[prop.name] = std::any_cast<uint64_t>(value);
		}
		else if (typeID == GUIDCreator::GetTypeID<double>())
		{
			node[prop.name] = std::any_cast<double>(value);
		}
		else if (typeID == GUIDCreator::GetTypeID<std::string>())
		{
			node[prop.name] = std::any_cast<std::string>(value);
		}
		else if (typeID == GUIDCreator::GetTypeID<HashingString>())
		{
			node[prop.name] = std::any_cast<HashingString>(value).ToString();
		}
		else if (typeID == GUIDCreator::GetTypeID<HashedGuid>())
		{
			node[prop.name] = std::any_cast<HashedGuid>(value).m_ID_Data;
		}
		else if (typeID == GUIDCreator::GetTypeID<Mathf::Vector2>())
		{
			Mathf::Vector2 vec = std::any_cast<Mathf::Vector2>(value);
			MetaYml::Node vecNode;
			vecNode.SetStyle(MetaYml::EmitterStyle::Flow);
			vecNode["x"] = vec.x;
			vecNode["y"] = vec.y;
			node[prop.name] = vecNode;
		}
		else if (typeID == GUIDCreator::GetTypeID<Mathf::Vector3>())
		{
			Mathf::Vector3 vec = std::any_cast<Mathf::Vector3>(value);
			MetaYml::Node vecNode;
			vecNode.SetStyle(MetaYml::EmitterStyle::Flow);
			vecNode["x"] = vec.x;
			vecNode["y"] = vec.y;
			vecNode["z"] = vec.z;
			node[prop.name] = vecNode;
		}
		else if (typeID == GUIDCreator::GetTypeID<Mathf::Color4>())
		{
			Mathf::Color4 color = std::any_cast<Mathf::Color4>(value);
			MetaYml::Node vecNode;
			vecNode.SetStyle(MetaYml::EmitterStyle::Flow);
			vecNode["r"] = color.x;
			vecNode["g"] = color.y;
			vecNode["b"] = color.z;
			vecNode["a"] = color.w;
			node[prop.name] = vecNode;
		}
		else if (typeID == GUIDCreator::GetTypeID<Mathf::Vector4>())
		{
			Mathf::Vector4 vec = std::any_cast<Mathf::Vector4>(value);
			MetaYml::Node vecNode;
			vecNode.SetStyle(MetaYml::EmitterStyle::Flow);
			vecNode["x"] = vec.x;
			vecNode["y"] = vec.y;
			vecNode["z"] = vec.z;
			vecNode["w"] = vec.w;
			node[prop.name] = vecNode;
		}
		else if (typeID == GUIDCreator::GetTypeID<Mathf::Quaternion>())
		{
			Mathf::Quaternion quat = std::any_cast<Mathf::Quaternion>(value);
			MetaYml::Node vecNode;
			vecNode.SetStyle(MetaYml::EmitterStyle::Flow);
			vecNode["x"] = quat.x;
			vecNode["y"] = quat.y;
			vecNode["z"] = quat.z;
			vecNode["w"] = quat.w;
			node[prop.name] = vecNode;
		}
		else
		{
			node[prop.name] = "[not suport type]"; // 기타 미지원 타입
		}
	}

	inline void YamlNodeToProperty(const Meta::Property& prop, void* instance, const MetaYml::Node& node)
	{
		if (node[prop.name])
		{
			if (prop.typeID == GUIDCreator::GetTypeID<int>())
			{
				prop.setter(instance, node[prop.name].as<int>());
			}
			else if (prop.typeID == GUIDCreator::GetTypeID<float>())
			{
				prop.setter(instance, node[prop.name].as<float>());
			}
			else if (prop.typeID == GUIDCreator::GetTypeID<bool>())
			{
				prop.setter(instance, node[prop.name].as<bool>());
			}
			else if (prop.typeID == GUIDCreator::GetTypeID<std::string>())
			{
				prop.setter(instance, node[prop.name].as<std::string>());
			}
			else if (prop.typeID == GUIDCreator::GetTypeID<HashingString>())
			{
				prop.setter(instance, HashingString(node[prop.name].as<std::string>()));
			}
			else if (prop.typeID == GUIDCreator::GetTypeID<HashedGuid>())
			{
				prop.setter(instance, HashedGuid(node[prop.name].as<uint32_t>()));
			}
			else if (prop.typeID == GUIDCreator::GetTypeID<Mathf::Vector2>())
			{
				prop.setter(instance, Mathf::Vector2(node[prop.name]["x"].as<float>(), node[prop.name]["y"].as<float>()));
			}
			else if (prop.typeID == GUIDCreator::GetTypeID<Mathf::Vector3>())
			{
				prop.setter(instance, Mathf::Vector3(
						node[prop.name]["x"].as<float>(), 
						node[prop.name]["y"].as<float>(), 
						node[prop.name]["z"].as<float>()
					)
				);
			}
			else if (prop.typeID == GUIDCreator::GetTypeID<Mathf::Vector4>())
			{
				prop.setter(instance, Mathf::Vector4(
					node[prop.name]["x"].as<float>(),
					node[prop.name]["y"].as<float>(),
					node[prop.name]["z"].as<float>(),
					node[prop.name]["w"].as<float>()
				)
				);
			}
			else if (prop.typeID == GUIDCreator::GetTypeID<Mathf::Quaternion>())
			{
				prop.setter(instance, Mathf::Quaternion(
						node[prop.name]["x"].as<float>(), 
						node[prop.name]["y"].as<float>(), 
						node[prop.name]["z"].as<float>(), 
						node[prop.name]["w"].as<float>()
					)
				);
			}
			else if (prop.typeID == GUIDCreator::GetTypeID<std::vector<int>>())
			{
				prop.setter(instance, node[prop.name].as<std::vector<int>>());
			}
			else
			{
				// 기타 미지원 타입
				Debug->LogError("YamlNodeToProperty: Unsupported type");
			}
		}
	}

	inline MetaYml::Node Serialize(void* instance, const Type& type)
	{
		MetaYml::Node node;

		if (type.name == "GameObject")
		{
			node[type.name] = type.typeID.m_ID_Data;
		}
		else if(type.name == "Component")
		{
			const Type& compRealType = *FindTypeByInstance(instance);
			node[compRealType.name] = type.typeID.m_ID_Data;
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
			// typeID 필드는 이미 넣었으므로 생략
			if (std::string(prop.name) == "m_typeID")
				continue;

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
						HashingString ty_name = prop.elementTypeName.c_str();

						if (ty_name == HashingString("int"))
						{
							arrayNode.SetStyle(MetaYml::EmitterStyle::Flow);
							arrayNode.push_back(*static_cast<int*>(element));
						}
						else if (ty_name == HashingString("float"))
						{
							arrayNode.SetStyle(MetaYml::EmitterStyle::Flow);
							arrayNode.push_back(*static_cast<float*>(element));
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
				if (type && type->typeID.m_ID_Data == typeID)
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

	inline Mathf::Vector4 YamlNodeToVector4(const MetaYml::Node& node)
	{
		if (node.IsMap())
		{
			return Mathf::Vector4(node["x"].as<float>(), node["y"].as<float>(), node["z"].as<float>(), node["w"].as<float>());
		}
		return Mathf::Vector4();
	}

	inline Mathf::Vector3 YamlNodeToVector3(const MetaYml::Node& node)
	{
		if (node.IsMap())
		{
			return Mathf::Vector3(node["x"].as<float>(), node["y"].as<float>(), node["z"].as<float>());
		}
		return Mathf::Vector3();
	}

	inline Mathf::Vector2 YamlNodeToVector2(const MetaYml::Node& node)
	{
		if (node.IsMap())
		{
			return Mathf::Vector2(node["x"].as<float>(), node["y"].as<float>());
		}
		return Mathf::Vector2();
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
				if (const Type* subType = MetaDataRegistry->Find(prop.typeName))
				{
					void* subInstance = reinterpret_cast<void*>(reinterpret_cast<char*>(instance) + prop.offset);
					Deserialize(subInstance, *subType, node[prop.name]);
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