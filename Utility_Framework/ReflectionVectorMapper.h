#pragma once
#include "MetaYaml.h"

namespace Meta
{
        extern void Deserialize(void* object, const Type& type, const MetaYml::Node& node);

	template<typename T>
	struct TypeMapper
	{
                static void InvokeForVector(void* vecPtr, const MetaYml::Node& arrayNode, const Type* elementType)
		{
			auto typedVec = reinterpret_cast<std::vector<T>*>(vecPtr);
			typedVec->clear();
			typedVec->reserve(arrayNode.size());

			for (const auto& node : arrayNode)
			{
				if constexpr (HasReflect<T>)
				{
					T item;
					Deserialize(&item, *elementType, node);
					typedVec->push_back(std::move(item));
				}
				else
				{
					typedVec->push_back(node.as<T>());
				}
			}
		}
	};
}