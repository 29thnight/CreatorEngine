#include "../Utility_Framework/Foundation/EngineNodeMap.h"
#pragma once
#include "../Utility_Framework/Foundation/EngineFunc.h"
#include "../Utility_Framework/Foundation/Core.Minimal.h"
#include "AniBehavior.h"


class AnimationBehviourFatory : public Singleton<AnimationBehviourFatory>
{
	friend class Singleton;
public:
	void ReisterFactory(std::string name, ce::owned_func <AniBehavior*()> func)
	{
		m_aniFactoryMap[name] = func;
	}

	AniBehavior* CreateBehaviour(std::string name)
	{
		auto it = m_aniFactoryMap.find(name);
		if (it != m_aniFactoryMap.end())
		{
			return it->second();
		}
		return nullptr; // 
	}

	ce::engine_node_map<std::string, ce::owned_func<AniBehavior* ()>> m_aniFactoryMap;
};

static auto AnimationFactorys = AnimationBehviourFatory::GetInstance();