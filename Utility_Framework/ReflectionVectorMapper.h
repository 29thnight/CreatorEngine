#pragma once
#include <yaml-cpp/yaml.h>

namespace Meta
{
	extern void Deserialize(void* object, const Type& type, const YAML::Node& node);

	template<typename T>
	struct TypeMapper
	{
		static void InvokeForVector(void* vecPtr, const YAML::Node& arrayNode, const Type* elementType)
		{
			auto typedVec = reinterpret_cast<std::vector<T>*>(vecPtr);
			typedVec->clear();
			typedVec->reserve(arrayNode.size());

			for (const auto& node : arrayNode)
			{
				// CT4-d: HasReflect가 아니라 HasRuntimeType — 신형(describe)
				// 타입은 Reflect() 멤버가 없어서 HasReflect로 물으면 as<T>()
				// 폴백으로 떨어져 YAML::convert<T> 미정의 컴파일 에러가 난다(실측).
				if constexpr (HasRuntimeType<T>)
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