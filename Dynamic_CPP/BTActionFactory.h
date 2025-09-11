#pragma once
#include "Core.Minimal.h"
#include "Export.h"
#include "BTHeader.h"

// Automation include ActionNodeClass header
#include "BP0034.h"
#include "BP0033.h"
#include "BP0032.h"
#include "BP0031.h"
#include "WaitAction.h"
#include "KnockBackAction.h"
#include "DetectAndTargetingAction.h"
#include "BTEntityInitAction.h"
#include "BP005Breath.h"
#include "BP004DeathWorm.h"
#include "BP003Impale.h"
#include "BP002FireBall.h"
#include "BP001BodyAtack.h"
#include "BossIdleAction.h"
#include "GroggyAction.h"
#include "MageActtack.h"
#include "RetreatAction.h"
#include "TeleportAction.h"
#include "DamegeAction.h"
#include "ChaseAction.h"
#include "Idle.h"
#include "AtteckAction.h"
#include "DaedAction.h"
#include "TestAction.h"

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
