#pragma once
#include "Core.Minimal.h"
#include "ClassProperty.h"

class GameBuilderSystem : public Singleton<GameBuilderSystem>
{
private:
	friend class Singleton<GameBuilderSystem>;
	GameBuilderSystem() = default;
	~GameBuilderSystem() = default;

public:
	void Initialize();
	void Finalize();

	bool BuildGame();

private:
	bool m_isInitialized{ false };
};
