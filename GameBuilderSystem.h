#pragma once
#ifndef DYNAMICCPP_EXPORTS
#include "Core.Minimal.h"
#include "DLLAcrossSingleton.h"

class GameBuilderSystem : public DLLCore::Singleton<GameBuilderSystem>
{
private:
	friend class DLLCore::Singleton<GameBuilderSystem>;
	GameBuilderSystem() = default;
	~GameBuilderSystem() = default;

public:
	void Initialize();
	void Finalize();

	void BuildGame();

private:
	std::wstring m_buildSlnPath{};
	std::wstring m_MSBuildPath{};
	std::wstring m_buildCommand{};
};
#endif // !DYNAMICCPP_EXPORTS