#pragma once
#include <unordered_map>
#include <functional>
#include "TypeTrait.h"

namespace Meta
{
	using VectorCreateFunc = std::function<void* ()>;

	class VectorFactoryRegistry : public Singleton<VectorFactoryRegistry>
	{
	private:
		std::unordered_map<HashedGuid, VectorCreateFunc> _creators;

	public:
		template<typename T>
		void Register()
		{
			_creators[TypeTrait::GUIDCreator::GetTypeID<std::vector<T>>()] = []()
				{
					return static_cast<void*>(new std::vector<T>());
				};
		}

		void* Create(HashedGuid typeID)
		{
			auto it = _creators.find(typeID);
			if (it != _creators.end())
				return it->second();

			return nullptr;
		}
	};

	static inline auto& VectorFactory = VectorFactoryRegistry::GetInstance();
}