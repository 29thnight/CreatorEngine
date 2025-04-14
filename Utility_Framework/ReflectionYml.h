#pragma once
#include "ReflectionFunction.h"
#include "ReflectionRegister.h"
#include <yaml-cpp/yaml.h>

namespace MetaYml = YAML;

namespace Meta
{
    inline MetaYml::Node Serialize(void* instance, const Type& type)
    {
		MetaYml::Node node;

        if (type.parent)
        {
            node.push_back(Serialize(instance, *type.parent));
        }

        for (const auto& prop : type.properties)
        {
            std::any value = prop.getter(instance);

            // Vector ó��
			if (IsVectorType(prop.typeName))
			{
				const std::string elementTypeName = ExtractVectorElementType(prop.typeName);
				auto value = prop.getter(instance);

				MetaYml::Node arrayNode;

				// shared_ptr<T>
				if (elementTypeName.find("std::shared_ptr<") != std::string::npos)
				{
					const std::string innerTypeName = ExtractVectorElementType(elementTypeName);
					auto vec = std::any_cast<std::vector<std::shared_ptr<void>>*>(value);

					for (auto& element : *vec)
					{
						if (const Type* subType = MetaDataRegistry->Find(innerTypeName))
						{
							arrayNode.push_back(Serialize(element.get(), *subType));
						}
						else
						{
							arrayNode.push_back(MetaYml::Node());
						}
					}
				}
				// T*
				else if (const Type* subType = MetaDataRegistry->Find(elementTypeName))
				{
					auto vec = std::any_cast<std::vector<void*>*>(value);

					for (auto& element : *vec)
					{
						arrayNode.push_back(Serialize(element, *subType));
					}
				}

				node[prop.name] = arrayNode;
				continue;
			}

            // Pointer ó��
            if (prop.isPointer)
            {
                if (value.has_value() && std::any_cast<void*>(value))
                {
                    const Type* subType = MetaDataRegistry->Find(prop.typeName);
                    node[prop.name] = Serialize(std::any_cast<void*>(value), *subType);
                }
                else
                {
                    node[prop.name] = YAML::Node();
                }
                continue;
            }

            // Enum ó��
            if (MetaEnumRegistry->Find(prop.typeName))
            {
                node[prop.name] = std::any_cast<int>(value);
                continue;
            }

            // Struct ó��
            if (const Type* subType = MetaDataRegistry->Find(prop.typeName))
            {
                void* subInstance = reinterpret_cast<void*>(reinterpret_cast<char*>(instance) + prop.offset);
                node[prop.name] = Serialize(subInstance, *subType);
                continue;
            }

			if (prop.typeName == "int")
			{
				node[prop.name] = std::any_cast<int>(value);
			}
			else if (prop.typeName == "float")
			{
				node[prop.name] = std::any_cast<float>(value);
			}
			else if (prop.typeName == "bool")
			{
				node[prop.name] = std::any_cast<bool>(value);
			}
			else if (prop.typeName == "std::string")
			{
				node[prop.name] = std::any_cast<std::string>(value);
			}
			else if (prop.typeName == "HashingString")
			{
				node[prop.name] = std::any_cast<HashingString>(value).ToString();
			}
			else
			{
				// ó�� �Ұ� Ÿ�� �� null ó�� or �����α�
				node[prop.name] = YAML::Node();
			}
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

			// Vector ó��
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

			// Pointer ó��
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

			// Enum ó��
			if (MetaEnumRegistry->Find(prop.typeName))
			{
				prop.setter(instance, node[prop.name].as<int>());
				continue;
			}

			// Struct ó��
			if (const Type* subType = MetaDataRegistry->Find(prop.typeName))
			{
				void* subInstance = reinterpret_cast<void*>(reinterpret_cast<char*>(instance) + prop.offset);
				Deserialize(subInstance, *subType, node[prop.name]);
				continue;
			}

			// �⺻ Ÿ�� ó��
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