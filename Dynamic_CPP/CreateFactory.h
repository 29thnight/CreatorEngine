#pragma once
#include "Core.Minimal.h"
#include "Export.h"

// Automation include ScriptClass header
#include "Swrod.h"
#include "EntityItem.h"
#include "EntityAsis.h"
#include "Temp.h"
#include "Entity.h"
#include "Player.h"
#include "TestBehavior.h"
#include "AsisMove.h"

class CreateFactory : public Singleton<CreateFactory>
{
private:
	friend class Singleton;
	CreateFactory() = default;
	~CreateFactory() = default;
public:
	// Register a factory function for creating ModuleBehavior instances
	void RegisterFactory(const std::string& className, std::function<ModuleBehavior*()> factoryFunction)
	{
		if (factoryMap.find(className) != factoryMap.end())
		{
			std::cout << "Factory for class " << className << " is already registered." << std::endl;
			return; // or throw an exception
		}

		factoryMap[className] = factoryFunction;
	}
	// Create a ModuleBehavior instance using the registered factory function
	ModuleBehavior* CreateInstance(const std::string& className)
	{
		auto it = factoryMap.find(className);
		if (it != factoryMap.end())
		{
			return it->second();
		}
		return nullptr; // or throw an exception
	}
	std::unordered_map<std::string, std::function<ModuleBehavior*()>> factoryMap;
};

static inline auto& ModuleFactory = CreateFactory::GetInstance();
