#pragma once
#ifndef DYNAMICCPP_EXPORTS
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

	void BuildGame();
	//For Test
	bool PackageGameAssets();
	bool UnpackageGameAssets();

private:
	bool m_isInitialized{ false };
};
#endif // !DYNAMICCPP_EXPORTS