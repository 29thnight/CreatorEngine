#pragma once
#include "ReflectionFunction.h"
#include "ReflectionRegister.h"
#include "../ScriptBinder/IComponent.h"
#include <yaml-cpp/yaml.h>

namespace Meta
{
	// 컴포넌트 기준 Type 찾기
	inline const Type* FindTypeByInstance(void* instance)
	{
		if (instance == nullptr)
			return nullptr;

		const IComponent* comp = static_cast<const IComponent*>(instance);
		std::size_t typeID = comp->GetTypeID();

		return MetaDataRegistry->Find(typeID);
	}
}

namespace MetaYml = YAML;
class GameObject;
constexpr size_t ComponentTypeID = 3079321533;
namespace Meta
{
	inline void LegacyTypeHandler(const std::string& type_name, const Meta::Property& prop, MetaYml::Node& node, std::any& value)
	{
		HashingString ty_name = type_name.c_str();

		if (ty_name == HashingString("int"))
			node[prop.name] = std::any_cast<int>(value);
		else if (ty_name == HashingString("float"))
			node[prop.name] = std::any_cast<float>(value);
		else if (ty_name == HashingString("bool"))
			node[prop.name] = std::any_cast<bool>(value);
		else if (ty_name == HashingString("std::string"))
			node[prop.name] = std::any_cast<std::string>(value);
		else if (ty_name == HashingString("HashingString"))
			node[prop.name] = std::any_cast<HashingString>(value).ToString();
		else if (ty_name == HashingString("HashedGuid"))
			node[prop.name] = std::any_cast<HashedGuid>(value).m_ID_Data;
		else
			node[prop.name] = MetaYml::Node(); // 기타 미지원 타입
	}

	inline MetaYml::Node Serialize(void* instance, const Type& type)
	{
		MetaYml::Node node;

		// 부모 먼저 직렬화
		if (type.parent)
		{
			node[type.name] = Serialize(instance, *type.parent);
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
						if(subType->typeID.m_ID_Data != ComponentTypeID)
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

				node[type.name][prop.name] = arrayNode;
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
			LegacyTypeHandler(prop.typeName, prop, node, value);
		}

		return node;
	}


    template<typename T>
    inline MetaYml::Node Serialize(T* instance)
    {
        return Serialize(reinterpret_cast<void*>(instance), T::Reflect());
    }

	inline void Deserialize(void* instance, const Type& type, const MetaYml::Node& node)
	{
		if (type.parent)
		{
			Deserialize(instance, *type.parent, node);
		}

		for (const auto& prop : type.properties)
		{
			if (!node[prop.name])
				continue;

			// Vector 처리
			if (IsVectorType(prop.typeName))
			{
				const std::string elementTypeName = ExtractVectorElementType(prop.typeName);
				auto vec = std::any_cast<std::vector<void*>*>(prop.getter(instance));
				vec->clear();

				for (auto elementNode : node[prop.name])
				{
					if (const Type* subType = MetaDataRegistry->Find(elementTypeName))
					{
						void* elementInstance = MetaFactoryRegistry->Create(elementTypeName);
						Deserialize(elementInstance, *subType, elementNode);
						vec->push_back(elementInstance);
					}
				}
				continue;
			}

			// Pointer 처리
			if (prop.isPointer)
			{
				void* ptr = TypeCast->ToVoidPtr(prop.typeInfo, prop.getter(instance));
				if (!ptr)
				{
					ptr = MetaFactoryRegistry->Create(prop.typeName);
					prop.setter(instance, ptr);
				}

				if (const Type* subType = MetaDataRegistry->Find(prop.typeName))
				{
					Deserialize(ptr, *subType, node[prop.name]);
				}
				continue;
			}

			// Enum 처리
			if (MetaEnumRegistry->Find(prop.typeName))
			{
				prop.setter(instance, node[prop.name].as<int>());
				continue;
			}

			// Struct 처리
			if (const Type* subType = MetaDataRegistry->Find(prop.typeName))
			{
				void* subInstance = reinterpret_cast<void*>(reinterpret_cast<char*>(instance) + prop.offset);
				Deserialize(subInstance, *subType, node[prop.name]);
				continue;
			}

			// 기본 타입 처리
			if (prop.typeName == "int")
				prop.setter(instance, node[prop.name].as<int>());
			else if (prop.typeName == "float")
				prop.setter(instance, node[prop.name].as<float>());
			else if (prop.typeName == "bool")
				prop.setter(instance, node[prop.name].as<bool>());
			else if (prop.typeName == "std::string")
				prop.setter(instance, node[prop.name].as<std::string>());
		}
	}

	template<typename T>
	inline void Deserialize(T* instance, const MetaYml::Node& node)
	{
		Deserialize(reinterpret_cast<void*>(instance), T::Reflect(), node);
	}
}