#include "DLLAcrossSingleton.h"

namespace DLLCore
{
	std::mutex ISingleton::mu{};
	std::unordered_map<uint64_t, void*> ISingleton::instance{};

	auto ISingleton::CreateInstance(uint64_t hash, std::size_t size) -> Result
	{
		std::lock_guard<std::mutex> lock{ mu };

		auto it = instance.find(hash);
		if (it == instance.end())
		{
			void* ptr = ::operator new(size);
			std::memset(ptr, 0, size);
			instance.insert({ hash, ptr });

			return Result{ ptr, true };
		}
		return Result{ it->second, false };
	}

	void ISingleton::DeleteInstance(uint64_t hash)
	{
		std::lock_guard<std::mutex> lock{ mu };

		auto it = instance.find(hash);
		if (it == instance.end())
			return;

		::operator delete(it->second);
		instance.erase(it);
	}
}