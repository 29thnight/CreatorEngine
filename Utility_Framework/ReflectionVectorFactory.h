#pragma once
#include <unordered_map>
#include <functional>
#include "TypeTrait.h"
#include "DLLAcrossSingleton.h"

namespace Meta
{
	using VectorCreateFunc = std::function<void* ()>;

	class VectorFactoryRegistry : public DLLCore::Singleton<VectorFactoryRegistry>
	{
	private:
		std::unordered_map<HashedGuid, VectorCreateFunc> _creators;
		friend DLLCore::Singleton<VectorFactoryRegistry>;

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

	static auto VectorFactory = VectorFactoryRegistry::GetInstance();
}