#pragma once
#include "ReflectionFunction.h"
#include "ReflectionRegister.h"
#include "IObject.h"
#include "DataSystem.h"
#include "ModuleBehavior.h"
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
		HashedGuid typeID = prop.typeID;

		if (typeID == type_guid(int))
		{
			node[prop.name] = std::any_cast<int>(value);
		}
		else if (typeID == type_guid(float))
		{
			node[prop.name] = std::any_cast<float>(value);
		}
		else if (typeID == type_guid(bool))
		{
			node[prop.name] = std::any_cast<bool>(value);
		}
		else if (typeID == type_guid(uint32_t))
		{
			node[prop.name] = std::any_cast<uint32_t>(value);
		}
		else if (typeID == type_guid(int64_t))
		{
			node[prop.name] = std::any_cast<int64_t>(value);
		}
		else if (typeID == type_guid(uint64_t))
		{
			node[prop.name] = std::any_cast<uint64_t>(value);
		}
		else if (typeID == type_guid(double))
		{
			node[prop.name] = std::any_cast<double>(value);
		}
		else if (typeID == type_guid(file::path))
		{
			node[prop.name] = std::any_cast<file::path>(value).string();
		}
		else if (typeID == type_guid(std::string))
		{
			node[prop.name] = std::any_cast<std::string>(value);
		}
		else if (typeID == type_guid(HashingString))
		{
			node[prop.name] = std::any_cast<HashingString>(value).ToString();
		}
		else if (typeID == type_guid(HashedGuid))
		{
			node[prop.name] = std::any_cast<HashedGuid>(value).m_ID_Data;
		}
		else if (typeID == type_guid(Mathf::Vector2))
		{
			Mathf::Vector2 vec = std::any_cast<Mathf::Vector2>(value);
			MetaYml::Node vecNode;
			vecNode.SetStyle(MetaYml::EmitterStyle::Flow);
			vecNode["x"] = vec.x;
			vecNode["y"] = vec.y;
			node[prop.name] = vecNode;
		}
		else if (typeID == type_guid(Mathf::Vector3))
		{
			Mathf::Vector3 vec = std::any_cast<Mathf::Vector3>(value);
			MetaYml::Node vecNode;
			vecNode.SetStyle(MetaYml::EmitterStyle::Flow);
			vecNode["x"] = vec.x;
			vecNode["y"] = vec.y;
			vecNode["z"] = vec.z;
			node[prop.name] = vecNode;
		}
		else if (typeID == type_guid(Mathf::Color4))
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
		else if (typeID == type_guid(Mathf::Vector4))
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
		else if (typeID == type_guid(Mathf::Quaternion))
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
		else if (typeID == type_guid(FileGuid))
		{
			FileGuid fileGuid = std::any_cast<FileGuid>(value);
			node[prop.name] = fileGuid.ToString();
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
			if (prop.typeID == type_guid(int))
			{
				prop.setter(instance, node[prop.name].as<int>());
			}
			else if (prop.typeID == type_guid(float))
			{
				prop.setter(instance, node[prop.name].as<float>());
			}
			else if (prop.typeID == type_guid(bool))
			{
				prop.setter(instance, node[prop.name].as<bool>());
			}
			else if (prop.typeID == type_guid(uint32_t))
			{
				prop.setter(instance, node[prop.name].as<uint32_t>());
			}
			else if (prop.typeID == type_guid(int64_t))
			{
				prop.setter(instance, node[prop.name].as<int64_t>());
			}
			else if (prop.typeID == type_guid(uint64_t))
			{
				prop.setter(instance, node[prop.name].as<uint64_t>());
			}
			else if (prop.typeID == type_guid(std::string))
			{
				prop.setter(instance, node[prop.name].as<std::string>());
			}
			else if (prop.typeID == type_guid(file::path))
			{
				prop.setter(instance, node[prop.name].as<std::string>());
			}
			else if (prop.typeID == type_guid(HashingString))
			{
				prop.setter(instance, HashingString(node[prop.name].as<std::string>()));
			}
			else if (prop.typeID == type_guid(HashedGuid))
			{
				prop.setter(instance, HashedGuid(node[prop.name].as<uint32_t>()));
			}
			else if (prop.typeID == type_guid(Mathf::Vector2))
			{
				prop.setter(instance, Mathf::Vector2(node[prop.name]["x"].as<float>(), node[prop.name]["y"].as<float>()));
			}
			else if (prop.typeID == type_guid(Mathf::Vector3))
			{
				prop.setter(instance, Mathf::Vector3(
						node[prop.name]["x"].as<float>(), 
						node[prop.name]["y"].as<float>(), 
						node[prop.name]["z"].as<float>()
					)
				);
			}
			else if (prop.typeID == type_guid(Mathf::Vector4))
			{
				prop.setter(instance, Mathf::Vector4(
					node[prop.name]["x"].as<float>(),
					node[prop.name]["y"].as<float>(),
					node[prop.name]["z"].as<float>(),
					node[prop.name]["w"].as<float>()
				)
				);
			}
            else if (prop.typeID == type_guid(Mathf::Color4))
            {
                prop.setter(instance, Mathf::Color4(
                    node[prop.name]["r"].as<float>(), 
                    node[prop.name]["g"].as<float>(), 
                    node[prop.name]["b"].as<float>(), 
                    node[prop.name]["a"].as<float>()
                )
                );
            }
			else if (prop.typeID == type_guid(Mathf::Quaternion))
			{
				prop.setter(instance, Mathf::Quaternion(
						node[prop.name]["x"].as<float>(), 
						node[prop.name]["y"].as<float>(),
						node[prop.name]["z"].as<float>(), 
						node[prop.name]["w"].as<float>()
					)
				);
			}
			else if (prop.typeID == GUIDCreator::GetTypeID<FileGuid>())
			{
				prop.setter(instance, FileGuid(node[prop.name].as<std::string>()));
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
			node[compRealType.name] = compRealType.typeID.m_ID_Data;
			if (compRealType.typeID == type_guid(ModuleBehavior))
			{
				// ModuleBehavior는 특별히 처리
				auto* script = static_cast<ModuleBehavior*>(instance);
				const auto& scriptType = script->ScriptReflect();

				MetaYml::Node scriptNode = Serialize(instance, scriptType);
				for (const auto& it : scriptNode)
				{
					node[it.first.Scalar()] = it.second;
				}
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
						HashedGuid ty_name = prop.elementTypeID;

						if (ty_name == type_guid(int))
						{
							arrayNode.SetStyle(MetaYml::EmitterStyle::Flow);
							arrayNode.push_back(*static_cast<int*>(element));
						}
						else if (ty_name == type_guid(float))
						{
							arrayNode.SetStyle(MetaYml::EmitterStyle::Flow);
							arrayNode.push_back(*static_cast<float*>(element));
						}
						else if (ty_name == type_guid(bool))
						{
							arrayNode.SetStyle(MetaYml::EmitterStyle::Flow);
							arrayNode.push_back(*static_cast<bool*>(element));
						}
						else if (ty_name == type_guid(std::string))
						{
							arrayNode.SetStyle(MetaYml::EmitterStyle::Flow);
							arrayNode.push_back(*static_cast<std::string*>(element));
						}
						else if (ty_name == type_guid(HashingString))
						{
							arrayNode.SetStyle(MetaYml::EmitterStyle::Flow);
							arrayNode.push_back(static_cast<HashingString*>(element)->ToString());
						}
						else if (ty_name == type_guid(HashedGuid))
						{
							arrayNode.SetStyle(MetaYml::EmitterStyle::Flow);
							arrayNode.push_back(static_cast<HashedGuid*>(element)->m_ID_Data);
						}
						else if (ty_name == type_guid(FileGuid))
						{
							FileGuid fileGuid = *static_cast<FileGuid*>(element);
							arrayNode.SetStyle(MetaYml::EmitterStyle::Flow);
							arrayNode.push_back(fileGuid.ToString());
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

					if (prop.elementTypeID == type_guid(int))
					{
						auto vec = node[prop.name].as<std::vector<int>>();
						auto castInstance = reinterpret_cast<std::vector<int>*>(reinterpret_cast<char*>(instance) + prop.offset);
						castInstance->clear(); // Clear existing elements
						for (const auto& elem : vec)
						{
							castInstance->push_back(elem);
						}
					}
					else if (prop.elementTypeID == type_guid(float))
					{
						auto vec = node[prop.name].as<std::vector<float>>();
						auto castInstance = reinterpret_cast<std::vector<float>*>(reinterpret_cast<char*>(instance) + prop.offset);
						castInstance->clear(); // Clear existing elements
						for (const auto& elem : vec)
						{
							castInstance->push_back(elem);
						}
					}
					else if (prop.elementTypeID == type_guid(bool))
					{
						auto vec = node[prop.name].as<std::vector<bool>>();
						auto castInstance = reinterpret_cast<std::vector<bool>*>(reinterpret_cast<char*>(instance) + prop.offset);
						castInstance->clear(); // Clear existing elements
						for (const auto& elem : vec)
						{
							castInstance->push_back(elem);
						}
					}
					else if (prop.elementTypeID == type_guid(std::string))
					{
						auto vec = node[prop.name].as<std::vector<std::string>>();
						auto castInstance = reinterpret_cast<std::vector<std::string>*>(reinterpret_cast<char*>(instance) + prop.offset);
						castInstance->clear(); // Clear existing elements
						for (const auto& elem : vec)
						{
							castInstance->push_back(elem);
						}
					}
					else if (prop.elementTypeID == type_guid(HashingString))
					{
						auto vec = node[prop.name].as<std::vector<std::string>>();
						auto castInstance = reinterpret_cast<std::vector<HashingString>*>(reinterpret_cast<char*>(instance) + prop.offset);
						castInstance->clear(); // Clear existing elements
						for (const auto& elem : vec)
						{
							castInstance->emplace_back(elem);
						}
					}
					else if (prop.elementTypeID == type_guid(HashedGuid))
					{
						auto vec = node[prop.name].as<std::vector<uint32_t>>();
						auto castInstance = reinterpret_cast<std::vector<HashedGuid>*>(reinterpret_cast<char*>(instance) + prop.offset);
						castInstance->clear(); // Clear existing elements
						for (const auto& elem : vec)
						{
							castInstance->emplace_back(elem);
						}
					}
					else if (prop.elementTypeID == type_guid(FileGuid))
					{
						auto vec = node[prop.name].as<std::vector<std::string>>();
						auto castInstance = reinterpret_cast<std::vector<FileGuid>*>(reinterpret_cast<char*>(instance) + prop.offset);
						castInstance->clear(); // Clear existing elements
						for (const auto& elem : vec)
						{
							castInstance->emplace_back(elem);
						}
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
	inline void DeserializePrefab(void* instance, const Type& type,
		const MetaYml::Node& newNode,
		MetaYml::Node& prevNode)
	{
		MetaYml::Node currentNode = Serialize(instance, type);
		MetaYml::Node patchedNode = currentNode;

		if (type.parent)
		{
			DeserializePrefab(instance, *type.parent, newNode, prevNode);
		}

		for (const auto& prop : type.properties)
		{
			if (!newNode[prop.name])
				continue;

			const auto& currProp = currentNode[prop.name];
			const auto& prevProp = prevNode[prop.name];

			if (currProp && prevProp && YAML::Dump(currProp) == YAML::Dump(prevProp))
			{
				patchedNode[prop.name] = newNode[prop.name];
			}
		}

		Deserialize(instance, type, patchedNode);
		prevNode = newNode;
	}

	template<typename T>
	inline void DeserializePrefab(T* instance, const MetaYml::Node& newNode,
		MetaYml::Node& prevNode)
	{
		DeserializePrefab(reinterpret_cast<void*>(instance), T::Reflect(), newNode, prevNode);
	}
}