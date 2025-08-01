#pragma once
#include "Core.Minimal.h"
#include "Export.h"
#include "AniBehaviour.h"

// Automation include AniScriptClass header


class AniBehaviourFactory : public Singleton<AniBehaviourFactory>
{
private:
	friend class Singleton;
	AniBehaviourFactory() = default;
	~AniBehaviourFactory() = default;
public:
	// Register a factory function for creating AniBehaviour instances
	void RegisterFactory(const std::string& className, std::function<AniBehaviour* ()> factoryFunction)
	{
		if (factoryMap.find(className) != factoryMap.end())
		{
			std::cout << "Factory for class " << className << " is already registered." << std::endl;
			return; // or throw an exception
		}

		factoryMap[className] = factoryFunction;
	}
	// Create a ModuleBehavior instance using the registered factory function
	AniBehaviour* CreateInstance(const std::string& className)
	{
		auto it = factoryMap.find(className);
		if (it != factoryMap.end())
		{
			return it->second();
		}
		return nullptr; // or throw an exception
	}
	std::unordered_map<std::string, std::function<AniBehaviour* ()>> factoryMap;
};
