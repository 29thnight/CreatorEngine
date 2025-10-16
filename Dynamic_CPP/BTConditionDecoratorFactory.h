#pragma once
#include "Core.Minimal.h"
#include "Export.h"
#include "BTHeader.h"

// Automation include ConditionDecoratorNodeClass header
#include "ActionCountCheck.h"
#include "IsHazadPatten.h"
#include "IsUseAsis.h"
#include "BP2IsPatten.h"
#include "IsInitialize.h"
#include "IsBossRangeAttack.h"
#include "BP1IsPatten.h"
#include "IsBossAtteck.h"
#include "IsStartBoss.h"
#include "Phase3.h"
#include "Phase2.h"
#include "Phase1.h"
#include "IsDamege.h"
#include "TestConCec.h"


class ConditionDecoratorCreateFactory : public Singleton<ConditionDecoratorCreateFactory>
{
private:
	friend class Singleton;
	ConditionDecoratorCreateFactory() = default;
	~ConditionDecoratorCreateFactory() = default;
public:
	// Register a factory function for creating ConditionDecoratorNode instances
	void RegisterFactory(const std::string& className, std::function<BT::ConditionDecoratorNode* ()> factoryFunction)
	{
		if (factoryMap.find(className) != factoryMap.end())
		{
			std::cout << "Factory for class " << className << " is already registered." << std::endl;
			return; // or throw an exception
		}

		factoryMap[className] = factoryFunction;
	}
	// Create a ConditionDecoratorNode instance using the registered factory function
	BT::ConditionDecoratorNode* CreateInstance(const std::string& className)
	{
		auto it = factoryMap.find(className);
		if (it != factoryMap.end())
		{
			return it->second();
		}
		return nullptr; // or throw an exception
	}
	std::unordered_map<std::string, std::function<BT::ConditionDecoratorNode* ()>> factoryMap;
};