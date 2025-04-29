#pragma once
#include "AniBehaviour.h"


class anibegvioutFatory : public Singleton<anibegvioutFatory>
{
	friend class Singleton;
public:


	void ReisterFactory(std::string name, std::function <AniBehaviour*()> func)
	{
		anifactorymap[name] = func;
	}

	AniBehaviour* CreateBehaviour(std::string name)
	{
		auto it = anifactorymap.find(name);
		if (it != anifactorymap.end())
		{
			return it->second();
		}
		return nullptr; // 
	}

	std::unordered_map<std::string, std::function<AniBehaviour* ()>> anifactorymap;
};

static auto& aniFactory = anibegvioutFatory::GetInstance();