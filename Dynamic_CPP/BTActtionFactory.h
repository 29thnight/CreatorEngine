#pragma once
#include "Core.Minimal.h"
#include "Export.h"
#include "BTHeader.h"

// Automation include ActionNodeClass header


class ActionCreateFactory : public Singleton<ActionCreateFactory>
{
private:
	friend class Singleton;
	ActionCreateFactory() = default;
	~ActionCreateFactory() = default;
public:
	// Register a factory function for creating ModuleBehavior instances
	void RegisterFactory(const std::string& className, std::function<BT::ActionNode* ()> factoryFunction)
	{
		if (factoryMap.find(className) != factoryMap.end())
		{
			std::cout << "Factory for class " << className << " is already registered." << std::endl;
			return; // or throw an exception
		}

		factoryMap[className] = factoryFunction;
	}
	// Create a ModuleBehavior instance using the registered factory function
	BT::ActionNode* CreateInstance(const std::string& className)
	{
		auto it = factoryMap.find(className);
		if (it != factoryMap.end())
		{
			return it->second();
		}
		return nullptr; // or throw an exception
	}
	std::unordered_map<std::string, std::function<BT::ActionNode* ()>> factoryMap;
};
