#pragma once
#include "Core.Minimal.h"
#include "AniBehavior.h"


class AnimationBehviourFatory : public Singleton<AnimationBehviourFatory>
{
	friend class Singleton;
public:
	void ReisterFactory(std::string name, std::function <AniBehavior*()> func)
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

	std::unordered_map<std::string, std::function<AniBehavior* ()>> m_aniFactoryMap;
};

static auto& AnimationFactorys = AnimationBehviourFatory::GetInstance();