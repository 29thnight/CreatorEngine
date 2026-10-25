#pragma once
#include "../../../Engine/Utility_Framework/Foundation/Core.Minimal.h"
#include "../../../Engine/Utility_Framework/Reflection/ClassProperty.h"

class GameBuilderSystem : public Singleton<GameBuilderSystem>
{
private:
	friend class Singleton<GameBuilderSystem>;
	GameBuilderSystem() = default;
	~GameBuilderSystem() = default;

public:
	bool BuildGame();
};
