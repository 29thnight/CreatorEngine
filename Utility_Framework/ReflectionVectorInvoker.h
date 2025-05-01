#pragma once
#include <unordered_map>
#include <functional>
#include "TypeTrait.h"
#include "ReflectionVectorMapper.h"
#include "LogSystem.h"

namespace Meta
{
	using VectorInvokeFunc = std::function<void(void*, const YAML::Node&, const Type*)>;

	class VectorInvokerRegistry : public Singleton<VectorInvokerRegistry>
	{
	private:
		std::unordered_map<HashedGuid, VectorInvokeFunc> _handlers;

	public:
		template<typename T>
		void Register()
		{
			_handlers[TypeTrait::GUIDCreator::GetTypeID<std::vector<T>>()] =
				[](void* vecPtr, const YAML::Node& arrayNode, const Type* elementType)
				{
					TypeMapper<T>::InvokeForVector(vecPtr, arrayNode, elementType);
				};
		}

		void Invoke(HashedGuid vectorTypeID, void* vecPtr, const YAML::Node& arrayNode, const Type* elementType)
		{
			auto it = _handlers.find(vectorTypeID);
			if (it != _handlers.end())
			{
				it->second(vecPtr, arrayNode, elementType);
			}
			else
			{
				Debug->LogError("VectorInvoker: no handler for typeID");
			}
		}
	};

	static inline auto& VectorInvoker = VectorInvokerRegistry::GetInstance();
}