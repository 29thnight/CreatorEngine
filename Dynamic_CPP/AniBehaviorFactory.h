#pragma once
#include "Core.Minimal.h"
#include "Export.h"

// Automation include AniScriptClass header
#include "BossMeleeAni.h"
#include "BossRangedAni.h"
#include "BossWarningAni.h"
#include "BossPopupAni.h"
#include "BossBurrowAni.h"
#include "PlayerRangeAttackSpecial.h"
#include "PlayerRangeAttackEnd.h"
#include "PlayerRangeAttackReady.h"
#include "PlayerThrow.h"
#include "PlayerGrab.h"
#include "PlayerBombCharing.h"
#include "PlayerHit.h"
#include "PlayerDash.h"
#include "PlayerStun.h"
#include "PlayerBombAttack.h"
#include "PlayerRangeAttack.h"
#include "MosterMeleeAni.h"
#include "PlayerAttackAH.h"

class AniBehaviorFactory : public Singleton<AniBehaviorFactory>
{
private:
	friend class Singleton;
	AniBehaviorFactory() = default;
	~AniBehaviorFactory() = default;
public:
	// Register a factory function for creating AniBehaviour instances
	void RegisterFactory(const std::string& className, std::function<AniBehavior* ()> factoryFunction)
	{
		if (factoryMap.find(className) != factoryMap.end())
		{
			std::cout << "Factory for class " << className << " is already registered." << std::endl;
			return; // or throw an exception
		}

		factoryMap[className] = factoryFunction;
	}
	// Create a ModuleBehavior instance using the registered factory function
	AniBehavior* CreateInstance(const std::string& className)
	{
		auto it = factoryMap.find(className);
		if (it != factoryMap.end())
		{
			return it->second();
		}
		return nullptr; // or throw an exception
	}
	std::unordered_map<std::string, std::function<AniBehavior* ()>> factoryMap;
};
