#pragma once
#include "Core.Minimal.h"
#include "Export.h"
#include "BTHeader.h"

// Automation include ConditionNodeClass header
#include "IsChase.h"
#include "IsGroggy.h"
#include "IsTeleport.h"
#include "IsMageAttack.h"
#include "IsReteat.h"
#include "IsDetect.h"
#include "IsAtteck.h"
#include "IsDaed.h"
#include "TestCon.h"


class ConditionCreateFactory : public Singleton<ConditionCreateFactory>
{
private:
	friend class Singleton;
	ConditionCreateFactory() = default;
	~ConditionCreateFactory() = default;
public:
	// Register a factory function for creating ModuleBehavior instances
	void RegisterFactory(const std::string& className, std::function<BT::ConditionNode* ()> factoryFunction)
	{
		if (factoryMap.find(className) != factoryMap.end())
		{
			std::cout << "Factory for class " << className << " is already registered." << std::endl;
			return; // or throw an exception
		}

		factoryMap[className] = factoryFunction;
	}
	// Create a ModuleBehavior instance using the registered factory function
	BT::ConditionNode* CreateInstance(const std::string& className)
	{
		auto it = factoryMap.find(className);
		if (it != factoryMap.end())
		{
			return it->second();
		}
		return nullptr; // or throw an exception
	}
	std::unordered_map<std::string, std::function<BT::ConditionNode* ()>> factoryMap;
};